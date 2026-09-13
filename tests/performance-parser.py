import importlib.util
from pathlib import Path
import tempfile
spec = importlib.util.spec_from_file_location('measure', Path(__file__).resolve().parents[1] / 'scripts/measure-performance.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
assert m.cpu_seconds('01:02.50') == 62.5
assert m.cpu_seconds('1-02:03:04') == 93784
with tempfile.TemporaryDirectory() as d:
    p = Path(d) / 'log'
    stages = [('observed',100,0,0), ('observed',200,0,0), ('stop-begin',500,1920,1080),
              ('stop-end',600,0,0), ('mode-request',610,1920,1080), ('mode-ready',720,1920,1080),
              ('first-render-submit',800,1280,720), ('first-render-submit',900,1920,1080)]
    p.write_text('\n'.join(f'DeskPort resize stage={s} tick_ms={t} width={w} height={h}' for s,t,w,h in stages))
    result = m.summarize_log(p)
    assert result['completed'] == 1 and result['samples'][0]['to_render_submit_ms'] == 700
    assert result['samples'][0]['mode_ms'] == 110
    p.write_text('DeskPort resize stage=stop-begin tick_ms=1000 width=1920 height=1080\n'
                 'DeskPort resize stage=first-render-submit tick_ms=20 width=1920 height=1080')
    result = m.summarize_log(p)
    assert result['completed'] == 0 and result['incomplete'] == 1
print('PASS: CPU time units, latest observation, mismatched frames and process tick reset')
