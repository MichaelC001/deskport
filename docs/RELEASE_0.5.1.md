# DeskPort 0.5.1

Desktop maintenance release candidate. Publication and the complete platform
matrix remain pending verification.

## Changes

- Handle failed ENet host creation without dereferencing a null host during
  session startup; preserve the existing connection error path.
- Preserve the configured connection entry when refreshing authenticated peer
  endpoints, avoiding accidental replacement of the saved entry.

## Release status

macOS Apple Silicon and Linux x86_64 packages are being prepared from the current
main baseline. Windows x64 integration remains on its development branch while
Defender detections and trusted code signing are resolved. No Windows acceptance
or complete cross-platform publication is claimed by this candidate.

Package validation does not establish native GPU, live input, WAN latency or
long-session acceptance. Mobile clients and private mobile sources are excluded.
