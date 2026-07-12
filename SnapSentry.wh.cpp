// ==WindhawkMod==
// @id              snap-sentry
// @name            SnapSentry
// @description     Copy saved screenshots, delete them automatically, or choose what to do from a notification.
// @version         0.4.0
// @author          Mario0318
// @github          https://github.com/Mario0318
// @include         windhawk.exe
// @compilerOptions -lole32 -lshell32 -lcomctl32 -lwindowscodecs -lruntimeobject -ladvapi32 -luuid
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# SnapSentry

Watches **Pictures\\Screenshots** and handles new screenshots as they are saved.
Copy the image, delete the file after a delay, or choose what to do from a
notification.

## Clipboard modes

* **Image** — decodes the picture with Windows Imaging Component (WIC) and places
  a self-contained bitmap on the clipboard (`CF_DIBV5` + `CF_DIB`). This survives
  deleting the source file, so it is the mode to use with automatic deletion.
* **File** — places the file itself on the clipboard (`CF_HDROP`) for pasting into
  File Explorer. Automatic deletion is suppressed because the payload only
  references the file.
* **Path** — places the full path on the clipboard as Unicode text. Automatic
  deletion is suppressed for the same reason.
* **None** — leaves the clipboard untouched (useful for a delete-only workflow).

## Privacy notes

Deleting a screenshot does not remove copies already stored in clipboard history,
cloud sync, backups, or other programs. Files that were already in the folder when
SnapSentry started are left alone.

## About the popup

The action popup is a real Windows toast notification (Action Center style),
matching your light/dark theme automatically. Showing it requires a small,
one-time registration: a Start Menu shortcut named `SnapSentry.lnk` (so Windows
has an identity to attach notifications to) and a `HKCU` registry entry under
`Software\Classes\CLSID` (so button clicks route back to SnapSentry). Both are
created automatically the first time the mod runs and are safe to delete by hand
if you uninstall the mod. If toast registration fails for any reason, SnapSentry
automatically falls back to a native dialog box instead, so the mod keeps working
either way.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- enabled: true
  $name: Enable processing
- delaySeconds: 5
  $name: Seconds before deletion
  $description: 0 deletes as soon as clipboard copying finishes. Also used as the action popup countdown.
- deleteFile: true
  $name: Delete the saved screenshot
  $description: Only applies to Image and None clipboard modes. File and Path modes reference the file, so deletion is always suppressed for them.
- showActionPopup: true
  $name: Show companion action popup
  $description: A toast notification offering Delete now, Copy image and delete, or Keep (falls back to a dialog box if toast registration isn't available). The configured automatic action runs when the countdown expires.
- clipboardMode: image
  $name: Clipboard content
  $options:
  - image: Image (recommended with deletion)
  - file: File object (deletion suppressed)
  - path: Full path text (deletion suppressed)
  - none: Don't change clipboard
- folder: ""
  $name: Folder override
  $description: Leave empty to watch Pictures\\Screenshots.
- logDetails: false
  $name: Verbose logging (may include file paths)
  $description: When off, log lines omit file paths. Turn on only while debugging.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <initguid.h>  // Defines the GUIDs pulled in by the headers below in-TU.
#include <knownfolders.h>
#include <shlobj.h>
#include <propkey.h>   // PKEY_AppUserModel_ID, PKEY_AppUserModel_ToastActivatorCLSID
#include <commctrl.h>
#include <wincodec.h>
#include <roapi.h>
#include <winstring.h>
#include <windows.data.xml.dom.h>
#include <windows.ui.notifications.h>

// The MinGW SDK bundled with Windhawk doesn't ship
// notificationactivationcallback.h, so declare its stable Win32 ABI locally.
struct NOTIFICATION_USER_INPUT_DATA {
    LPCWSTR Key;
    LPCWSTR Value;
};

MIDL_INTERFACE("53E31837-6600-4A81-9395-75CFFE746F94")
INotificationActivationCallback : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Activate(
        LPCWSTR appUserModelId,
        LPCWSTR invokedArgs,
        const NOTIFICATION_USER_INPUT_DATA* data,
        ULONG dataCount) = 0;
};

DEFINE_GUID(IID_INotificationActivationCallback,
            0x53e31837, 0x6600, 0x4a81, 0x93, 0x95, 0x75, 0xcf, 0xfe, 0x74,
            0x6f, 0x94);

#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <set>
#include <string>

// ============================================================================
// Settings and shared state
// ============================================================================

struct Settings {
    bool enabled;
    int delaySeconds;
    bool deleteFile;
    bool popup;
    std::wstring mode;
    std::wstring folder;
    bool logDetails;
};

static CRITICAL_SECTION g_lock;       // Guards g_settings, g_queue, g_inflight.
static Settings g_settings;
static std::deque<std::wstring> g_queue;   // Full paths waiting to be processed.
static std::set<std::wstring> g_inflight;  // Names queued or in progress (dedup).

static HANDLE g_stopEvent;   // Manual-reset: set once at shutdown.
static HANDLE g_reloadEvent; // Auto-reset: settings changed, re-open the folder.
static HANDLE g_workEvent;   // Auto-reset: queue has work.
static HANDLE g_watchThread;
static HANDLE g_workerThread;
static std::atomic<HWND> g_dialog{nullptr};  // Open action dialog, for shutdown.

enum {
    ACTION_AUTO = 100,
    ACTION_DELETE = 101,
    ACTION_COPY_DELETE = 102,
    ACTION_KEEP = 103,
};

static CRITICAL_SECTION g_toastLock;   // Guards g_toastAction.
static int g_toastAction = ACTION_AUTO;
static HANDLE g_toastActionEvent;      // Auto-reset: a toast was activated/dismissed.
static std::atomic<bool> g_toastRegistered{false};  // AUMID+CLSID registration ok.

// ============================================================================
// Settings
// ============================================================================

static std::wstring DefaultScreenshotsFolder() {
    std::wstring result;
    PWSTR pictures = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &pictures))) {
        result.assign(pictures);
        result += L"\\Screenshots";
        CoTaskMemFree(pictures);
    }
    return result;
}

static void LoadSettings() {
    Settings s{};
    s.enabled = Wh_GetIntSetting(L"enabled") != 0;
    s.delaySeconds = Wh_GetIntSetting(L"delaySeconds");
    if (s.delaySeconds < 0) {
        s.delaySeconds = 0;
    }
    s.deleteFile = Wh_GetIntSetting(L"deleteFile") != 0;
    s.popup = Wh_GetIntSetting(L"showActionPopup") != 0;
    s.logDetails = Wh_GetIntSetting(L"logDetails") != 0;

    PCWSTR mode = Wh_GetStringSetting(L"clipboardMode");
    s.mode = mode;
    Wh_FreeStringSetting(mode);

    PCWSTR folder = Wh_GetStringSetting(L"folder");
    s.folder = folder;
    Wh_FreeStringSetting(folder);
    if (s.folder.empty()) {
        s.folder = DefaultScreenshotsFolder();
    }

    EnterCriticalSection(&g_lock);
    g_settings = std::move(s);
    LeaveCriticalSection(&g_lock);
}

static Settings SnapshotSettings() {
    EnterCriticalSection(&g_lock);
    Settings s = g_settings;
    LeaveCriticalSection(&g_lock);
    return s;
}

// True if the stop event became signalled within the wait.
static bool WaitStop(DWORD ms) {
    return WaitForSingleObject(g_stopEvent, ms) == WAIT_OBJECT_0;
}

// ============================================================================
// File name helpers
// ============================================================================

static bool IsSupportedImage(const std::wstring& name) {
    auto dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) {
        return false;
    }
    std::wstring ext = name.substr(dot);
    CharLowerBuffW(ext.data(), (DWORD)ext.size());
    return ext == L".png" || ext == L".jpg" || ext == L".jpeg" ||
           ext == L".bmp" || ext == L".gif" || ext == L".webp";
}

// A change-notification name must be a plain child file name. Reject anything
// with a path separator or a relative component so we can never act outside the
// watched folder.
static bool IsSafeChildName(const std::wstring& name) {
    if (name.empty() || name == L"." || name == L"..") {
        return false;
    }
    return name.find_first_of(L"\\/") == std::wstring::npos;
}

// ============================================================================
// Clipboard payloads
// ============================================================================

static bool ClipboardText(const std::wstring& text) {
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!h) {
        return false;
    }
    void* p = GlobalLock(h);
    memcpy(p, text.c_str(), bytes);
    GlobalUnlock(h);

    if (!OpenClipboard(nullptr)) {
        GlobalFree(h);
        return false;
    }
    EmptyClipboard();
    bool ok = SetClipboardData(CF_UNICODETEXT, h) != nullptr;
    if (!ok) {
        GlobalFree(h);  // Ownership stays with us on failure.
    }
    CloseClipboard();
    return ok;
}

static bool ClipboardFile(const std::wstring& path) {
    size_t bytes = sizeof(DROPFILES) + (path.size() + 2) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    if (!h) {
        return false;
    }
    auto* drop = (DROPFILES*)GlobalLock(h);
    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;
    memcpy((BYTE*)drop + sizeof(DROPFILES), path.c_str(),
           (path.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(h);

    if (!OpenClipboard(nullptr)) {
        GlobalFree(h);
        return false;
    }
    EmptyClipboard();
    bool ok = SetClipboardData(CF_HDROP, h) != nullptr;
    if (!ok) {
        GlobalFree(h);
    }
    CloseClipboard();
    return ok;
}

// Fills a DIB pixel region from a top-down 32bpp BGRA source, writing the rows
// bottom-up as DIB clipboard formats expect.
static void WriteBottomUp(BYTE* dest, const BYTE* topDown, UINT width,
                          UINT height) {
    size_t stride = (size_t)width * 4;
    for (UINT y = 0; y < height; y++) {
        memcpy(dest + (size_t)y * stride,
               topDown + (size_t)(height - 1 - y) * stride, stride);
    }
}

// Decodes any WIC-supported image into self-contained CF_DIBV5 + CF_DIB payloads.
// This is what makes the copied image survive deletion of the source file.
static bool ClipboardImage(const std::wstring& path) {
    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
        return false;
    }

    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    BYTE* topDown = nullptr;
    HGLOBAL hV5 = nullptr;
    HGLOBAL hDib = nullptr;
    bool ok = false;

    do {
        if (FAILED(factory->CreateDecoderFromFilename(
                path.c_str(), nullptr, GENERIC_READ,
                WICDecodeMetadataCacheOnDemand, &decoder))) {
            break;
        }
        if (FAILED(decoder->GetFrame(0, &frame)) ||
            FAILED(factory->CreateFormatConverter(&converter)) ||
            FAILED(converter->Initialize(
                frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
            break;
        }

        UINT width = 0, height = 0;
        if (FAILED(converter->GetSize(&width, &height)) || width == 0 ||
            height == 0 || width > 30000 || height > 30000) {
            break;
        }

        size_t stride = (size_t)width * 4;
        size_t pixels = stride * height;
        topDown = (BYTE*)malloc(pixels);
        if (!topDown ||
            FAILED(converter->CopyPixels(nullptr, (UINT)stride, (UINT)pixels,
                                         topDown))) {
            break;
        }

        hV5 = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPV5HEADER) + pixels);
        hDib = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + pixels);
        if (!hV5 || !hDib) {
            break;
        }

        BYTE* v5 = (BYTE*)GlobalLock(hV5);
        auto* bv5 = (BITMAPV5HEADER*)v5;
        ZeroMemory(bv5, sizeof(*bv5));
        bv5->bV5Size = sizeof(BITMAPV5HEADER);
        bv5->bV5Width = (LONG)width;
        bv5->bV5Height = (LONG)height;
        bv5->bV5Planes = 1;
        bv5->bV5BitCount = 32;
        bv5->bV5Compression = BI_BITFIELDS;
        bv5->bV5SizeImage = (DWORD)pixels;
        bv5->bV5RedMask = 0x00FF0000;
        bv5->bV5GreenMask = 0x0000FF00;
        bv5->bV5BlueMask = 0x000000FF;
        bv5->bV5AlphaMask = 0xFF000000;
        bv5->bV5CSType = LCS_WINDOWS_COLOR_SPACE;
        WriteBottomUp(v5 + sizeof(BITMAPV5HEADER), topDown, width, height);
        GlobalUnlock(hV5);

        BYTE* dib = (BYTE*)GlobalLock(hDib);
        auto* bih = (BITMAPINFOHEADER*)dib;
        ZeroMemory(bih, sizeof(*bih));
        bih->biSize = sizeof(BITMAPINFOHEADER);
        bih->biWidth = (LONG)width;
        bih->biHeight = (LONG)height;
        bih->biPlanes = 1;
        bih->biBitCount = 32;
        bih->biCompression = BI_RGB;
        bih->biSizeImage = (DWORD)pixels;
        WriteBottomUp(dib + sizeof(BITMAPINFOHEADER), topDown, width, height);
        GlobalUnlock(hDib);

        if (!OpenClipboard(nullptr)) {
            break;
        }
        EmptyClipboard();
        bool setV5 = SetClipboardData(CF_DIBV5, hV5) != nullptr;
        if (setV5) {
            hV5 = nullptr;  // Clipboard owns it now.
        }
        bool setDib = SetClipboardData(CF_DIB, hDib) != nullptr;
        if (setDib) {
            hDib = nullptr;
        }
        CloseClipboard();
        ok = setV5 || setDib;
    } while (false);

    if (hV5) {
        GlobalFree(hV5);
    }
    if (hDib) {
        GlobalFree(hDib);
    }
    free(topDown);
    if (converter) {
        converter->Release();
    }
    if (frame) {
        frame->Release();
    }
    if (decoder) {
        decoder->Release();
    }
    factory->Release();
    return ok;
}

// ============================================================================
// Toast notification identity (AUMID + COM activator CLSID)
//
// Windows requires three things to agree before an unpackaged Win32 app gets
// interactive toast notifications: a Start Menu shortcut carrying the AUMID
// (PKEY_AppUserModel_ID) and the activator CLSID (PKEY_AppUserModel_ToastActivatorCLSID),
// a registry LocalServer32 entry for that CLSID, and a live COM registration
// (CoRegisterClassObject) for the process that wants to handle button clicks.
// ============================================================================

static constexpr wchar_t kAppUserModelId[] = L"Mario0318.SnapSentry";
static constexpr wchar_t kShortcutName[] = L"SnapSentry.lnk";

// Generated once for this mod; do not reuse elsewhere and do not regenerate --
// Windows ties the shortcut and registry state below to this exact value.
// {304BD1DF-F3CE-414D-A33B-3BA70D2CE081}
DEFINE_GUID(CLSID_SnapSentryToastActivator, 0x304bd1df, 0xf3ce, 0x414d, 0xa3,
            0x3b, 0x3b, 0xa7, 0x0d, 0x2c, 0xe0, 0x81);

static bool SetStringProp(IPropertyStore* store, REFPROPERTYKEY key,
                          const wchar_t* value) {
    PROPVARIANT pv{};
    pv.vt = VT_LPWSTR;
    pv.pwszVal = (PWSTR)CoTaskMemAlloc((wcslen(value) + 1) * sizeof(wchar_t));
    if (!pv.pwszVal) {
        return false;
    }
    wcscpy_s(pv.pwszVal, wcslen(value) + 1, value);
    bool ok = SUCCEEDED(store->SetValue(key, pv));
    PropVariantClear(&pv);  // Also frees pwszVal (CoTaskMemAlloc-compatible).
    return ok;
}

static bool SetClsidProp(IPropertyStore* store, REFPROPERTYKEY key,
                         REFCLSID clsid) {
    PROPVARIANT pv{};
    pv.vt = VT_CLSID;
    pv.puuid = (CLSID*)CoTaskMemAlloc(sizeof(CLSID));
    if (!pv.puuid) {
        return false;
    }
    *pv.puuid = clsid;
    bool ok = SUCCEEDED(store->SetValue(key, pv));
    PropVariantClear(&pv);
    return ok;
}

static bool ShortcutHasCorrectProperties(IShellLinkW* link) {
    IPropertyStore* store = nullptr;
    if (FAILED(link->QueryInterface(IID_PPV_ARGS(&store)))) {
        return false;
    }
    PROPVARIANT aumid{};
    PROPVARIANT clsid{};
    bool ok = SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID, &aumid)) &&
              aumid.vt == VT_LPWSTR && aumid.pwszVal &&
              wcscmp(aumid.pwszVal, kAppUserModelId) == 0 &&
              SUCCEEDED(store->GetValue(PKEY_AppUserModel_ToastActivatorCLSID,
                                        &clsid)) &&
              clsid.vt == VT_CLSID && clsid.puuid &&
              IsEqualCLSID(*clsid.puuid, CLSID_SnapSentryToastActivator);
    PropVariantClear(&aumid);
    PropVariantClear(&clsid);
    store->Release();
    return ok;
}

// Creates (or repairs) %APPDATA%\Microsoft\Windows\Start Menu\Programs\SnapSentry.lnk
// pointing at the current windhawk.exe, tagged with our AUMID and activator CLSID.
// Idempotent: does nothing if a correctly-tagged shortcut already exists.
static bool EnsureAumidRegistered() {
    PWSTR programs = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &programs))) {
        return false;
    }
    std::wstring path = std::wstring(programs) + L"\\" + kShortcutName;
    CoTaskMemFree(programs);

    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&link)))) {
        return false;
    }

    bool ok = false;
    IPersistFile* file = nullptr;
    do {
        if (FAILED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
            break;
        }
        if (SUCCEEDED(file->Load(path.c_str(), STGM_READ)) &&
            ShortcutHasCorrectProperties(link)) {
            ok = true;
            break;  // Already correctly registered.
        }

        WCHAR exePath[MAX_PATH];
        if (!GetModuleFileNameW(nullptr, exePath, ARRAYSIZE(exePath))) {
            break;
        }
        link->SetPath(exePath);
        link->SetArguments(L"");
        link->SetDescription(L"SnapSentry (Windhawk)");

        IPropertyStore* store = nullptr;
        if (FAILED(link->QueryInterface(IID_PPV_ARGS(&store)))) {
            break;
        }
        bool propsOk =
            SetStringProp(store, PKEY_AppUserModel_ID, kAppUserModelId) &&
            SetClsidProp(store, PKEY_AppUserModel_ToastActivatorCLSID,
                        CLSID_SnapSentryToastActivator) &&
            SUCCEEDED(store->Commit());
        store->Release();
        if (!propsOk) {
            break;
        }

        ok = SUCCEEDED(file->Save(path.c_str(), TRUE));
    } while (false);

    if (file) {
        file->Release();
    }
    link->Release();
    return ok;
}

// Writes HKCU\Software\Classes\CLSID\{...}\LocalServer32 so Windows treats our
// CLSID as a valid, launchable COM server for the AUMID above. While SnapSentry
// is already running, CoRegisterClassObject (below) intercepts activation before
// Windows would ever need to launch a process via this key; a cold launch (the
// mod's process not already running when a toast button is clicked) is not
// specially handled and is a known limitation -- see ARCHITECTURE.md.
static bool EnsureClsidRegistered() {
    WCHAR exePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePath, ARRAYSIZE(exePath))) {
        return false;
    }
    WCHAR command[MAX_PATH + 64];
    swprintf_s(command, L"\"%s\" -tool-mod \"%s\"", exePath, WH_MOD_ID);

    WCHAR clsidStr[64];
    StringFromGUID2(CLSID_SnapSentryToastActivator, clsidStr, ARRAYSIZE(clsidStr));
    std::wstring keyPath = std::wstring(L"Software\\Classes\\CLSID\\") + clsidStr +
                           L"\\LocalServer32";

    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath.c_str(), 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    LSTATUS status =
        RegSetValueExW(key, nullptr, 0, REG_SZ, (const BYTE*)command,
                       (DWORD)((wcslen(command) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

// ============================================================================
// COM activator: invoked when a toast button is clicked (routed via the CLSID
// registered above). Parses which button and hands the result to whichever
// ShowToast() call is currently waiting.
// ============================================================================

class ToastActivator : public INotificationActivationCallback {
public:
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }  // Static lifetime.
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** obj) override {
        if (riid == IID_IUnknown ||
            riid == IID_INotificationActivationCallback) {
            *obj = static_cast<INotificationActivationCallback*>(this);
            return S_OK;
        }
        *obj = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE
    Activate(LPCWSTR, LPCWSTR invokedArgs, const NOTIFICATION_USER_INPUT_DATA*,
            ULONG) override {
        int action = ACTION_AUTO;
        if (invokedArgs) {
            if (wcscmp(invokedArgs, L"delete") == 0) {
                action = ACTION_DELETE;
            } else if (wcscmp(invokedArgs, L"copydelete") == 0) {
                action = ACTION_COPY_DELETE;
            } else if (wcscmp(invokedArgs, L"keep") == 0) {
                action = ACTION_KEEP;
            }
        }
        EnterCriticalSection(&g_toastLock);
        g_toastAction = action;
        LeaveCriticalSection(&g_toastLock);
        SetEvent(g_toastActionEvent);
        return S_OK;
    }
};

static ToastActivator g_toastActivator;

class ToastActivatorFactory : public IClassFactory {
public:
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** obj) override {
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *obj = static_cast<IClassFactory*>(this);
            return S_OK;
        }
        *obj = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid,
                                             void** obj) override {
        if (outer) {
            return CLASS_E_NOAGGREGATION;
        }
        return g_toastActivator.QueryInterface(riid, obj);
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
};

static ToastActivatorFactory g_toastActivatorFactory;
static DWORD g_toastActivatorCookie;

// ============================================================================
// Toast notification display and wait
// ============================================================================

static std::wstring XmlEscape(const std::wstring& in) {
    std::wstring out;
    out.reserve(in.size());
    for (wchar_t c : in) {
        switch (c) {
            case L'&': out += L"&amp;"; break;
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            case L'"': out += L"&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

// Builds and shows the toast, then waits up to s.delaySeconds for a button
// click (via ToastActivator::Activate, dispatched by CoWaitForMultipleHandles)
// or an explicit dismissal. Returns false -- meaning "use the dialog instead"
// -- if registration hasn't succeeded or any WinRT step fails, so the mod stays
// usable even where toast notifications don't work.
static bool ShowToast(const std::wstring& path, const Settings& s, int& action) {
    using namespace ABI::Windows::UI::Notifications;
    using namespace ABI::Windows::Data::Xml::Dom;
    using namespace ABI::Windows::Foundation;
    using Microsoft::WRL::ComPtr;
    using Microsoft::WRL::Wrappers::HStringReference;

    if (!g_toastRegistered.load()) {
        return false;
    }

    std::wstring name = path.substr(path.find_last_of(L"\\/") + 1);
    std::wstring xml =
        L"<toast><visual><binding template=\"ToastGeneric\">"
        L"<text>Screenshot saved</text><text>" +
        XmlEscape(name) +
        L"</text><text placement=\"attribution\">SnapSentry</text>"
        L"</binding></visual><actions>"
        L"<action content=\"Delete now\" arguments=\"delete\" activationType=\"background\"/>"
        L"<action content=\"Copy image &amp; delete\" arguments=\"copydelete\" activationType=\"background\"/>"
        L"<action content=\"Keep\" arguments=\"keep\" activationType=\"background\"/>"
        L"<action content=\"Use automatic action\" arguments=\"auto\" activationType=\"background\"/>"
        L"</actions></toast>";

    ComPtr<IToastNotificationManagerStatics> toastStatics;
    if (FAILED(RoGetActivationFactory(
            HStringReference(
                RuntimeClass_Windows_UI_Notifications_ToastNotificationManager)
                .Get(),
            IID_PPV_ARGS(&toastStatics)))) {
        return false;
    }
    ComPtr<IToastNotifier> notifier;
    if (FAILED(toastStatics->CreateToastNotifierWithId(
            HStringReference(kAppUserModelId).Get(), &notifier))) {
        return false;
    }

    ComPtr<IInspectable> docInspectable;
    if (FAILED(RoActivateInstance(
            HStringReference(RuntimeClass_Windows_Data_Xml_Dom_XmlDocument).Get(),
            &docInspectable))) {
        return false;
    }
    ComPtr<IXmlDocument> doc;
    ComPtr<IXmlDocumentIO> docIO;
    if (FAILED(docInspectable.As(&doc)) || FAILED(docInspectable.As(&docIO)) ||
        FAILED(docIO->LoadXml(HStringReference(xml.c_str()).Get()))) {
        return false;
    }

    ComPtr<IToastNotificationFactory> toastFactory;
    if (FAILED(RoGetActivationFactory(
            HStringReference(RuntimeClass_Windows_UI_Notifications_ToastNotification)
                .Get(),
            IID_PPV_ARGS(&toastFactory)))) {
        return false;
    }
    ComPtr<IToastNotification> toast;
    if (FAILED(toastFactory->CreateToastNotification(doc.Get(), &toast))) {
        return false;
    }

    ResetEvent(g_toastActionEvent);
    if (FAILED(notifier->Show(toast.Get()))) {
        return false;
    }

    // Our own timer is authoritative for the automatic action, independent of
    // the OS's on-screen banner duration -- the toast stays clickable from
    // Action Center even after it visually collapses.
    DWORD timeoutMs =
        s.delaySeconds > 0 ? (DWORD)s.delaySeconds * 1000 : INFINITE;
    HANDLE waits[] = {g_stopEvent, g_toastActionEvent};
    DWORD start = GetTickCount();
    for (;;) {
        DWORD elapsed = GetTickCount() - start;
        DWORD remaining = timeoutMs == INFINITE
                              ? INFINITE
                              : (elapsed >= timeoutMs ? 0 : timeoutMs - elapsed);
        DWORD idx = 0;
        HRESULT hr = CoWaitForMultipleHandles(
            COWAIT_DISPATCH_CALLS | COWAIT_DISPATCH_WINDOW_MESSAGES, remaining,
            ARRAYSIZE(waits), waits, &idx);
        if (hr == RPC_S_CALLPENDING) {  // Timed out: apply the automatic action.
            action = ACTION_AUTO;
            break;
        }
        if (FAILED(hr)) {
            action = ACTION_AUTO;  // Unexpected; fall back to the safe default.
            break;
        }
        if (idx == 0) {  // Stop requested: never delete on shutdown.
            action = ACTION_KEEP;
            break;
        }
        EnterCriticalSection(&g_toastLock);
        action = g_toastAction;
        LeaveCriticalSection(&g_toastLock);
        break;
    }

    notifier->Hide(toast.Get());
    return true;
}

// ============================================================================
// Action dialog (fallback, used when toast notification registration failed)
// ============================================================================

struct DialogState {
    DWORD started;
    DWORD timeoutMs;  // INFINITE when there is no countdown.
    std::wstring baseText;
};

static HRESULT CALLBACK DialogCallback(HWND hwnd, UINT msg, WPARAM, LPARAM,
                                       LONG_PTR ref) {
    auto* state = reinterpret_cast<DialogState*>(ref);
    switch (msg) {
        case TDN_CREATED:
            g_dialog.store(hwnd);
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            break;
        case TDN_DESTROYED:
            g_dialog.store(nullptr);
            break;
        case TDN_TIMER:
            if (state->timeoutMs != INFINITE) {
                DWORD elapsed = GetTickCount() - state->started;
                if (elapsed >= state->timeoutMs) {
                    SendMessageW(hwnd, TDM_CLICK_BUTTON, ACTION_AUTO, 0);
                } else {
                    DWORD left = (state->timeoutMs - elapsed + 999) / 1000;
                    std::wstring text = state->baseText + L"\n\nAutomatic action in " +
                                        std::to_wstring(left) + L" s…";
                    SendMessageW(hwnd, TDM_SET_ELEMENT_TEXT, TDE_CONTENT,
                                 (LPARAM)text.c_str());
                }
            }
            break;
    }
    return S_OK;
}

static int AskAction(const std::wstring& path, const Settings& s) {
    std::wstring name = path.substr(path.find_last_of(L"\\/") + 1);
    DialogState state;
    state.started = GetTickCount();
    state.timeoutMs = s.delaySeconds > 0 ? (DWORD)s.delaySeconds * 1000 : INFINITE;
    state.baseText = name + L"\n\nChoose an action, or wait for the configured "
                            L"automatic action.";

    // Reserve the countdown line up front so TDF_SIZE_TO_CONTENT sizes the dialog
    // once instead of growing after the first timer tick.
    std::wstring initialText = state.baseText;
    if (state.timeoutMs != INFINITE) {
        initialText += L"\n\nAutomatic action in " +
                       std::to_wstring((state.timeoutMs + 999) / 1000) + L" s…";
    }

    TASKDIALOG_BUTTON buttons[] = {
        {ACTION_DELETE, L"Delete now"},
        {ACTION_COPY_DELETE, L"Copy image && delete"},
        {ACTION_KEEP, L"Keep file"},
        {ACTION_AUTO, L"Use automatic action"},
    };

    TASKDIALOGCONFIG c{};
    c.cbSize = sizeof(c);
    c.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW | TDF_SIZE_TO_CONTENT |
                TDF_CALLBACK_TIMER | TDF_ALLOW_DIALOG_CANCELLATION;
    c.pszWindowTitle = L"SnapSentry";
    c.pszMainIcon = TD_SHIELD_ICON;
    c.pszMainInstruction = L"Screenshot saved";
    c.pszContent = initialText.c_str();
    c.cButtons = ARRAYSIZE(buttons);
    c.pButtons = buttons;
    c.nDefaultButton = ACTION_COPY_DELETE;
    c.pfCallback = DialogCallback;
    c.lpCallbackData = (LONG_PTR)&state;

    int result = ACTION_AUTO;
    // Cancellation (Esc / shutdown-driven close) is treated as Keep: never delete.
    if (FAILED(TaskDialogIndirect(&c, &result, nullptr, nullptr))) {
        return ACTION_KEEP;
    }
    return result;
}

// Tries the toast notification first; falls back to the dialog box if toast
// registration didn't succeed on this machine or the WinRT call chain fails.
static int ChooseAction(const std::wstring& path, const Settings& s) {
    int action = ACTION_AUTO;
    if (ShowToast(path, s, action)) {
        return action;
    }
    return AskAction(path, s);
}

// ============================================================================
// Processing
// ============================================================================

// Deletes a watched-folder file, refusing to follow a reparse point so we never
// act on something that redirects outside the folder.
static void DeleteWatched(const std::wstring& path, const Settings& s) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return;  // Already gone.
    }
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
        Wh_Log(L"Refusing to delete reparse point%s",
               s.logDetails ? (L": " + path).c_str() : L"");
        return;
    }
    if (!DeleteFileW(path.c_str())) {
        Wh_Log(L"Delete failed (%lu)%s", GetLastError(),
               s.logDetails ? (L": " + path).c_str() : L"");
    }
}

// Waits until the file can be opened for reading (Snipping Tool has released it)
// or the mod is shutting down. Returns false if the file vanished or we stopped.
static bool WaitForStableFile(const std::wstring& path) {
    for (int i = 0; i < 30; i++) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            return true;
        }
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            return false;  // Deleted before we got to it.
        }
        if (WaitStop(100)) {
            return false;
        }
    }
    // Still locked after ~3s; assume it exists and is being held open.
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static void ProcessOne(const std::wstring& path) {
    Settings s = SnapshotSettings();

    if (!WaitForStableFile(path)) {
        return;
    }

    int action = s.popup ? ChooseAction(path, s) : ACTION_AUTO;
    if (action == ACTION_KEEP) {
        return;
    }
    if (action == ACTION_DELETE) {
        DeleteWatched(path, s);
        return;
    }

    // ACTION_COPY_DELETE forces the durable image path + deletion regardless of
    // the configured clipboard mode. ACTION_AUTO / timeout uses the settings.
    bool forceImage = (action == ACTION_COPY_DELETE);

    bool copied;
    if (forceImage || s.mode == L"image") {
        copied = ClipboardImage(path);
    } else if (s.mode == L"file") {
        copied = ClipboardFile(path);
    } else if (s.mode == L"path") {
        copied = ClipboardText(path);
    } else {  // "none"
        copied = true;
    }

    if (!copied) {
        Wh_Log(L"Clipboard copy failed%s",
               s.logDetails ? (L": " + path).c_str() : L"");
        return;  // Invariant: never delete when a requested copy failed.
    }

    // Deletion only makes sense for self-contained payloads (image / none).
    // File and Path payloads reference the file, so deleting would break them.
    bool payloadReferencesFile =
        !forceImage && (s.mode == L"file" || s.mode == L"path");
    bool wantsDelete = forceImage || (s.deleteFile && !payloadReferencesFile);
    if (!wantsDelete) {
        return;
    }

    // With the popup, the countdown already elapsed, so delete immediately.
    DWORD delay = (s.popup || forceImage) ? 0 : (DWORD)s.delaySeconds * 1000;
    if (!WaitStop(delay)) {
        DeleteWatched(path, s);
    }
}

// ============================================================================
// Worker: drains the queue on a dedicated COM (STA) thread
// ============================================================================

static bool DequeueOne(std::wstring& path) {
    EnterCriticalSection(&g_lock);
    bool has = !g_queue.empty();
    if (has) {
        path = std::move(g_queue.front());
        g_queue.pop_front();
    }
    LeaveCriticalSection(&g_lock);
    return has;
}

static void ReleaseInflight(const std::wstring& path) {
    std::wstring name = path.substr(path.find_last_of(L"\\/") + 1);
    EnterCriticalSection(&g_lock);
    g_inflight.erase(name);
    LeaveCriticalSection(&g_lock);
}

static DWORD WINAPI WorkerThread(LPVOID) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    RoInitialize(RO_INIT_SINGLETHREADED);

    bool aumidOk = EnsureAumidRegistered();
    bool clsidOk = EnsureClsidRegistered();
    HRESULT regHr = CoRegisterClassObject(
        CLSID_SnapSentryToastActivator, &g_toastActivatorFactory,
        CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &g_toastActivatorCookie);
    g_toastRegistered = aumidOk && clsidOk && SUCCEEDED(regHr);
    if (!g_toastRegistered.load()) {
        Wh_Log(
            L"Toast notification registration incomplete (aumid=%d clsid=%d "
            L"hr=0x%08lx); using the dialog instead",
            aumidOk, clsidOk, regHr);
    }

    // CoWaitForMultipleHandles (rather than plain WaitForMultipleObjects) pumps
    // incoming COM calls -- needed so a toast button click can be dispatched to
    // ToastActivator::Activate even while idling between screenshots.
    HANDLE waits[] = {g_stopEvent, g_workEvent};
    while (true) {
        DWORD idx = 0;
        HRESULT hr = CoWaitForMultipleHandles(
            COWAIT_DISPATCH_CALLS | COWAIT_DISPATCH_WINDOW_MESSAGES, INFINITE,
            ARRAYSIZE(waits), waits, &idx);
        if (FAILED(hr) || idx == 0) {
            break;  // Stop (or an unexpected error -- treat as shutdown).
        }
        std::wstring path;
        while (!WaitStop(0) && DequeueOne(path)) {
            ProcessOne(path);
            ReleaseInflight(path);
        }
    }

    if (g_toastRegistered.exchange(false)) {
        CoRevokeClassObject(g_toastActivatorCookie);
    }
    RoUninitialize();
    CoUninitialize();
    return 0;
}

static void Enqueue(const std::wstring& folder, const std::wstring& name) {
    if (!IsSafeChildName(name) || !IsSupportedImage(name)) {
        return;
    }
    EnterCriticalSection(&g_lock);
    bool added = g_inflight.insert(name).second;  // Dedup rapid duplicate events.
    if (added) {
        g_queue.push_back(folder + L"\\" + name);
    }
    LeaveCriticalSection(&g_lock);
    if (added) {
        SetEvent(g_workEvent);
    }
}

// ============================================================================
// Watcher: overlapped ReadDirectoryChangesW with a clean stop/reload path
// ============================================================================

static void ParseNotifications(const std::wstring& folder, const BYTE* buffer) {
    for (auto* info = (const FILE_NOTIFY_INFORMATION*)buffer;;) {
        if (info->Action == FILE_ACTION_ADDED ||
            info->Action == FILE_ACTION_RENAMED_NEW_NAME) {
            std::wstring name(info->FileName,
                              info->FileNameLength / sizeof(wchar_t));
            Enqueue(folder, name);
        }
        if (!info->NextEntryOffset) {
            break;
        }
        info = (const FILE_NOTIFY_INFORMATION*)((const BYTE*)info +
                                                info->NextEntryOffset);
    }
}

static DWORD WINAPI WatchThread(LPVOID) {
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) {
        return 0;
    }
    HANDLE readyWaits[] = {g_stopEvent, g_reloadEvent, ov.hEvent};

    while (!WaitStop(0)) {
        Settings s = SnapshotSettings();
        if (!s.enabled || s.folder.empty()) {
            HANDLE idle[] = {g_stopEvent, g_reloadEvent};
            WaitForMultipleObjects(2, idle, FALSE, 500);
            continue;
        }

        HANDLE dir = CreateFileW(
            s.folder.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            nullptr);
        if (dir == INVALID_HANDLE_VALUE) {
            HANDLE idle[] = {g_stopEvent, g_reloadEvent};
            WaitForMultipleObjects(2, idle, FALSE, 2000);  // Folder may appear later.
            continue;
        }

        BYTE buffer[16384];
        bool reopen = false;
        while (!reopen) {
            ResetEvent(ov.hEvent);
            DWORD bytes = 0;
            if (!ReadDirectoryChangesW(
                    dir, buffer, sizeof(buffer), FALSE,
                    FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE,
                    &bytes, &ov, nullptr)) {
                break;  // Re-open the directory.
            }

            DWORD w = WaitForMultipleObjects(3, readyWaits, FALSE, INFINITE);
            if (w == WAIT_OBJECT_0) {  // Stop.
                CancelIoEx(dir, &ov);
                GetOverlappedResult(dir, &ov, &bytes, TRUE);
                CloseHandle(dir);
                CloseHandle(ov.hEvent);
                return 0;
            }
            if (w == WAIT_OBJECT_0 + 1) {  // Settings changed.
                CancelIoEx(dir, &ov);
                GetOverlappedResult(dir, &ov, &bytes, TRUE);
                reopen = true;
                break;
            }
            // I/O completed.
            if (!GetOverlappedResult(dir, &ov, &bytes, FALSE)) {
                break;
            }
            if (bytes == 0) {
                // Buffer overflow: notifications were dropped. We can't tell which
                // files are new, and rescanning could delete pre-existing files,
                // so we deliberately skip rather than risk the safety invariant.
                Wh_Log(L"Change buffer overflow; some screenshots may be skipped");
                continue;
            }
            ParseNotifications(s.folder, buffer);
        }
        CloseHandle(dir);
    }

    CloseHandle(ov.hEvent);
    return 0;
}

// ============================================================================
// Tool-mod entry points
// ============================================================================

BOOL WhTool_ModInit() {
    InitializeCriticalSection(&g_lock);
    InitializeCriticalSection(&g_toastLock);
    LoadSettings();

    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);   // Manual reset.
    g_reloadEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);  // Auto reset.
    g_workEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);    // Auto reset.
    g_toastActionEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);  // Auto reset.
    if (!g_stopEvent || !g_reloadEvent || !g_workEvent || !g_toastActionEvent) {
        return FALSE;
    }

    g_workerThread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
    g_watchThread = CreateThread(nullptr, 0, WatchThread, nullptr, 0, nullptr);
    return g_workerThread != nullptr && g_watchThread != nullptr;
}

void WhTool_ModSettingsChanged() {
    LoadSettings();
    SetEvent(g_reloadEvent);  // Re-open the folder in case the path changed.
}

void WhTool_ModUninit() {
    SetEvent(g_stopEvent);

    // Dismiss an open action dialog as "Keep" so shutdown never triggers a delete.
    HWND dlg = g_dialog.load();
    if (dlg) {
        SendMessageW(dlg, TDM_CLICK_BUTTON, ACTION_KEEP, 0);
    }
    SetEvent(g_workEvent);

    HANDLE threads[] = {g_watchThread, g_workerThread};
    WaitForMultipleObjects(2, threads, TRUE, 5000);
    if (g_watchThread) {
        CloseHandle(g_watchThread);
    }
    if (g_workerThread) {
        CloseHandle(g_workerThread);
    }
    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
    }
    if (g_reloadEvent) {
        CloseHandle(g_reloadEvent);
    }
    if (g_workEvent) {
        CloseHandle(g_workEvent);
    }
    if (g_toastActionEvent) {
        CloseHandle(g_toastActionEvent);
    }
    DeleteCriticalSection(&g_lock);
    DeleteCriticalSection(&g_toastLock);
}

////////////////////////////////////////////////////////////////////////////////
// Windhawk tool mod boilerplate. Runs the mod in a dedicated windhawk.exe
// process instead of injecting into other processes. See:
// https://github.com/ramensoftware/windhawk/wiki/Mods-as-tools:-Running-mods-in-a-dedicated-process

bool g_isToolModProcessLauncher;
HANDLE g_toolModProcessMutex;

void WINAPI EntryPoint_Hook() {
    Wh_Log(L">");
    ExitThread(0);
}

BOOL Wh_ModInit() {
    DWORD sessionId;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &sessionId) &&
        sessionId == 0) {
        return FALSE;
    }

    bool isExcluded = false;
    bool isToolModProcess = false;
    bool isCurrentToolModProcess = false;
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLine(), &argc);
    if (!argv) {
        Wh_Log(L"CommandLineToArgvW failed");
        return FALSE;
    }

    for (int i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"-service") == 0 ||
            wcscmp(argv[i], L"-service-start") == 0 ||
            wcscmp(argv[i], L"-service-stop") == 0) {
            isExcluded = true;
            break;
        }
    }

    for (int i = 1; i < argc - 1; i++) {
        if (wcscmp(argv[i], L"-tool-mod") == 0) {
            isToolModProcess = true;
            if (wcscmp(argv[i + 1], WH_MOD_ID) == 0) {
                isCurrentToolModProcess = true;
            }
            break;
        }
    }

    LocalFree(argv);

    if (isExcluded) {
        return FALSE;
    }

    if (isCurrentToolModProcess) {
        g_toolModProcessMutex =
            CreateMutex(nullptr, TRUE, L"windhawk-tool-mod_" WH_MOD_ID);
        if (!g_toolModProcessMutex) {
            Wh_Log(L"CreateMutex failed");
            ExitProcess(1);
        }

        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            Wh_Log(L"Tool mod already running (%s)", WH_MOD_ID);
            ExitProcess(1);
        }

        if (!WhTool_ModInit()) {
            ExitProcess(1);
        }

        IMAGE_DOS_HEADER* dosHeader =
            (IMAGE_DOS_HEADER*)GetModuleHandle(nullptr);
        IMAGE_NT_HEADERS* ntHeaders =
            (IMAGE_NT_HEADERS*)((BYTE*)dosHeader + dosHeader->e_lfanew);

        DWORD entryPointRVA = ntHeaders->OptionalHeader.AddressOfEntryPoint;
        void* entryPoint = (BYTE*)dosHeader + entryPointRVA;

        Wh_SetFunctionHook(entryPoint, (void*)EntryPoint_Hook, nullptr);
        return TRUE;
    }

    if (isToolModProcess) {
        return FALSE;
    }

    g_isToolModProcessLauncher = true;
    return TRUE;
}

void Wh_ModAfterInit() {
    if (!g_isToolModProcessLauncher) {
        return;
    }

    WCHAR currentProcessPath[MAX_PATH];
    switch (GetModuleFileName(nullptr, currentProcessPath,
                              ARRAYSIZE(currentProcessPath))) {
        case 0:
        case ARRAYSIZE(currentProcessPath):
            Wh_Log(L"GetModuleFileName failed");
            return;
    }

    WCHAR
    commandLine[MAX_PATH + 2 +
                (sizeof(L" -tool-mod \"" WH_MOD_ID "\"") / sizeof(WCHAR)) - 1];
    swprintf_s(commandLine, L"\"%s\" -tool-mod \"%s\"", currentProcessPath,
               WH_MOD_ID);

    HMODULE kernelModule = GetModuleHandle(L"kernelbase.dll");
    if (!kernelModule) {
        kernelModule = GetModuleHandle(L"kernel32.dll");
        if (!kernelModule) {
            Wh_Log(L"No kernelbase.dll/kernel32.dll");
            return;
        }
    }

    using CreateProcessInternalW_t = BOOL(WINAPI*)(
        HANDLE hUserToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
        LPSECURITY_ATTRIBUTES lpProcessAttributes,
        LPSECURITY_ATTRIBUTES lpThreadAttributes, WINBOOL bInheritHandles,
        DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
        LPSTARTUPINFOW lpStartupInfo,
        LPPROCESS_INFORMATION lpProcessInformation,
        PHANDLE hRestrictedUserToken);
    CreateProcessInternalW_t pCreateProcessInternalW =
        (CreateProcessInternalW_t)GetProcAddress(kernelModule,
                                                 "CreateProcessInternalW");
    if (!pCreateProcessInternalW) {
        Wh_Log(L"No CreateProcessInternalW");
        return;
    }

    STARTUPINFO si{
        .cb = sizeof(STARTUPINFO),
        .dwFlags = STARTF_FORCEOFFFEEDBACK,
    };
    PROCESS_INFORMATION pi;
    if (!pCreateProcessInternalW(nullptr, currentProcessPath, commandLine,
                                 nullptr, nullptr, FALSE, NORMAL_PRIORITY_CLASS,
                                 nullptr, nullptr, &si, &pi, nullptr)) {
        Wh_Log(L"CreateProcess failed");
        return;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

void Wh_ModSettingsChanged() {
    if (g_isToolModProcessLauncher) {
        return;
    }

    WhTool_ModSettingsChanged();
}

void Wh_ModUninit() {
    if (g_isToolModProcessLauncher) {
        return;
    }

    WhTool_ModUninit();
    ExitProcess(0);
}
