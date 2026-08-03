# SnapSentry

SnapSentry watches the Windows Screenshots folder and handles new screenshots as
they are saved. It can copy the image, delete the saved file after a delay, or let
you choose from a notification.

## Actions

- **Delete now** removes the file without changing the clipboard.
- **Copy image and delete** copies the image, then removes the file.
- **Keep** leaves the screenshot alone.
- If nothing is selected, the configured automatic action runs after the delay.

Deletion can send the file to the Recycle Bin instead of removing it for good,
so an accidental delete is recoverable. This is optional and can be turned off
for a permanent delete.

## Clipboard modes

- **Image** copies a bitmap that remains pasteable after the file is deleted.
- **File** copies the file for pasting into File Explorer. Deletion is disabled.
- **Path** copies the location as text. Deletion is disabled. You can choose how
  the path is written: plain, quoted, as a clickable file link, or as a Markdown
  image reference.
- **None** leaves the clipboard unchanged.

## Naming

SnapSentry can rename each new screenshot using the title of the window that was
in front when it was taken, so files read like `2026-08-01 17-16-52 Preview.png`
instead of `Screenshot (12).png`. This is optional and off by default.

## Setup

Snipping Tool must be set to save screenshots automatically. The default folder
is `Pictures\Screenshots`. If Snipping Tool saves to any other directory, set **Folder
override** to that location.

While the action popup is turned on, SnapSentry registers itself with Windows so
its notification buttons work. If notifications are unavailable, SnapSentry uses a
standard dialog. Turning the popup off, or disabling the mod, removes that
registration again, so it leaves nothing behind.

## Privacy

SnapSentry only handles files created after it starts. It cannot remove copies
already retained by clipboard history, cloud sync, backups, or other applications.
Deleting a file is not secure erasure, especially on an SSD. When the Recycle Bin
option is on, a deleted screenshot stays recoverable there until the bin is emptied.

Avoid a cloud-synced screenshot folder when quick deletion matters. A sync client
may upload or retain the image before the local file is removed.

## Installation

Paste `SnapSentry.wh.cpp` into Windhawk's **Create a new mod** editor and compile
it. Supported formats are PNG, JPEG, BMP, GIF, and WebP.

## License

GNU General Public License v3.0.
