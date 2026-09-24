#!/usr/bin/env python3
"""Audit every extractable PE without executing Windows code."""
from pathlib import Path
import hashlib
import json
import re
import struct
import subprocess
import sys
import zipfile
external_runtime = re.compile(r'qt[56]|avcodec|avutil|avformat|avfilter|swscale|swresample|SDL[23]|libgcc|libstdc|libwinpthread|libmcfgthread|libssl|libcrypto|libplacebo|libopus|libcurl|libfreetype|libsoundio|vcruntime|msvcp[0-9]', re.I)
art = Path(sys.argv[1]).resolve()
suffix = '-'+sys.argv[2] if len(sys.argv) > 2 else ''
version = (Path(__file__).resolve().parents[2] / 'app/version.txt').read_text().strip()
assert re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', version), version
exe = art / (f'DeskPort-{version}-windows-x64-setup'+suffix+'.exe')
zip_path = art / (f'DeskPort-{version}-windows-x64-portable'+suffix+'.zip')
audit = art/('audit'+suffix)
audit.mkdir(exist_ok=True)
extracted = audit/'installer-extracted'
subprocess.run(['7z','x','-y',str(exe),'-o'+str(extracted)],check=True,stdout=(audit/'extract.log').open('w'))
subprocess.run(['7z','t',str(exe)],check=True,stdout=(audit/'installer-test.txt').open('w'))
subprocess.run(['7z','l','-slt',str(exe)],check=True,stdout=(audit/'installer-contents.txt').open('w'))
with zipfile.ZipFile(zip_path) as z:
    assert z.testzip() is None
    assert 'portable.dat' in z.namelist()
    files={n for n in z.namelist() if not n.endswith('/')}
    compared=[]
    for p in extracted.rglob('*'):
        if not p.is_file() or '$PLUGINSDIR' in p.parts: continue
        name=str(p.relative_to(extracted))
        assert name in files, name
        assert hashlib.sha256(p.read_bytes()).digest()==hashlib.sha256(z.read(name)).digest(), name
        compared.append(name)
    assert files-set(compared)=={'portable.dat'},files-set(compared)
    (audit/'payload-comparison.json').write_text(json.dumps({'matching_files':len(compared),'zip_only':['portable.dat'],'installer_plugins':['nsDialogs.dll','System.dll']},indent=2)+'\n')
    (audit/'portable-contents.txt').write_text('\n'.join(sorted(files))+'\n')
uninstaller=art/'Uninstall.exe'
assert uninstaller.is_file(), 'Generated uninstaller is required for auditing'
summary=[]
for p in [exe, uninstaller]+sorted(extracted.rglob('*')):
    if not p.is_file(): continue
    with p.open('rb') as f: head=f.read(64)
    if head[:2]!=b'MZ': continue
    data=p.read_bytes(); offset=struct.unpack_from('<I',data,60)[0]
    assert data[offset:offset+4]==b'PE\0\0'
    machine=struct.unpack_from('<H',data,offset+4)[0]
    text=subprocess.check_output(['objdump','-p',str(p)],text=True)
    label='setup' if p==exe else ('uninstaller' if p==uninstaller else str(p.relative_to(extracted)).replace('/','_').replace('$',''))
    (audit/(label+'.pe.txt')).write_text(text)
    imports=re.findall(r'DLL Name: (.+)',text)
    magic=struct.unpack_from('<H',data,offset+24)[0]
    directory=offset+24+(112 if magic==0x20b else 96)
    security=struct.unpack_from('<II',data,directory+4*8)
    delay=struct.unpack_from('<II',data,directory+13*8)
    summary.append({'file':str(p.relative_to(art)), 'machine':hex(machine), 'sha256':hashlib.sha256(data).hexdigest(),'imports':imports,'security_directory':security,'delay_import_directory':delay})
    if p.name=='DeskPort.exe':
        subprocess.run([sys.executable, str(Path(__file__).with_name('verify-version.py')), str(p), version], check=True)
        assert machine==0x8664
        assert not any(external_runtime.search(dll) for dll in imports)
        start=data.find(b'<assembly ')
        if start>=0:
            end=data.find(b'</assembly>',start)
            if end>=0: (audit/'application-manifest.xml').write_bytes(data[start:end+len(b'</assembly>')])
(audit/'pe-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
if '-full' in suffix:
    expected={'DeskPort.exe','deskport-maintenance.exe','deskport-driver-setup.exe','host/deskport-host.exe','host/deskport-display.exe','host/deskport-display-recovery.exe','driver/MttVDD.dll'}
    actual={str(p.relative_to(extracted)) for p in extracted.rglob('*') if p.is_file() and p.read_bytes()[:2]==b'MZ' and '$PLUGINSDIR' not in p.parts}
    assert actual==expected,(actual,expected)
    for entry in summary:
        name=entry['file']
        if 'installer-extracted/' in name and '$PLUGINSDIR' not in name:
            assert entry['machine']=='0x8664',entry
            assert not any(external_runtime.search(dll) for dll in entry['imports']),entry
else:
    assert len(summary)==5,summary
print('PASS: setup, generated uninstaller, all extracted payload and plugin PEs audited; ZIP and installer payloads match except portable.dat.')
