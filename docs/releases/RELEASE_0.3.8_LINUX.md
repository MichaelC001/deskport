# DeskPort 0.3.8 Linux startup-mode recovery preview

Fix KDE sharing startup when KWin initially announces a virtual-output mode or
scale different from the creation request. DeskPort now explicitly applies and
verifies the requested initial mode before making the virtual display primary
and mirroring the physical screens. Previously it stopped before starting capture
and repeated the same failure on every retry.

Virtual-display startup errors are also written to the application journal for
remote diagnosis. The previous mirroring/recovery behavior remains in place;
GNOME remains an adaptive extended display. This is a Linux Nix preview; macOS
stays on 0.3.5 and no mobile update is required.

Validation includes an isolated compositor fixture that creates the initial
output at a deliberately different size and 2× scale, then verifies reconciliation,
12 subsequent resizes, primary/mirror state and EOF/SIGKILL layout restoration.
Physical GPU behavior and iPad end-to-end acceptance remain separate checks.
