// SPDX-License-Identifier: GPL-3.0-or-later
// Manage only the virtual adapter created by this installer. Driver files retain
// their upstream signature. No certificate import or signing-policy changes.
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <newdev.h>
#include <devguid.h>
#include <string>
#include <vector>
#include <iostream>
static const wchar_t* regPath = L"Software\\DeskPort";
static const wchar_t* valueName = L"VirtualDisplayDevice";
static const wchar_t hardware[] = L"Root\\MttVDD\0";
static std::wstring owner() {
    wchar_t value[1024]{}; DWORD bytes = sizeof(value);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, regPath, valueName, RRF_RT_REG_SZ, nullptr, value, &bytes)) return {};
    return value;
}
static bool sameHardware(HDEVINFO set, SP_DEVINFO_DATA& device) {
    wchar_t ids[4096]{};
    if (!SetupDiGetDeviceRegistryPropertyW(set, &device, SPDRP_HARDWAREID, nullptr, reinterpret_cast<PBYTE>(ids), sizeof(ids), nullptr)) return false;
    for (auto id=ids; *id; id+=wcslen(id)+1) if (!_wcsicmp(id, L"Root\\MttVDD") || !_wcsicmp(id, L"MttVDD")) return true;
    return false;
}
static DWORD disableOwned(HDEVINFO set, SP_DEVINFO_DATA& device) {
    // Match the recovery helper's Configuration Manager path. The indirect
    // display class installer can return ERROR_INVALID_DATA while DWM still
    // holds the newly started adapter. Only touch the verified owned instance.
    ULONG status{}, problem{};
    CONFIGRET request=CR_SUCCESS;
    for(int attempt=0;attempt<100;++attempt) {
        if(CM_Get_DevNode_Status(&status,&problem,device.DevInst,0)==CR_SUCCESS &&
           problem==CM_PROB_DISABLED)return ERROR_SUCCESS;
        if(attempt%5==0) {
            request=CM_Disable_DevNode(device.DevInst,CM_DISABLE_UI_NOT_OK|CM_DISABLE_PERSIST);
            if(request!=CR_SUCCESS)
                std::cout << "Owned display disable retry: " << request << std::endl;
        }
        Sleep(100);
    }
    if(CM_Get_DevNode_Status(&status,&problem,device.DevInst,0)==CR_SUCCESS &&
       problem==CM_PROB_DISABLED)return ERROR_SUCCESS;
    return request==CR_SUCCESS ? ERROR_TIMEOUT : CM_MapCrToWin32Err(request,ERROR_GEN_FAILURE);
}
// The upstream driver has one machine-wide configuration pointer. Refuse
// co-existence with independent adapters above; preserve any previous pointer
// and restore it only while the value still belongs to this installation.
static DWORD configure(const std::wstring& inf) {
    const auto slash=inf.find_last_of(L"\\/");
    if(slash==std::wstring::npos)return ERROR_INVALID_PARAMETER;
    const auto path=inf.substr(0,slash);
    if(GetFileAttributesW((path+L"\\vdd_settings.xml").c_str())==INVALID_FILE_ATTRIBUTES)return ERROR_FILE_NOT_FOUND;
    HKEY app=nullptr,driver=nullptr;
    DWORD error=RegCreateKeyExW(HKEY_LOCAL_MACHINE,regPath,0,nullptr,0,KEY_READ|KEY_WRITE,nullptr,&app,nullptr);
    if(error)return error;
    error=RegCreateKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\MikeTheTech\\VirtualDisplayDriver",0,nullptr,0,KEY_READ|KEY_WRITE,nullptr,&driver,nullptr);
    if(error){RegCloseKey(app);return error;}
    wchar_t prior[32768]{};DWORD bytes=sizeof(prior),type=0;
    DWORD read=RegQueryValueExW(driver,L"VDDPATH",nullptr,&type,reinterpret_cast<BYTE*>(prior),&bytes);
    DWORD ownedBytes=0;
    if(RegQueryValueExW(app,L"VddConfigPath",nullptr,nullptr,nullptr,&ownedBytes)==ERROR_FILE_NOT_FOUND) {
        if(read==ERROR_SUCCESS&&type!=REG_SZ)error=ERROR_INVALID_DATA;
        else if(read!=ERROR_SUCCESS&&read!=ERROR_FILE_NOT_FOUND)error=read;
        else {
            DWORD existed=read==ERROR_SUCCESS;
            error=RegSetValueExW(app,L"VddPriorExisted",0,REG_DWORD,reinterpret_cast<BYTE*>(&existed),sizeof(existed));
            if(!error&&existed)error=RegSetValueExW(app,L"VddPriorPath",0,REG_SZ,reinterpret_cast<BYTE*>(prior),bytes);
        }
    }
    if(ownedBytes) {
        wchar_t expected[32768]{};DWORD size=sizeof(expected);
        const DWORD status=RegGetValueW(app,nullptr,L"VddConfigPath",RRF_RT_REG_SZ,nullptr,expected,&size);
        if(status||read||type!=REG_SZ||_wcsicmp(expected,prior))error=ERROR_ACCESS_DENIED;
    }
    const auto length=DWORD((path.size()+1)*sizeof(wchar_t));
    if(!error)error=RegSetValueExW(app,L"VddConfigPath",0,REG_SZ,reinterpret_cast<const BYTE*>(path.c_str()),length);
    if(!error)error=RegSetValueExW(driver,L"VDDPATH",0,REG_SZ,reinterpret_cast<const BYTE*>(path.c_str()),length);
    RegCloseKey(driver);RegCloseKey(app);return error;
}
static DWORD restoreConfiguration() {
    HKEY app=nullptr,driver=nullptr;
    DWORD error=RegOpenKeyExW(HKEY_LOCAL_MACHINE,regPath,0,KEY_READ|KEY_WRITE,&app);
    if(error==ERROR_FILE_NOT_FOUND)return 0;if(error)return error;
    wchar_t owned[32768]{},current[32768]{},previous[32768]{};DWORD bytes=sizeof(owned);
    error=RegGetValueW(app,nullptr,L"VddConfigPath",RRF_RT_REG_SZ,nullptr,owned,&bytes);
    if(error==ERROR_FILE_NOT_FOUND){RegCloseKey(app);return 0;}
    if(!error)error=RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\MikeTheTech\\VirtualDisplayDriver",0,KEY_READ|KEY_WRITE,&driver);
    if(!error){
        bytes=sizeof(current);error=RegGetValueW(driver,nullptr,L"VDDPATH",RRF_RT_REG_SZ,nullptr,current,&bytes);
        if(!error&&_wcsicmp(current,owned))error=ERROR_ACCESS_DENIED;
        DWORD existed=0;bytes=sizeof(existed);
        if(!error)error=RegGetValueW(app,nullptr,L"VddPriorExisted",RRF_RT_REG_DWORD,nullptr,&existed,&bytes);
        if(!error&&existed){bytes=sizeof(previous);error=RegGetValueW(app,nullptr,L"VddPriorPath",RRF_RT_REG_SZ,nullptr,previous,&bytes);if(!error)error=RegSetValueExW(driver,L"VDDPATH",0,REG_SZ,reinterpret_cast<BYTE*>(previous),bytes);}
        else if(!error){error=RegDeleteValueW(driver,L"VDDPATH");if(error==ERROR_FILE_NOT_FOUND)error=0;}
    }
    if(!error){for(auto value:{L"VddConfigPath",L"VddPriorExisted",L"VddPriorPath"})RegDeleteValueW(app,value);}
    if(driver)RegCloseKey(driver);RegCloseKey(app);return error;
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    if (std::wstring(argv[1]) == L"restore-config") return int(restoreConfiguration());
    if (std::wstring(argv[1]) == L"stage") {
        if (argc != 3) return 2;
        wchar_t destination[MAX_PATH]{}; wchar_t* filename = nullptr;
        const bool copied = SetupCopyOEMInfW(argv[2], nullptr, SPOST_PATH, SP_COPY_NOOVERWRITE,
            destination, MAX_PATH, nullptr, &filename);
        if (!copied) return GetLastError() == ERROR_FILE_EXISTS ? 0 : int(GetLastError());
        if (!filename || !*filename) return ERROR_INVALID_DATA;
        HKEY key;
        DWORD error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, regPath, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
        if (!error) {
            error = RegSetValueExW(key, L"OwnedDriverInf", 0, REG_SZ, reinterpret_cast<const BYTE*>(filename),
                DWORD((wcslen(filename) + 1) * sizeof(wchar_t)));
            RegCloseKey(key);
        }
        if (error) SetupUninstallOEMInfW(filename, 0, nullptr);
        return int(error);
    }
    if (std::wstring(argv[1]) == L"remove-package") {
        wchar_t filename[MAX_PATH]{}; DWORD size = sizeof(filename);
        const auto read = RegGetValueW(HKEY_LOCAL_MACHINE, regPath, L"OwnedDriverInf", RRF_RT_REG_SZ, nullptr, filename, &size);
        if (read == ERROR_FILE_NOT_FOUND) return 0;
        if (read) return int(read);
        const std::wstring inf(filename);
        if (inf.size() < 8 || inf.substr(0, 3) != L"oem" || inf.substr(inf.size() - 4) != L".inf" ||
            inf.find_first_not_of(L"0123456789", 3) != inf.size() - 4) return ERROR_INVALID_DATA;
        // No FORCEDELETE: Windows retains a package used by any other device,
        // including a currently disconnected device.
        if (!SetupUninstallOEMInfW(filename, 0, nullptr) && GetLastError() != ERROR_FILE_NOT_FOUND)
            return int(GetLastError());
        HKEY key;
        if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, regPath, 0, KEY_SET_VALUE, &key)) {
            RegDeleteValueW(key, L"OwnedDriverInf"); RegCloseKey(key);
        }
        return 0;
    }
    const auto owned = owner();
    auto set = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, 0);
    if (set == INVALID_HANDLE_VALUE) return int(GetLastError());
    SP_DEVINFO_DATA device{}; device.cbSize = sizeof(device);
    for (DWORD i=0; SetupDiEnumDeviceInfo(set, i, &device); ++i) {
        if (!sameHardware(set, device)) continue;
        wchar_t id[1024]{};
        if (!SetupDiGetDeviceInstanceIdW(set, &device, id, 1024, nullptr)) { SetupDiDestroyDeviceInfoList(set); return 3; }
        if (owned.empty() || _wcsicmp(owned.c_str(), id)) {
            if (std::wstring(argv[1]) == L"remove") continue;
            std::cerr << "An independently installed virtual display exists; it was left unchanged." << std::endl;
            SetupDiDestroyDeviceInfoList(set); return 4;
        }
        if (std::wstring(argv[1]) == L"remove") {
            const bool ok = SetupDiCallClassInstaller(DIF_REMOVE, set, &device);
            DWORD error = ok ? ERROR_SUCCESS : GetLastError();
            SP_DEVINSTALL_PARAMS_W parameters{}; parameters.cbSize = sizeof(parameters);
            if (ok && SetupDiGetDeviceInstallParamsW(set, &device, &parameters) &&
                (parameters.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART))) error = ERROR_SUCCESS_REBOOT_REQUIRED;
            if (ok) { HKEY key; if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE,regPath,0,KEY_SET_VALUE,&key)) { RegDeleteValueW(key,valueName); RegCloseKey(key); } }
            SetupDiDestroyDeviceInfoList(set); return int(error);
        }
        const auto disabled=disableOwned(set,device);
        SetupDiDestroyDeviceInfoList(set);
        if(disabled)return int(disabled);
        return argc==3 ? int(configure(argv[2])) : ERROR_INVALID_PARAMETER;
    }
    SetupDiDestroyDeviceInfoList(set);
    if (std::wstring(argv[1]) == L"remove") return 0;
    if (std::wstring(argv[1]) != L"install" || argc != 3) return 2;
    const DWORD configured=configure(argv[2]);if(configured)return int(configured);
    set = SetupDiCreateDeviceInfoList(&GUID_DEVCLASS_DISPLAY, nullptr);
    if (set == INVALID_HANDLE_VALUE) return int(GetLastError());
    device = {}; device.cbSize=sizeof(device);
    bool registered = false;
    bool ok = SetupDiCreateDeviceInfoW(set,L"DeskPortVirtualDisplay",&GUID_DEVCLASS_DISPLAY,L"DeskPort private virtual display",nullptr,DICD_GENERATE_ID,&device)
        && SetupDiSetDeviceRegistryPropertyW(set,&device,SPDRP_HARDWAREID,reinterpret_cast<const BYTE*>(hardware),sizeof(hardware))
        && SetupDiCallClassInstaller(DIF_REGISTERDEVICE,set,&device);
    registered = ok;
    BOOL reboot = FALSE;
    if (ok) ok = UpdateDriverForPlugAndPlayDevicesW(nullptr, L"Root\\MttVDD", argv[2], INSTALLFLAG_NONINTERACTIVE, &reboot);
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    if (ok) {
        wchar_t id[1024]{}; HKEY key;
        ok = SetupDiGetDeviceInstanceIdW(set,&device,id,1024,nullptr) &&
            RegCreateKeyExW(HKEY_LOCAL_MACHINE,regPath,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS;
        if (ok) {
            error=RegSetValueExW(key,valueName,0,REG_SZ,reinterpret_cast<const BYTE*>(id),DWORD((wcslen(id)+1)*sizeof(wchar_t)));
            RegCloseKey(key); ok=error==ERROR_SUCCESS;
        } else { error=GetLastError(); if (!error) error=ERROR_GEN_FAILURE; }
    }
    if (ok) { error=disableOwned(set,device); if(error==ERROR_SUCCESS_REBOOT_REQUIRED)reboot=TRUE;else if(error)ok=false; }
    if (!ok && registered) SetupDiCallClassInstaller(DIF_REMOVE,set,&device);
    SetupDiDestroyDeviceInfoList(set);
    std::cout << "Driver setup result: " << error << "; reboot required: " << reboot << std::endl;
    return int(ok && reboot ? ERROR_SUCCESS_REBOOT_REQUIRED : error);
}
