# 0.4.2-preview.1 — Confirmed takeover and desktop fine tuning

This is a development prerelease for macOS arm64 and NixOS x86_64, not a stable
release. Existing stable downloads remain unchanged.

- Confirm before taking over an occupied host. Cancellation keeps the current
  client connected; confirmation terminates its stream and releases input before
  admitting the new client. Admission stays on the authenticated display channel.
- Per-device desktop fine tuning applies a final multiplier after workspace
  calculation: 0.5, 0.6, 0.7, 0.8, 0.9, 1.0, 1.2, 1.3, 1.4 and 1.5. Smaller values
  enlarge text and controls. Host protocol limits still apply.
- Recover macOS sharing when the dedicated virtual display cannot be separated,
  and provide an isolated, temporary extended-display acceptance host.
- Companion native clients add confirmed takeover, Android density-aware sizing,
  direct touch improvements and per-device desktop adjustment. They are delivered
  privately; this release does not publish mobile apps.

Validation includes Linux Nix builds, macOS packaging, shared C/C++/Qt workspace
checks, 57 binding cases, 19 UI cases, per-device settings isolation, and topology
sanitizer checks. Native clients passed 40 Android transport cases, Apple display
transport/lifecycle checks and iPad/iPhone simulator persistence/rotation tests.

Real-device takeover cancellation/confirmation, input release, and final tuning
visual checks remain pending after manual activation. An isolated extended-display
test does not establish production mirror/primary-only acceptance. No installed
host service is restarted by preparing this release.
