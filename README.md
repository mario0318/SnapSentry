# SnapSentry

SnapSentry watches the Windows Screenshots folder and handles new screenshots as
they are saved. It can copy the image, delete the saved file after a delay, or let
you choose from a notification.

## Actions

- **Delete now** removes the file without changing the clipboard.
- **Copy image and delete** copies the image, then removes the file.
- **Keep** leaves the screenshot alone.
- If nothing is selected, the configured automatic action runs after the delay.

Deletion is off by default, since it is the irreversible part; turn on **Delete the
saved screenshot** to opt in. When it is on, deletion sends the file to the Recycle
Bin so an accidental delete is recoverable, unless you turn that off for a permanent
delete.

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
its notification buttons work. If the notification can't be shown, SnapSentry falls
back to a standard dialog; but if you have turned its notifications off, it stays
quiet instead, still copying to the clipboard but showing no dialog and never auto
deleting. Turning the popup off, or disabling the mod, removes that registration
again, so it leaves nothing behind.

## Privacy

SnapSentry treats any supported image written into the watched folder within the
last few seconds as a new screenshot. Files that were already there, and copies of
older images dragged in by hand or synced from another device, are left alone. A
brand new file saved or downloaded straight into the folder cannot be told apart
from a capture, so avoid pointing the folder override at a place where downloads
land. It cannot remove copies
already retained by clipboard history, cloud sync, backups, or other applications.
Deleting a file is not secure erasure, especially on an SSD. When the Recycle Bin
option is on, a deleted screenshot stays recoverable there until the bin is emptied.

Avoid a cloud-synced screenshot folder when quick deletion matters. A sync client
may upload or retain the image before the local file is removed.

## Installation

Paste `SnapSentry.wh.cpp` into Windhawk's **Create a new mod** editor and compile
it. Supported formats are PNG, JPEG, JFIF, BMP, GIF, WebP, and TIFF. When copying the
picture, multi-page or animated images (a multi-page TIFF, an animated GIF or WebP)
are kept rather than deleted, since only their first page or frame can be put on the
clipboard.

## License

GNU General Public License v3.0.
