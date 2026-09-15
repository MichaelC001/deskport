# Shared clipboard

## 0.3.2 prerelease (2026-09-15)

The active, authenticated desktop session supports bidirectional UTF-8 text,
clipboard images and file/folder copies. Existing per-device sharing preferences
remain in use. Both endpoints need 0.3.2 for images/files; older peers retain the
previous text-only protocol. No initial clipboard contents are pushed on connect.

- **Text:** send immediately after a detected change (250 ms observation cadence),
  retaining the negotiated legacy limit of 128 MiB UTF-8 for text-only peers.
  Native v2 peers support 128 MiB UTF-8 text.
- **Images:** advertise a PNG offer without reading/encoding image bytes at copy
  time. Read the source and transfer in 256 KiB chunks only when the receiver
  requests the image. macOS TIFF offers are converted to PNG on demand. The
  encoded image limit is 128 MiB; TIFF conversion is limited to 32 megapixels.
- **Files/folders:** snapshot local paths and metadata, advertise only the item
  count, then request the manifest and file chunks when the destination reads
  file URLs. Download into a private temporary directory before publishing usable
  local URLs. macOS uses one lazy NSPasteboardItem per top-level file/folder;
  Wayland supplies a text/uri-list containing all top-level URLs. No FUSE,
  macFUSE, cloud service or additional privileges are required.

File copies are **copies**, not remote moves. Symlinks and special files are not
transferred. Directories, hidden files, multiple top-level files, empty files and
empty directories are supported within a 4096-entry / 16 GiB per-copy limit.
The session also caps accumulated completed temporary file downloads at 16 GiB
and checks available disk space before downloading. Paths with unsupported
components or case/Unicode-normalization collisions are rejected as a whole.
Permissions/extended attributes/resource forks are not preserved. A macOS
package is transferred as a directory tree of regular files.

### Demand means data access, not a guaranteed user gesture

Clipboard history tools (including Klipper), previews and applications inspecting
file URLs can trigger downloads before the user chooses Paste. Finder/Dolphin
may request data to update their UI. DeskPort cannot distinguish these reads
from a genuine paste. Disabling image/file history may avoid some early reads;
DeskPort does not change the user's history preferences.

Files are fully downloaded before their local URLs are returned, so the target
application may wait on the first paste. Very large files or slow connections
can exceed an application's native clipboard timeout. This is not streaming
remote-file access and does not claim RDP/FUSE-style arbitrary-size pasting.
A request times out after 30 seconds without a response. Copying new local
content, a newer remote offer, or disconnecting cancels an active download;
partial results are never published. A completed download is reused for repeat
paste during the session. Normal helper shutdown removes temporary downloads;
forced process termination can leave private OS temporary directories behind.
No text/image clipboard history is persisted.

### Architecture and ordering

A dedicated `--clipboard-helper` process owns the platform clipboard and its Qt
event loop. The parent relays bounded JSON frames over the existing pinned,
mutually authenticated TLS connection. Only the active approved exclusive session
may start a helper. EOF closes it; host stop, access removal and disconnect revoke
its channel. The SDL/media thread never waits for file or image I/O.

V2 exchanges `offer`, `read`, `data` and heartbeat messages. Text travels in the
offer; binary payloads use bounded Base64 chunks. The host assigns revisions;
stale concurrent offers lose to that revision. Clients coalesce new copies made
before acknowledgement and resend the latest against the acknowledged revision.
Clipboard markers suppress echoes. File reads refer only to an advertised index,
never a peer-supplied local path; open/fstat checks reject replaced files.

KDE Wayland uses `ext-data-control-v1` on a separate Wayland connection so an
unfocused helper can observe and own the selection. Missing data-control support
is a reported failure, not a claim that ordinary unfocused Qt clipboard access
works. X11/offscreen use Qt's MIME provider; macOS uses AppKit item providers.

## Validation and manual acceptance

Automated checks cover protocol ordering, 100 bidirectional Unicode text copies,
no image payload on copy, chunk boundaries, repeat-read caching, directories,
multiple and empty files, source changes, invalid paths/manifests and disconnect.
Native named macOS pasteboards and an isolated virtual KWin compositor verify
format-only queries versus actual reads from a separate process. These checks
never use the user's general clipboard or live remote hosts.

After manually activating both endpoints:

1. Copy text in both directions, including while DeskPort is inactive.
2. Copy a screenshot on each side; paste into a browser/editor that accepts
   images. Repeat paste and compare the actual contents.
3. Copy multiple files plus a small directory in Finder, paste in Dolphin;
   repeat Dolphin to Finder. Verify bytes, names and empty directories.
4. Repeat with Klipper history enabled/disabled and observe whether history
   causes early download. No claim of “only after Ctrl/Cmd+V” is made.
5. Copy new content during a large download; disconnect/reconnect; verify that
   no partial file URLs or old clipboard contents are replayed.
6. Confirm streaming/input remains responsive during transfers. First-paste
   latency and large-file Finder/Dolphin compatibility remain user acceptance.

References: [Microsoft RDP clipboard protocol](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpeclip/),
[RustDesk clipboard design](https://github.com/rustdesk/rustdesk/blob/master/libs/clipboard/README.md),
[AppKit lazy item providers](https://developer.apple.com/documentation/appkit/nspasteboarditemdataprovider),
[Wayland protocol source](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/staging/ext-data-control/ext-data-control-v1.xml).
The vendored XML and generated C/header retain the upstream permissive license;
regenerate using wayland-scanner client-header/private-code.
