#pragma once

#include <Limelight.h>
#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <memory>
#include <vector>

// Private, local presentation protocol. Never sends pointers, native structs,
// decoded pixels, or process-relative timestamps across the process boundary.
class EncodedVideoFrame final {
public:
    static constexpr int MaxBytes = 32 * 1024 * 1024;
    static constexpr int MaxSegments = 4096;
    static constexpr quint32 Magic = 0x44505631; // DPV1
    DECODE_UNIT unit{};
    quint64 generation = 0;

    EncodedVideoFrame() = default;
    EncodedVideoFrame(const EncodedVideoFrame&) = delete;
    EncodedVideoFrame& operator=(const EncodedVideoFrame&) = delete;

    static QByteArray encode(const DECODE_UNIT& unit, quint64 generation) {
        if (!generation || unit.fullLength <= 0 || unit.fullLength > MaxBytes ||
            (unit.frameType != FRAME_TYPE_IDR && unit.frameType != FRAME_TYPE_PFRAME)) return {};
        quint32 count = 0;
        int total = 0;
        for (auto entry = unit.bufferList; entry; entry = entry->next) {
            if (++count > MaxSegments || !entry->data || entry->length <= 0 ||
                entry->length > unit.fullLength - total || entry->bufferType < BUFFER_TYPE_PICDATA ||
                entry->bufferType > BUFFER_TYPE_VPS) return {};
            total += entry->length;
        }
        if (total != unit.fullLength || !count) return {};
        QByteArray bytes;
        QDataStream out(&bytes, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_5_15);
        const auto reassembly = unit.enqueueTimeMs >= unit.receiveTimeMs ?
            qMin<quint64>(unit.enqueueTimeMs - unit.receiveTimeMs, 60000) : 0;
        out << Magic << generation << qint32(unit.frameNumber) << quint8(unit.frameType)
            << quint16(unit.frameHostProcessingLatency) << quint32(reassembly)
            << quint32(unit.presentationTimeMs) << quint8(unit.hdrActive) << quint8(unit.colorspace)
            << quint32(unit.fullLength) << count;
        for (auto entry = unit.bufferList; entry; entry = entry->next) {
            out << quint8(entry->bufferType) << quint32(entry->length);
            out.writeRawData(entry->data, entry->length);
        }
        return out.status() == QDataStream::Ok ? bytes : QByteArray{};
    }

    static std::unique_ptr<EncodedVideoFrame> decode(const QByteArray& bytes, quint64 now) {
        if (bytes.size() > MaxBytes + MaxSegments * 5 + 64) return {};
        auto frame = std::make_unique<EncodedVideoFrame>();
        QDataStream in(bytes);
        in.setVersion(QDataStream::Qt_5_15);
        quint32 magic, reassembly, presentation, size, count;
        qint32 number;
        quint8 type, hdr, colorspace;
        quint16 latency;
        in >> magic >> frame->generation >> number >> type >> latency >> reassembly
           >> presentation >> hdr >> colorspace >> size >> count;
        if (in.status() != QDataStream::Ok || magic != Magic || !frame->generation ||
            type > FRAME_TYPE_IDR || hdr > 1 || reassembly > 60000 || !size || size > MaxBytes ||
            !count || count > MaxSegments || size + quint64(count) * 5 > quint64(in.device()->bytesAvailable())) return {};
        frame->m_Data.resize(size);
        frame->m_Entries.resize(count);
        quint32 offset = 0;
        for (quint32 i = 0; i < count; ++i) {
            quint8 bufferType;
            quint32 length;
            in >> bufferType >> length;
            if (in.status() != QDataStream::Ok || bufferType > BUFFER_TYPE_VPS || !length || length > size - offset) return {};
            auto& entry = frame->m_Entries[i];
            entry.data = frame->m_Data.data() + offset;
            entry.length = int(length);
            entry.bufferType = bufferType;
            entry.next = i + 1 < count ? &frame->m_Entries[i + 1] : nullptr;
            if (in.readRawData(entry.data, entry.length) != entry.length) return {};
            offset += length;
        }
        if (offset != size || !in.atEnd() || in.status() != QDataStream::Ok) return {};
        frame->unit.frameNumber = number;
        frame->unit.frameType = type;
        frame->unit.frameHostProcessingLatency = latency;
        frame->unit.enqueueTimeMs = now;
        frame->unit.receiveTimeMs = now - qMin<quint64>(now, reassembly);
        frame->unit.presentationTimeMs = presentation;
        frame->unit.fullLength = int(size);
        frame->unit.bufferList = frame->m_Entries.data();
        frame->unit.hdrActive = hdr;
        frame->unit.colorspace = colorspace;
        return frame;
    }
private:
    QByteArray m_Data;
    std::vector<LENTRY> m_Entries;
};
