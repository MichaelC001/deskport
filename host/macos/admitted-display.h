// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <CoreGraphics/CoreGraphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// CoreGraphics can keep a process-local mode cache that omits displays created
// after host startup. The display owner publishes its verified backing mode
// atomically before acknowledging admission. Bounds/identity still come from the
// live compositor; an absent, removed or mismatched target fails closed.
static inline int dp_admitted_display_mode(CGDirectDisplayID wanted, int *width, int *height, int *scale) {
    const char *path = getenv("DESKPORT_CAPTURE_DISPLAY_FILE");
    if (!path) return 0;
    FILE *file = fopen(path, "r");
    if (!file) return 0;
    unsigned id = 0; int w = 0, h = 0, s = 0;
    const int fields = fscanf(file, "%u %d %d %d", &id, &w, &h, &s);
    fclose(file);
    if (fields != 4 || !id || (wanted && id != wanted) ||
        w < 640 || h < 360 || w > 16384 || h > 16384 ||
        (s != 1 && s != 2) || w % s || h % s || !CGDisplayIsActive(id)) return 0;
    const CGRect bounds = CGDisplayBounds(id);
    if (fabs(bounds.size.width - w / s) > 1 || fabs(bounds.size.height - h / s) > 1) return 0;
    *width = w; *height = h; *scale = s;
    return 1;
}
