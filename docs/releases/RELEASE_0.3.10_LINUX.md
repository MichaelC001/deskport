# DeskPort 0.3.10 Linux device address editor preview

Change a bound computer's IP address or domain directly from its device card's
**More → Change address** menu, or from **Device settings**. The editor is
available while the device is offline. Previously this operation was only exposed
under **Add a device → Saved access → Edit device**.

All entry points share the existing validated editor. Saving preserves the pinned
certificate and device identity, retains the configured ports, and updates the
saved address shown in settings. Domains remain names and are resolved again on
connection. Advanced port overrides remain optional. Finish the current session
before changing connection information. Legacy PIN-paired hosts retain their
existing management flow.

This Linux Nix prerelease retains the 0.3.9 virtual-display cleanup, physical-layout
restoration and KDE input-origin correction. macOS packages remain on 0.3.5.
No iPad update is needed for the desktop address editor.

Validation covers editing a saved device through the settings UI, rejecting an
invalid URL without changing stored data, retaining identity and ports, and
showing the new domain when the editor reopens. Existing binding regression tests
cover persistence and IPv6 addresses. UI (19), binding (27), seven-language catalog checks and the Nix build passed.
Live connection to a changed endpoint remains a user check.
