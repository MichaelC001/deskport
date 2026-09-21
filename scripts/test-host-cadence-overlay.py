#!/usr/bin/env python3
"""Exercise both repository-owned upstream revisions without a live session."""
from pathlib import Path
import subprocess, tarfile, tempfile
root = Path(__file__).resolve().parents[1]
for archive in ('sunshine-nix.tar.gz', 'sunshine.tar.gz'):
    with tempfile.TemporaryDirectory(prefix='deskport-cadence-') as tmp:
        target = Path(tmp)
        with tarfile.open(root / 'host/vendor' / archive) as f:
            f.extractall(target, filter='data')
        if not (target / 'src').exists():
            target = next(p for p in target.iterdir() if (p / 'src').exists())
        for name in ('smart-stream', 'session-settings', 'session-takeover', 'linux-display', 'input-activity', 'sync-cadence', 'linux-cadence'):
            subprocess.run(['python3', str(root / f'scripts/patch-host-{name}.py'), str(target)], check=True)
        video = (target / 'src/video.cpp').read_text()
        assert 'const bool smart = config.deskport_smart;' in video
        assert 'ctx->config.deskport_smart' in video
        assert 'deskport_cadence.emplace(ctx.config.framerate)' in video
        assert 'pos->deskport_timestamp.reset()' in video
        print(f'PASS: async/sync/input/PipeWire overlays: {archive}')
