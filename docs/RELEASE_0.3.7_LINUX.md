# DeskPort 0.3.7 Linux primary-display mirroring preview

KDE sharing now makes the owned virtual display primary and mirrors its viewport
onto the other enabled screens. The client controls resolution and 1×/2× scale
without leaving the physical desktop in a separate extended workspace. Physical
pixel modes and rotation are preserved; differing aspect ratios may add borders.

An independent recovery process records the original output order and replication
sources before any layout change. It restores them on sharing shutdown or helper
process death, including SIGKILL. Individual application window positions and
monitor hotplug during sharing still require acceptance testing.

GNOME retains the adaptive extended-display backend from 0.3.6; physical mirroring
on GNOME is not part of this fix. This is a Linux Nix preview; macOS stays on 0.3.5
and the existing mobile client can be used.

Validation: isolated KWin with two outputs, 12 mode/scale changes, virtual-primary
and replication checks, software capture in landscape and portrait, missing-output
capture refusal, and original layout restoration after EOF and SIGKILL. Real
physical-screen presentation and iPad input alignment still need user acceptance.
