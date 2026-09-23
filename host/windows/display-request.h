// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
// One bounded request at a time; no paths, commands or device IDs cross UAC.
struct DisplayModeRequest {
    DWORD width;
    DWORD height;
    DWORD result;
};
