// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit elevated recovery/guard utility. No antivirus or trust-policy APIs.
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <devguid.h>
#include <shlobj.h>
#include <sddl.h>
#include <aclapi.h>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <cstring>
#include <algorithm>

struct Screen { wchar_t name[32]; DEVMODEW mode; DWORD primary; };
struct Snapshot {
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    std::vector<Screen> screens;
};
static std::wstring owner() {
    wchar_t value[1024]{}; DWORD bytes=sizeof(value);
    if (RegGetValueW(HKEY_LOCAL_MACHINE,L"Software\\DeskPort",L"VirtualDisplayDevice",RRF_RT_REG_SZ,nullptr,value,&bytes)) return {};
    std::wstring result(value);
    if (result.rfind(L"ROOT\\DESKPORTVIRTUALDISPLAY\\",0)!=0) return {};
    return result;
}
static DWORD deviceState(bool change=false, bool enable=false) {
    const auto id=owner(); if (id.empty()) return ERROR_NOT_FOUND;
    auto set=SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY,nullptr,nullptr,0);
    if (set==INVALID_HANDLE_VALUE) return GetLastError();
    DWORD result=ERROR_NOT_FOUND; SP_DEVINFO_DATA info{};info.cbSize=sizeof(info);
    for (DWORD i=0;SetupDiEnumDeviceInfo(set,i,&info);++i) {
        wchar_t instance[1024]{},hw[4096]{};
        if (!SetupDiGetDeviceInstanceIdW(set,&info,instance,1024,nullptr) || _wcsicmp(id.c_str(),instance)) continue;
        if (!SetupDiGetDeviceRegistryPropertyW(set,&info,SPDRP_HARDWAREID,nullptr,reinterpret_cast<PBYTE>(hw),sizeof(hw),nullptr) || _wcsicmp(hw,L"Root\\MttVDD")) { result=ERROR_INVALID_DATA;break; }
        ULONG status{},problem{};
        if (CM_Get_DevNode_Status(&status,&problem,info.DevInst,0)!=CR_SUCCESS) {result=ERROR_NOT_READY;break;}
        if (!change) { std::cout<<"owned_device_problem="<<problem<<"\n";result=problem;break; }
        if ((!enable && problem==CM_PROB_DISABLED) || (enable && !problem && (status&DN_STARTED))) {result=0;break;}
        SP_PROPCHANGE_PARAMS params{};params.ClassInstallHeader.cbSize=sizeof(SP_CLASSINSTALL_HEADER);
        params.ClassInstallHeader.InstallFunction=DIF_PROPERTYCHANGE;
        params.StateChange=enable?DICS_ENABLE:DICS_DISABLE;params.Scope=DICS_FLAG_GLOBAL;
        if (!SetupDiSetClassInstallParamsW(set,&info,&params.ClassInstallHeader,sizeof(params)) || !SetupDiCallClassInstaller(DIF_PROPERTYCHANGE,set,&info)) {result=GetLastError();break;}
        SP_DEVINSTALL_PARAMS_W install{};install.cbSize=sizeof(install);
        const bool restartRequested=SetupDiGetDeviceInstallParamsW(set,&info,&install) &&
            (install.Flags&(DI_NEEDREBOOT|DI_NEEDRESTART));
        result=ERROR_TIMEOUT;
        for (int attempt=0;attempt<50;++attempt) {
            if (CM_Get_DevNode_Status(&status,&problem,info.DevInst,0)==CR_SUCCESS &&
                (enable ? (!problem && (status&DN_STARTED)) : problem==CM_PROB_DISABLED)) {result=0;break;}
            Sleep(100);
        }
        // SetupAPI can retain a restart flag while this particular enable or
        // disable transition has already completed. Do not abandon recovery
        // before checking Config Manager's actual device state.
        if(result!=0&&restartRequested)result=ERROR_SUCCESS_REBOOT_REQUIRED;
        break;
    }
    SetupDiDestroyDeviceInfoList(set);return result;
}
static bool capture(Snapshot& s) {
    for(int attempt=0;attempt<5;++attempt) {
        UINT32 np{},nm{};
        if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS|QDC_VIRTUAL_MODE_AWARE,&np,&nm)) return false;
        if(np>64 || nm>512 || !np) return false;
        s.paths.resize(np);s.modes.resize(nm);
        LONG rc=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS|QDC_VIRTUAL_MODE_AWARE,&np,s.paths.data(),&nm,s.modes.data(),nullptr);
        if(rc==ERROR_INSUFFICIENT_BUFFER)continue;
        if(rc)return false;
        s.paths.resize(np);s.modes.resize(nm);s.screens.clear();
        DISPLAY_DEVICEW d{};d.cb=sizeof(d);
        for(DWORD i=0;EnumDisplayDevicesW(nullptr,i,&d,0);++i) {
            if(d.StateFlags&DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) {
                Screen item{};std::copy_n(d.DeviceName,32,item.name);item.mode.dmSize=sizeof(DEVMODEW);
                if(!EnumDisplaySettingsExW(d.DeviceName,ENUM_CURRENT_SETTINGS,&item.mode,0))return false;
                item.primary=!!(d.StateFlags&DISPLAY_DEVICE_PRIMARY_DEVICE);s.screens.push_back(item);
            }
            d={};d.cb=sizeof(d);
        }
        return !s.screens.empty() && s.screens.size()<=64;
    }
    return false;
}
static bool save(const std::wstring& path,const Snapshot& s) {
    DWORD header[]{0x44505232,2,DWORD(s.paths.size()),DWORD(s.modes.size()),DWORD(s.screens.size()),sizeof(DISPLAYCONFIG_PATH_INFO),sizeof(DISPLAYCONFIG_MODE_INFO),sizeof(Screen)};
    auto h=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return false;
    auto write=[h](const void* p,DWORD n){DWORD wrote{};return WriteFile(h,p,n,&wrote,nullptr)&&wrote==n;};
    bool ok=write(header,sizeof(header))&&write(s.paths.data(),DWORD(s.paths.size()*sizeof(s.paths[0])))&&write(s.modes.data(),DWORD(s.modes.size()*sizeof(s.modes[0])))&&write(s.screens.data(),DWORD(s.screens.size()*sizeof(Screen)))&&FlushFileBuffers(h);
    CloseHandle(h);return ok;
}
static bool load(const std::wstring& path,Snapshot& s) {
    std::ifstream f(std::filesystem::path(path),std::ios::binary);DWORD h[8]{};
    if(!f.read(reinterpret_cast<char*>(h),sizeof(h))||h[0]!=0x44505232||h[1]!=2||!h[2]||h[2]>64||h[3]>512||!h[4]||h[4]>64||h[5]!=sizeof(DISPLAYCONFIG_PATH_INFO)||h[6]!=sizeof(DISPLAYCONFIG_MODE_INFO)||h[7]!=sizeof(Screen))return false;
    s.paths.resize(h[2]);s.modes.resize(h[3]);s.screens.resize(h[4]);
    f.read(reinterpret_cast<char*>(s.paths.data()),s.paths.size()*sizeof(s.paths[0]));
    f.read(reinterpret_cast<char*>(s.modes.data()),s.modes.size()*sizeof(s.modes[0]));
    f.read(reinterpret_cast<char*>(s.screens.data()),s.screens.size()*sizeof(Screen));
    return bool(f)&&f.peek()==std::char_traits<char>::eof();
}
static bool equivalent(const Snapshot& before,const Snapshot& after) {
    if(before.screens.size()!=after.screens.size())return false;
    for(const auto& a:before.screens) {
        auto it=std::find_if(after.screens.begin(),after.screens.end(),[&](const Screen& b){return !_wcsicmp(a.name,b.name);});
        if(it==after.screens.end())return false;
        const auto& b=*it;
        if(a.primary!=b.primary||a.mode.dmPelsWidth!=b.mode.dmPelsWidth||a.mode.dmPelsHeight!=b.mode.dmPelsHeight||a.mode.dmPosition.x!=b.mode.dmPosition.x||a.mode.dmPosition.y!=b.mode.dmPosition.y||a.mode.dmDisplayOrientation!=b.mode.dmDisplayOrientation||a.mode.dmBitsPerPel!=b.mode.dmBitsPerPel||a.mode.dmDisplayFrequency!=b.mode.dmDisplayFrequency)return false;
    }
    return true;
}
static int restoreSnapshot(Snapshot before) {
    const auto disabled=deviceState(true,false);
    std::cout<<"disable_result="<<disabled<<std::endl;
    if(disabled)return int(disabled);
    LONG rc=ERROR_INVALID_STATE;
    // PnP disable completion precedes the display topology transition. First
    // accept an already restored layout, otherwise retry the original snapshot
    // while Windows retires the indirect output. Never persist new defaults.
    for(int i=0;i<50;++i) {
        Snapshot now;
        if(capture(now)&&equivalent(before,now)&&deviceState()==CM_PROB_DISABLED){std::cout<<"RECOVERY_VERIFIED"<<std::endl;return 0;}
        if(i%5==0) {
            rc=SetDisplayConfig(UINT32(before.paths.size()),before.paths.data(),UINT32(before.modes.size()),before.modes.data(),SDC_APPLY|SDC_USE_SUPPLIED_DISPLAY_CONFIG|SDC_VIRTUAL_MODE_AWARE);
            // Removing an indirect adapter can invalidate saved target timing
            // indices. Let Windows recompute them, but only accept success once
            // the physical layout, rotation, frequency and pixel format match.
            if(rc==ERROR_INVALID_DATA)
                rc=SetDisplayConfig(UINT32(before.paths.size()),before.paths.data(),UINT32(before.modes.size()),before.modes.data(),SDC_APPLY|SDC_USE_SUPPLIED_DISPLAY_CONFIG|SDC_VIRTUAL_MODE_AWARE|SDC_ALLOW_CHANGES);
            if(rc==ERROR_ACCESS_DENIED||rc==ERROR_NOT_SUPPORTED)return int(rc);
        }
        Sleep(100);
    }
    std::cout<<"restore_topology_result="<<rc<<std::endl;
    if(rc)return int(rc);
    return ERROR_INVALID_STATE;
}
static int restore(const std::wstring& path) {
    Snapshot before;if(!load(path,before))return ERROR_INVALID_DATA;
    return restoreSnapshot(std::move(before));
}
// Restore synchronously before Windows ends the interactive session. Loading
// user32 means a console control handler alone cannot cover logoff/shutdown.
static Snapshot* shutdownSnapshot=nullptr;
static HANDLE shutdownRelease=nullptr;
static LRESULT CALLBACK recoveryWindow(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_QUERYENDSESSION || (message==WM_ENDSESSION&&wp)) {
        if(shutdownSnapshot)restoreSnapshot(*shutdownSnapshot);
        if(shutdownRelease)SetEvent(shutdownRelease);
        return TRUE;
    }
    return DefWindowProcW(window,message,wp,lp);
}
// Production leases accept no file paths or device identifiers from the client.
// Snapshot storage is fixed, administrator-owned, and excludes reparse points.
static std::wstring recoveryDirectory() {
    PWSTR base=nullptr;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData,0,nullptr,&base)))return {};
    std::wstring path(base);CoTaskMemFree(base);
    PSECURITY_DESCRIPTOR sd=nullptr;
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&sd,nullptr))return {};
    SECURITY_ATTRIBUTES sa{sizeof(sa),sd,FALSE};
    for(const auto* part:{L"\\DeskPort",L"\\Recovery"}) {
        path+=part;
        if(!CreateDirectoryW(path.c_str(),&sa)&&GetLastError()!=ERROR_ALREADY_EXISTS){LocalFree(sd);return {};}
        const auto attr=GetFileAttributesW(path.c_str());
        if(attr==INVALID_FILE_ATTRIBUTES||(attr&FILE_ATTRIBUTE_REPARSE_POINT)||!(attr&FILE_ATTRIBUTE_DIRECTORY)){LocalFree(sd);return {};}
        PSID ownerSid=nullptr;PSECURITY_DESCRIPTOR existing=nullptr;
        if(GetNamedSecurityInfoW(path.data(),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION,&ownerSid,nullptr,nullptr,nullptr,&existing)!=ERROR_SUCCESS){LocalFree(sd);return {};}
        const bool trusted=IsWellKnownSid(ownerSid,WinBuiltinAdministratorsSid)||IsWellKnownSid(ownerSid,WinLocalSystemSid);
        LocalFree(existing);if(!trusted){LocalFree(sd);return {};}
    }
    if(!SetFileSecurityW(path.c_str(),DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,sd)){LocalFree(sd);return {};}
    LocalFree(sd);return path;
}
static int productionLease(DWORD pid,const std::wstring& nonce) {
    if(nonce.size()!=36||nonce.find_first_not_of(L"0123456789abcdefABCDEF-")!=std::wstring::npos)return ERROR_INVALID_PARAMETER;
    auto parent=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!parent)return int(GetLastError());
    wchar_t own[32768]{},peer[32768]{};DWORD length=32768,os{},ps{};
    GetModuleFileNameW(nullptr,own,32768);
    if(!QueryFullProcessImageNameW(parent,0,peer,&length)||!ProcessIdToSessionId(pid,&ps)||!ProcessIdToSessionId(GetCurrentProcessId(),&os)||ps!=os){CloseHandle(parent);return ERROR_ACCESS_DENIED;}
    const auto expected=(std::filesystem::path(own).parent_path()/L"deskport-display.exe").wstring();
    if(_wcsicmp(expected.c_str(),peer)){CloseHandle(parent);return ERROR_ACCESS_DENIED;}
    const auto eventPrefix=L"Local\\DeskPort.Display."+nonce;
    auto release=OpenEventW(SYNCHRONIZE,FALSE,(eventPrefix+L".release").c_str());
    auto ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,(eventPrefix+L".ready").c_str());
    if(!release||!ready){if(release)CloseHandle(release);if(ready)CloseHandle(ready);CloseHandle(parent);return ERROR_ACCESS_DENIED;}
    auto mutex=CreateMutexW(nullptr,TRUE,L"Global\\DeskPort.Display.RecoveryLease");
    if(!mutex||GetLastError()==ERROR_ALREADY_EXISTS){if(mutex)CloseHandle(mutex);CloseHandle(release);CloseHandle(ready);CloseHandle(parent);return ERROR_BUSY;}
    const auto directory=recoveryDirectory();
    Snapshot original;
    int result=0;bool captured=false;
    const auto snapshot=directory+L"\\"+nonce+L".bin";
    if(directory.empty()||deviceState()!=CM_PROB_DISABLED||!capture(original)||!save(snapshot,original))result=ERROR_INVALID_STATE;
    else {
        captured=true;
        // Parent death during UAC approval must never activate the device.
        if(WaitForSingleObject(parent,0)!=WAIT_TIMEOUT)result=ERROR_CANCELLED;
        else {
            result=int(deviceState(true,true));
            if(!result){
                WNDCLASSW cls{};cls.lpfnWndProc=recoveryWindow;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"DeskPortDisplayRecovery";
                RegisterClassW(&cls);
                shutdownSnapshot=&original;shutdownRelease=release;
                const auto window=CreateWindowExW(0,cls.lpszClassName,L"DeskPort display recovery",0,0,0,0,0,nullptr,nullptr,cls.hInstance,nullptr);
                if(!window)result=int(GetLastError());
                else {
                    SetEvent(ready);HANDLE watched[]{parent,release};
                    for(;;){
                        const auto wait=MsgWaitForMultipleObjects(2,watched,FALSE,INFINITE,QS_ALLINPUT);
                        if(wait!=WAIT_OBJECT_0+2)break;
                        MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
                    }
                    DestroyWindow(window);
                }
                shutdownSnapshot=nullptr;shutdownRelease=nullptr;
            }
        }
    }
    if(captured){const int recovered=restoreSnapshot(original);if(recovered)result=recovered;else DeleteFileW(snapshot.c_str());}
    ReleaseMutex(mutex);CloseHandle(mutex);CloseHandle(release);CloseHandle(ready);CloseHandle(parent);
    return result;
}
static void marker(const std::wstring& path,const std::string& value) {std::ofstream f(std::filesystem::path(path),std::ios::binary);f<<value;f.flush();}
int wmain(int argc,wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(argc<2)return 2;
    const std::wstring action=argv[1];
    if(action==L"boot-disable"&&argc==2){
        // A boot recovery task must not race a new, explicitly authorized lease.
        auto lock=CreateMutexW(nullptr,TRUE,L"Global\\DeskPort.Display.RecoveryLease");
        if(!lock)return int(GetLastError());
        if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(lock);return ERROR_BUSY;}
        const DWORD result=deviceState(true,false);ReleaseMutex(lock);CloseHandle(lock);
        return result==ERROR_NOT_FOUND?0:int(result);
    }
    if(action==L"lease"&&argc==4){wchar_t* end=nullptr;DWORD pid=wcstoul(argv[2],&end,10);return pid&&!*end?productionLease(pid,argv[3]):2;}
    if(action==L"inspect") {
        const auto status=deviceState();Snapshot s;if(!capture(s))return 3;
        std::cout<<"active_outputs="<<s.screens.size()<<" virtual_rect="<<GetSystemMetrics(SM_XVIRTUALSCREEN)<<","<<GetSystemMetrics(SM_YVIRTUALSCREEN)<<","<<GetSystemMetrics(SM_CXVIRTUALSCREEN)<<","<<GetSystemMetrics(SM_CYVIRTUALSCREEN)<<std::endl;
        for(const auto& d:s.screens)std::wcout<<d.name<<L" primary="<<d.primary<<L" xy="<<d.mode.dmPosition.x<<L","<<d.mode.dmPosition.y<<L" size="<<d.mode.dmPelsWidth<<L"x"<<d.mode.dmPelsHeight<<std::endl;
        return status==CM_PROB_DISABLED?0:1;
    }
    if(action==L"snapshot"&&argc==3) {Snapshot s;return deviceState()==CM_PROB_DISABLED&&capture(s)&&save(argv[2],s)?0:3;}
    if(action==L"recover"&&argc==3)return restore(argv[2]);
    if(action!=L"guard"||argc!=5)return 2;
    wchar_t* end=nullptr;const DWORD pid=wcstoul(argv[3],&end,10);if(!pid||*end)return 2;
    const DWORD seconds=wcstoul(argv[4],&end,10);if(*end||seconds<5||seconds>300)return 2;
    auto parent=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!parent)return int(GetLastError());
    DWORD ownSession{},parentSession{};
    if(!ProcessIdToSessionId(GetCurrentProcessId(),&ownSession)||!ProcessIdToSessionId(pid,&parentSession)||ownSession!=parentSession){CloseHandle(parent);return ERROR_ACCESS_DENIED;}
    const std::wstring path=argv[2];Snapshot saved;
    if(deviceState()!=CM_PROB_DISABLED||!capture(saved)||!save(path,saved)){CloseHandle(parent);return 3;}
    marker(path+L".armed","snapshot durable; guard active\n");
    DWORD result=0;const auto deadline=GetTickCount64()+seconds*1000ULL;
    try {
        bool enabled=false;
        while(GetTickCount64()<deadline&&WaitForSingleObject(parent,100)==WAIT_TIMEOUT) {
            if(GetFileAttributesW((path+L".release").c_str())!=INVALID_FILE_ATTRIBUTES)break;
            if(!enabled&&GetFileAttributesW((path+L".enable").c_str())!=INVALID_FILE_ATTRIBUTES) {
                result=deviceState(true,true);if(result)break;enabled=true;
                marker(path+L".active","owned device enabled\n");
            }
        }
    } catch(...) {result=ERROR_UNHANDLED_EXCEPTION;}
    const int recovered=restore(path);CloseHandle(parent);
    marker(path+L".result","operation="+std::to_string(result)+" recovery="+std::to_string(recovered)+"\n");
    return recovered?recovered:int(result);
}
