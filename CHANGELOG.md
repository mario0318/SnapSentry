# Changelog

Notable changes to SnapSentry, by published version. Dates are catalog release dates.

## 0.19.7 - unreleased
- Added opt-in removal of identical recent screenshots in Image clipboard mode. A byte-for-byte duplicate seen in the recent ten-minute window is recycled only after its own copy succeeds. The comparison resets when settings change or folder watching restarts, and never touches files that were already in the folder.
- Added an audit line in the log for each automatic result: copied, kept, recycled, skipped, or deleted.
- When Windows cannot deliver the notification, SnapSentry now copies only instead of waiting on a popup that never appears.
- Fixed a watched-folder path with a stray leading or trailing space being ignored, which left the folder unwatched. Such paths are now trimmed.

## 0.18.8 - 2026-08-24
- When a multi-page or animated image is kept instead of deleted, a notice now says so; before, that only appeared in the log.
- More reliable folder watching on network or sync-backed locations that don't deliver change notifications.
- Copies large images using less memory.
- Fixed a watched folder whose path ends in a separator. The composed path was not in a form the shell accepts, so deleting to the Recycle Bin failed and the file was kept, and the File and Path clipboard modes copied a malformed path. The folder is now cleaned up once when it is read.

## 0.17.3 - 2026-08-20
- Recognizes `.tif`, `.tiff`, and `.jfif` images in a watched folder, alongside the PNG, JPEG, BMP, GIF, and WebP it already handled.

## 0.17.2 - 2026-08-17
- The watched folder is clearly your own to pick, throughout the mod description, readme, and the folder setting.
- Tighter, shorter settings descriptions.
- New readme screenshot showing a renamed capture in the notification.
- Fixed a stray colon in a setting description that broke the settings YAML.
- Documented what happens when SnapSentry's notifications are turned off in Windows.
- The Recycle Bin note now covers automatic deletions, not only the popup buttons.
- Names the Start Menu shortcut and registry entry the popup leaves behind, rather than describing them vaguely.

## 0.16.0 - 2026-08-10
- Initial release.
