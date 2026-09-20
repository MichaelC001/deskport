#!/usr/bin/env python3
"""Retain host source notices and header-only dependency copyright text."""
import os
from pathlib import Path
import shutil
import sys
wb = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1])
roots = [wb/'full/sunshine/third-party']
roots += sorted((wb/'full/host-build/_deps').glob('*-src'))
for root in roots:
    label = 'sunshine-third-party' if root.name == 'third-party' else root.name
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in ('.git', 'node_modules', '__pycache__')]
        for name in files:
            p = Path(base)/name
            lower = name.lower()
            # NV codec headers carry their license inside the header itself.
            codec_header = 'nv_codec_headers' in label and p.suffix == '.h'
            ff_codec_header = 'nv-codec-headers' in p.parts and p.suffix == '.h'
            if lower.startswith(('license', 'licence', 'copying', 'copyright', 'notice')) or codec_header or ff_codec_header:
                target = out/'host-source-notices'/label/p.relative_to(root)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(p, target)
