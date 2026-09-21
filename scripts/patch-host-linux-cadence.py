#!/usr/bin/env python3
"""Adapt PipeWire idle detection/polling without reading GPU pixels back to CPU."""
from pathlib import Path
import sys
p = Path(sys.argv[1]) / 'src/platform/linux/pipewire.cpp'
s = p.read_text()
if 'bool deskport_smart = false;' in s:
    raise SystemExit(0)
def patch(old, new):
    global s
    if s.count(old) != 1:
        raise SystemExit(f'Linux cadence anchor mismatch: {old[:80]!r}')
    s = s.replace(old, new)
patch('''      img_descriptor->pw_damage = (damage && damage->region.size.width > 0 && damage->region.size.height > 0) ? std::optional<bool>(true) : std::nullopt;''', '''      // SPA's first invalid region terminates the damage array. Distinguish
      // an explicitly empty array from missing metadata (which means unknown).
      img_descriptor->pw_damage = damage ? std::optional<bool>(spa_meta_region_is_valid(damage)) : std::nullopt;''')
patch('''      // calculate frame interval we should capture at''', '''      deskport_smart = config.deskport_smart;
      // calculate frame interval we should capture at''')
patch('snapshot(pull_free_image_cb, img_out, 1000ms, *cursor)', 'snapshot(pull_free_image_cb, img_out, deskport_smart ? 10ms : 1000ms, *cursor)')
patch('''    bool is_buffer_redundant(const egl::img_descriptor_t *img) {''', '''    bool deskport_smart = false;
    bool is_buffer_redundant(const egl::img_descriptor_t *img) {
      // Only skip explicit unchanged content after a delivered frame. A sequence
      // gap might hide the last changed frame, so deliver it conservatively.
      if (deskport_smart && last_seq && img->seq && *img->seq == *last_seq + 1 &&
          img->pw_damage.has_value() && !*img->pw_damage) {
        last_seq = img->seq;
        return true;
      }''')
p.write_text(s)
