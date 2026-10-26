#!/usr/bin/env python3
"""Initialize Linux GPU environment before the asynchronous logger starts."""
from pathlib import Path
import sys

root = Path(sys.argv[1]) / 'src'
platform = root / 'platform/linux/misc.cpp'
main = root / 'main.cpp'
source = platform.read_text()
entry = main.read_text()
marker = 'void prepare_gpu_environment()'
if marker not in source:
    anchor = '  std::unique_ptr<deinit_t> init() {\n'
    if source.count(anchor) != 1:
        raise SystemExit('Unrecognized Linux platform initialization')
    start = source.index(anchor) + len(anchor)
    end = source.index('    // These are allowed to fail.', start)
    environment = source[start:end]
    if '"AMD_DEBUG"' not in environment or '"RADV_PERFTEST"' not in environment:
        raise SystemExit('Unrecognized Linux GPU environment initialization')
    source = source[:start] + source[end:]
    source = source.replace(anchor,
        '  // setenv may invalidate environ while the log formatter calls localtime.\n'
        '  // Run this before logging::init starts its background thread.\n'
        '  void prepare_gpu_environment() {\n' + environment.rstrip() + '\n  }\n\n' + anchor, 1)

declaration = '#ifdef __linux__\nnamespace platf { void prepare_gpu_environment(); }\n#endif\n\n'
call = ('#ifdef __linux__\n'
        '  platf::prepare_gpu_environment();\n'
        '#endif\n\n')
if declaration not in entry:
    anchor = 'using namespace std::literals;'
    if entry.count(anchor) != 1:
        raise SystemExit('Unrecognized host entry declarations')
    entry = entry.replace(anchor, declaration + anchor, 1)
if call not in entry:
    anchor = '  auto log_deinit_guard = logging::init('
    if entry.count(anchor) != 1:
        raise SystemExit('Unrecognized host logging initialization')
    entry = entry.replace(anchor, call + anchor, 1)
platform.write_text(source)
main.write_text(entry)
