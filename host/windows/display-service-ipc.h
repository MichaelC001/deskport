// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <wtsapi32.h>
#include <sddl.h>
#include <string>
#include <vector>
#include "display-service-policy.h"
#include "protected-path.h"

namespace deskport::display_service {
constexpr wchar_t ServiceName[] = L"DeskPortDisplayService";
constexpr wchar_t PipeName[] = L"\\\\.\\pipe\\DeskPort.DisplayService.v1";
constexpr wchar_t OwnershipValue[] = L"DisplayServiceOwned";
struct Handle {
    HANDLE h = nullptr;
    explicit Handle(HANDLE value = nullptr): h(value) {}
    ~Handle() { if (valid()) CloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    operator HANDLE() const { return h; }
    bool valid() const { return h && h != INVALID_HANDLE_VALUE; }
};
inline std::wstring imagePath(HANDLE process = GetCurrentProcess()) {
    wchar_t path[32768]{}; DWORD size = 32768;
    return QueryFullProcessImageNameW(process, 0, path, &size) ? std::wstring(path, size) : std::wstring();
}
inline std::wstring sibling(const std::wstring& name) {
    const auto own = imagePath();
    const auto slash = own.find_last_of(L'\\');
    return slash == std::wstring::npos ? std::wstring() : own.substr(0, slash + 1) + name;
}
inline std::wstring serviceCommand(const std::wstring& path) { return L"\"" + path + L"\" --service"; }
inline std::wstring stopName(const std::wstring& nonce) { return L"Global\\DeskPort.DisplayService.Stop." + nonce; }
inline std::uint64_t creationTime(HANDLE process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) return 0;
    return (std::uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}
inline std::vector<BYTE> tokenInfo(HANDLE token, TOKEN_INFORMATION_CLASS kind) {
    DWORD size = 0; GetTokenInformation(token, kind, nullptr, 0, &size);
    if (!size || size > 65536) return {};
    std::vector<BYTE> data(size);
    if (!GetTokenInformation(token, kind, data.data(), size, &size)) return {};
    return data;
}
inline bool sameUser(HANDLE first, HANDLE second) {
    const auto a = tokenInfo(first, TokenUser), b = tokenInfo(second, TokenUser);
    return !a.empty() && !b.empty() && EqualSid(reinterpret_cast<const TOKEN_USER*>(a.data())->User.Sid,
        reinterpret_cast<const TOKEN_USER*>(b.data())->User.Sid);
}
inline std::wstring logonSid(HANDLE token) {
    const auto data = tokenInfo(token, TokenGroups);
    if (data.empty()) return {};
    const auto groups = reinterpret_cast<const TOKEN_GROUPS*>(data.data());
    for (DWORD i = 0; i < groups->GroupCount; ++i) {
        if ((groups->Groups[i].Attributes & SE_GROUP_LOGON_ID) != SE_GROUP_LOGON_ID) continue;
        LPWSTR sid = nullptr;
        if (!ConvertSidToStringSidW(groups->Groups[i].Sid, &sid)) return {};
        std::wstring result(sid); LocalFree(sid); return result;
    }
    return {};
}
inline bool isSystem() {
    Handle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.h)) return false;
    const auto data = tokenInfo(token, TokenUser);
    return !data.empty() && IsWellKnownSid(reinterpret_cast<const TOKEN_USER*>(data.data())->User.Sid, WinLocalSystemSid);
}
inline bool trustedServer(ULONG pid, const std::wstring& executable) {
    if (!protectedExecutable(executable)) return false;
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) return false;
    SC_HANDLE service = OpenServiceW(manager, ServiceName, SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS);
    CloseServiceHandle(manager);
    if (!service) return false;
    SERVICE_STATUS_PROCESS status{}; DWORD size = 0;
    bool valid = QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(&status), sizeof(status), &size) &&
        status.dwCurrentState == SERVICE_RUNNING && status.dwProcessId == pid;
    QueryServiceConfigW(service, nullptr, 0, &size);
    std::vector<BYTE> data(size && size <= 65536 ? size : 0);
    if (data.empty() || !QueryServiceConfigW(service, reinterpret_cast<QUERY_SERVICE_CONFIGW*>(data.data()), DWORD(data.size()), &size)) valid = false;
    if (valid) {
        const auto config = reinterpret_cast<const QUERY_SERVICE_CONFIGW*>(data.data());
        valid = config->dwServiceType == SERVICE_WIN32_OWN_PROCESS && config->lpServiceStartName &&
            !_wcsicmp(config->lpServiceStartName, L"LocalSystem") && config->lpBinaryPathName &&
            !_wcsicmp(config->lpBinaryPathName, serviceCommand(executable).c_str());
    }
    CloseServiceHandle(service); return valid;
}
inline DWORD transfer(HANDLE pipe, void* data, DWORD size, bool write, HANDLE stop, DWORD timeout) {
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event.valid()) return GetLastError();
    OVERLAPPED operation{}; operation.hEvent = event; DWORD bytes = 0;
    const BOOL done = write ? WriteFile(pipe, data, size, &bytes, &operation) : ReadFile(pipe, data, size, &bytes, &operation);
    if (!done) {
        const auto error = GetLastError(); if (error != ERROR_IO_PENDING) return error;
        HANDLE waits[]{event, stop};
        const auto waited = WaitForMultipleObjects(stop ? 2 : 1, waits, FALSE, timeout);
        if (waited != WAIT_OBJECT_0) {
            CancelIoEx(pipe, &operation); GetOverlappedResult(pipe, &operation, &bytes, TRUE);
            return waited == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_OPERATION_ABORTED;
        }
        if (!GetOverlappedResult(pipe, &operation, &bytes, FALSE)) return GetLastError();
    }
    return bytes == size ? ERROR_SUCCESS : ERROR_INVALID_DATA;
}
// The returned process handle has query/synchronize rights only. No token or
// privileged handle is ever transferred to the user process.
inline DWORD acquire(const std::wstring& nonce, HANDLE& process) {
    process = nullptr;
    Request request;
    if (nonce.size() != 36) return ERROR_INVALID_PARAMETER;
    for (unsigned i = 0; i < 36; ++i) request.nonce[i] = char16_t(nonce[i]);
    if (!validRequest(request)) return ERROR_INVALID_PARAMETER;
    if (!WaitNamedPipeW(PipeName, 5000)) return GetLastError();
    Handle pipe(CreateFileW(PipeName, FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE, 0, nullptr, OPEN_EXISTING,
        // Identification is sufficient for the broker's token comparisons and
        // prevents an untrusted pipe endpoint from borrowing caller authority.
        FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr));
    if (!pipe.valid()) return GetLastError();
    ULONG pid = 0;
    if (!GetNamedPipeServerProcessId(pipe, &pid) || !trustedServer(pid, sibling(L"deskport-display-service.exe"))) return ERROR_ACCESS_DENIED;
    Response response;
    DWORD error = transfer(pipe, &request, sizeof(request), true, nullptr, 5000);
    if (!error) error = transfer(pipe, &response, sizeof(response), false, nullptr, 10000);
    if (!error && !validResponse(response)) error = ERROR_INVALID_DATA;
    if (!error) error = response.error;
    if (!error) {
        process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, response.pid);
        DWORD session = 0, ownSession = 0;
        if (!process || !ProcessIdToSessionId(response.pid, &session) ||
            !ProcessIdToSessionId(GetCurrentProcessId(), &ownSession) || session != ownSession ||
            _wcsicmp(imagePath(process).c_str(), sibling(L"deskport-display-recovery.exe").c_str())) error = ERROR_ACCESS_DENIED;
    }
    if (!error) error = transfer(pipe, &response, sizeof(response), true, nullptr, 1000);
    if (error && process) { CloseHandle(process); process = nullptr; }
    return error;
}
}
