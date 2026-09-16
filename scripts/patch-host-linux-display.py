#!/usr/bin/env python3
"""Use the owned Mutter PipeWire node; never prompt for or capture another screen."""
from pathlib import Path
import difflib
import subprocess
import sys

root = Path(sys.argv[1])
marker = root / 'deskport-linux-display.patch'
if marker.exists():
    subprocess.run(['git', 'apply', '--reverse', '--check', str(marker.resolve())], cwd=root, check=True)
    raise SystemExit(0)
changes = {}
def edit(name, old, new):
    path = root / 'src/platform/linux' / name
    original, source = changes.get(path, (path.read_text(), path.read_text()))
    if source.count(old) != 1:
        raise SystemExit(f'Expected one Linux display anchor in {name}: {old!r}')
    changes[path] = original, source.replace(old, new, 1)

edit('portalgrab.cpp', '// local includes', '#include <boost/property_tree/json_parser.hpp>\n#include <fstream>\n\n// local includes')
edit('portalgrab.cpp', '      // Connect DBus portal session', '''      if (const char* path = std::getenv("DESKPORT_VIRTUAL_DISPLAY")) {
        // The parent owns the Mutter session and keeps its sizing consumer alive.
        // Re-read the atomically replaced descriptor after each resize/reconnect.
        try {
          boost::property_tree::ptree state;
          boost::property_tree::read_json(path, state);
          const auto output = state.get<std::string>("output");
          const auto node = state.get<uint32_t>("node");
          const auto serial = state.get<uint64_t>("serial");
          const auto width = state.get<int>("width");
          const auto height = state.get<int>("height");
          if (serial == 0 || (serial & SPA_ID_INVALID) == SPA_ID_INVALID || node == 0 || node == PW_ID_ANY || width < 640 || width > 7680 || height < 360 || height > 4320) return -1;
          for (const auto& monitor : wl::monitors()) {
            if (monitor->name != output) continue;
            if (monitor->viewport.width != width || monitor->viewport.height != height) return -1;
            pipewire.set_negotiate_maxframerate(false);
            out_pipewire_fd = -1;
            out_pipewire_node = node;
            out_pipewire_object_serial = serial;
            this->width = width; this->height = height;
            this->offset_x = monitor->viewport.offset_x; this->offset_y = monitor->viewport.offset_y;
            this->logical_width = monitor->viewport.logical_width;
            this->logical_height = monitor->viewport.logical_height;
            BOOST_LOG(info) << "DeskPort owned virtual capture: " << output << " " << width << "x" << height;
            return 0;
          }
        } catch (const std::exception& exception) {
          BOOST_LOG(error) << "DeskPort virtual capture descriptor: " << exception.what();
        }
        return -1; // Never fall back to a portal dialog or the physical monitor.
      }
      // Connect DBus portal session''')
edit('portalgrab.cpp', '  std::vector<std::string> portal_display_names() {', '''  std::vector<std::string> portal_display_names() {
    if (std::getenv("DESKPORT_VIRTUAL_DISPLAY")) return {"DeskPort"};''')
edit('kwingrab.cpp', '#include \"src/video.h\"', '#include \"src/video.h\"\n#include \"src/config.h\"')
# KWin's upstream fallback to the first output must not expose a physical monitor
# if the dedicated display disappears during capture startup.
edit('kwingrab.cpp', '      // Fall back to first element from the map in case of error', '''      if ((!output || !out_params) && (output_name.starts_with("DeskPort-") || output_name.starts_with("Virtual-DeskPort-"))) {
        BOOST_LOG(error) << "DeskPort virtual output is unavailable; refusing physical display fallback";
        return -1;
      }
      // Fall back to first element from the map in case of error''')
# Enumeration must retain the explicit target even during output loss; otherwise
# Sunshine can select a physical output before the capture-level guard sees it.
edit('kwingrab.cpp', '    return screencast->get_output_names();', """    const auto& wanted = config::video.output_name;
    if (wanted.starts_with("DeskPort-") || wanted.starts_with("Virtual-DeskPort-")) return {wanted};
    return screencast->get_output_names();""")
patch = ''.join(''.join(difflib.unified_diff(old.splitlines(True), new.splitlines(True),
    fromfile='a/' + str(path.relative_to(root)), tofile='b/' + str(path.relative_to(root)))) for path, (old, new) in changes.items())
marker.write_text(patch)
subprocess.run(['git', 'apply', '--check', str(marker.resolve())], cwd=root, check=True)
subprocess.run(['git', 'apply', str(marker.resolve())], cwd=root, check=True)
