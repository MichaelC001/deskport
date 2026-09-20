#!/usr/bin/env bash
# The Windows SDK is case-insensitive; mingw-w64 on Linux is not. Generate
# forwarding headers for the mixed-case spellings used by the sources.
source "$(dirname "$0")/env.sh"

SHIM="$WB/compat-include"
mkdir -p "$SHIM"

for h in Windows.h Winsock2.h Ws2tcpip.h DbgHelp.h Mswsock.h VersionHelpers.h \
         ShellScalingApi.h ShlObj.h ShlWApi.h Wincrypt.h Winerror.h Objbase.h \
         Psapi.h Shlobj.h Strsafe.h Wingdi.h Winuser.h Knownfolders.h \
         Propsys.h Propvarutil.h Setupapi.h Timeapi.h Uxtheme.h Unknwn.h; do
  lower="$(echo "$h" | tr 'A-Z' 'a-z')"
  if ! printf '#include <%s>\n' "$lower" | cmp -s - "$SHIM/$h"; then
    printf '#include <%s>\n' "$lower" > "$SHIM/$h"
  fi
done
echo "case shims written to $SHIM"
