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

## Validation status

Implementation and acceptance evidence are recorded per run. Synthetic event
checks are separate from native candidate UI and end-to-end host acceptance.
The initial macOS adapter/overlay regression passed. Linux Nix, additional
platform compilation and native IME sessions are recorded below as they finish.
No deployed application, personal host, clipboard, mobile repository, system
permission or persistent input-source configuration is changed by this work.
