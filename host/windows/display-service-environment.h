// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <vector>

namespace deskport::display_service {
// Use only canonical system paths resolved by the broker. In particular,
// SHGetKnownFolderPath(ProgramData) expands SystemDrive in a fresh SYSTEM
// process; SystemRoot alone is insufficient. Never inherit a client's profile.
inline std::vector<wchar_t> systemEnvironment(const std::wstring& windows, const std::wstring& data) {
    const auto absolute = [](const std::wstring& path) {
        if (path.size() < 3 || path[1] != L':' || path[2] != L'\\' ||
            !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z'))) return false;
        for (const auto c : path) if (c < 32) return false;
        return true;
    };
    if (!absolute(windows) || !absolute(data)) return {};
    std::vector<wchar_t> result;
    for (const auto& item : {L"ALLUSERSPROFILE=" + data, L"ProgramData=" + data,
                            L"SystemDrive=" + windows.substr(0, 2), L"SystemRoot=" + windows,
                            L"WINDIR=" + windows}) {
        result.insert(result.end(), item.begin(), item.end()); result.push_back(0);
    }
    result.push_back(0);
    return result;
}
}
