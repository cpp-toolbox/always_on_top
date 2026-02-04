#include <windows.h>
#include <dwmapi.h>      
#include <string>
#include <vector>
#include <set>
#include "resource.h"

#define IDM_EXIT 1001
#define IDM_RESTORE_ALL 1002
#define WM_TRAYICON (WM_USER + 1)
#define TRAY_ICON_ID 1

NOTIFYICONDATAW nid;
HWND g_hwnd;
HICON g_hIcon = NULL;

struct WindowInfo {
    HWND hwnd;
    std::wstring title;
};

std::vector<WindowInfo> g_windows;
std::set<HWND> g_modified_windows;  // Track windows we've made topmost

BOOL CALLBACK enum_windows_proc(HWND hwnd, LPARAM lParam) {
    // Skip invisible windows
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }
    
    // Skip windows without a title
    wchar_t title[256];
    GetWindowTextW(hwnd, title, 256);
    if (wcslen(title) == 0) {
        return TRUE;
    }
    
    // Get window styles
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    
    // Skip tool windows (floating palettes, toolbars, etc.)
    if (exStyle & WS_EX_TOOLWINDOW) {
        return TRUE;
    }
    
    // Skip windows that are owned by other windows (like dropdown menus)
    if (GetWindow(hwnd, GW_OWNER) != NULL) {
        return TRUE;
    }
    
    // Must be an app window OR have a window edge (real windows have borders)
    // Also check if it would appear on taskbar
    bool isAppWindow = (exStyle & WS_EX_APPWINDOW) != 0;
    bool hasCaption = (style & WS_CAPTION) == WS_CAPTION;
    bool isPopup = (style & WS_POPUP) != 0;
    
    // Skip if it's not a proper app window
    if (!isAppWindow && !hasCaption) {
        return TRUE;
    }
    
    // Skip cloaked windows (Windows 10/11 virtual desktop, hidden UWP apps)
    BOOL isCloaked = FALSE;
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &isCloaked, sizeof(isCloaked));
    if (SUCCEEDED(hr) && isCloaked) {
        return TRUE;
    }
    
    // Skip our own window
    if (hwnd == g_hwnd) {
        return TRUE;
    }
    
    // This window passed all filters - add it to the list
    g_windows.push_back({hwnd, title});
    
    return TRUE;
}

// Restore all windows we modified back to normal
void restore_all_windows() {
    for (HWND hwnd : g_modified_windows) {
        if (IsWindow(hwnd)) {  // Check if window still exists
            SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, 
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
    g_modified_windows.clear();
}

void toggle_topmost(HWND hwnd) {
    // Get window info for debugging
    wchar_t title[256];
    GetWindowTextW(hwnd, title, 256);
    
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    bool was_topmost = (exStyle & WS_EX_TOPMOST) != 0;
    
    // Get process info
    DWORD processId;
    GetWindowThreadProcessId(hwnd, &processId);
    
    // Try to set topmost
    BOOL result;
    if (was_topmost) {
        result = SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, 
                              SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        result = SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, 
                              SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    
    DWORD error = GetLastError();
    
    // Verify the change
    Sleep(100);
    exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    bool is_now_topmost = (exStyle & WS_EX_TOPMOST) != 0;
    
    // Build detailed debug message
    wchar_t msg[1024];
    
    if (was_topmost && !is_now_topmost) {
        swprintf(msg, 1024, L"Window removed from always on top.");
        g_modified_windows.erase(hwnd);
    } else if (!was_topmost && is_now_topmost) {
        swprintf(msg, 1024, L"Window set to always on top.");
        g_modified_windows.insert(hwnd);
    } else {
        // Failed - show debug info
        swprintf(msg, 1024, 
            L"Failed to change always on top status.\n\n"
            L"Debug Info:\n"
            L"Window: %s\n"
            L"HWND: 0x%p\n"
            L"Process ID: %lu\n"
            L"SetWindowPos returned: %s\n"
            L"GetLastError: %lu\n"
            L"Style: 0x%08X\n"
            L"ExStyle: 0x%08X\n"
            L"Was Topmost: %s\n"
            L"Is Now Topmost: %s",
            title,
            hwnd,
            processId,
            result ? L"TRUE" : L"FALSE",
            error,
            style,
            exStyle,
            was_topmost ? L"Yes" : L"No",
            is_now_topmost ? L"Yes" : L"No"
        );
        
        if (!was_topmost) {
            g_modified_windows.erase(hwnd);
        }
    }
    
    MessageBoxW(NULL, msg, L"Info", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
}

void show_window_selection_menu(HWND hwnd) {
    g_windows.clear();
    EnumWindows(enum_windows_proc, 0);
    
    if (g_windows.empty()) {
        MessageBoxW(hwnd, L"No windows found!", L"Error", MB_OK | MB_ICONERROR);
        return;
    }
    
    HMENU menu = CreatePopupMenu();
    
    // Add "Restore All" option if we have modified windows
    if (!g_modified_windows.empty()) {
        wchar_t restoreText[64];
        swprintf(restoreText, 64, L"Restore All (%zu windows)", g_modified_windows.size());
        AppendMenuW(menu, MF_STRING, IDM_RESTORE_ALL, restoreText);
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    }
    
    for (size_t i = 0; i < g_windows.size() && i < 50; i++) {
        LONG ex_style = GetWindowLong(g_windows[i].hwnd, GWL_EXSTYLE);
        bool is_topmost = (ex_style & WS_EX_TOPMOST) != 0;
        
        std::wstring menu_text = g_windows[i].title;
        
        // Mark windows we modified with a bullet
        if (g_modified_windows.count(g_windows[i].hwnd)) {
            menu_text = L"● " + menu_text;
        }
        // Mark all topmost windows with a checkmark
        else if (is_topmost) {
            menu_text = L"✓ " + menu_text;
        }
        
        AppendMenuW(menu, MF_STRING, 2000 + i, menu_text.c_str());
    }
    
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");
    
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, 
                             pt.x, pt.y, 0, hwnd, NULL);
    
    if (cmd == IDM_RESTORE_ALL) {
        restore_all_windows();
        MessageBoxW(NULL, L"All windows restored to normal.", L"Info", 
                    MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
    } else if (cmd >= 2000 && cmd < 2000 + (int)g_windows.size()) {
        toggle_topmost(g_windows[cmd - 2000].hwnd);
    } else if (cmd == IDM_EXIT) {
        restore_all_windows();
        PostQuitMessage(0);
    }
    
    DestroyMenu(menu);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE hInstance = ((LPCREATESTRUCT)lParam)->hInstance;
            
            // Load custom icon from resources
            g_hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TRAYICON));
            if (g_hIcon == NULL) {
                // Fallback to default icon if resource not found
                g_hIcon = LoadIcon(NULL, IDI_APPLICATION);
            }
            
            nid.cbSize = sizeof(NOTIFYICONDATAW);
            nid.hWnd = hwnd;
            nid.uID = TRAY_ICON_ID;
            nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            nid.uCallbackMessage = WM_TRAYICON;
            nid.hIcon = g_hIcon;
            wcscpy_s(nid.szTip, L"Always On Top Manager");
            Shell_NotifyIconW(NIM_ADD, &nid);
            break;
        }
            
        case WM_TRAYICON:
            if (lParam == WM_LBUTTONUP || lParam == WM_RBUTTONUP) {
                show_window_selection_menu(hwnd);
            }
            break;
        
        case WM_CLOSE:
            restore_all_windows();
            DestroyWindow(hwnd);
            break;
            
        case WM_DESTROY:
            Shell_NotifyIconW(NIM_DELETE, &nid);
            PostQuitMessage(0);
            break;
            
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPSTR lpCmdLine, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"AlwaysOnTopClass";
    
    // Prevent multiple instances
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"AlwaysOnTopManager_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"Always On Top Manager is already running!", 
                    L"Info", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    
    WNDCLASSW wc = {};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TRAYICON));
    
    RegisterClassW(&wc);
    
    g_hwnd = CreateWindowExW(
        0, CLASS_NAME, L"Always On Top Manager",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, hInstance, NULL
    );
    
    if (g_hwnd == NULL) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
        return 0;
    }
    
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    
    return 0;
}
