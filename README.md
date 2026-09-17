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

## Identical recent screenshots

The optional **Remove identical recent screenshots** setting compares each new
image with recent screenshots handled by this running SnapSentry instance. It
uses an exact byte-for-byte comparison and recycles only the incoming duplicate
after its copy succeeds. It does not scan the folder or touch files that were
already present when watching began.

## Audit trail

SnapSentry writes one final-result audit line for each handled screenshot. It
records whether the file was copied, kept, recycled, recycled as an exact
duplicate, skipped, or permanently deleted. Failed cleanup is recorded as kept.
Paths stay out of these lines unless verbose logging is enabled. Audit results
are log-only, so popup-off keeps SnapSentry's no-footprint behavior.

## Development roadmap

SnapSentry is staying a small, local screenshot tool. Future work will focus on
clearer naming, clipboard shortcuts, and better handling of short screenshot
bursts. Folder-wide retention is deliberately not the next feature. If it is
added later, it will need a separate opt-in design that only acts on files
SnapSentry can prove it observed, never a blanket cleanup of an existing
screenshot folder.

If you want to work in one of these areas, please open an issue first so changes
can stay compatible with the safety rules above.

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
picture, a multi-frame image such as a multi-page TIFF or animated GIF is kept rather
than deleted, since only its first frame can be put on the clipboard.

## License

GNU General Public License v3.0.
