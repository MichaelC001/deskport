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
edit('kwingrab.cpp', '#include "src/config.h"', '#include "src/config.h"\n#include <boost/property_tree/json_parser.hpp>\n#include <fstream>')
edit('kwingrab.cpp', '      if (screencast->start(display_name) < 0) {', '''      std::string target = display_name;
      if (const char* path = std::getenv("DESKPORT_VIRTUAL_DISPLAY")) {
        try {
          boost::property_tree::ptree state;
          boost::property_tree::read_json(path, state);
          target = state.get<std::string>("output");
          if (!target.starts_with("DeskPort-") && !target.starts_with("Virtual-DeskPort-")) return -1;
        } catch (const std::exception&) { return -1; }
      }
      if (screencast->start(target) < 0) {''')
# Hyprland's dedicated headless output uses direct WLR screencopy. Keep the
# admitted identity during enumeration, then verify its descriptor and pixels
# before attaching; an absent output must never become the first physical one.
edit('wlgrab.cpp', '#include "src/video.h"', '''#include "src/video.h"
#include "src/config.h"
#include <boost/property_tree/json_parser.hpp>
#include <fstream>''')
edit('wlgrab.cpp', '      auto monitor = interface.monitors[0].get();', '''      if (interface.monitors.empty()) return -1;
      auto monitor = interface.monitors[0].get();
      std::string owned_output;
      int owned_width = 0, owned_height = 0, owned_scale = 0;
      if (const auto path = std::getenv("DESKPORT_VIRTUAL_DISPLAY")) {
        try {
          boost::property_tree::ptree state;
          boost::property_tree::read_json(path, state);
          owned_output = state.get<std::string>("output");
          owned_width = state.get<int>("width");
          owned_height = state.get<int>("height");
          owned_scale = state.get<int>("scale");
          if (!owned_output.starts_with("DeskPort-") || owned_output != display_name ||
              owned_width < 640 || owned_width > 7680 || owned_height < 360 || owned_height > 4320 ||
              owned_width % 4 || owned_height % 4 || (owned_scale != 1 && owned_scale != 2)) return -1;
        } catch (const std::exception&) {
          BOOST_LOG(error) << "DeskPort owned Hyprland output is not admitted";
          return -1;
        }
      }''')
edit('wlgrab.cpp', '        if (!matched) {', '''        if (!matched && (!owned_output.empty() || display_name.starts_with("DeskPort-"))) {
          BOOST_LOG(error) << "DeskPort owned Hyprland output disappeared; refusing physical display fallback";
          return -1;
        }
        if (!matched) {''')
edit('wlgrab.cpp', '      output = monitor->output;', '''      if (!owned_output.empty() && (monitor->viewport.width != owned_width || monitor->viewport.height != owned_height ||
          monitor->viewport.logical_width * owned_scale != owned_width || monitor->viewport.logical_height * owned_scale != owned_height)) {
        BOOST_LOG(error) << "DeskPort owned Hyprland output mode does not match its admission";
        return -1;
      }
      output = monitor->output;''')
edit('wlgrab.cpp', '    return display_names;', '''    if (config::video.output_name.starts_with("DeskPort-")) return {config::video.output_name};
    return display_names;''')
main = root / 'src/main.cpp'
original = main.read_text()
anchor = 'if (video::probe_encoders()) {'
if original.count(anchor) != 1: raise SystemExit('Unexpected encoder probe anchor')
changes[main] = original, original.replace(anchor, 'if (!std::getenv("DESKPORT_ON_DEMAND_DISPLAY") && video::probe_encoders()) {', 1)
patch = ''.join(''.join(difflib.unified_diff(old.splitlines(True), new.splitlines(True),
    fromfile='a/' + str(path.relative_to(root)), tofile='b/' + str(path.relative_to(root)))) for path, (old, new) in changes.items())
marker.write_text(patch)
subprocess.run(['git', 'apply', '--check', str(marker.resolve())], cwd=root, check=True)
subprocess.run(['git', 'apply', str(marker.resolve())], cwd=root, check=True)
