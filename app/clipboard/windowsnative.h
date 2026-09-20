#pragma once
#include "native.h"
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QBuffer>
#include <QUrl>
#include <QDir>
#include <QScopedValueRollback>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <vector>
#include <cstring>

// OLE format discovery must never materialize a remote file/image. Qt's
// CF_HDROP converter asks QMimeData::urls() even during format enumeration.
namespace DeskPortWindowsClipboard {
inline CLIPFORMAT markerFormat() { return CLIPFORMAT(RegisterClipboardFormatW(L"application/x-deskport-clipboard")); }
inline CLIPFORMAT pngFormat() { return CLIPFORMAT(RegisterClipboardFormatW(L"PNG")); }
class Formats final : public IEnumFORMATETC {
    ULONG refs = 1;
    std::vector<FORMATETC> formats;
    size_t position = 0;
public:
    explicit Formats(std::vector<FORMATETC> values, size_t at = 0) : formats(std::move(values)), position(at) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (id != IID_IUnknown && id != IID_IEnumFORMATETC) return E_NOINTERFACE;
        *result = static_cast<IEnumFORMATETC*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Next(ULONG count, FORMATETC* out, ULONG* fetched) override {
        if (!out || (!fetched && count != 1)) return E_POINTER;
        ULONG n = 0;
        while (n < count && position < formats.size()) out[n++] = formats[position++];
        if (fetched) *fetched = n;
        return n == count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG count) override {
        const auto n = qMin<size_t>(count, formats.size() - position); position += n;
        return n == count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Reset() override { position = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC** out) override {
        if (!out) return E_POINTER;
        *out = new Formats(formats, position); return S_OK;
    }
};
class Data final : public IDataObject {
    ULONG refs = 1;
    bool rendering = false;
    std::vector<FORMATETC> formats;
    ClipboardNative::Reader reader;
    QHash<CLIPFORMAT, QByteArray> cache;
    QByteArray render(CLIPFORMAT format) {
        if (cache.contains(format)) return cache.value(format);
        if (!reader || rendering) return {};
        QScopedValueRollback<bool> busy(rendering, true);
        // Copy the callable because a nested loop can revoke this offer.
        const auto read = reader;
        QByteArray bytes;
        if (format == CF_UNICODETEXT) {
            auto text = QString::fromUtf8(read(QStringLiteral("text/plain;charset=utf-8")));
            if (text.contains(QChar(0))) return {};
            bytes = QByteArray(reinterpret_cast<const char*>(text.utf16()), (text.size()+1)*2);
        } else if (format == CF_HDROP) {
            QString paths;
            for (auto line : read(QStringLiteral("text/uri-list")).split('\n')) {
                line = line.trimmed(); if (line.isEmpty() || line.startsWith('#')) continue;
                const auto url = QUrl::fromEncoded(line);
                if (!url.isLocalFile() || !url.host().isEmpty()) return {};
                const auto path = QDir::toNativeSeparators(url.toLocalFile());
                if (path.contains(QChar(0))) return {};
                paths += path; paths += QChar(0);
            }
            if (paths.isEmpty()) return {};
            paths += QChar(0);
            DROPFILES drop = {}; drop.pFiles = sizeof(drop); drop.fWide = TRUE;
            bytes = QByteArray(reinterpret_cast<const char*>(&drop), sizeof(drop));
            bytes.append(reinterpret_cast<const char*>(paths.utf16()), paths.size()*2);
        } else if (format == pngFormat() || format == CF_DIB) {
            const auto png = read(QStringLiteral("image/png"));
            QBuffer input; input.setData(png); input.open(QIODevice::ReadOnly);
            QImageReader decoder(&input, "PNG"); const auto size = decoder.size();
            if (!size.isValid() || qint64(size.width())*size.height() > 32LL*1024*1024) return {};
            if (format == pngFormat()) bytes = png;
            else {
                const auto image = decoder.read().convertToFormat(QImage::Format_ARGB32);
                if (image.isNull()) return {};
                BITMAPINFOHEADER header = {}; header.biSize = sizeof(header);
                header.biWidth = image.width(); header.biHeight = -image.height();
                header.biPlanes = 1; header.biBitCount = 32; header.biCompression = BI_RGB;
                header.biSizeImage = DWORD(image.width()*image.height()*4);
                bytes = QByteArray(reinterpret_cast<const char*>(&header), sizeof(header));
                for (int y=0; y<image.height(); ++y)
                    bytes.append(reinterpret_cast<const char*>(image.constScanLine(y)), image.width()*4);
            }
        } else if (format == markerFormat()) bytes = read(QStringLiteral("application/x-deskport-clipboard"));
        if (!reader) return {}; // Offer was revoked while rendering.
        if (!bytes.isEmpty()) cache.insert(format, bytes);
        return bytes;
    }
public:
    Data(const QStringList& types, ClipboardNative::Reader callback) : reader(std::move(callback)) {
        auto add = [&](CLIPFORMAT id) { if (id) formats.push_back({id, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}); };
        if (types.contains("text/plain;charset=utf-8") || types.contains("text/plain")) add(CF_UNICODETEXT);
        if (types.contains("text/uri-list")) add(CF_HDROP);
        if (types.contains("image/png")) {
            add(pngFormat()); add(CF_DIB);
            formats.push_back({CF_BITMAP, nullptr, DVASPECT_CONTENT, -1, TYMED_GDI});
        }
        if (types.contains("application/x-deskport-clipboard")) add(markerFormat());
    }
    void revoke() { reader = {}; cache.clear(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (id != IID_IUnknown && id != IID_IDataObject) return E_NOINTERFACE;
        *result = static_cast<IDataObject*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n=--refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override {
        if (!format) return E_POINTER;
        if (format->dwAspect != DVASPECT_CONTENT) return DV_E_DVASPECT;
        if (format->lindex != -1) return DV_E_LINDEX;
        for (const auto& f : formats) if (f.cfFormat == format->cfFormat) return (f.tymed & format->tymed) ? S_OK : DV_E_TYMED;
        return DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override {
        if (!medium) return E_POINTER;
        *medium = {};
        const auto valid = QueryGetData(format); if (FAILED(valid)) return valid;
        AddRef(); const auto bytes = render(format->cfFormat == CF_BITMAP ? CF_DIB : format->cfFormat); Release();
        if (bytes.isEmpty()) return DV_E_FORMATETC;
        if (format->cfFormat == CF_BITMAP) {
            if (bytes.size() < qsizetype(sizeof(BITMAPINFOHEADER))) return DV_E_FORMATETC;
            void* bits = nullptr;
            HBITMAP bitmap = CreateDIBSection(nullptr, reinterpret_cast<const BITMAPINFO*>(bytes.constData()),
                                             DIB_RGB_COLORS, &bits, nullptr, 0);
            if (!bitmap || !bits) { if (bitmap) DeleteObject(bitmap); return E_OUTOFMEMORY; }
            std::memcpy(bits, bytes.constData()+sizeof(BITMAPINFOHEADER), size_t(bytes.size()-sizeof(BITMAPINFOHEADER)));
            medium->tymed = TYMED_GDI; medium->hBitmap = bitmap; return S_OK;
        }
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, SIZE_T(bytes.size()));
        if (!memory) return E_OUTOFMEMORY;
        auto data = GlobalLock(memory);
        if (!data) { GlobalFree(memory); return E_OUTOFMEMORY; }
        std::memcpy(data, bytes.constData(), size_t(bytes.size())); GlobalUnlock(memory);
        medium->tymed = TYMED_HGLOBAL; medium->hGlobal = memory; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return DATA_E_FORMATETC; }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override { if (out) out->ptd=nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC** out) override {
        if (!out) return E_POINTER;
        *out = nullptr; if (direction != DATADIR_GET) return E_NOTIMPL;
        *out = new Formats(formats); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
};
}
