#!/usr/bin/env python3
"""Resolve the owned Windows output again after a driver mode-list reload."""
from pathlib import Path
import sys
root = Path(sys.argv[1])
p = root / 'src/display_device.cpp'
s = p.read_text()
if '// DeskPort dynamic Windows capture identity' not in s:
    s = '#include <fstream>\n#include <filesystem>\n' + s
    old = '''    if (output_name.rfind(R"(\\\\.\\DISPLAY)", 0) == 0 && std::getenv("DESKPORT_HOST_STATE_DIR")) {
      return output_name;
    }'''
    new = r'''    // DeskPort dynamic Windows capture identity: re-enumeration changes GDI IDs.
    if (const auto state = _wgetenv(L"DESKPORT_HOST_STATE_DIR"); state && *state) {
      std::ifstream file(std::filesystem::path(state) / L"windows-capture-output");
      std::string name;
      std::getline(file, name);
      const std::string prefix = R"(\\.\DISPLAY)";
      if (name.size() > prefix.size() && name.size() <= prefix.size() + 10 &&
          name.rfind(prefix, 0) == 0 && name.find_first_not_of("0123456789", prefix.size()) == std::string::npos) {
        return name;
      }
      // Fail closed while the guardian is rebuilding the owned display.
      return "DeskPort-owned-display-unavailable";
    }'''
    if old not in s: raise SystemExit('Windows display mapping anchor changed')
    p.write_text(s.replace(old, new))
p = root / 'src/video.cpp'
s = p.read_text()
if '// DeskPort must never fall back to a physical desktop' not in s:
    old = '    display_names = platf::display_names(dev_type);'
    new = old + '''
#ifdef _WIN32
    // DeskPort must never fall back to a physical desktop while the owned
    // output is absent or has been assigned a new GDI name.
    if (std::getenv("DESKPORT_HOST_STATE_DIR")) {
      display_names.assign(1, output_name);
      current_display_index = 0;
      return;
    }
#endif'''
    if s.count(old) != 1: raise SystemExit('Display enumeration anchor changed')
    p.write_text(s.replace(old,new))
