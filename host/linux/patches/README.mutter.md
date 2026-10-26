# Mutter stream-close lifetime fix

`mutter-51-stream-close-lifetime.patch` fixes a compositor ownership bug in
Mutter 51.0 and 51.1's PipeWire close dispatcher. It is not applied by the
DeskPort application build: Mutter runs inside the user's GNOME Shell process.

When PipeWire disappears, `pipewire_loop_source_dispatch()` emits `closed` on
the source. The handler stops the stream and can release the session's last
stream reference. GLib keeps the source alive during signal emission, but the
source's stream pointer is borrowed. Once emission releases its source reference,
`meta_stream_source_virtual_dispose()` accesses the already freed stream to find
the backend and monitor manager. The Ubuntu 26.10 / Mutter `51.0-1ubuntu3` trace
ends in `g_signal_handler_disconnect()` called with an invalid monitor manager.

The patch retains the stream across signal emission. This keeps the backend
available until source disposal finishes, without extending either object's
lifetime beyond the dispatcher. The upstream source and its copyright notices
remain intact. References:

- [Mutter 51.0 dispatcher](https://github.com/GNOME/mutter/blob/51.0/src/backends/meta-stream-source.c)
- [Stream close handler](https://github.com/GNOME/mutter/blob/51.0/src/backends/meta-stream.c)
- [Virtual source disposal](https://github.com/GNOME/mutter/blob/51.0/src/backends/meta-stream-source-virtual.c)

## Regression

Run with a pristine reviewed Mutter source and a C compiler/GLib development
environment. Put `TMPDIR` under the managed test output directory.

```sh
python3 scripts/test-mutter-stream-lifetime.py /path/to/mutter/src/backends/meta-stream-source.c
```

The test compiles the actual dispatcher against a GObject ownership fixture.
It requires the original code to fail, applies the patch, then verifies 1,000
close cycles and ordinary non-closing dispatches. It has been run against both
51.0 and 51.1 sources. Native service-loss acceptance is a separate gate:

```sh
DESKPORT_GNOME_TEST_SESSION=1 python3 scripts/test-gnome-display.py /path/to/deskport-display --pipewire-loss
```

Run the latter only in an owned disposable desktop VM. It stops and kills that
guest's PipeWire service, checks all three display policies, asserts that the
Mutter D-Bus owner did not change, verifies physical-layout restoration and
journal removal, then starts a fresh owned lease and capture.

## Explicit system integration

Nix users can select `overlays.mutterStreamLifetime` from this repository's
flake, or import `host/linux/mutter-stream-lifetime-overlay.nix`. The overlay
preserves existing Mutter patches and deliberately rejects unreviewed versions.
It is never added implicitly when installing DeskPort. A distribution maintainer
can instead apply the same source patch when rebuilding its Mutter package.

Selecting/building this compositor fix is distinct from system activation and
starting a GNOME session with the rebuilt compositor. Do not overwrite a loaded
Mutter library or restart a personal desktop as part of a DeskPort update.
See [GNOME validation](../../../docs/GNOME_DISPLAY.md) for the exact runtime
evidence and remaining boundaries.
