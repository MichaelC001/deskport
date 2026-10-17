// SPDX-License-Identifier: GPL-3.0-or-later
// Installed display broker only: no network listener, host config or input API.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include "display-service-ipc.h"
#include "display-service-environment.h"
#include <shlobj.h>

using namespace deskport::display_service;
namespace {
HANDLE stopping = nullptr;
SERVICE_STATUS_HANDLE serviceHandle = nullptr;
DWORD checkpoint = 0;
void report(DWORD state, DWORD error = 0) {
    SERVICE_STATUS status{};
    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState = state;
    status.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    status.dwWin32ExitCode = error;
    if (state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING) {
        status.dwWaitHint = 60000; status.dwCheckPoint = ++checkpoint;
    }
    SetServiceStatus(serviceHandle, &status);
}
DWORD WINAPI control(DWORD code, DWORD, void*, void*) {
    if (code == SERVICE_CONTROL_STOP || code == SERVICE_CONTROL_SHUTDOWN) SetEvent(stopping);
    return NO_ERROR;
}
struct Worker {
    HANDLE process = nullptr, stop = nullptr;
    DWORD session = 0, pid = 0;
    ~Worker() { clear(); }
    void clear() {
        if (process) CloseHandle(process);
        if (stop) CloseHandle(stop);
        process = stop = nullptr; session = pid = 0;
    }
    void poll() {
        if (!process) return;
        if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) clear();
        else if (session != WTSGetActiveConsoleSessionId()) SetEvent(stop);
    }
};
DWORD authorize(HANDLE pipe, Handle& parent, DWORD& session, std::wstring& logon) {
    ULONG pid = 0;
    if (!GetNamedPipeClientProcessId(pipe, &pid)) return ERROR_ACCESS_DENIED;
    parent.h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    Handle processToken, callerToken, consoleToken;
    const auto expected = sibling(L"deskport-display.exe");
    if (!parent.valid() || !OpenProcessToken(parent, TOKEN_QUERY, &processToken.h)) return ERROR_ACCESS_DENIED;
    if (!ImpersonateNamedPipeClient(pipe)) return ERROR_ACCESS_DENIED;
    const bool opened = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &callerToken.h);
    // Any failure must deny; never continue running client work as SYSTEM.
    if (!RevertToSelf()) ExitProcess(ERROR_ACCESS_DENIED);
    if (!opened) return ERROR_ACCESS_DENIED;
    const auto tokenSession = tokenInfo(callerToken, TokenSessionId);
    const auto integrity = tokenInfo(callerToken, TokenIntegrityLevel);
    if (tokenSession.size() != sizeof(DWORD) || integrity.empty()) return ERROR_ACCESS_DENIED;
    DWORD processSession = 0;
    if (!ProcessIdToSessionId(pid, &processSession)) return ERROR_ACCESS_DENIED;
    session = *reinterpret_cast<const DWORD*>(tokenSession.data());
    logon = logonSid(callerToken);
    const auto label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(integrity.data())->Label.Sid;
    const auto rid = *GetSidSubAuthority(label, *GetSidSubAuthorityCount(label) - 1);
    const bool haveConsole = WTSQueryUserToken(session, &consoleToken.h);
    const Caller caller{session, WTSGetActiveConsoleSessionId(),
        protectedExecutable(expected) && !_wcsicmp(imagePath(parent).c_str(), expected.c_str()),
        WaitForSingleObject(parent, 0) == WAIT_TIMEOUT,
        rid >= SECURITY_MANDATORY_MEDIUM_RID,
        sameUser(callerToken, processToken),
        !logon.empty() && logon == logonSid(processToken) && processSession == session,
        haveConsole && sameUser(callerToken, consoleToken) && logon == logonSid(consoleToken)};
    return authorized(caller) ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
}
DWORD launch(const Request& request, HANDLE parent, DWORD session, const std::wstring& logon, Worker& worker) {
    worker.poll();
    if (worker.process) return ERROR_BUSY;
    if (session != WTSGetActiveConsoleSessionId() || WaitForSingleObject(parent, 0) != WAIT_TIMEOUT ||
        WaitForSingleObject(stopping, 0) != WAIT_TIMEOUT) return ERROR_CANCELLED;
    const auto executable = sibling(L"deskport-display-recovery.exe");
    if (!protectedExecutable(executable)) return ERROR_ACCESS_DENIED;
    const auto parentCreated = creationTime(parent), brokerCreated = creationTime(GetCurrentProcess());
    if (!parentCreated || !brokerCreated) return ERROR_ACCESS_DENIED;
    std::wstring nonce;
    for (unsigned i = 0; i < 36; ++i) nonce += wchar_t(request.nonce[i]);
    PSECURITY_DESCRIPTOR eventDescriptor = nullptr, processDescriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:SYG:SYD:P(A;;GA;;;SY)(A;;GA;;;BA)",
        SDDL_REVISION_1, &eventDescriptor, nullptr)) return GetLastError();
    SECURITY_ATTRIBUTES eventSecurity{sizeof(eventSecurity), eventDescriptor, FALSE};
    Handle stop(CreateEventW(&eventSecurity, TRUE, FALSE, stopName(nonce).c_str()));
    DWORD error = !stop.valid() ? GetLastError() : GetLastError() == ERROR_ALREADY_EXISTS ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS;
    LocalFree(eventDescriptor);
    if (error) return error;
    // Permit the exact console logon to observe completion, never terminate,
    // duplicate handles, change the DACL or read a SYSTEM token.
    const auto sddl = L"O:SYG:SYD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00101000;;;" + logon + L")";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &processDescriptor, nullptr)) return GetLastError();
    SECURITY_ATTRIBUTES processSecurity{sizeof(processSecurity), processDescriptor, FALSE};
    Handle own, token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &own.h) ||
        !DuplicateTokenEx(own, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation, TokenPrimary, &token.h) ||
        !SetTokenInformation(token, TokenSessionId, &session, sizeof(session))) {
        error = GetLastError(); LocalFree(processDescriptor); return error;
    }
    wchar_t systemRoot[MAX_PATH]{};
    const auto length = GetWindowsDirectoryW(systemRoot, MAX_PATH);
    if (!length || length >= MAX_PATH) { LocalFree(processDescriptor); return ERROR_INVALID_DATA; }
    PWSTR commonData = nullptr;
    const auto located = SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &commonData);
    auto environment = SUCCEEDED(located) && commonData ? systemEnvironment(systemRoot, commonData) : std::vector<wchar_t>();
    if (commonData) CoTaskMemFree(commonData);
    if (environment.empty()) { LocalFree(processDescriptor); return ERROR_PATH_NOT_FOUND; }
    std::wstring command = L"\"" + executable + L"\" service-lease " + std::to_wstring(GetProcessId(parent)) +
        L" " + nonce + L" " + std::to_wstring(GetCurrentProcessId()) + L" " +
        std::to_wstring(parentCreated) + L" " + std::to_wstring(brokerCreated);
    const auto cwd = executable.substr(0, executable.find_last_of(L'\\'));
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.lpDesktop = const_cast<LPWSTR>(L"winsta0\\default");
    PROCESS_INFORMATION child{};
    // Cross-session handle inheritance is forbidden. The guardian opens only
    // fixed, authenticated process/event objects in its own session.
    const bool started = CreateProcessAsUserW(token, executable.c_str(), command.data(), &processSecurity, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW, environment.data(), cwd.c_str(), &startup, &child);
    error = started ? ERROR_SUCCESS : GetLastError(); LocalFree(processDescriptor);
    if (error) return error;
    CloseHandle(child.hThread);
    worker.process = child.hProcess; worker.pid = child.dwProcessId; worker.session = session;
    worker.stop = stop.h; stop.h = nullptr;
    // Do not put the independent recovery guardian in a kill-on-close job.
    // It restores on parent death, broker death, stop or session change.
    return ERROR_SUCCESS;
}
DWORD connectPipe(HANDLE pipe, Worker& worker) {
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event.valid()) return GetLastError();
    OVERLAPPED operation{}; operation.hEvent = event;
    if (ConnectNamedPipe(pipe, &operation)) return ERROR_SUCCESS;
    DWORD error = GetLastError();
    if (error == ERROR_PIPE_CONNECTED) return ERROR_SUCCESS;
    if (error != ERROR_IO_PENDING) return error;
    HANDLE waits[]{event, stopping};
    for (;;) {
        const auto waited = WaitForMultipleObjects(2, waits, FALSE, 500);
        worker.poll();
        if (waited == WAIT_TIMEOUT) continue;
        DWORD bytes = 0;
        if (waited != WAIT_OBJECT_0) {
            CancelIoEx(pipe, &operation); GetOverlappedResult(pipe, &operation, &bytes, TRUE);
            return ERROR_OPERATION_ABORTED;
        }
        return GetOverlappedResult(pipe, &operation, &bytes, FALSE) ? ERROR_SUCCESS : GetLastError();
    }
}
void WINAPI serviceMain(DWORD, LPWSTR*) {
    Handle stop(CreateEventW(nullptr, TRUE, FALSE, nullptr)); stopping = stop;
    serviceHandle = RegisterServiceCtrlHandlerExW(ServiceName, control, nullptr);
    if (!serviceHandle) return;
    report(SERVICE_START_PENDING);
    if (!stop.valid() || !isSystem() || !protectedExecutable(imagePath())) {
        report(SERVICE_STOPPED, ERROR_ACCESS_DENIED); return;
    }
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    // Medium authenticated clients may exchange data, but cannot create pipe
    // instances or modify ACLs. Remote clients and low integrity are rejected.
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x0012019b;;;AU)S:(ML;;NW;;;ME)",
        SDDL_REVISION_1, &descriptor, nullptr)) { report(SERVICE_STOPPED, GetLastError()); return; }
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    Handle pipe(CreateNamedPipeW(PipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, sizeof(Response), sizeof(Request), 1000, &security));
    DWORD error = pipe.valid() ? ERROR_SUCCESS : GetLastError(); LocalFree(descriptor);
    if (error) { report(SERVICE_STOPPED, error); return; }
    Worker worker;
    report(SERVICE_RUNNING);
    while (WaitForSingleObject(stop, 0) == WAIT_TIMEOUT) {
        error = connectPipe(pipe, worker);
        if (error) break;
        Request request{}; Response response; Handle parent; DWORD session = 0; std::wstring logon;
        error = transfer(pipe, &request, sizeof(request), false, stop, 5000);
        if (!error && !validRequest(request)) error = ERROR_INVALID_DATA;
        if (!error) error = authorize(pipe, parent, session, logon);
        if (!error) error = launch(request, parent, session, logon, worker);
        response.error = error; response.pid = error ? 0 : worker.pid;
        const auto sent = transfer(pipe, &response, sizeof(response), true, stop, 1000);
        if (!error) {
            Response ack;
            if (sent || transfer(pipe, &ack, sizeof(ack), false, stop, 1000) ||
                ack.magic != Magic || ack.version != Version || ack.error || ack.pid != worker.pid) SetEvent(worker.stop);
        }
        DisconnectNamedPipe(pipe);
    }
    report(SERVICE_STOP_PENDING);
    if (worker.process) {
        SetEvent(worker.stop);
        const auto deadline = GetTickCount64() + 60000;
        while (WaitForSingleObject(worker.process, 1000) == WAIT_TIMEOUT && GetTickCount64() < deadline) report(SERVICE_STOP_PENDING);
        if (WaitForSingleObject(worker.process, 0) != WAIT_OBJECT_0) error = ERROR_TIMEOUT;
    }
    report(SERVICE_STOPPED, error == ERROR_OPERATION_ABORTED || WaitForSingleObject(stop, 0) == WAIT_OBJECT_0 ?
        (worker.process && WaitForSingleObject(worker.process, 0) != WAIT_OBJECT_0 ? ERROR_TIMEOUT : ERROR_SUCCESS) : error);
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2 || wcscmp(argv[1], L"--service")) return ERROR_INVALID_PARAMETER;
    SERVICE_TABLE_ENTRYW table[]{{const_cast<LPWSTR>(ServiceName), serviceMain}, {nullptr, nullptr}};
    return StartServiceCtrlDispatcherW(table) ? ERROR_SUCCESS : int(GetLastError());
}
