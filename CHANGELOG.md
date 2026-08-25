# Changelog

Notable changes to SnapSentry, by published version. Dates are catalog release dates.

## 0.18.4 - 2026-08-24
- When a multi-page or animated image is kept instead of deleted, a notice now says so; before, that only appeared in the log.
- More reliable folder watching on network or sync-backed locations that don't deliver change notifications.
- Copies large images using less memory.
- Deleting to the Recycle Bin works again for a watched folder whose path ends in a separator. The composed path was not in the form the shell accepts, so the delete failed and the file was kept.

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
