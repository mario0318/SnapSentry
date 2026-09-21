# SnapSentry

Watches your Windows **Screenshots** folder, wherever it is, or any other folder
you point it at, and handles new screenshots as they are saved. Copy the image,
rename the file, delete it after a delay, or choose what to do from a
notification.

![The SnapSentry notification](https://raw.githubusercontent.com/mario0318/SnapSentry/9f28c82bb5b901c399d6773ea8a2e09195bc3f1a/assets/notification-rename.png)

## Clipboard modes

* **Image** copies the picture so it stays pasteable even after the file is deleted.
  For a multi-page or animated image only the first frame is copied, so the file is
  kept rather than deleted.
* **File** copies the file for pasting into File Explorer. Deletion is disabled.
* **Path** copies the full path as text. You can pick plain text, a quoted path, a
  file link, or a Markdown image link, which is handy for pasting a screenshot
  straight into notes or a bug report. Deletion is disabled.
* **None** leaves the clipboard unchanged.

## Naming

You can have SnapSentry rename each new screenshot from the window that was in
front when it was taken, together with a timestamp, so a file ends up named for
what it shows instead of Screenshot (1). The file stays in the same folder.

## Identical recent screenshots

When **Remove identical recent screenshots** is enabled, SnapSentry compares a
new image with images handled recently by this running instance, within a short
window of about ten minutes. It works only with the **Image** clipboard mode,
because that is the mode that makes a durable copy before cleanup. An exact
byte-for-byte match is always sent to the Recycle Bin on the automatic path,
even when **Delete the screenshot after copying** is off or ordinary deletion
is set to permanent. If recycling fails, the duplicate stays in place. The older
copy must still contain the same bytes when cleanup runs. If normal deletion is
on, it usually removes each earlier copy before a later identical image arrives,
so there may be no keeper for duplicate cleanup to find. The comparison is
limited to 64 recent entries, resets when settings change or folder watching
restarts, and does not scan or touch files that were already in the folder when
watching began. Images with different embedded metadata are kept even if they
look the same.

## Which folder it watches

By default this is your Windows Screenshots folder, wherever Windows keeps it. In
the settings you can type the full path to any folder instead, and shortcuts like
%USERPROFILE% are filled in for you. Every new image that arrives in the folder you
choose is treated the same way, so pick one where screenshots land rather than one
that collects downloads. The image types treated as new screenshots are .png, .jpg,
.jpeg, .jfif, .bmp, .gif, .webp, .tif, and .tiff; with deletion on, only freshly
created files of these types are ever removed, so it is worth knowing when you pick a
folder.

## The notification

The popup is a real Windows notification, so it matches your light or dark theme.
While the popup is turned on, SnapSentry leaves two things on your machine so the
notification buttons work, a SnapSentry shortcut in your Start Menu programs
folder and one registry entry. Turning the popup off or disabling the mod removes
both again. If the notification can't be shown, SnapSentry falls back
to a standard dialog. If you have turned its notifications off, it takes
that as a cue to stay quiet: it still copies to the clipboard but shows no dialog
and never auto deletes. A multi-page or animated image is kept rather than
deleted, because only its first frame can go on the clipboard, and afterwards a
short notice says so, when the notification is available.

## Privacy

SnapSentry treats any supported image written into the watched folder in about the
last half minute as a new screenshot. Files that were already there, and copies of
older images dragged in or synced from another device, are left alone. A brand new
file saved or downloaded straight into the folder cannot be told apart from a
capture, so avoid pointing the folder at a place where downloads land. By default a
deleted screenshot goes to the Recycle Bin so it can be restored; if you turn that
off, deletion is permanent. Deleting a screenshot does not remove copies already
stored in clipboard history, cloud sync, backups, or other programs. Duplicate
detection keeps only short-lived hashes, not image data.

## Installation

Install it from the Windhawk catalog at
[windhawk.net/mods/snap-sentry](https://windhawk.net/mods/snap-sentry).
`SnapSentry.wh.cpp` here is the same source, so it can also be pasted into
Windhawk's **Create a new mod** editor and compiled. Snipping Tool has to be set
to save screenshots automatically for there to be anything to handle.

## Development roadmap

SnapSentry is staying a small, local screenshot tool. Exact duplicate detection
for screenshots from the current session shipped in 0.21.1. From here the likely
direction is clearer naming, clipboard shortcuts, and better handling of short
screenshot bursts. Folder-wide retention is deliberately not next. If it is added
later, it will need a separate opt-in design that only acts on files SnapSentry
can prove it observed, never a blanket cleanup of an existing screenshot folder.

If you want to work in one of these areas, please open an issue first so changes
can stay compatible with the safety rules above.

## License

GNU General Public License v3.0.
