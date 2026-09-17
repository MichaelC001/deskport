# DeskPort 0.3.12 development prerelease

macOS can enumerate disconnected display connector placeholders without a stable
UUID. In 0.3.11 these entries made the original-layout snapshot fail, preventing
client-requested virtual-display resolutions from being applied and leaving the
stream at the default size.

Skip entries only when they are offline, inactive, not mirroring another display,
and lack a UUID. Preserve identifiable disabled displays for restoration, and
continue rejecting unidentified online or active displays.

The production topology adapter regression suite covers disconnected placeholders,
disabled displays, all three policies, reconnect, failed commits and crash recovery
with AddressSanitizer and UndefinedBehaviorSanitizer. Physical streaming and
layout restoration still require user verification after activation.

Delivery targets are the signed/notarized macOS arm64 package and the NixOS
x86_64 source package. This is a prerelease; the stable release is unchanged.
