// SPDX-License-Identifier: GPL-3.0-or-later
// A session-scoped Windows display-mode owner. Never edits registry defaults.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QLockFile>
#include <QSaveFile>
#include <QSettings>
#include <QString>
#include <QUuid>
#include <windows.h>
#include <shellapi.h>
#include <setupapi.h>
#include <devguid.h>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

namespace {
void send(const QJsonObject& value) {
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
    std::cout << bytes.constData() << std::endl;
}
QString ownedAdapterHardware(const QString& instance) {
    if (instance.isEmpty()) return {};
    auto devices = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) return {};
    QString result;
    SP_DEVINFO_DATA info{}; info.cbSize = sizeof(info);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(devices, i, &info); ++i) {
        wchar_t id[1024]{}, hardware[4096]{};
        if (SetupDiGetDeviceInstanceIdW(devices, &info, id, 1024, nullptr) &&
            instance.compare(QString::fromWCharArray(id), Qt::CaseInsensitive) == 0 &&
            SetupDiGetDeviceRegistryPropertyW(devices, &info, SPDRP_HARDWAREID, nullptr,
                reinterpret_cast<PBYTE>(hardware), sizeof(hardware), nullptr)) {
            result = QString::fromWCharArray(hardware);
            break;
        }
    }
    // EnumDisplayDevices exposes hardware IDs, not PnP instance IDs. Only map
    // when the verified owned instance is the sole adapter with this hardware ID.
    int matches = 0;
    for (DWORD i = 0; !result.isEmpty() && SetupDiEnumDeviceInfo(devices, i, &info); ++i) {
        wchar_t hardware[4096]{};
        if (SetupDiGetDeviceRegistryPropertyW(devices, &info, SPDRP_HARDWAREID, nullptr,
                reinterpret_cast<PBYTE>(hardware), sizeof(hardware), nullptr) &&
            result.compare(QString::fromWCharArray(hardware), Qt::CaseInsensitive) == 0) ++matches;
    }
    SetupDiDestroyDeviceInfoList(devices);
    return matches == 1 ? result : QString();
}
// The elevated companion survives this helper being killed. It owns the full
// pre-sharing topology and disables the exact owned PnP instance on release.
struct DisplayLease {
    HANDLE ready=nullptr, release=nullptr, process=nullptr;
    QString error;
    bool start() {
        std::cerr << "Display lease: requesting Windows authorization" << std::endl;
        const auto nonce=QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto prefix=QStringLiteral("Local\\DeskPort.Display.")+nonce;
        ready=CreateEventW(nullptr,TRUE,FALSE,reinterpret_cast<LPCWSTR>((prefix+".ready").utf16()));
        release=CreateEventW(nullptr,TRUE,FALSE,reinterpret_cast<LPCWSTR>((prefix+".release").utf16()));
        if(!ready||!release){error="Cannot create display recovery events";return false;}
        const auto executable=QDir::toNativeSeparators(QCoreApplication::applicationDirPath()+"/deskport-display-recovery.exe");
        const auto arguments=QStringLiteral("lease %1 %2").arg(GetCurrentProcessId()).arg(nonce);
        const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
        struct ComScope { HRESULT result; ~ComScope(){if(SUCCEEDED(result))CoUninitialize();} } comScope{com};
        SHELLEXECUTEINFOW info{};info.cbSize=sizeof(info);info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC;
        info.lpVerb=L"runas";info.lpFile=reinterpret_cast<LPCWSTR>(executable.utf16());
        info.lpParameters=reinterpret_cast<LPCWSTR>(arguments.utf16());info.nShow=SW_HIDE;
        if(!ShellExecuteExW(&info)||!info.hProcess){const auto code=GetLastError();error=QString("Windows display authorization was canceled or unavailable (error %1)").arg(code);return false;}
        process=info.hProcess;
        std::cerr << "Display lease: authorization returned; waiting for guard" << std::endl;
        HANDLE waiting[]{ready,process};
        if(WaitForMultipleObjects(2,waiting,FALSE,15000)!=WAIT_OBJECT_0){error="Independent display recovery guard could not prepare the owned device";return false;}
        std::cerr << "Display lease: guard ready" << std::endl;
        return true;
    }
    bool finish() {
        if(!process)return true;
        SetEvent(release);
        const auto waited=WaitForSingleObject(process,15000);DWORD result=ERROR_TIMEOUT;
        if(waited==WAIT_OBJECT_0)GetExitCodeProcess(process,&result);
        if(waited==WAIT_OBJECT_0){CloseHandle(process);process=nullptr;}
        if(result){std::cerr<<"Full display recovery failed: "<<result<<"; durable snapshot retained"<<std::endl;return false;}
        return true;
    }
    ~DisplayLease(){finish();if(process)CloseHandle(process);if(ready)CloseHandle(ready);if(release)CloseHandle(release);}
};
QString path;
QString output;
DEVMODEW original{};
bool changed = false;
bool privateVirtual = false;
bool current(DEVMODEW& mode) {
    mode = {}; mode.dmSize = sizeof(mode);
    return EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(output.utf16()), ENUM_CURRENT_SETTINGS, &mode, 0);
}
bool writeState(bool pending) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const auto bytes = QJsonDocument(QJsonObject{
        {"version", 1}, {"output", output}, {"pending", pending}, {"virtual", privateVirtual},
        {"original", QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(&original), sizeof(original)).toBase64())}
    }).toJson();
    return file.write(bytes) == bytes.size() && file.commit();
}
// GDI mode changes may reload unrelated registry modes. Preserve the full
// active CCD mode set and reapply it without saving any display defaults.
struct ActiveTopology {
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    bool read() {
        for (int attempt=0; attempt<5; ++attempt) {
            UINT32 np=0,nm=0;
            if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS|QDC_VIRTUAL_MODE_AWARE,&np,&nm)||np>64||nm>512)return false;
            paths.resize(np);modes.resize(nm);
            const auto rc=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS|QDC_VIRTUAL_MODE_AWARE,&np,paths.data(),&nm,modes.data(),nullptr);
            if(rc==ERROR_INSUFFICIENT_BUFFER)continue;
            if(rc)return false;
            paths.resize(np);modes.resize(nm);return true;
        }
        return false;
    }
};
ActiveTopology sessionTopology;
bool applyMode(DEVMODEW target) {
    if(!privateVirtual)return ChangeDisplaySettingsExW(reinterpret_cast<LPCWSTR>(output.utf16()),&target,nullptr,0,nullptr)==DISP_CHANGE_SUCCESSFUL;
    ActiveTopology after;
    const auto& before=sessionTopology;
    if(before.paths.empty())return false;
    if(ChangeDisplaySettingsExW(reinterpret_cast<LPCWSTR>(output.utf16()),&target,nullptr,0,nullptr)!=DISP_CHANGE_SUCCESSFUL||!after.read())return false;
    auto sameAdapter=[](LUID a,LUID b){return a.LowPart==b.LowPart&&a.HighPart==b.HighPart;};
    bool found=false;
    for(auto& path:after.paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};
        name.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        name.header.size=sizeof(name);name.header.adapterId=path.sourceInfo.adapterId;name.header.id=path.sourceInfo.id;
        if(DisplayConfigGetDeviceInfo(&name.header))return false;
        const bool owned=output.compare(QString::fromWCharArray(name.viewGdiDeviceName),Qt::CaseInsensitive)==0;
        if(owned) {
            const auto index=(path.flags&DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE)?path.sourceInfo.sourceModeInfoIdx:path.sourceInfo.modeInfoIdx;
            if(index>=after.modes.size()||after.modes[index].infoType!=DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)return false;
            auto& source=after.modes[index].sourceMode;
            source.position=target.dmPosition;source.width=target.dmPelsWidth;source.height=target.dmPelsHeight;
            found=true;
        } else {
            const auto oldPath=std::find_if(before.paths.begin(),before.paths.end(),[&](const DISPLAYCONFIG_PATH_INFO& p){return sameAdapter(p.sourceInfo.adapterId,path.sourceInfo.adapterId)&&p.sourceInfo.id==path.sourceInfo.id&&sameAdapter(p.targetInfo.adapterId,path.targetInfo.adapterId)&&p.targetInfo.id==path.targetInfo.id;});
            if(oldPath==before.paths.end())return false;
            path.targetInfo.rotation=oldPath->targetInfo.rotation;
            path.targetInfo.scaling=oldPath->targetInfo.scaling;
            path.targetInfo.refreshRate=oldPath->targetInfo.refreshRate;
            path.targetInfo.scanLineOrdering=oldPath->targetInfo.scanLineOrdering;
            // Copy mode payloads by stable adapter/type/id, retaining the new
            // array indices referenced by the queried paths.
            for(auto& mode:after.modes) {
                if(!sameAdapter(mode.adapterId,path.sourceInfo.adapterId)&&!sameAdapter(mode.adapterId,path.targetInfo.adapterId))continue;
                auto old=std::find_if(before.modes.begin(),before.modes.end(),[&](const DISPLAYCONFIG_MODE_INFO& m){return sameAdapter(m.adapterId,mode.adapterId)&&m.id==mode.id&&m.infoType==mode.infoType;});
                if(old!=before.modes.end())mode=*old;
            }
        }
    }
    if(!found)return false;
    const auto rc=SetDisplayConfig(UINT32(after.paths.size()),after.paths.data(),UINT32(after.modes.size()),after.modes.data(),SDC_APPLY|SDC_USE_SUPPLIED_DISPLAY_CONFIG|SDC_VIRTUAL_MODE_AWARE);
    if(rc)std::cerr<<"Preserving display topology failed: "<<rc<<std::endl;
    if(rc)return false;
    ActiveTopology verified;
    if(!verified.read())return false;
    for(const auto& old:before.modes) {
        if(old.infoType!=DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)continue;
        const auto mode=std::find_if(verified.modes.begin(),verified.modes.end(),[&](const DISPLAYCONFIG_MODE_INFO& m){return m.infoType==old.infoType&&m.id==old.id&&sameAdapter(m.adapterId,old.adapterId);});
        if(mode==verified.modes.end())return false;
        const auto& a=old.sourceMode;const auto& b=mode->sourceMode;
        if(a.width!=b.width||a.height!=b.height||a.position.x!=b.position.x||a.position.y!=b.position.y||a.pixelFormat!=b.pixelFormat)return false;
    }
    return true;
}
bool restore() {
    if (!changed) return true;
    // Apply only the display that this helper changed, without modifying saved defaults.
    if (!applyMode(original))
        return false;
    changed = false;
    return writeState(false);
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    DisplayLease recovery;
    const auto directory = qEnvironmentVariable("DESKPORT_DISPLAY_STATE_DIR");
    if (directory.isEmpty() || !QDir().mkpath(directory)) {
        send({{"error", "Missing private display state directory"}}); return 1;
    }
    QLockFile lock(directory + "/windows-display.lock"); lock.setStaleLockTime(0);
    if (!lock.tryLock()) { send({{"error", "Another display owner is running"}}); return 1; }
    path = directory + "/windows-display.json";
    const auto lease = CreateMutexW(nullptr, FALSE, L"Global\\DeskPort.WindowsDisplay.Owner");
    if (!lease || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (lease) CloseHandle(lease);
        send({{"error", "Another DeskPort display owner is active in this Windows session"}}); return 1;
    }
    QFile previous(path);
    if (previous.exists()) {
        if (!previous.open(QIODevice::ReadOnly)) { send({{"error", "Cannot read display recovery state"}}); return 1; }
        const auto saved = QJsonDocument::fromJson(previous.readAll()).object();
        previous.close(); // Windows cannot atomically replace an open recovery file.
        if (saved["pending"].toBool()) {
            const auto bytes = QByteArray::fromBase64(saved["original"].toString().toLatin1());
            output = saved["output"].toString();
            if (saved["version"].toInt() != 1 || bytes.size() != sizeof(original) || !output.startsWith("\\\\.\\DISPLAY")) {
                send({{"error", "Invalid display recovery record; original record retained"}}); return 1;
            }
            memcpy(&original, bytes.constData(), sizeof(original));
            if (original.dmSize != sizeof(original) || original.dmDriverExtra != 0) {
                send({{"error", "Invalid saved display mode"}}); return 1;
            }
            changed = true;
            if (!restore()) { send({{"error", "Previous display restoration failed; reconnect the display and retry"}}); return 1; }
        }
    }
    // Bind only the device instance created by DeskPort, never an independently
    // installed VDD adapter with the same vendor or friendly name.
    QSettings machine("HKEY_LOCAL_MACHINE\\Software\\DeskPort", QSettings::NativeFormat);
    const auto ownedInstance = machine.value("VirtualDisplayDevice").toString();
    if(!ownedInstance.isEmpty()&&!sessionTopology.read()){send({{"error", "Cannot capture the pre-sharing display topology"}});return 1;}
    if (!ownedInstance.isEmpty() && !recovery.start()) { send({{"error", recovery.error}}); return 1; }
    const auto owned = ownedAdapterHardware(ownedInstance);
    if (!ownedInstance.isEmpty() && owned.isEmpty()) {
        send({{"error", "The owned virtual display is missing or ambiguous; no other display was selected"}}); return 1;
    }
    output.clear();
    QString physical;
    DISPLAY_DEVICEW device{}; device.cb = sizeof(device);
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &device, 0); ++i) {
        if (!owned.isEmpty() && QString::fromWCharArray(device.DeviceID).compare(owned, Qt::CaseInsensitive) == 0) {
            output = QString::fromWCharArray(device.DeviceName);
            privateVirtual = true;
        }
        if ((device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) && (device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP))
            physical = QString::fromWCharArray(device.DeviceName);
        device = {}; device.cb = sizeof(device);
    }
    if (!owned.isEmpty() && output.isEmpty()) {
        send({{"error", "The owned virtual adapter has no available display output"}}); return 1;
    }
    if (output.isEmpty()) output = physical;
    if (output.isEmpty() || !current(original)) {
        original = {}; original.dmSize = sizeof(original);
        if (output.isEmpty() || !EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(output.utf16()), ENUM_REGISTRY_SETTINGS, &original, 0)) {
            send({{"error", "Cannot read the selected Windows display mode"}}); return 1;
        }
    }
    if (privateVirtual) {
        std::cerr << "Display lease: positioning owned output" << std::endl;
        DEVMODEW primary{}; primary.dmSize = sizeof(primary);
        if (!EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(physical.utf16()), ENUM_CURRENT_SETTINGS, &primary, 0)) {
            send({{"error", "Cannot determine the physical desktop layout"}}); return 1;
        }
        LONG right = primary.dmPosition.x + LONG(primary.dmPelsWidth);
        DISPLAY_DEVICEW other{}; other.cb = sizeof(other);
        for (DWORD i=0; EnumDisplayDevicesW(nullptr,i,&other,0); ++i) {
            if ((other.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) &&
                output != QString::fromWCharArray(other.DeviceName)) {
                DEVMODEW mode{}; mode.dmSize=sizeof(mode);
                if (EnumDisplaySettingsExW(other.DeviceName,ENUM_CURRENT_SETTINGS,&mode,0))
                    right=qMax(right,mode.dmPosition.x+LONG(mode.dmPelsWidth));
            }
            other={}; other.cb=sizeof(other);
        }
        for(const auto& mode:sessionTopology.modes) {
            if(mode.infoType==DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)
                right=qMax(right,mode.sourceMode.position.x+LONG(mode.sourceMode.width));
        }
        // Windows may reactivate a remembered placement above the primary.
        // Always place this session's owned output to the right of all existing
        // outputs; the external lease restores the actual pre-session topology.
        original.dmPosition.x = right;
        original.dmPosition.y = primary.dmPosition.y;
        original.dmFields |= DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT;
        if (!writeState(true) || !applyMode(original)) {
            send({{"error", "Cannot activate the DeskPort virtual display"}}); return 1;
        }
        std::cerr << "Display lease: owned output positioned" << std::endl;
    }
    if (!writeState(false)) {
        send({{"error", "Cannot snapshot the current Windows display"}}); return 1;
    }
    QJsonArray modes; QSet<QString> seenModes;
    for(DWORD index=0; index<4096; ++index) {
        DEVMODEW mode{}; mode.dmSize=sizeof(mode);
        if(!EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(output.utf16()),index,&mode,0))break;
        const int w=int(mode.dmPelsWidth),h=int(mode.dmPelsHeight);
        const auto key=QString::number(w)+"x"+QString::number(h);
        if(w>=640&&w<=7680&&h>=360&&h<=4320&&w%4==0&&h%4==0&&mode.dmBitsPerPel==original.dmBitsPerPel&&!seenModes.contains(key)&&modes.size()<96) {
            seenModes.insert(key);modes.append(QJsonObject{{"width",w},{"height",h}});
        }
    }
    send({{"displayId", 1}, {"width", int(original.dmPelsWidth)}, {"height", int(original.dmPelsHeight)}, {"scale", 1}, {"virtual", privateVirtual}, {"displayModes",modes}});
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.size() > 4096) break;
        const auto request = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        const int seq = request["seq"].toInt();
        QJsonObject response{{"seq", seq}, {"scale", 1}};
        QString error;
        bool fatalTopologyError=false;
        const int width = request["width"].toInt(), height = request["height"].toInt();
        if (!seq) error = "Missing request sequence";
        else if (!request["session"].toBool()) {
            if (!restore()) error = "Windows rejected restoration; recovery record retained";
        } else if (request["displayPolicy"].toInt() != 0) {
            error = "This Windows display supports the extended desktop policy only";
        } else if (width < 640 || width > 7680 || height < 360 || height > 4320 || width % 4 || height % 4) {
            error = "Invalid display size";
        } else {
            DEVMODEW target{}; bool found = false;
            for (DWORD index = 0; ; ++index) {
                target = {}; target.dmSize = sizeof(target);
                if (!EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(output.utf16()), index, &target, 0)) break;
                if (int(target.dmPelsWidth) == width && int(target.dmPelsHeight) == height && target.dmBitsPerPel == original.dmBitsPerPel) {
                    found = true; break;
                }
            }
            if (!found) {
                target = original;
                target.dmPelsWidth = DWORD(width); target.dmPelsHeight = DWORD(height);
                target.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
            }
            if (ChangeDisplaySettingsExW(reinterpret_cast<LPCWSTR>(output.utf16()), &target, nullptr, CDS_TEST, nullptr) != DISP_CHANGE_SUCCESSFUL)
                error = "Windows rejected the requested display mode";
            else if (!writeState(true)) error = "Cannot persist display recovery state";
            else {
                changed = true;
                target.dmPosition=original.dmPosition;
                target.dmFields|=DM_POSITION;
                if (!applyMode(target)) {
                    error = "Display mode application failed; ending the display lease to restore the desktop";
                    fatalTopologyError=true;
                }
            }
        }
        DEVMODEW actual{};
        if (current(actual)) { response["width"] = int(actual.dmPelsWidth); response["height"] = int(actual.dmPelsHeight); }
        else if (error.isEmpty()) error = "Cannot verify the resulting display mode";
        if (!error.isEmpty()) response["error"] = error;
        send(response);
        if(fatalTopologyError)break;
    }
    if (!restore()) { std::cerr << "Display restoration failed; recovery record retained" << std::endl; return 2; }
    if (!recovery.finish()) return 3;
    CloseHandle(lease);
    return 0;
}
