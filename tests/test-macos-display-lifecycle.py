#!/usr/bin/env python3
"""Opt-in native virtual-display lifecycle check in an isolated extended workspace.
Never starts/replaces the installed host or captures the physical desktop.
"""
import argparse,json,os,select,subprocess,tempfile,time
from pathlib import Path
p=argparse.ArgumentParser(); p.add_argument('helper'); p.add_argument('--native',action='store_true'); args=p.parse_args()
if not args.native: p.error('--native is required to create a temporary extended display')
with tempfile.TemporaryDirectory(prefix='deskport-display-lifecycle-') as tmp:
    probe=Path(tmp)/'displays.c'
    probe.write_text('#include <CoreGraphics/CoreGraphics.h>\n#include <stdio.h>\nint main(){CGDirectDisplayID ids[128];uint32_t n=0;if(CGGetOnlineDisplayList(128,ids,&n))return 1;for(unsigned i=0;i<n;i++)printf("%u\\n",ids[i]);return 0;}')
    subprocess.run(['xcrun','clang',str(probe),'-framework','CoreGraphics','-o',str(Path(tmp)/'displays')],check=True)
    def displays(): return set(subprocess.check_output([str(Path(tmp)/'displays')],text=True).split())
    baseline=displays()
    env=dict(os.environ,DESKPORT_DISPLAY_ISOLATED='1',DESKPORT_DISPLAY_STATE_DIR=tmp,DESKPORT_DISPLAY_SERIAL='2147483901')
    log=open(Path(tmp)/'helper.log','wb')
    child=subprocess.Popen([str(Path(args.helper).resolve()),'2560','1440'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=log,env=env)
    pending=b''
    def receive(seq=None):
        global pending
        deadline=time.monotonic()+20
        while time.monotonic()<deadline:
            if b'\n' not in pending:
                if not select.select([child.stdout],[],[],0.2)[0]: continue
                data=os.read(child.stdout.fileno(),65536)
                if not data: raise RuntimeError('helper exited')
                pending+=data
            while b'\n' in pending:
                line,pending=pending.split(b'\n',1); response=json.loads(line)
                if (seq is None and 'ready' in response) or response.get('seq')==seq:
                    assert 'error' not in response,response
                    return response
        raise TimeoutError('helper response')
    try:
        first=receive(); assert first==dict(ready=True,active=False),first
        assert displays()==baseline, 'startup created an unexpected display'
        print('PASS startup ready without a virtual display',flush=True)
        seq=0
        for cycle in range(3):
            for active in (True,False):
                seq+=1
                request=dict(seq=seq,width=2560,height=1440,scale=2,session=active,displayPolicy=2)
                child.stdin.write(json.dumps(request).encode()+b'\n');child.stdin.flush()
                result=receive(seq)
                if active:
                    assert result['displayId']>0 and result['width']==2560 and result['height']==1440,result
                    ident=str(result['displayId']); assert ident in displays()
                else:
                    assert result['active'] is False and result['width']==0 and result['height']==0,result
                    assert ident not in displays(), 'virtual display still registered after release'
                    assert displays()==baseline, 'physical display membership changed'
                print(f'PASS cycle {cycle+1}: '+('created' if active else 'removed and restored'),flush=True)
    finally:
        child.stdin.close()
        try: child.wait(timeout=10)
        except subprocess.TimeoutExpired: child.terminate();child.wait(timeout=5)
        log.close()
        if child.returncode: print((Path(tmp)/'helper.log').read_text())
    assert child.returncode==0,child.returncode
