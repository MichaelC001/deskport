# Local input methods in the desktop viewer

The per-device **Keyboard & pointer → Use the local input method** option uses
the client operating system's IME. Reconnect after changing the option. It is
off by default, retaining physical keys and the remote operating system's IME.
Use physical mode for games and applications that require scancodes.

Local mode routes the focused, visible viewer's keyboard even when the pointer
moves into its native candidate popup or outside the picture. System shortcut
capture is disabled so OS input-method switching remains available. Focus loss,
hiding/minimizing, explicit input release and disconnect cancel the local
composition, discard queued text and release remotely held keys. A candidate
popup that does not steal focus can be selected with either keyboard or mouse.

Composition text is displayed locally at the top left, with selection and an
insertion cursor using Qt platform font fallback. SDL provides native candidate
UI on Windows, macOS and Linux (subject to the installed input method/backend).
The fixed candidate anchor is in SDL logical window coordinates and is updated
when input is activated. It is not the remote caret. Preedit uses drawable
pixels; on HiDPI displays the preview and native popup need not align exactly.
The preview temporarily covers statistics in the same corner.

Desktop does not currently subscribe to the existing optional `text-caret`
protocol. Host caret readers depend on application accessibility support and
can explicitly report unknown geometry. This change does not infer a caret from
the mouse, video, or a stale reading and makes no new wire/capability changes.
The shared behavior record is core `protocol/LOCAL_TEXT_INPUT.md`.

Only committed UTF-8 enters the existing reliable text-event channel. No
clipboard is read or written. Physical printable keys, dead keys, AltGr/Option,
local language-switch chords and candidate navigation stay local. Outside
composition, Ctrl/Meta command chords and non-text editing/navigation keys are
forwarded, with held modifiers tracked for release. DeskPort's existing
Ctrl+Alt+Shift shortcuts remain available and cancel composition first.
When composing, Enter/Escape/arrows/Backspace belong to the IME. If the input
method emits multiple commit fragments, each fragment is sent once, in order.

This mode depends on a host that accepts Moonlight UTF-8 events and a target
application that supports its host's Unicode insertion. It cannot reproduce
remote application composition APIs, preedit attributes or a hardware-key-only
workflow. A rejected transport enqueue is logged without the text. There is no
automatic physical-key replay, which would risk duplicates.

## Reproducible isolated checks

Run `nix develop -c python3 scripts/test-local-text-input.py` for synthetic SDL
Chinese, Japanese, Korean, combining marks, supplementary Unicode, cancellation,
wrong-window/stale rejection, command/AltGr routing and preedit-surface checks.
This runs with dummy SDL video/offscreen Qt and never connects to a host.
The device preference regression verifies default-off and device isolation.

For native input, reserve the shared host's GUI resource first, then run:

```sh
DESKPORT_IME_TEST_LOG=/absolute/build/path/native-ime.log \
  nix develop -c python3 scripts/test-local-text-input.py --native
```

The disposable native SDL window uses the production IME adapter and font path,
with a local text sink replacing network transport. Type only synthetic text.
The window closes after two minutes or its close action. Its log contains the
synthetic commit/preedit text, consumed scancodes and no clipboard or host data.
Use each installed CJK input method to compose, select a candidate, cancel,
change focus, hide/restore and enter another string; inspect native candidate
placement separately from the committed sink. The harness does not prove full
remote application insertion or streaming acceptance.

## Validation status — 2026-10-17

| Check | Result | Boundary |
| --- | --- | --- |
| macOS production input/session, overlay, Metal and SDL renderer translation units | Passed in the locked devShell | Compilation; no packaged stream acceptance |
| Windows production input/session, D3D9/D3D11 and SDL renderer translation units | MinGW cross compilation passed | No installed application replacement |
| Linux full Nix package | Passed for functional source `bda33d89` | Same package with builder-only CPU 0–3 / nice 15 pre-configure hooks; four build cores |
| Synthetic SDL adapter checks on macOS and Linux | Passed | Chinese/Japanese/Korean, combining marks, emoji, long preedit, stale/window guards, cancellation, commands and AltGr |
| Native Cocoa / Apple Pinyin adapter | Passed | Two `你好` commits; Escape, hide during composition, recall, and input-source restoration read back |
| Native Windows / existing Microsoft Pinyin adapter | Passed | Two `你` commits; candidate confirmation, Escape, hide during composition and recall; source list and original foreground thread layout unchanged |
| Native Linux X11 / IBus adapter | Passed | Real libpinyin `你好`, Anthy `日本語`, Hangul `한` + `글` commits; Escape and hide/recall passed in an isolated namespace. CJK glyphs and the 1x Pinyin popup were inspected; owned processes exited |
| Per-device settings | 6 checks passed | Default-off, isolated persistence and existing display/language settings |
| Qt UI | 27 checks passed | Includes the switch and disabled system-shortcut capture; offscreen |
| Shipped translations | 17,867 finished entries compared at runtime | Resource consistency, not native-speaker review |
| Shared core and adapters | Passed | Matching Git/Nix pins, shared catalog and workspace fixtures |

The native tests use the production local adapter and a local commit sink. They
never connect to a host or read/write the clipboard. The Windows task requests
the interactive user and a limited run level and is removed afterwards. Deployed
DeskPort processes, services and pairing data are not replaced or restarted.

Native Japanese/Korean input methods on Windows/macOS were not enabled or
installed. Full remote application insertion, a native Wayland input-method
session, packaged fullscreen candidate placement, candidate mouse selection and
HiDPI visual acceptance remain separate checks. Native candidate confirmation
in the isolated harness does not establish end-to-end streaming/input acceptance.
