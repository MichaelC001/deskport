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
#include <chrono>
#include <filesystem>
#include <iostream>
#include <cstring>
#include <algorithm>
#include "protected-path.h"
#include "display-request.h"
#include "display-service-ipc.h"
#include "display-dpi.h"

// The broker must remain observable without owning (and killing) this guard.
// Recovery still runs after the broker exits unexpectedly.
static HANDLE brokerProcess=nullptr, brokerStop=nullptr;

// DPI belongs to the interactive user, never to SYSTEM's profile. Keep the
// authenticated helper's token alive through helper/broker death and logoff.
// Protected journal I/O always runs after reverting; only the bounded CCD call
// impersonates the original user. No client-selected device/path enters here.
class DpiLease {
    HANDLE user=nullptr;
    std::wstring journal,instance,sid;
    struct Record { DWORD magic=0x44504449,version=2;int percentage=0,relative=0,minimum=0;DWORD sidLength=0,instanceLength=0; };
    DWORD observe(deskport::display_dpi::Source& source,deskport::display_dpi::Range& range) {
        auto rc=deskport::display_dpi::ownedSource(instance,source);if(rc)return rc;
        if(!user||!ImpersonateLoggedOnUser(user))return ERROR_ACCESS_DENIED;
        rc=deskport::display_dpi::query(source,range);
        if(!RevertToSelf())ExitProcess(ERROR_ACCESS_DENIED);
        return rc;
    }
    DWORD apply(int percentage,bool relativeStep=false) {
        deskport::display_dpi::Source source{};deskport::display_dpi::Range range{};
        auto rc=observe(source,range);if(rc)return rc;int relative=0;
        if(relativeStep) {relative=percentage;if(relative<range.minimum||relative>range.maximum)return ERROR_NOT_SUPPORTED;}
        else if(!deskport::display_dpi::relative(range,percentage,relative))return ERROR_NOT_SUPPORTED;
        // Re-resolve immediately before writing; source IDs are retired when
        // the VDD is recreated. A stale source must not target another output.
        deskport::display_dpi::Source current{};
        rc=deskport::display_dpi::ownedSource(instance,current);
        if(rc||!deskport::display_dpi::same(source.adapter,current.adapter)||source.id!=current.id)return rc?rc:ERROR_RETRY;
        if(!ImpersonateLoggedOnUser(user))return ERROR_ACCESS_DENIED;
        rc=deskport::display_dpi::set(current,relative);
        if(!RevertToSelf())ExitProcess(ERROR_ACCESS_DENIED);
        return rc;
    }
    DWORD load(int& percentage,int* step=nullptr) {
        auto file=CreateFileW(journal.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(file==INVALID_HANDLE_VALUE)return GetLastError();
        auto read=[file](void* p,DWORD n){DWORD got=0;return ReadFile(file,p,n,&got,nullptr)&&got==n;};
        Record record{};bool ok=read(&record,sizeof(record))&&record.magic==0x44504449&&record.version==2&&
            record.sidLength>0&&record.sidLength<=256&&record.instanceLength>0&&record.instanceLength<=1024;
        std::wstring savedSid,savedInstance;
        if(ok){savedSid.resize(record.sidLength);savedInstance.resize(record.instanceLength);
            ok=read(savedSid.data(),DWORD(savedSid.size()*sizeof(wchar_t)))&&read(savedInstance.data(),DWORD(savedInstance.size()*sizeof(wchar_t)));}
        LARGE_INTEGER size{};ok=ok&&GetFileSizeEx(file,&size)&&size.QuadPart==LONGLONG(sizeof(record)+(savedSid.size()+savedInstance.size())*sizeof(wchar_t));
        CloseHandle(file);
        if(!ok||savedSid!=sid||_wcsicmp(savedInstance.c_str(),instance.c_str()))return ERROR_ACCESS_DENIED;
        bool known=false;for(const auto value:deskport::display_dpi::percentages)if(record.percentage==value)known=true;
        const deskport::display_dpi::Range savedRange{record.minimum,record.relative,record.relative>0?record.relative:0};
        if(!known||!deskport::display_dpi::valid(savedRange)||deskport::display_dpi::percent(savedRange)!=record.percentage)return ERROR_INVALID_DATA;
        percentage=record.percentage;if(step)*step=record.relative;return 0;
    }
public:
    DpiLease(HANDLE parent,std::wstring path,std::wstring device):journal(std::move(path)),instance(std::move(device)) {
        if(OpenProcessToken(parent,TOKEN_QUERY|TOKEN_DUPLICATE,&user)) {
            const auto data=deskport::display_service::tokenInfo(user,TokenUser);LPWSTR text=nullptr;
            if(!data.empty()&&ConvertSidToStringSidW(reinterpret_cast<const TOKEN_USER*>(data.data())->User.Sid,&text)) {sid=text;LocalFree(text);}
        }
    }
    ~DpiLease(){if(user)CloseHandle(user);}
    DWORD inspect(DWORD& percentage) {
        deskport::display_dpi::Source source{};deskport::display_dpi::Range range{};
        auto rc=observe(source,range);if(!rc)percentage=DWORD(deskport::display_dpi::percent(range));return rc;
    }
    DWORD capture() {
        int old=0;const auto exists=load(old);if(!exists)return 0;if(exists!=ERROR_FILE_NOT_FOUND)return exists;
        deskport::display_dpi::Source source{};deskport::display_dpi::Range range{};auto rc=observe(source,range);if(rc)return rc;
        const auto percentage=deskport::display_dpi::percent(range);
        if(sid.empty()||sid.size()>256||instance.empty()||instance.size()>1024)return ERROR_ACCESS_DENIED;
        Record record;record.percentage=int(percentage);record.relative=range.current;record.minimum=range.minimum;record.sidLength=DWORD(sid.size());record.instanceLength=DWORD(instance.size());
        auto file=CreateFileW(journal.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return GetLastError();
        auto write=[file](const void* p,DWORD n){DWORD wrote=0;return WriteFile(file,p,n,&wrote,nullptr)&&wrote==n;};
        const bool ok=write(&record,sizeof(record))&&write(sid.data(),DWORD(sid.size()*sizeof(wchar_t)))&&
            write(instance.data(),DWORD(instance.size()*sizeof(wchar_t)))&&FlushFileBuffers(file);
        CloseHandle(file);if(!ok){DeleteFileW(journal.c_str());return ERROR_WRITE_FAULT;}return 0;
    }
    DWORD change(DWORD scale,DWORD& percentage) {
        int old=0;auto rc=load(old);if(rc)return rc;
        rc=apply(int(scale)*100);if(!rc)rc=inspect(percentage);
        return !rc&&percentage!=scale*100?ERROR_INVALID_STATE:rc;
    }
    DWORD restore(bool retire=true,bool retiringSource=false) {
        // Windows persists a step relative to the mode-dependent recommendation.
        // Retiring a source must restore that saved step; restoring its absolute
        // percentage at another resolution would change the next baseline DPI.
        int percentage=0,step=0;auto rc=load(percentage,&step);if(rc==ERROR_FILE_NOT_FOUND)return 0;if(rc)return rc;
        const auto deadline=GetTickCount64()+3000;
        do{rc=apply(retiringSource?step:percentage,retiringSource);if(!rc)break;Sleep(100);}while(GetTickCount64()<deadline);
        if(!rc&&retire&&!DeleteFileW(journal.c_str()))rc=GetLastError();
        return rc;
    }
};

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
        bool requested=false,restartRequested=false;DWORD requestError=0;
        if(!enable) {
            // Disable the verified owned instance through Configuration Manager.
            // The display class installer intermittently rejects a second
            // property change in the same lease process with ERROR_INVALID_DATA.
            const auto rc=CM_Disable_DevNode(info.DevInst,CM_DISABLE_UI_NOT_OK|CM_DISABLE_PERSIST);
            requested=rc==CR_SUCCESS;requestError=CM_MapCrToWin32Err(rc,ERROR_GEN_FAILURE);
        } else {
            SP_PROPCHANGE_PARAMS params{};params.ClassInstallHeader.cbSize=sizeof(SP_CLASSINSTALL_HEADER);
            params.ClassInstallHeader.InstallFunction=DIF_PROPERTYCHANGE;
            params.StateChange=DICS_ENABLE;params.Scope=DICS_FLAG_GLOBAL;
            if (!SetupDiSetClassInstallParamsW(set,&info,&params.ClassInstallHeader,sizeof(params))) {result=GetLastError();break;}
            requested=SetupDiCallClassInstaller(DIF_PROPERTYCHANGE,set,&info);
            requestError=requested?ERROR_SUCCESS:GetLastError();
            SP_DEVINSTALL_PARAMS_W install{};install.cbSize=sizeof(install);
            restartRequested=SetupDiGetDeviceInstallParamsW(set,&info,&install) &&
                (install.Flags&(DI_NEEDREBOOT|DI_NEEDRESTART));
        }
        result=ERROR_TIMEOUT;
        for (int attempt=0;attempt<100;++attempt) {
            if (CM_Get_DevNode_Status(&status,&problem,info.DevInst,0)==CR_SUCCESS &&
                (enable ? (!problem && (status&DN_STARTED)) : problem==CM_PROB_DISABLED)) {result=0;break;}
            if(!enable&&attempt>0&&attempt%5==0) {
                // A just-ended capture/mode change can temporarily veto removal.
                // A rejected request is not queued: retry it after teardown,
                // rather than merely waiting for a state change that cannot occur.
                const auto rc=CM_Disable_DevNode(info.DevInst,CM_DISABLE_UI_NOT_OK|CM_DISABLE_PERSIST);
                requested=rc==CR_SUCCESS;requestError=CM_MapCrToWin32Err(rc,ERROR_GEN_FAILURE);
                if(!requested)std::cout<<"disable_retry_cr="<<rc<<std::endl;
            }
            Sleep(100);
        }
        // SetupAPI can retain a restart flag while this particular enable or
        // disable transition has already completed. Do not abandon recovery
        // before checking Config Manager's actual device state.
        // An indirect-display class installer may report ERROR_INVALID_DATA
        // after requesting a transition that subsequently completes. The
        // observed device state above is authoritative; retain the API error
        // only when the requested state was never reached.
        if(result!=0)result=!requested?requestError:restartRequested?ERROR_SUCCESS_REBOOT_REQUIRED:result;
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
static int runWorker(const std::wstring& arguments) {
    // The process that enabled the indirect adapter can retain display-class
    // handles which veto its later removal. Restore from a fresh, bounded
    // worker, using only our own binary and the guardian's protected snapshot.
    wchar_t executable[32768]{};
    if(!GetModuleFileNameW(nullptr,executable,32768))return int(GetLastError());
    std::wstring command=L"\""+std::wstring(executable)+L"\" "+arguments;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
    if(!CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child))return int(GetLastError());
    CloseHandle(child.hThread);
    DWORD result=ERROR_TIMEOUT;
    if(WaitForSingleObject(child.hProcess,20000)==WAIT_OBJECT_0)GetExitCodeProcess(child.hProcess,&result);
    else {TerminateProcess(child.hProcess,ERROR_TIMEOUT);WaitForSingleObject(child.hProcess,5000);}
    CloseHandle(child.hProcess);return int(result);
}
// Restore synchronously before Windows ends the interactive session. Loading
// user32 means a console control handler alone cannot cover logoff/shutdown.
static Snapshot* shutdownSnapshot=nullptr;
static HANDLE shutdownRelease=nullptr;
static DpiLease* shutdownDpi=nullptr;
static LRESULT CALLBACK recoveryWindow(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_QUERYENDSESSION || (message==WM_ENDSESSION&&wp)) {
        if(shutdownDpi)shutdownDpi->restore(true,true);
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
// Keep one negotiated mode in the application's administrator-owned XML.
// This does not use the vendor's global command pipe or alter driver trust.
static DWORD configureMode(DWORD width,DWORD height) {
    if(width<640||width>7680||height<360||height>4320||width%4||height%4)return ERROR_INVALID_PARAMETER;
    wchar_t ownedPath[32768]{},vendorPath[32768]{};DWORD n=sizeof(ownedPath),m=sizeof(vendorPath);
    if(RegGetValueW(HKEY_LOCAL_MACHINE,L"Software\\DeskPort",L"VddConfigPath",RRF_RT_REG_SZ,nullptr,ownedPath,&n)||
       RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\MikeTheTech\\VirtualDisplayDriver",L"VDDPATH",RRF_RT_REG_SZ,nullptr,vendorPath,&m)||
       _wcsicmp(ownedPath,vendorPath))return ERROR_INVALID_STATE;
    const auto path=std::filesystem::path(ownedPath)/L"vdd_settings.xml";
    if(!protectedExecutable(path.wstring()))return ERROR_ACCESS_DENIED;
    std::ifstream in(path,std::ios::binary);
    std::string xml((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());in.close();
    if(xml.empty()||xml.size()>65536)return ERROR_INVALID_DATA;
    const std::string begin="<!-- DeskPort negotiated mode -->",end="<!-- /DeskPort negotiated mode -->";
    auto first=xml.find(begin);
    if(first!=std::string::npos){const auto last=xml.find(end,first);if(last==std::string::npos)return ERROR_INVALID_DATA;xml.erase(first,last+end.size()-first);}
    const auto insert=xml.find("</resolutions>");if(insert==std::string::npos)return ERROR_INVALID_DATA;
    xml.insert(insert,begin+"<resolution><width>"+std::to_string(width)+"</width><height>"+std::to_string(height)+"</height><refresh_rate>60</refresh_rate></resolution>"+end);
    const auto temporary=path.wstring()+L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
    auto file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return GetLastError();
    DWORD written{};const bool ok=WriteFile(file,xml.data(),DWORD(xml.size()),&written,nullptr)&&written==xml.size()&&FlushFileBuffers(file);CloseHandle(file);
    DWORD result=ok?ERROR_SUCCESS:ERROR_WRITE_FAULT;
    if(!result&&!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))result=GetLastError();
    if(result)DeleteFileW(temporary.c_str());
    return result;
}
static int productionLease(DWORD pid,const std::wstring& nonce,std::uint64_t expectedCreated=0) {
    if(nonce.size()!=36||nonce.find_first_not_of(L"0123456789abcdefABCDEF-")!=std::wstring::npos)return ERROR_INVALID_PARAMETER;
    auto parent=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!parent)return int(GetLastError());
    if(expectedCreated&&deskport::display_service::creationTime(parent)!=expectedCreated){CloseHandle(parent);return ERROR_ACCESS_DENIED;}
    wchar_t own[32768]{},peer[32768]{};DWORD length=32768,os{},ps{};
    GetModuleFileNameW(nullptr,own,32768);
    if(!QueryFullProcessImageNameW(parent,0,peer,&length)||!ProcessIdToSessionId(pid,&ps)||!ProcessIdToSessionId(GetCurrentProcessId(),&os)||ps!=os){CloseHandle(parent);return ERROR_ACCESS_DENIED;}
    if(brokerProcess&&os!=WTSGetActiveConsoleSessionId()){CloseHandle(parent);return ERROR_ACCESS_DENIED;}
    const auto expected=(std::filesystem::path(own).parent_path()/L"deskport-display.exe").wstring();
    if(_wcsicmp(expected.c_str(),peer)){CloseHandle(parent);return ERROR_ACCESS_DENIED;}
    const auto eventPrefix=L"Local\\DeskPort.Display."+nonce;
    auto release=OpenEventW(SYNCHRONIZE|EVENT_MODIFY_STATE,FALSE,(eventPrefix+L".release").c_str());
    auto ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,(eventPrefix+L".ready").c_str());
    if(!release||!ready){if(release)CloseHandle(release);if(ready)CloseHandle(ready);CloseHandle(parent);return ERROR_ACCESS_DENIED;}
    auto request=OpenEventW(SYNCHRONIZE,FALSE,(eventPrefix+L".request").c_str());
    auto completed=OpenEventW(EVENT_MODIFY_STATE,FALSE,(eventPrefix+L".completed").c_str());
    auto mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,(eventPrefix+L".mode").c_str());
    auto mode=mapping?static_cast<DisplayModeRequest*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(DisplayModeRequest))):nullptr;
    if(!request||!completed||!mode){if(mode)UnmapViewOfFile(mode);if(mapping)CloseHandle(mapping);if(request)CloseHandle(request);if(completed)CloseHandle(completed);CloseHandle(release);CloseHandle(ready);CloseHandle(parent);return ERROR_ACCESS_DENIED;}
    const auto alive=[&]{
        return WaitForSingleObject(parent,0)==WAIT_TIMEOUT&&WaitForSingleObject(release,0)==WAIT_TIMEOUT&&
            (!brokerStop||WaitForSingleObject(brokerStop,0)==WAIT_TIMEOUT)&&
            (!brokerProcess||WaitForSingleObject(brokerProcess,0)==WAIT_TIMEOUT);
    };
    auto mutex=CreateMutexW(nullptr,TRUE,L"Global\\DeskPort.Display.RecoveryLease");
    if(!mutex||GetLastError()==ERROR_ALREADY_EXISTS){if(mutex)CloseHandle(mutex);CloseHandle(release);CloseHandle(ready);CloseHandle(parent);return ERROR_BUSY;}
    const auto directory=recoveryDirectory();
    Snapshot original;
    int result=0;bool captured=false;
    const auto snapshot=directory+L"\\"+nonce+L".bin";
    DpiLease dpi(parent,snapshot+L".dpi",owner());
    std::ofstream leaseLog;
    if(!directory.empty())leaseLog.open(std::filesystem::path(snapshot+L".log"),std::ios::binary);
    auto previousOutput=leaseLog ? std::cout.rdbuf(leaseLog.rdbuf()) : nullptr;
    // A previous guardian that was itself terminated (not just its client)
    // leaves the owned device enabled with its snapshot. Holding the lease
    // mutex proves no lease is active, so recover from that snapshot first,
    // but only one written since this boot: older ones describe stale layouts.
    if(!directory.empty()&&deviceState()!=CM_PROB_DISABLED) {
        const auto bootTime=std::filesystem::file_time_type::clock::now()-std::chrono::milliseconds(GetTickCount64());
        std::filesystem::path newest;std::filesystem::file_time_type newestTime{};
        std::error_code ec;
        for(const auto& entry:std::filesystem::directory_iterator(directory,ec)) {
            if(entry.path().extension()!=L".bin"||entry.path()==std::filesystem::path(snapshot))continue;
            const auto when=entry.last_write_time(ec);
            if(!ec&&when>bootTime&&(newest.empty()||when>newestTime)){newest=entry.path();newestTime=when;}
        }
        if(!newest.empty()) {
            DpiLease orphan(parent,newest.wstring()+L".dpi",owner());
            const auto dpiRecovered=orphan.restore(true,true);
            const int recovered=runWorker(L"recover \""+newest.wstring()+L"\"");
            std::cout<<"orphaned_snapshot_recovery="<<recovered<<std::endl;
            if(!recovered&&!dpiRecovered)DeleteFileW(newest.c_str());
        }
    }
    if(directory.empty()||deviceState()!=CM_PROB_DISABLED||!capture(original)||!save(snapshot,original))result=ERROR_INVALID_STATE;
    else {
        captured=true;
        if(!alive())result=ERROR_CANCELLED;
        else {
            // Keep driver/class-installer handles out of the long-lived guard.
            result=runWorker(L"enable-lease "+std::to_wstring(GetCurrentProcessId())+L" "+nonce);
            if(!result){
                WNDCLASSW cls{};cls.lpfnWndProc=recoveryWindow;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"DeskPortDisplayRecovery";
                RegisterClassW(&cls);
                shutdownSnapshot=&original;shutdownRelease=release;shutdownDpi=&dpi;
                const auto window=CreateWindowExW(0,cls.lpszClassName,L"DeskPort display recovery",0,0,0,0,0,nullptr,nullptr,cls.hInstance,nullptr);
                if(!window)result=int(GetLastError());
                else {
                    SetEvent(ready);HANDLE watched[]{parent,release,request,brokerStop,brokerProcess};
                    const DWORD count=brokerStop&&brokerProcess?5:3;
                    for(;;){
                        const auto wait=MsgWaitForMultipleObjects(count,watched,FALSE,INFINITE,QS_ALLINPUT);
                        // Cancellation wins even if a resize event with a lower
                        // wait index is signaled at the same time.
                        if(!alive())break;
                        if(wait==WAIT_OBJECT_0+2) {
                            MemoryBarrier();const DWORD operation=mode->operation,width=mode->width,height=mode->height,scale=mode->scale;
                            if(!deskport::display_dpi::validRequest(operation,width,height,scale)) {
                                mode->result=ERROR_INVALID_PARAMETER;MemoryBarrier();SetEvent(completed);continue;
                            }
                            if(operation!=deskport::display_dpi::ResizeMode) {
                                DWORD percentage=0;
                                auto code=operation==deskport::display_dpi::CaptureDpi?dpi.capture():
                                    operation==deskport::display_dpi::SetDpi?dpi.change(scale,percentage):
                                    operation==deskport::display_dpi::RestoreDpi?dpi.restore():
                                    operation==deskport::display_dpi::ResetDpi?dpi.restore(false,true):dpi.inspect(percentage);
                                mode->dpiPercent=percentage;mode->result=code;MemoryBarrier();SetEvent(completed);continue;
                            }
                            // Restore the saved virtual DPI before retiring its
                            // current source. Keep the journal across recreation.
                            auto resized=dpi.restore(false,true);
                            if(!resized)resized=runWorker(L"resize-lease "+std::to_wstring(GetCurrentProcessId())+L" "+nonce+L" "+std::to_wstring(width)+L" "+std::to_wstring(height));
                            if(!resized&&alive())resized=runWorker(L"enable-lease "+std::to_wstring(GetCurrentProcessId())+L" "+nonce);
                            else if(!resized)resized=ERROR_CANCELLED;
                            mode->result=DWORD(resized);MemoryBarrier();SetEvent(completed);
                            if(resized){result=resized;break;}
                            continue;
                        }
                        if(wait!=WAIT_OBJECT_0+count)break;
                        MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
                    }
                    DestroyWindow(window);
                }
                shutdownSnapshot=nullptr;shutdownRelease=nullptr;shutdownDpi=nullptr;
            }
        }
    }
    if(captured){const auto dpiRecovered=dpi.restore(true,true);if(dpiRecovered)result=int(dpiRecovered);
        const int recovered=runWorker(L"recover \""+snapshot+L"\"");std::cout<<"recovery_worker_result="<<recovered<<" dpi_recovery_result="<<dpiRecovered<<std::endl;if(recovered)result=recovered;else if(!dpiRecovered)DeleteFileW(snapshot.c_str());}
    if(previousOutput){std::cout.flush();std::cout.rdbuf(previousOutput);leaseLog.close();}
    if(!result)DeleteFileW((snapshot+L".log").c_str());
    UnmapViewOfFile(mode);CloseHandle(mapping);CloseHandle(request);CloseHandle(completed);
    ReleaseMutex(mutex);CloseHandle(mutex);CloseHandle(release);CloseHandle(ready);CloseHandle(parent);
    return result;
}
static void marker(const std::wstring& path,const std::string& value) {std::ofstream f(std::filesystem::path(path),std::ios::binary);f<<value;f.flush();}
int wmain(int argc,wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(argc<2)return 2;
    const std::wstring action=argv[1];
    if(action==L"service-lease"&&argc==7) {
        using namespace deskport::display_service;
        wchar_t *parentEnd=nullptr,*brokerEnd=nullptr,*parentTimeEnd=nullptr,*brokerTimeEnd=nullptr;
        const DWORD parent=wcstoul(argv[2],&parentEnd,10),broker=wcstoul(argv[4],&brokerEnd,10);
        const auto parentCreated=wcstoull(argv[5],&parentTimeEnd,10),brokerCreated=wcstoull(argv[6],&brokerTimeEnd,10);
        const std::wstring nonce=argv[3];
        Request request;
        if(nonce.size()!=36)return ERROR_INVALID_PARAMETER;
        for(unsigned i=0;i<36;++i)request.nonce[i]=char16_t(nonce[i]);
        if(!parent||*parentEnd||!broker||*brokerEnd||!parentCreated||*parentTimeEnd||!brokerCreated||*brokerTimeEnd||
           !validRequest(request)||!isSystem())return ERROR_ACCESS_DENIED;
        Handle process(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,broker));
        Handle stop(OpenEventW(SYNCHRONIZE,FALSE,stopName(nonce).c_str()));
        if(!process.valid()||!stop.valid()||creationTime(process)!=brokerCreated||
           !trustedServer(broker,sibling(L"deskport-display-service.exe")))return ERROR_ACCESS_DENIED;
        brokerProcess=process;brokerStop=stop;
        const int result=productionLease(parent,nonce,parentCreated);
        brokerProcess=brokerStop=nullptr;
        return result;
    }
    if((action==L"enable-lease"&&argc==4)||(action==L"resize-lease"&&argc==6)) {
        const std::wstring nonce=argv[3];wchar_t* end=nullptr;const DWORD pid=wcstoul(argv[2],&end,10);
        if(!pid||*end||nonce.size()!=36||nonce.find_first_not_of(L"0123456789abcdefABCDEF-")!=std::wstring::npos)return ERROR_INVALID_PARAMETER;
        auto guardian=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        if(!guardian)return int(GetLastError());
        wchar_t own[32768]{},peer[32768]{};DWORD length=32768,os{},ps{};
        GetModuleFileNameW(nullptr,own,32768);
        const auto directory=recoveryDirectory();Snapshot before;
        const bool authorized=QueryFullProcessImageNameW(guardian,0,peer,&length)&&!_wcsicmp(own,peer)&&
            ProcessIdToSessionId(pid,&ps)&&ProcessIdToSessionId(GetCurrentProcessId(),&os)&&ps==os&&
            !directory.empty()&&load(directory+L"\\"+nonce+L".bin",before)&&WaitForSingleObject(guardian,0)==WAIT_TIMEOUT;
        DWORD result=authorized?ERROR_SUCCESS:ERROR_ACCESS_DENIED;
        if(!result&&action==L"resize-lease") {
            wchar_t *ew=nullptr,*eh=nullptr;const auto width=wcstoul(argv[4],&ew,10),height=wcstoul(argv[5],&eh,10);
            result=(*ew||*eh)?ERROR_INVALID_PARAMETER:configureMode(width,height);
            if(!result)result=deviceState(true,false);
        }
        if(!result&&action==L"enable-lease") {
            if(WaitForSingleObject(guardian,0)==WAIT_TIMEOUT)result=deviceState(true,true);
            else result=ERROR_CANCELLED;
        }
        CloseHandle(guardian);return int(result);
    }
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
