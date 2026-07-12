# Architecture and safety model

## Process model

SnapSentry is a Windhawk **tool mod** (`@include windhawk.exe`). It uses the
official "mods as tools" boilerplate: the code injected into `windhawk.exe` acts
only as a launcher, spawns a dedicated `windhawk.exe -tool-mod snap-sentry`
process, guards it with a named mutex, and neutralises that process's entry point
so all real work runs from the `WhTool_ModInit/SettingsChanged/Uninit` callbacks.
No code is injected into Snipping Tool, Explorer, or the shell.

Reference: https://github.com/ramensoftware/windhawk/wiki/Mods-as-tools:-Running-mods-in-a-dedicated-process

## Data flow

```
Screenshots folder
  -> overlapped ReadDirectoryChangesW (watcher thread)
  -> validate name + extension, dedup
  -> queue
  -> worker thread: stability wait -> toast (or dialog) action/timeout -> clipboard copy -> conditional deletion
```

Two threads keep the watcher responsive while a toast/dialog or clipboard work is
in progress:

- **Watcher thread** — overlapped `ReadDirectoryChangesW`, waiting on the I/O
  event plus a stop event and a reload event. It only enqueues; it never blocks on
  processing.
- **Worker thread** — a single COM apartment (STA) thread that decodes images,
  shows the toast/dialog, writes the clipboard, and deletes. Serialising here keeps
  clipboard and COM use on one apartment and processes bursts one at a time. It
  also owns the toast-notification COM activator registration for its entire
  lifetime (registered at start, revoked at stop) and waits via
  `CoWaitForMultipleHandles` rather than `WaitForMultipleObjects` throughout, so
  that an incoming toast button click can always be dispatched, not just while a
  toast is actively being waited on.

## Core invariants

1. Act only on files that are **newly added or renamed after the mod starts**.
   Pre-existing files are never touched.
2. Never delete until any required clipboard operation reports success.
3. Never delete a file whose clipboard payload only references it (`file`/`path`
   modes suppress deletion).
4. Keep — and toast/dialog cancellation / shutdown — always wins and cancels
   deletion. An ambiguous toast dismissal (swipe-away, `UserCanceled`) is treated
   the same as dialog Esc: Keep, never delete.
5. Never act on a name that is not a plain child of the watched folder, and never
   delete a reparse point (so a link cannot redirect a delete outside the folder).
6. Stop promptly on unload, including while a toast/dialog is open or a delete is
   delayed.
7. The automatic-action deadline (`delaySeconds`) is always enforced by SnapSentry's
   own timer, never by the OS toast platform's own display-duration heuristics —
   so the configured countdown means the same thing whether the toast or the
   dialog fallback is showing.

## Key implementation choices

- **Toast notification as the primary popup, dialog as fallback.** A real Windows
  toast automatically matches the system light/dark theme and gets Windows' own
  accessibility/DPI handling for free — a hand-rolled window would have to
  reimplement both. Getting there for an unpackaged Win32 app requires three
  pieces of registration that must all agree: a Start Menu shortcut carrying the
  AUMID (`PKEY_AppUserModel_ID`) and activator CLSID
  (`PKEY_AppUserModel_ToastActivatorCLSID`), a `HKCU\...\CLSID\{...}\LocalServer32`
  registry entry for that CLSID, and a live `CoRegisterClassObject` registration
  for the process that should receive button clicks. `EnsureAumidRegistered()` /
  `EnsureClsidRegistered()` do this idempotently on every start; if any of the
  three fails, `g_toastRegistered` stays false and `ShowToast()` returns `false`
  immediately, which routes every prompt through the proven `TaskDialogIndirect`
  fallback instead — the mod never depends on toast registration having worked.
- **Two independent activation paths, one source of truth.** A toast click can
  arrive either as an in-process `IToastNotification::Activated` event (the
  common case, since SnapSentry runs continuously while watching) or as a COM
  call to the registered `ToastActivator::Activate` (the documented path for
  "background" `activationType` actions, and the only path that would work if the
  process had to be relaunched). Both paths write the same `g_toastAction` under
  `g_toastLock` and signal the same `g_toastActionEvent`, so `ShowToast()`'s wait
  loop doesn't care which one fired.
- **WIC decoding.** The clipboard image is built by decoding the file with Windows
  Imaging Component to 32bpp BGRA and publishing self-contained `CF_DIBV5` (with an
  alpha mask) and `CF_DIB` payloads. This replaces the earlier Shell `IDataObject`
  route, which requested `CF_DIBV5` from Explorer's data object for an image file —
  that object offers `CF_HDROP`, not decoded bitmap bits, so it failed for exactly
  the formats the mod targets.
- **Settings snapshot under a lock.** Settings live behind a `CRITICAL_SECTION`.
  Readers take a by-value snapshot, so a reload during processing cannot tear a
  `std::wstring`. A settings change also signals the reload event so the watcher
  re-opens the (possibly changed) folder.
- **Overlapped watch with explicit stop/reload.** Shutdown and folder changes are
  driven by events and `CancelIoEx`, rather than depending on
  `CancelSynchronousIo` racing a blocking call.
- **Deduplication.** Rapid duplicate notifications (add + rename, partial-write
  size changes) are collapsed by an in-flight name set so a screenshot prompts at
  most once.
- **Buffer overflow is fail-safe.** If `ReadDirectoryChangesW` reports a dropped
  buffer (`bytes == 0`), SnapSentry logs and skips rather than rescanning — a
  rescan could not distinguish new files from pre-existing ones and would risk
  invariant 1.

## Residual risks — must be closed before submission

These require the Windhawk toolchain and live Windows builds. Version 0.3.0
(everything except the toast subsystem) has had a local syntax-only check pass;
version 0.4.0's toast code has had **no compiler or runtime verification at all**
and is the highest-risk code in the mod:

- **Toast subsystem is unverified — compile this first.** `EnsureAumidRegistered`,
  `EnsureClsidRegistered`, `ToastActivator`/`ToastActivatorFactory`, and
  `ShowToast` are new WinRT-ABI/WRL/COM-activation code that has not been
  compiled. Specific things likely to need adjustment on first build: whether
  `Microsoft::WRL::Callback<T>` accepts a plain lambda directly in this SDK/Clang
  combination, exact header/library names for the WinRT ABI headers under
  Windhawk's toolchain, and whether `-lruntimeobject`/`-ladvapi32` are the correct
  (and only) additional libraries needed.
- **Toast registration can still fail at runtime even if it compiles.** Group
  Policy, notification settings, or a stale/conflicting shortcut could all prevent
  registration from completing. `g_toastRegistered` gates this: if false,
  `ShowToast()` always returns `false` and `ChooseAction()` falls through to the
  dialog, so this should degrade gracefully rather than silently doing nothing —
  but that fallback path itself needs to be exercised, not just assumed.
- **Cold-launch COM activation is not handled.** If SnapSentry's tool-mod process
  isn't running when a toast button is clicked (e.g. the user disabled the mod
  between the toast appearing and clicking it), Windows would try to launch a new
  process via the registry `LocalServer32` entry; nothing in this mod is written
  to detect "I was launched for COM activation" and complete the handshake in that
  scenario. In practice this should be rare, since the watcher process is meant to
  run continuously, but it's an unhandled edge case worth documenting rather than
  silently assuming away.
- **Not yet compiled in Windhawk (0.3.0 baseline).** The tool-mod boilerplate, WIC
  linkage (`initguid.h` + `-lwindowscodecs`), C++20 designated initializers, and
  `TaskDialogIndirect` availability must be confirmed by an actual build with all
  warnings shown.
- **`TaskDialogIndirect` requires ComCtl32 v6.** If the `windhawk.exe` process does
  not have a v6 activation context, dialog creation fails; the code treats that as
  Keep (safe, but the fallback popup would silently not appear either). Verify at
  runtime.
- **WIC coverage.** Confirm real Snipping Tool PNG/JPEG plus BMP/GIF/WEBP all decode
  and paste into Office, a browser, Paint, and a chat app, including after the file
  is deleted.
- **Toast/dialog placement / DPI / focus.** Multi-monitor, mixed-DPI, and
  fullscreen focus-stealing behaviour is unverified for both the toast and the
  dialog fallback.
- **Localization.** UI strings and setting descriptions are English-only.

## Future candidates

- A tray history of filenames and outcomes only.
- Configurable filename rules and per-folder policies.
- Optional clipboard clearing after a second privacy timeout, clearly warning that
  it cannot recall content already captured by clipboard history or other apps.
- Suppressing the redundant Snipping Tool toast (currently both may appear).
