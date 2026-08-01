# SnapSentry

SnapSentry watches the Windows Screenshots folder and handles new screenshots as
they are saved. It can copy the image, delete the saved file after a delay, or let
you choose from a notification.

## Actions

- **Delete now** removes the file without changing the clipboard.
- **Copy image and delete** copies the image, then removes the file.
- **Keep** leaves the screenshot alone.
- If nothing is selected, the configured automatic action runs after the delay.

## Clipboard modes

- **Image** copies a bitmap that remains pasteable after the file is deleted.
- **File** copies the file for pasting into File Explorer. Deletion is disabled.
- **Path** copies the full path as text. Deletion is disabled.
- **None** leaves the clipboard unchanged.

## Setup

Snipping Tool must be set to save screenshots automatically. The default folder
is `Pictures\Screenshots`. If Snipping Tool saves to any other directory, set **Folder
override** to that location.

The first run registers SnapSentry with Windows so its notification buttons work.
If notifications are unavailable, SnapSentry uses a standard dialog.

## Privacy

SnapSentry only handles files created after it starts. It cannot remove copies
already retained by clipboard history, cloud sync, backups, or other applications.
Deleting a file is not secure erasure, especially on an SSD.

Avoid a cloud-synced screenshot folder when quick deletion matters. A sync client
may upload or retain the image before the local file is removed.

## Installation

Paste `SnapSentry.wh.cpp` into Windhawk's **Create a new mod** editor and compile
it. Supported formats are PNG, JPEG, BMP, GIF, and WebP.

## License

GNU General Public License v3.0.
