#!/usr/bin/env python3
"""Run production broker policy and verify installer/build integration, without Windows."""
from pathlib import Path
import argparse
import os
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-directory', type=Path, required=True)
args = parser.parse_args()
out = args.output_directory.resolve()
out.mkdir(parents=True, exist_ok=True)
binary = out / 'windows-display-service-policy'
subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-I' + str(root), str(root / 'tests/windows-display-service.cpp'), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)

# Execute the actual guardian cancellation predicate and resize-enable branch
# against failing API doubles. A pending resize must never defeat stop/death.
recovery = (root / 'host/windows/display-recovery.cpp').read_text()
start = recovery.index('    const auto alive=[&]{')
predicate = recovery[start:recovery.index('\n    };', start) + len('\n    };')]
start = recovery.index('                            if(!resized&&alive())')
enable = recovery[start:recovery.index('                            mode->result=', start)]
unit = out / 'guardian-cancellation.cpp'
unit.write_text(r'''
#include <cassert>
#include <string>
constexpr int WAIT_TIMEOUT=258, ERROR_CANCELLED=1223;
int parent=1,release=2,brokerStop=3,brokerProcess=4;
bool signaled[5]{};
int WaitForSingleObject(int h,int){return signaled[h]?0:WAIT_TIMEOUT;}
int GetCurrentProcessId(){return 123;}
int main(){
''' + predicate + r'''
for(unsigned bits=0;bits<16;++bits){
 for(int i=1;i<=4;++i)signaled[i]=bool(bits&(1u<<(i-1)));
 assert(alive()==(bits==0));
 int calls=0,resized=0;std::wstring nonce=L"fixture";
 auto runWorker=[&](const std::wstring&){++calls;return 0;};
''' + enable + r'''
 assert(calls==(bits==0?1:0));
 assert(resized==(bits==0?0:ERROR_CANCELLED));
}
brokerStop=brokerProcess=0;signaled[1]=signaled[2]=false;
assert(alive());signaled[2]=true;assert(!alive());
}
''')
binary = out / 'guardian-cancellation'
subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                str(unit), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
assert recovery.index('if(!alive())break;') < recovery.index('if(wait==WAIT_OBJECT_0+2)')
print('PASS production guardian cancellation priority and no re-enable after parent/release/broker stop/death')

# These are packaging contract gates, not an emulation of SCM or native capture.
installer = (root / 'winbuild/scripts/deskport-full.nsi').read_text()
assert installer.count('register-display-service ') == 1
assert installer.count('remove-display-service ') == 1
assert installer.count('stop-installation ') == 2
assert installer.index('stop-installation ') < installer.index('File /r "${PAYLOAD}\\*.*"')
assert installer.index('remove-display-service ') < installer.index('RMDir /r "$INSTDIR\\host"')
assert 'host\\deskport-display-service.exe' in installer
for name in ['package-full.sh', 'audit-package.py', 'build-maintenance.sh', 'export-full-materials.py']:
    assert 'deskport-display-service.exe' in (root / 'winbuild/scripts' / name).read_text(), name
helper = (root / 'host/windows/display-helper.cpp').read_text()
assert 'display_service::acquire(' in helper
assert 'ShellExecuteEx' not in helper and 'L"runas"' not in helper
assert 'FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION' in (root / 'host/windows/display-service-ipc.h').read_text()
assert 'parent,release,request,brokerStop,brokerProcess' in recovery
assert 'isSystem()' in recovery and 'trustedServer(broker,' in recovery
print('PASS installer ordering, complete payload/build integration, no UAC fallback and independent recovery wiring')
