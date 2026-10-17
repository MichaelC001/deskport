// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "display-service-ipc.h"

namespace deskport::display_service {
struct ServiceHandle {
    SC_HANDLE h = nullptr;
    explicit ServiceHandle(SC_HANDLE value = nullptr): h(value) {}
    ~ServiceHandle() { if (h) CloseServiceHandle(h); }
    operator SC_HANDLE() const { return h; }
};
inline DWORD waitService(SC_HANDLE service, DWORD target, DWORD timeout) {
    const auto deadline = GetTickCount64() + timeout;
    for (;;) {
        SERVICE_STATUS_PROCESS status{}; DWORD size = 0;
        if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(&status), sizeof(status), &size)) return GetLastError();
        if (status.dwCurrentState == target) return target == SERVICE_STOPPED ? status.dwWin32ExitCode : ERROR_SUCCESS;
        if (target == SERVICE_RUNNING && status.dwCurrentState == SERVICE_STOPPED) return status.dwWin32ExitCode ? status.dwWin32ExitCode : ERROR_SERVICE_NOT_ACTIVE;
        if (GetTickCount64() >= deadline) return ERROR_TIMEOUT;
        Sleep(100);
    }
}
inline DWORD stopService(SC_HANDLE service) {
    const auto deadline = GetTickCount64() + 65000;
    bool requested = false;
    for (;;) {
        SERVICE_STATUS_PROCESS status{}; DWORD size = 0;
        if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(&status), sizeof(status), &size)) return GetLastError();
        if (status.dwCurrentState == SERVICE_STOPPED) return requested ? status.dwWin32ExitCode : ERROR_SUCCESS;
        if (GetTickCount64() >= deadline) return ERROR_TIMEOUT;
        if (status.dwCurrentState == SERVICE_RUNNING) {
            SERVICE_STATUS changed{};
            if (ControlService(service, SERVICE_CONTROL_STOP, &changed)) requested = true;
            else if (GetLastError() != ERROR_SERVICE_NOT_ACTIVE && GetLastError() != ERROR_SERVICE_CANNOT_ACCEPT_CTRL) return GetLastError();
        }
        Sleep(100);
    }
}
// Installer operations only. A matching name without our ownership marker and
// exact protected ImagePath is foreign and must never be changed or removed.
inline DWORD installService(const std::wstring& root, const std::wstring& operation) {
    if (root.size() < 3 || root[1] != L':' || root[2] != L'\\' || root.find(L'"') != std::wstring::npos) return ERROR_INVALID_PARAMETER;
    const auto executable = root + L"\\host\\deskport-display-service.exe";
    const auto command = serviceCommand(executable);
    const bool registration = operation == L"register";
    if (!registration && operation != L"stop" && operation != L"remove") return ERROR_INVALID_PARAMETER;
    if (registration && !protectedExecutable(executable)) return ERROR_ACCESS_DENIED;
    ServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | (registration ? SC_MANAGER_CREATE_SERVICE : 0)));
    if (!manager.h) return GetLastError();
    ServiceHandle service(OpenServiceW(manager, ServiceName, SERVICE_ALL_ACCESS));
    if (!service.h) {
        const auto error = GetLastError();
        if (error != ERROR_SERVICE_DOES_NOT_EXIST) return error;
        if (!registration) return ERROR_SUCCESS;
    } else {
        DWORD owned = 0, size = sizeof(owned);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"Software\\DeskPort", OwnershipValue, RRF_RT_REG_DWORD, nullptr, &owned, &size) || owned != 1) return ERROR_ACCESS_DENIED;
        size = 0; QueryServiceConfigW(service, nullptr, 0, &size);
        std::vector<BYTE> data(size && size <= 65536 ? size : 0);
        if (data.empty() || !QueryServiceConfigW(service, reinterpret_cast<QUERY_SERVICE_CONFIGW*>(data.data()), DWORD(data.size()), &size)) return ERROR_ACCESS_DENIED;
        const auto config = reinterpret_cast<const QUERY_SERVICE_CONFIGW*>(data.data());
        if (config->dwServiceType != SERVICE_WIN32_OWN_PROCESS || !config->lpBinaryPathName ||
            _wcsicmp(config->lpBinaryPathName, command.c_str()) || !config->lpServiceStartName ||
            _wcsicmp(config->lpServiceStartName, L"LocalSystem")) return ERROR_ACCESS_DENIED;
        const auto error = stopService(service);
        if (error) return error;
    }
    if (!registration) {
        if (operation == L"stop") return ERROR_SUCCESS;
        if (!DeleteService(service)) return GetLastError();
        HKEY key = nullptr;
        if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\DeskPort", 0, KEY_SET_VALUE, &key)) {
            RegDeleteValueW(key, OwnershipValue); RegCloseKey(key);
        }
        return ERROR_SUCCESS;
    }
    bool created = false;
    if (!service.h) {
        service.h = CreateServiceW(manager, ServiceName, L"DeskPort display lease service", SERVICE_ALL_ACCESS,
            SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, command.c_str(), nullptr, nullptr, nullptr, L"LocalSystem", nullptr);
        if (!service.h) return GetLastError();
        created = true;
    } else if (!ChangeServiceConfigW(service, SERVICE_NO_CHANGE, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
        command.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)) return GetLastError();
    // Only administrators manage the service. Users can authenticate the pipe
    // server by reading SCM configuration/status, never start/control it.
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    DWORD error = ERROR_SUCCESS;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00020085;;;AU)",
        SDDL_REVISION_1, &descriptor, nullptr)) error = GetLastError();
    if (!error && !SetServiceObjectSecurity(service, DACL_SECURITY_INFORMATION, descriptor)) error = GetLastError();
    if (descriptor) LocalFree(descriptor);
    SC_ACTION actions[]{{SC_ACTION_RESTART, 5000}, {SC_ACTION_RESTART, 30000}, {SC_ACTION_NONE, 0}};
    SERVICE_FAILURE_ACTIONSW recovery{}; recovery.dwResetPeriod = 86400; recovery.cActions = 3; recovery.lpsaActions = actions;
    if (!error && !ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS, &recovery)) error = GetLastError();
    HKEY key = nullptr;
    if (!error) error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"Software\\DeskPort", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (!error) {
        DWORD one = 1;
        error = RegSetValueExW(key, OwnershipValue, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one));
        RegCloseKey(key);
    }
    if (!error && !StartServiceW(service, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) error = GetLastError();
    if (!error) error = waitService(service, SERVICE_RUNNING, 15000);
    if (error && created) {
        // Keep ownership and payload for the installer's retained uninstaller.
        // If ownership could not be saved, remove only the service just created.
        DWORD owned = 0, size = sizeof(owned);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"Software\\DeskPort", OwnershipValue, RRF_RT_REG_DWORD, nullptr, &owned, &size) || owned != 1) DeleteService(service);
    }
    return error;
}
}
