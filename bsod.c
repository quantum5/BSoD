#define _WIN32_WINNT 0x0500
#define WINVER       0x0500

#include <windows.h>

#include <aclapi.h>
#include <shlwapi.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof *(x))

#define WM_ENFORCE_FOCUS (WM_APP + 0)

#define TM_DISPLAY   0xBEEF
#define TM_AUTOKILL  0xDEAD
#define TM_FORCEDESK 0xFAC

#define AUTOKILL_TIMEOUT 50000
#define DISPLAY_DELAY    1000
#define FORCE_INTERVAL   1000

#define HDLG_MSGBOX ((HWND) 0xDEADBEEF)
#define IDC_EDIT1   1024

#if defined(_MSC_VER) && _MSC_VER <= 1200
#define wnsprintf wnsprintfA
int wnsprintfA(PSTR pszDest, int cchDest, PCSTR pszFmt, ...);
typedef unsigned char *RPC_CSTR;
#endif

typedef BOOL(WINAPI *LPFN_SHUTDOWNBLOCKREASONCREATE)(HWND, LPCWSTR);
typedef BOOL(WINAPI *LPFN_SHUTDOWNBLOCKREASONDESTROY)(HWND);

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK DlgProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK LowLevelKeyboardProc(int, WPARAM, LPARAM);
LRESULT CALLBACK LowLevelMouseProc(int, WPARAM, LPARAM);

#define PASSWORD_LENGTH (sizeof(szRealPassword) - 1)
const char szRealPassword[] = {0x54, 0x50, 0x44, 0x4b, 0x51, 0x50,
                               0x48, 0x10, 0xb,  0x46, 0x44, 0x00};
const char szClassName[] = "BlueScreenOfDeath";

HINSTANCE hInst;
HWND hwnd;  // Main window
HWND scwnd; // Static bitmap control
HWND hdlg;  // Password popup

HACCEL hAccel;
HHOOK hhkKeyboard, hhkMouse;
HDESK hOldDesk, hNewDesk;

#ifdef NOTASKMGR
HKEY hSystemPolicy;
#endif

ACCEL accel[] = {
    {FALT | FVIRTKEY,                     '1',       0xBE00},
    {FALT | FVIRTKEY,                     '3',       0xBE01},
    {FALT | FVIRTKEY,                     '5',       0xBE02},
    {FALT | FVIRTKEY,                     '7',       0xBE03},
    {FALT | FVIRTKEY,                     VK_F2,     0xBE04},
    {FALT | FVIRTKEY,                     VK_F4,     0xBE05},
    {FALT | FVIRTKEY,                     VK_F6,     0xBE06},
    {FALT | FVIRTKEY,                     VK_F8,     0xBE07},
    {FALT | FCONTROL | FSHIFT | FVIRTKEY, VK_DELETE, 0xDEAD},
};
BOOL bAccel[ARRAY_SIZE(accel) - 1];

char szDeskName[40];

LPFN_SHUTDOWNBLOCKREASONCREATE fShutdownBlockReasonCreate;
LPFN_SHUTDOWNBLOCKREASONDESTROY fShutdownBlockReasonDestroy;

void GenerateUUID(LPSTR szUuid) {
    UUID bUuid;
    RPC_CSTR rstrUUID;

    UuidCreate(&bUuid);
    UuidToString(&bUuid, &rstrUUID);
    lstrcpy(szUuid, (LPCSTR) rstrUUID);
    RpcStringFree(&rstrUUID);
}

// Define memset to avoid pulling in libc
void *memset(void *s, int c, size_t n) {
    char *p = (char *) s;
    while (n--)
        *p++ = (char) c;
    return s;
}

#ifdef NOTASKMGR
void DisableTaskManager(void) {
    DWORD dwOne = 1;
    if (hSystemPolicy)
        RegSetValueEx(hSystemPolicy, "DisableTaskMgr", 0, REG_DWORD, (LPBYTE) &dwOne,
                      sizeof(DWORD));
}

void EnableTaskManager(void) {
    DWORD dwZero = 0;
    if (hSystemPolicy)
        RegSetValueEx(hSystemPolicy, "DisableTaskMgr", 0, REG_DWORD, (LPBYTE) &dwZero,
                      sizeof(DWORD));
}
#endif

DWORD ProtectProcess(void) {
    ACL acl;

    if (!InitializeAcl(&acl, sizeof acl, ACL_REVISION))
        return GetLastError();

    return SetSecurityInfo(GetCurrentProcess(), SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, NULL,
                           NULL, &acl, NULL);
}

STICKYKEYS StartupStickyKeys = {sizeof(STICKYKEYS), 0};
TOGGLEKEYS StartupToggleKeys = {sizeof(TOGGLEKEYS), 0};
FILTERKEYS StartupFilterKeys = {sizeof(FILTERKEYS), 0};

void AllowAccessibilityShortcutKeys(BOOL bAllowKeys) {
    if (bAllowKeys) {
        SystemParametersInfo(SPI_SETSTICKYKEYS, sizeof(STICKYKEYS), &StartupStickyKeys, 0);
        SystemParametersInfo(SPI_SETTOGGLEKEYS, sizeof(TOGGLEKEYS), &StartupToggleKeys, 0);
        SystemParametersInfo(SPI_SETFILTERKEYS, sizeof(FILTERKEYS), &StartupFilterKeys, 0);
    } else {
        STICKYKEYS skOff = StartupStickyKeys;
        TOGGLEKEYS tkOff = StartupToggleKeys;
        FILTERKEYS fkOff = StartupFilterKeys;

        if ((skOff.dwFlags & SKF_STICKYKEYSON) == 0) {
            skOff.dwFlags &= ~SKF_HOTKEYACTIVE;
            skOff.dwFlags &= ~SKF_CONFIRMHOTKEY;
            SystemParametersInfo(SPI_SETSTICKYKEYS, sizeof(STICKYKEYS), &skOff, 0);
        }
        if ((tkOff.dwFlags & TKF_TOGGLEKEYSON) == 0) {
            tkOff.dwFlags &= ~TKF_HOTKEYACTIVE;
            tkOff.dwFlags &= ~TKF_CONFIRMHOTKEY;
            SystemParametersInfo(SPI_SETTOGGLEKEYS, sizeof(TOGGLEKEYS), &tkOff, 0);
        }
        if ((fkOff.dwFlags & FKF_FILTERKEYSON) == 0) {
            fkOff.dwFlags &= ~FKF_HOTKEYACTIVE;
            fkOff.dwFlags &= ~FKF_CONFIRMHOTKEY;
            SystemParametersInfo(SPI_SETFILTERKEYS, sizeof(FILTERKEYS), &fkOff, 0);
        }
    }
}

int UnixTime() {
    union {
        __int64 scalar;
        FILETIME ft;
    } time;
    GetSystemTimeAsFileTime(&time.ft);
    return (int) ((time.scalar - 116444736000000000i64) / 10000000i64);
}

LPSTR bsod1 = "\r\n\
A problem has been detected and Windows has been shut down to prevent damage\r\n\
to your computer.\r\n\
\r\n\
The problem seems to be caused by the following file: ";

LPSTR bsod2 = "\r\n\r\n";

LPSTR bsod3 = "\r\n\
\r\n\
If this is the first time you've seen this stop error screen,\r\n\
restart your computer. If this screen appears again, follow\r\n\
these steps:\r\n\
\r\n\
Check to make sure any new hardware or software is properly installed.\r\n\
If this is a new installation, ask your hardware or software manufacturer\r\n\
for any Windows updates you might need.\r\n\
\r\n\
If problems continue, disable or remove any newly installed hardware\r\n\
or software. Disable BIOS memory options such as caching or shadowing.\r\n\
If you need to use Safe Mode to remove or disable components, restart\r\n\
your computer, press F8 to select Advanced Startup Options, and then\r\n\
select Safe Mode.\r\n\
\r\n\
Technical information:\r\n\
\r\n";

LPSTR bsod4 = "*** STOP: 0x%08X (0x%08X,0x%08X,0x%08X,0x%08X)";
LPSTR bsod5 = "\r\n\r\n\r\n***  ";
LPSTR bsod6 = "%s - Address %08X base at %08X, DateStamp %08x";

int Random() {
    static int seed = 0;
    if (!seed)
        seed = UnixTime();
    seed = 1103515245 * seed + 12345;
    seed &= 0x7FFFFFFF;
    return seed;
}

LPSTR lpBadDrivers[] = {
    "HTTP.SYS",  "SPCMDCON.SYS", "NTFS.SYS",   "ACPI.SYS",  "AMDK8.SYS", "ATI2MTAG.SYS",
    "CDROM.SYS", "BEEP.SYS",     "BOWSER.SYS", "EVBDX.SYS", "TCPIP.SYS", "RDPDR.SYS",
};

typedef struct {
    LPSTR name;
    DWORD code;
} BUG_CHECK_CODE;

BUG_CHECK_CODE lpErrorCodes[] = {
    {"INVALID_SOFTWARE_INTERRUPT",  0x07},
    {"KMODE_EXCEPTION_NOT_HANDLED", 0x1E},
    {"PAGE_FAULT_IN_NONPAGED_AREA", 0x50},
    {"KERNEL_STACK_INPAGE_ERROR",   0x77},
    {"KERNEL_DATA_INPAGE_ERROR",    0x7A},
};

HBITMAP RenderBSoD(void) {
    HBITMAP hbmp;
    HDC hdc;
    HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 128));
    RECT rect = {0, 0, 640, 480};
    HFONT hFont;
    char bsod[2048] = {0};
    char buf[1024];
    LPSTR lpName;
    BUG_CHECK_CODE bcc;
    DWORD dwAddress;
    int i, k;

    // Initialize RNG
    k = Random() & 0xFF;
    for (i = 0; i < k; ++i)
        Random();

    hdc = CreateCompatibleDC(GetDC(hwnd));
    hbmp = CreateCompatibleBitmap(GetDC(hwnd), 640, 480);
    hFont = CreateFont(14, 8, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_RASTER_PRECIS,
                       CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, FF_MODERN, "Lucida Console");

    lstrcat(bsod, bsod1);
    lpName = lpBadDrivers[Random() % ARRAY_SIZE(lpBadDrivers)];
    bcc = lpErrorCodes[Random() % ARRAY_SIZE(lpErrorCodes)];
    lstrcat(bsod, lpName);
    lstrcat(bsod, bsod2);
    lstrcat(bsod, bcc.name);
    lstrcat(bsod, bsod3);
    switch (Random() % 4) {
    case 0:
        wnsprintf(buf, ARRAY_SIZE(buf), bsod4, bcc.code, Random() | 1 << 31, Random() & 0xF,
                  Random() | 1 << 31, 0);
        break;
    case 1:
        wnsprintf(buf, ARRAY_SIZE(buf), bsod4, bcc.code, Random() | 1 << 31, Random() | 1 << 31,
                  Random() & 0xF, Random() & 0xF);
        break;
    case 2:
        wnsprintf(buf, ARRAY_SIZE(buf), bsod4, bcc.code, Random() | 1 << 31, 0, Random() & 0xF,
                  Random() & 0xF);
        break;
    default:
        wnsprintf(buf, ARRAY_SIZE(buf), bsod4, bcc.code, Random() | 1 << 31, Random() | 1 << 31,
                  Random() | 1 << 31, Random() | 1 << 31);
        break;
    }
    lstrcat(bsod, buf);
    lstrcat(bsod, bsod5);
    dwAddress = Random() | 1 << 31;
    wnsprintf(buf, ARRAY_SIZE(buf), bsod6, lpName, dwAddress, dwAddress & 0xFFFF0000, UnixTime());
    lstrcat(bsod, buf);

    SelectObject(hdc, hbmp);
    SelectObject(hdc, hFont);
    FillRect(hdc, &rect, hBrush);
    SetBkColor(hdc, RGB(0, 0, 128));
    SetTextColor(hdc, RGB(255, 255, 255));
    DrawText(hdc, bsod, -1, &rect, 0);

    DeleteDC(hdc);
    return hbmp;
}

DWORD APIENTRY RawEntryPoint() {
    MSG messages;
    WNDCLASSEX wincl;
    HMODULE user32;

    hInst = (HINSTANCE) GetModuleHandle(NULL);
    GenerateUUID(szDeskName);

    ProtectProcess();

    // Save the current sticky/toggle/filter key settings so they can be
    // restored them later
    SystemParametersInfo(SPI_GETSTICKYKEYS, sizeof(STICKYKEYS), &StartupStickyKeys, 0);
    SystemParametersInfo(SPI_GETTOGGLEKEYS, sizeof(TOGGLEKEYS), &StartupToggleKeys, 0);
    SystemParametersInfo(SPI_GETFILTERKEYS, sizeof(FILTERKEYS), &StartupFilterKeys, 0);

    hOldDesk = GetThreadDesktop(GetCurrentThreadId());
    hNewDesk = CreateDesktop(szDeskName, NULL, NULL, 0, GENERIC_ALL, NULL);
    SetThreadDesktop(hNewDesk);
    SwitchDesktop(hNewDesk);

#ifdef NOTASKMGR
    if (RegCreateKeyEx(HKEY_CURRENT_USER,
                       "Software\\Microsoft\\Windows\\"
                       "CurrentVersion\\Policies\\System",
                       0, NULL, 0, KEY_SET_VALUE, NULL, &hSystemPolicy, NULL)) {
        hSystemPolicy = NULL;
    }
#endif

    wincl.hInstance = hInst;
    wincl.lpszClassName = szClassName;
    wincl.lpfnWndProc = WndProc;
    wincl.style = CS_DBLCLKS;
    wincl.cbSize = sizeof(WNDCLASSEX);

    wincl.hIcon = LoadIcon(NULL, MAKEINTRESOURCE(1));
    wincl.hIconSm = LoadIcon(NULL, MAKEINTRESOURCE(1));
    wincl.hCursor = NULL;
    wincl.lpszMenuName = NULL;
    wincl.cbClsExtra = 0;
    wincl.cbWndExtra = 0;
    wincl.hbrBackground = (HBRUSH) GetStockObject(BLACK_BRUSH);

    if (!RegisterClassEx(&wincl))
        return 0;

    hwnd = CreateWindowEx(0, szClassName, "Blue Screen of Death", WS_POPUP, CW_USEDEFAULT,
                          CW_USEDEFAULT, 640, 480, NULL, NULL, hInst, NULL);

    user32 = GetModuleHandle("user32");
    fShutdownBlockReasonCreate =
        (LPFN_SHUTDOWNBLOCKREASONCREATE) GetProcAddress(user32, "ShutdownBlockReasonCreate");
    fShutdownBlockReasonDestroy =
        (LPFN_SHUTDOWNBLOCKREASONDESTROY) GetProcAddress(user32, "ShutdownBlockReasonDestroy");

    hhkKeyboard = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInst, 0);
    hhkMouse = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, hInst, 0);

    ShowWindow(hwnd, SW_MAXIMIZE);
    UpdateWindow(hwnd);

    while (GetMessage(&messages, NULL, 0, 0) > 0) {
        if (!TranslateAccelerator(hwnd, hAccel, &messages)) {
            TranslateMessage(&messages);
            DispatchMessage(&messages);
        }
    }

    ExitProcess(messages.wParam);
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        return 1;
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        KBDLLHOOKSTRUCT *key = (KBDLLHOOKSTRUCT *) lParam;

        switch (key->vkCode) {
        case VK_LWIN:
        case VK_RWIN:
        case VK_TAB:
        case VK_ESCAPE:
        case VK_LBUTTON:
        case VK_RBUTTON:
        case VK_CANCEL:
        case VK_MBUTTON:
        case VK_CLEAR:
        case VK_PAUSE:
        case VK_CAPITAL:
        case VK_KANA:
        case VK_JUNJA:
        case VK_FINAL:
        case VK_HANJA:
        case VK_NONCONVERT:
        case VK_MODECHANGE:
        case VK_ACCEPT:
        case VK_END:
        case VK_HOME:
        case VK_LEFT:
        case VK_UP:
        case VK_RIGHT:
        case VK_DOWN:
        case VK_SELECT:
        case VK_PRINT:
        case VK_EXECUTE:
        case VK_SNAPSHOT:
        case VK_INSERT:
        case VK_HELP:
        case VK_APPS:
        case VK_SLEEP:
        case VK_NUMLOCK:
        case VK_SCROLL:
        case VK_PROCESSKEY:
        case VK_PACKET:
        case VK_ATTN:
            return 1;
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    int i;
    switch (message) {
    case WM_CREATE:
        scwnd = CreateWindowEx(0, "STATIC", "", SS_BITMAP | WS_CHILD | WS_VISIBLE, 0, 0, 640, 480,
                               hwnd, (HMENU) -1, NULL, NULL);
#ifndef NOAUTOKILL
        SetTimer(hwnd, TM_AUTOKILL, AUTOKILL_TIMEOUT, NULL);
#endif
        SetTimer(hwnd, TM_DISPLAY, DISPLAY_DELAY, NULL);
        SetTimer(hwnd, TM_FORCEDESK, FORCE_INTERVAL, NULL);

        SetCursor(NULL);

        if (fShutdownBlockReasonCreate)
            fShutdownBlockReasonCreate(hwnd, L"You can't shutdown with a BSoD running.");

        hAccel = CreateAcceleratorTable(accel, ARRAY_SIZE(accel));

        // Force to front
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_ASYNCWINDOWPOS | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
        SetForegroundWindow(hwnd);
        LockSetForegroundWindow(1);

        AllowAccessibilityShortcutKeys(FALSE);
#ifdef NOTASKMGR
        DisableTaskManager();
#endif
        break;

    case WM_SHOWWINDOW:
        break;

    case WM_TIMER:
        switch (wParam) {
        case TM_DISPLAY: {
            RECT rectClient;

            KillTimer(hwnd, TM_DISPLAY);
            SendMessage(scwnd, STM_SETIMAGE, (WPARAM) IMAGE_BITMAP, (LPARAM) RenderBSoD());
            GetClientRect(hwnd, &rectClient);
            SetWindowPos(scwnd, 0, 0, 0, rectClient.right, rectClient.bottom,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            InvalidateRect(hwnd, NULL, TRUE);
            break;
        }
#ifndef NOAUTOKILL
        case TM_AUTOKILL:
            KillTimer(hwnd, TM_AUTOKILL);
            DestroyWindow(hwnd);
            break;
#endif
        case TM_FORCEDESK:
            SwitchDesktop(hNewDesk);
            break;
        }
        break;

    case WM_DESTROY:
        if (fShutdownBlockReasonDestroy)
            fShutdownBlockReasonDestroy(hwnd);

        UnhookWindowsHookEx(hhkKeyboard);
        UnhookWindowsHookEx(hhkMouse);

        DestroyAcceleratorTable(hAccel);
        LockSetForegroundWindow(0);
        AllowAccessibilityShortcutKeys(TRUE);
        SetThreadDesktop(hOldDesk);
        SwitchDesktop(hOldDesk);
        CloseDesktop(hNewDesk);
#ifdef NOTASKMGR
        EnableTaskManager();
#endif
        PostQuitMessage(0);
        break;

    case WM_CLOSE:
        switch (DialogBox(hInst, MAKEINTRESOURCE(32), hwnd, DlgProc)) {
        case 1:
            DestroyWindow(hwnd);
            break;
        case 2:
            hdlg = HDLG_MSGBOX;
            MessageBox(hwnd,
                       "You got the password wrong!\n"
                       "Good luck guessing!",
                       "Error!", MB_ICONERROR);
            hdlg = NULL;
            break;
        default:
            hdlg = HDLG_MSGBOX;
            MessageBox(hwnd,
                       "You just abandoned the perfect chance to exit!\n"
                       "Good luck trying!",
                       "Error!", MB_ICONERROR);
            hdlg = NULL;
        }
        break;

    case WM_KEYDOWN:
        return 0;

    case WM_COMMAND:
    case WM_SYSCOMMAND:
        if (HIWORD(wParam) == 1) {
            if (LOWORD(wParam) == 0xDEAD) {
                for (i = 0; i < ARRAY_SIZE(bAccel); ++i)
                    if (!bAccel[i])
                        return 0;

                SendMessage(hwnd, WM_CLOSE, 0, 0);
            } else if ((LOWORD(wParam) & 0xFF00) == 0xBE00) {
                int index = LOWORD(wParam) & 0xFF;
                if (index < ARRAY_SIZE(bAccel))
                    bAccel[index] = TRUE;
            }
        }
        break;

    case WM_QUERYENDSESSION:
        break;

    case WM_ENFORCE_FOCUS:
        if (GetForegroundWindow() != hdlg) {
            if (hdlg && hdlg != HDLG_MSGBOX) {
                SetFocus(hdlg);
                SetForegroundWindow(hdlg);
            } else if (!hdlg) {
                SetFocus(hwnd);
                SetForegroundWindow(hwnd);
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            }
        }
        SetCursor(NULL);
        ShowWindow(hwnd, SW_SHOWMAXIMIZED);
        break;

    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE)
            break;
        if (!HIWORD(wParam))
            break;

    case WM_NCACTIVATE: // Above are cases that focus can be gain or lost
    case WM_KILLFOCUS:  // All the cases that focus is lost
        PostMessage(hwnd, WM_ENFORCE_FOCUS, 0, 0);
        return 1;

    case WM_SIZE:
        if (wParam != SIZE_MAXIMIZED)
            ShowWindow(hwnd, SW_SHOWMAXIMIZED);
        break;

    default:
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}

INT_PTR CALLBACK DlgProc(HWND hWndDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    UNREFERENCED_PARAMETER(lParam);

    switch (msg) {
    case WM_INITDIALOG:
        hdlg = hWndDlg;
        break;

    case WM_COMMAND:
        switch (wParam) {
        case IDOK: {
            DWORD dwLength, i;
            TCHAR szPassword[PASSWORD_LENGTH + 1];
            dwLength = GetDlgItemText(hWndDlg, IDC_EDIT1, szPassword, PASSWORD_LENGTH + 1);
            if (dwLength != PASSWORD_LENGTH) {
                EndDialog(hWndDlg, 2);
                hdlg = NULL;
                break;
            }

            for (i = 0; i < PASSWORD_LENGTH; ++i)
                szPassword[i] ^= 37;

            EndDialog(hWndDlg, lstrcmp(szPassword, szRealPassword) ? 2 : 1);
            hdlg = NULL;
            break;
        }

        case IDCANCEL:
            EndDialog(hWndDlg, 0);
            hdlg = NULL;
            break;
        }
        break;

    default:
        return FALSE;
    }
    return TRUE;
}
