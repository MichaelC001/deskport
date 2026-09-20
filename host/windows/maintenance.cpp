// SPDX-License-Identifier: GPL-3.0-or-later
// Installer helper: close only applications using this installation's binaries.
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <restartmanager.h>
#include "recovery-task.h"
#include <string>
#include <iostream>
#include <vector>
static std::wstring quote(const std::wstring& value) {
    std::wstring result=L"\""; size_t slashes=0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') result.append(slashes*2+1,L'\\');
        else result.append(slashes,L'\\');
        slashes=0; result+=c;
    }
    result.append(slashes*2,L'\\'); return result+L"\"";
}
int wmain(int argc, wchar_t** argv) {
    if(argc==3&&(std::wstring(argv[1])==L"register-recovery"||std::wstring(argv[1])==L"remove-recovery"))return int(recoveryTask(argv[2],std::wstring(argv[1])==L"remove-recovery"));
    if (argc >= 4 && std::wstring(argv[1]) == L"relaunch") {
        const DWORD pid=wcstoul(argv[2],nullptr,10);
        auto process=OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        if (!process && GetLastError() != ERROR_INVALID_PARAMETER) return int(GetLastError());
        if (process) {
            wchar_t image[32768]; DWORD length=32768;
            if (!QueryFullProcessImageNameW(process,0,image,&length) || _wcsicmp(image,argv[3])) { CloseHandle(process); return 3; }
            const auto waited=WaitForSingleObject(process,30000); CloseHandle(process);
            if (waited != WAIT_OBJECT_0) return 4;
        }
        std::wstring command;
        for (int i=3;i<argc;++i) { if (!command.empty()) command+=L' '; command+=quote(argv[i]); }
        STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION child{};
        if (!CreateProcessW(argv[3],command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&child)) return int(GetLastError());
        CloseHandle(child.hThread); CloseHandle(child.hProcess); return 0;
    }
    if (argc != 3 || std::wstring(argv[1]) != L"stop") return 2;
    std::wstring root = argv[2];
    if (root.empty() || root.find(L'"') != std::wstring::npos) return 2;
    DWORD session{}; WCHAR key[CCH_RM_SESSION_KEY + 1]{};
    DWORD status = RmStartSession(&session, 0, key);
    if (status != ERROR_SUCCESS) return int(status);
    std::wstring app = root + L"\\DeskPort.exe";
    std::wstring host = root + L"\\host\\deskport-host.exe";
    std::wstring display = root + L"\\host\\deskport-display.exe";
    std::wstring recovery = root + L"\\host\\deskport-display-recovery.exe";
    // Close the owner first: sending shutdown to its helpers concurrently races
    // the display lease's EOF/recovery protocol. Never terminate the guardian.
    LPCWSTR owner[]{app.c_str()};
    status = RmRegisterResources(session, 1, owner, 0, nullptr, 0, nullptr);
    if (status == ERROR_SUCCESS) status = RmShutdown(session, 0, nullptr);
    std::cout << "Restart Manager shutdown result: " << status << std::endl;
    if (status == ERROR_SUCCESS || status == ERROR_FAIL_SHUTDOWN) {
        // Resource registration after shutdown is not permitted in the same
        // RM session. Use a separate read-only session for final verification.
        RmEndSession(session);
        status = RmStartSession(&session, 0, key);
        if (status != ERROR_SUCCESS) return int(status);
        LPCWSTR children[]{app.c_str(), host.c_str(), display.c_str(), recovery.c_str()};
        status = RmRegisterResources(session, 4, children, 0, nullptr, 0, nullptr);
        if (status == ERROR_SUCCESS) {
            const ULONGLONG deadline = GetTickCount64() + 15000;
            do {
                UINT needed = 0, count = 0; DWORD reasons = 0;
                status = RmGetList(session, &needed, &count, nullptr, &reasons);
                if (status == ERROR_SUCCESS && needed == 0 && count == 0) break;
                if (status == ERROR_MORE_DATA && needed <= 1024) {
                    std::vector<RM_PROCESS_INFO> processes(needed);
                    count=needed;
                    status=RmGetList(session,&needed,&count,processes.data(),&reasons);
                    if (status == ERROR_SUCCESS) {
                        bool live=false;
                        for (UINT i=0;i<count;++i) {
                            auto handle=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,processes[i].Process.dwProcessId);
                            if (!handle) { if(GetLastError()!=ERROR_INVALID_PARAMETER)live=true; continue; }
                            FILETIME created{},exited{},kernel{},user{};
                            if (!GetProcessTimes(handle,&created,&exited,&kernel,&user)) live=true;
                            else if (CompareFileTime(&created,&processes[i].Process.ProcessStartTime)==0 && WaitForSingleObject(handle,0)!=WAIT_OBJECT_0) live=true;
                            CloseHandle(handle);
                        }
                        if (!live) break;
                    }
                }
                if (status != ERROR_MORE_DATA && status != ERROR_SUCCESS) break;
                status = ERROR_FAIL_SHUTDOWN;
                Sleep(100);
            } while (GetTickCount64() < deadline);
        }
    }
    RmEndSession(session);
    std::cout << "Restart Manager result: " << status << std::endl;
    return int(status);
}
