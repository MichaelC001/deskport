#!/usr/bin/env bash
source "$(dirname "$0")/env.sh"
mkdir -p "$WB/full"
"$CXX" -std=c++17 -O2 -static -municode "$SRC_ROOT/host/windows/maintenance.cpp" -o "$WB/full/deskport-maintenance.exe" -lrstrtmgr -ltaskschd -lole32 -loleaut32 -luuid -ladvapi32
"$CXX" -O2 -static -municode "$SRC_ROOT/host/windows/driver-setup.cpp" -o "$WB/full/deskport-driver-setup.exe" -lsetupapi -lcfgmgr32 -lnewdev -ladvapi32 -luuid
"$CXX" -std=c++17 -O2 -static -municode "$SRC_ROOT/host/windows/display-recovery.cpp" -o "$WB/full/deskport-display-recovery.exe" -lsetupapi -lcfgmgr32 -ladvapi32 -luuid -luser32 -lshell32 -lole32 -lwtsapi32
"$CXX" -std=c++17 -O2 -static -municode "$SRC_ROOT/host/windows/display-service.cpp" -o "$WB/full/deskport-display-service.exe" -ladvapi32 -lwtsapi32
