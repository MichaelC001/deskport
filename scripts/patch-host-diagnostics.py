#!/usr/bin/env python3
"""A null-device log target must never rotate or create any host log files."""
from pathlib import Path
import sys
p = Path(sys.argv[1]) / 'src/logging.cpp'
s = p.read_text()
old = '''    if (const auto rotation_error = rotate_log_file(log_path)) {
      std::cerr << "Failed to rotate log file '" << log_file << "': " << rotation_error.message() << '\\n';
    }'''
new = '''    // DeskPort consumes stdout via its opt-in, privacy-filtered diagnostics sink.
    // Never try to rename /dev/null or create generations of a device target.
    const bool file_logging = log_file != "/dev/null" && log_file != "NUL" && log_file != "\\\\\\\\.\\\\NUL";
    if (file_logging) {
      if (const auto rotation_error = rotate_log_file(log_path)) {
        std::cerr << "Failed to rotate log file: " << rotation_error.message() << '\\n';
      }
    }'''
marker = '    const bool file_logging = '
if marker not in s:
    if old in s:
        s = s.replace(old, new, 1)
    elif 'rotate_log_file' not in s:
        # The pinned Linux host predates upstream rotation.
        anchor = '    sink = boost::make_shared<text_sink>();'
        if s.count(anchor) != 1: raise SystemExit('Unrecognized host initialization')
        guard = next(line for line in new.splitlines() if 'const bool file_logging' in line)
        s = s.replace(anchor, guard + '\n' + anchor, 1)
    else:
        raise SystemExit('Unrecognized host rotation implementation')
    old_sink = '    sink->locked_backend()->add_stream(boost::make_shared<std::ofstream>(log_file));'
    if s.count(old_sink) != 1: raise SystemExit('Unrecognized host file sink')
    s = s.replace(old_sink, '    if (file_logging) sink->locked_backend()->add_stream(boost::make_shared<std::ofstream>(log_file));', 1)
    p.write_text(s)
