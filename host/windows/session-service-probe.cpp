// SPDX-License-Identifier: GPL-3.0-or-later
// Development-only service boundary probe. Never launches applications, reads
// host configuration, injects input, or accepts paths/commands over its pipe.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <wtsapi32.h>
#include <sddl.h>
#include <string>
#include <vector>
#include <iostream>

namespace {
constexpr wchar_t ServiceName[]=L"DeskPortSessionProbe";
constexpr wchar_t PipeName[]=L"\\\\.\\pipe\\DeskPort.SessionProbe.v1";
constexpr DWORD Magic=0x44505350, Version=1, Probe=1;
struct Message { DWORD magic, version, value, session; };
static_assert(sizeof(Message)==16);
struct Handle {
    HANDLE h=nullptr;
    explicit Handle(HANDLE value=nullptr):h(value){}
    ~Handle(){if(h && h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
    Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
    operator HANDLE() const{return h;}
    bool valid() const{return h && h!=INVALID_HANDLE_VALUE;}
};
HANDLE stopEvent=nullptr;
SERVICE_STATUS_HANDLE serviceHandle=nullptr;
SERVICE_STATUS status{};

std::vector<BYTE> tokenInfo(HANDLE token,TOKEN_INFORMATION_CLASS kind) {
    DWORD bytes=0; GetTokenInformation(token,kind,nullptr,0,&bytes);
    if(!bytes || bytes>65536)return {};
    std::vector<BYTE> result(bytes);
    if(!GetTokenInformation(token,kind,result.data(),bytes,&bytes))return {};
    return result;
}
bool sameUser(HANDLE a,HANDLE b) {
    const auto left=tokenInfo(a,TokenUser),right=tokenInfo(b,TokenUser);
    return !left.empty() && !right.empty() &&
        EqualSid(reinterpret_cast<const TOKEN_USER*>(left.data())->User.Sid,
                 reinterpret_cast<const TOKEN_USER*>(right.data())->User.Sid);
}
bool systemToken(HANDLE token) {
    const auto info=tokenInfo(token,TokenUser);
    return !info.empty() && IsWellKnownSid(reinterpret_cast<const TOKEN_USER*>(info.data())->User.Sid,WinLocalSystemSid);
}
bool isSystem() {
    Handle token;return OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.h) && systemToken(token);
}
std::wstring imagePath(HANDLE process) {
    wchar_t path[32768];DWORD size=32768;
    return QueryFullProcessImageNameW(process,0,path,&size)?std::wstring(path,size):std::wstring();
}
bool sameImage(HANDLE process) {
    const auto own=imagePath(GetCurrentProcess()),other=imagePath(process);
    return !own.empty() && !other.empty() && !_wcsicmp(own.c_str(),other.c_str());
}
bool trustedServer(ULONG pid) {
    // A standard user cannot necessarily query a SYSTEM process token. Use the
    // administrator-controlled SCM record, not an inaccessible token or a
    // client-supplied claim, to authenticate this fixed read-only service.
    SC_HANDLE manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);
    if(!manager)return false;
    SC_HANDLE service=OpenServiceW(manager,ServiceName,SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS);
    CloseServiceHandle(manager);
    if(!service)return false;
    SERVICE_STATUS_PROCESS state{};DWORD bytes=0;
    bool valid=QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&state),sizeof(state),&bytes) &&
        state.dwCurrentState==SERVICE_RUNNING && state.dwProcessId==pid;
    QueryServiceConfigW(service,nullptr,0,&bytes);
    std::vector<BYTE> data(bytes>0 && bytes<=65536?bytes:0);
    if(data.empty() || !QueryServiceConfigW(service,reinterpret_cast<QUERY_SERVICE_CONFIGW*>(data.data()),DWORD(data.size()),&bytes))valid=false;
    if(valid) {
        const auto config=reinterpret_cast<const QUERY_SERVICE_CONFIGW*>(data.data());
        const auto expected=L"\""+imagePath(GetCurrentProcess())+L"\" --service";
        valid=config->lpServiceStartName && !_wcsicmp(config->lpServiceStartName,L"LocalSystem") &&
            config->lpBinaryPathName && !_wcsicmp(config->lpBinaryPathName,expected.c_str());
    }
    CloseServiceHandle(service);return valid;
}

// The message has already been read, so impersonation represents that message's
// security context. Any failure is a denial, never a fallback to SYSTEM.
DWORD authorize(HANDLE pipe,DWORD& session) {
    ULONG pid=0;if(!GetNamedPipeClientProcessId(pipe,&pid))return ERROR_ACCESS_DENIED;
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));
    Handle processToken,callerToken,consoleToken;
    if(!process.valid() || !sameImage(process) ||
       !OpenProcessToken(process,TOKEN_QUERY,&processToken.h))return ERROR_ACCESS_DENIED;
    if(!ImpersonateNamedPipeClient(pipe))return ERROR_ACCESS_DENIED;
    const bool opened=OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&callerToken.h);
    // Continuing while impersonated would change the service's trust boundary.
    if(!RevertToSelf())ExitProcess(ERROR_ACCESS_DENIED);
    if(!opened || !sameUser(callerToken,processToken))return ERROR_ACCESS_DENIED;
    const auto sid=tokenInfo(callerToken,TokenSessionId);
    if(sid.size()!=sizeof(DWORD))return ERROR_ACCESS_DENIED;
    session=*reinterpret_cast<const DWORD*>(sid.data());
    if(!session || session!=WTSGetActiveConsoleSessionId() ||
       !WTSQueryUserToken(session,&consoleToken.h) || !sameUser(callerToken,consoleToken))return ERROR_ACCESS_DENIED;
    DWORD processSession=0;
    if(!ProcessIdToSessionId(pid,&processSession) || processSession!=session ||
       WaitForSingleObject(process,0)!=WAIT_TIMEOUT)return ERROR_ACCESS_DENIED;
    return ERROR_SUCCESS;
}

// All service-side I/O is bounded and cancellable; a silent client cannot block
// service shutdown or monopolize the single pipe instance indefinitely.
DWORD transfer(HANDLE pipe,void* message,DWORD size,bool write,HANDLE stop,DWORD timeout) {
    Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    if(!event.valid())return GetLastError();
    OVERLAPPED ov{};ov.hEvent=event;DWORD bytes=0;
    const BOOL done=write?WriteFile(pipe,message,size,&bytes,&ov):ReadFile(pipe,message,size,&bytes,&ov);
    if(!done) {
        DWORD error=GetLastError();if(error!=ERROR_IO_PENDING)return error;
        HANDLE waits[]{event,stop};
        DWORD waited=WaitForMultipleObjects(stop?2:1,waits,FALSE,timeout);
        if(waited!=WAIT_OBJECT_0) {
            CancelIoEx(pipe,&ov);GetOverlappedResult(pipe,&ov,&bytes,TRUE);
            return waited==WAIT_TIMEOUT?ERROR_TIMEOUT:ERROR_OPERATION_ABORTED;
        }
        if(!GetOverlappedResult(pipe,&ov,&bytes,FALSE))return GetLastError();
    }
    return bytes==size?ERROR_SUCCESS:ERROR_INVALID_DATA;
}
DWORD connectPipe(HANDLE pipe) {
    Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    if(!event.valid())return GetLastError();
    OVERLAPPED ov{};ov.hEvent=event;
    if(ConnectNamedPipe(pipe,&ov))return ERROR_SUCCESS;
    DWORD error=GetLastError();if(error==ERROR_PIPE_CONNECTED)return ERROR_SUCCESS;
    if(error!=ERROR_IO_PENDING)return error;
    HANDLE waits[]{event,stopEvent};DWORD bytes=0;
    if(WaitForMultipleObjects(2,waits,FALSE,INFINITE)!=WAIT_OBJECT_0) {
        CancelIoEx(pipe,&ov);GetOverlappedResult(pipe,&ov,&bytes,TRUE);return ERROR_OPERATION_ABORTED;
    }
    return GetOverlappedResult(pipe,&ov,&bytes,FALSE)?ERROR_SUCCESS:GetLastError();
}
DWORD probeSession(DWORD session) {
    if(session!=WTSGetActiveConsoleSessionId())return ERROR_ACCESS_DENIED;
    Handle own,token;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_DUPLICATE,&own.h) ||
       !DuplicateTokenEx(own,MAXIMUM_ALLOWED,nullptr,SecurityImpersonation,TokenPrimary,&token.h) ||
       !SetTokenInformation(token,TokenSessionId,&session,sizeof(session)))return GetLastError();
    Handle job(CreateJobObjectW(nullptr,nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!job.valid() || !SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))return GetLastError();
    const auto exe=imagePath(GetCurrentProcess());if(exe.empty())return ERROR_FILE_NOT_FOUND;
    std::wstring command=L"\""+exe+L"\" --worker";
    wchar_t systemRoot[MAX_PATH];const UINT length=GetWindowsDirectoryW(systemRoot,MAX_PATH);
    if(!length || length>=MAX_PATH)return ERROR_INVALID_DATA;
    std::wstring env=L"SystemRoot="+std::wstring(systemRoot);env.push_back(0);env.push_back(0);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.lpDesktop=const_cast<LPWSTR>(L"winsta0\\default");
    PROCESS_INFORMATION child{};
    const auto cwd=exe.substr(0,exe.find_last_of(L'\\'));
    if(!CreateProcessAsUserW(token,exe.c_str(),command.data(),nullptr,nullptr,FALSE,
        CREATE_SUSPENDED|CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT,env.data(),cwd.c_str(),&startup,&child))return GetLastError();
    Handle process(child.hProcess),thread(child.hThread);
    if(!AssignProcessToJobObject(job,process)) {DWORD error=GetLastError();TerminateProcess(process,error);WaitForSingleObject(process,5000);return error;}
    if(ResumeThread(thread)==DWORD(-1))return GetLastError();
    HANDLE waits[]{process,stopEvent};
    const DWORD waited=WaitForMultipleObjects(2,waits,FALSE,5000);
    if(waited!=WAIT_OBJECT_0) {TerminateJobObject(job,ERROR_TIMEOUT);WaitForSingleObject(process,5000);return ERROR_TIMEOUT;}
    DWORD code=ERROR_GEN_FAILURE;GetExitCodeProcess(process,&code);return code;
}
DWORD WINAPI control(DWORD code,DWORD,void*,void*) {
    if(code==SERVICE_CONTROL_STOP || code==SERVICE_CONTROL_SHUTDOWN)SetEvent(stopEvent);
    return NO_ERROR;
}
void report(DWORD state,DWORD error=0) {
    status.dwServiceType=SERVICE_WIN32_OWN_PROCESS;status.dwCurrentState=state;
    status.dwControlsAccepted=state==SERVICE_RUNNING?SERVICE_ACCEPT_STOP|SERVICE_ACCEPT_SHUTDOWN:0;
    status.dwWin32ExitCode=error;SetServiceStatus(serviceHandle,&status);
}
void WINAPI serviceMain(DWORD,LPWSTR*) {
    Handle stop(CreateEventW(nullptr,TRUE,FALSE,nullptr));stopEvent=stop;
    serviceHandle=RegisterServiceCtrlHandlerExW(ServiceName,control,nullptr);
    if(!serviceHandle)return;
    report(SERVICE_START_PENDING);
    if(!stop.valid() || !isSystem()) {report(SERVICE_STOPPED,ERROR_ACCESS_DENIED);return;}
    PSECURITY_DESCRIPTOR sd=nullptr;
    // Authenticated medium-integrity users may read/write data, but cannot
    // create pipe instances. Low-integrity callers remain excluded. Only this
    // narrow IPC endpoint has a medium label; service/worker tokens stay SYSTEM.
    // 0x0012019b is FILE_GENERIC_READ | FILE_GENERIC_WRITE with
    // FILE_CREATE_PIPE_INSTANCE removed: data, attributes, DACL read and sync.
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x0012019b;;;AU)S:(ML;;NW;;;ME)",SDDL_REVISION_1,&sd,nullptr)) {report(SERVICE_STOPPED,GetLastError());return;}
    SECURITY_ATTRIBUTES security{sizeof(security),sd,FALSE};
    Handle pipe(CreateNamedPipeW(PipeName,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,sizeof(Message),sizeof(Message),1000,&security));
    DWORD error=pipe.valid()?ERROR_SUCCESS:GetLastError();LocalFree(sd);
    if(error) {report(SERVICE_STOPPED,error);return;}
    report(SERVICE_RUNNING);
    while(WaitForSingleObject(stop,0)==WAIT_TIMEOUT) {
        if(connectPipe(pipe))break;
        Message request{};DWORD session=0;
        error=transfer(pipe,&request,sizeof(request),false,stop,5000);
        if(!error)error=authorize(pipe,session);
        if(!error && (request.magic!=Magic || request.version!=Version || request.value!=Probe || request.session!=0))error=ERROR_INVALID_DATA;
        if(!error)error=probeSession(session);
        Message response{Magic,Version,error,session};
        if(!transfer(pipe,&response,sizeof(response),true,stop,1000)) {
            Message acknowledgement{};
            transfer(pipe,&acknowledgement,sizeof(acknowledgement),false,stop,1000);
        }
        // No FlushFileBuffers: an unresponsive client could block it indefinitely.
        DisconnectNamedPipe(pipe);
    }
    report(SERVICE_STOPPED);
}
DWORD client(bool invalid,bool anonymous) {
    const auto fail=[](const char* stage,DWORD error){std::cout<<"stage="<<stage<<" error="<<error<<"\n";return error;};
    if(!WaitNamedPipeW(PipeName,5000))return fail("wait",GetLastError());
    Handle pipe(CreateFileW(PipeName,FILE_READ_DATA|FILE_WRITE_DATA|SYNCHRONIZE,0,nullptr,OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|(anonymous?SECURITY_ANONYMOUS:SECURITY_IMPERSONATION),nullptr));
    if(!pipe.valid())return fail("open",GetLastError());
    ULONG serverPid=0;if(!GetNamedPipeServerProcessId(pipe,&serverPid))return fail("server-pid",GetLastError());
    if(!trustedServer(serverPid))return fail("scm",ERROR_ACCESS_DENIED);
    Message request{Magic,Version,invalid?999u:Probe,0},response{};
    DWORD error=transfer(pipe,&request,sizeof(request),true,nullptr,5000);
    if(!error)error=transfer(pipe,&response,sizeof(response),false,nullptr,10000);
    if(!error && (response.magic!=Magic || response.version!=Version))error=ERROR_INVALID_DATA;
    if(!error) {
        Message acknowledgement{Magic,Version,2,0};
        transfer(pipe,&acknowledgement,sizeof(acknowledgement),true,nullptr,1000);
    }
    if(!error)error=response.value;
    std::cout<<"error="<<error<<" session="<<response.session<<"\n";
    return error;
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return ERROR_INVALID_PARAMETER;
    const std::wstring mode=argv[1];
    if(mode==L"--service") {
        SERVICE_TABLE_ENTRYW table[]{{const_cast<LPWSTR>(ServiceName),serviceMain},{nullptr,nullptr}};
        return StartServiceCtrlDispatcherW(table)?0:GetLastError();
    }
    if(mode==L"--worker") {
        DWORD session=0;
        if(!isSystem() || !ProcessIdToSessionId(GetCurrentProcessId(),&session) || !session || session!=WTSGetActiveConsoleSessionId())return ERROR_ACCESS_DENIED;
        HDESK desktop=OpenDesktopW(L"Winlogon",0,FALSE,GENERIC_ALL);
        if(!desktop)return GetLastError();
        CloseDesktop(desktop);return 0;
    }
    if(mode==L"--low") {
        // Negative test: lower only this short-lived probe process, never the
        // service, another process, a file, or a system security policy.
        Handle token;PSID sid=nullptr;
        if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_ADJUST_DEFAULT,&token.h))return GetLastError();
        if(!ConvertStringSidToSidW(L"S-1-16-4096",&sid))return GetLastError();
        TOKEN_MANDATORY_LABEL label{};label.Label.Sid=sid;label.Label.Attributes=SE_GROUP_INTEGRITY;
        const BOOL lowered=SetTokenInformation(token,TokenIntegrityLevel,&label,sizeof(label)+GetLengthSid(sid));
        const DWORD error=lowered?ERROR_SUCCESS:GetLastError();LocalFree(sid);
        if(error)return error;
        std::cout<<"integrity=low\n";
        return client(false,false);
    }
    if(mode==L"--probe" || mode==L"--invalid" || mode==L"--anonymous")return client(mode==L"--invalid",mode==L"--anonymous");
    return ERROR_INVALID_PARAMETER;
}
