#include <windows.h>
#include <string>
#include <vector>
#define IDM_EXIT 1001
#define IDM_TOGGLE_TOPMOST 1002
#define WM_TRAYICON (WM_USER + 1)
#define TRAY_ICON_ID 1

NOTIFYICONDATAW nid;
HWND g_hwnd;
HWND g_target_window = NULL;
bool g_is_topmost = false;

struct WindowInfo {
    HWND hwnd;
    std::wstring title;
};

std::vector<WindowInfo> g_windows;

BOOL CALLBACK enum_windows_proc(HWND hwnd, LPARAM lParam) {
    if (IsWindowVisible(hwnd)) {
        wchar_t title[256];
        GetWindowTextW(hwnd, title, 256);
        if (wcslen(title) > 0) {
            g_windows.push_back({hwnd, title});
        }
    }
    return TRUE;
}

void toggle_topmost(HWND hwnd) {
    LONG ex_style = GetWindowLong(hwnd, GWL_EXSTYLE);
    bool was_topmost = (ex_style & WS_EX_TOPMOST) != 0;
    
    if (was_topmost) {
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, 
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, 
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    
    // Verify the change
    Sleep(100); // Give it a moment to apply
    ex_style = GetWindowLong(hwnd, GWL_EXSTYLE);
    bool is_now_topmost = (ex_style & WS_EX_TOPMOST) != 0;
    
    wchar_t msg[256];
    if (was_topmost && !is_now_topmost) {
        swprintf(msg, 256, L"Window removed from always on top.");
    } else if (!was_topmost && is_now_topmost) {
        swprintf(msg, 256, L"Window set to always on top.");
    } else if (!was_topmost && !is_now_topmost) {
        swprintf(msg, 256, L"Failed to set window to always on top.\nThe window may not allow this change.");
    } else {
        swprintf(msg, 256, L"Failed to remove always on top status.");
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
    for (size_t i = 0; i < g_windows.size() && i < 50; i++) {
        // Show current topmost status
        LONG ex_style = GetWindowLong(g_windows[i].hwnd, GWL_EXSTYLE);
        bool is_topmost = (ex_style & WS_EX_TOPMOST) != 0;
        
        std::wstring menu_text = g_windows[i].title;
        if (is_topmost) {
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
    
    if (cmd >= 2000 && cmd < 2000 + (int)g_windows.size()) {
        toggle_topmost(g_windows[cmd - 2000].hwnd);
    } else if (cmd == IDM_EXIT) {
        PostQuitMessage(0);
    }
    
    DestroyMenu(menu);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            nid.cbSize = sizeof(NOTIFYICONDATAW);
            nid.hWnd = hwnd;
            nid.uID = TRAY_ICON_ID;
            nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            nid.uCallbackMessage = WM_TRAYICON;
            nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
            wcscpy_s(nid.szTip, L"Always On Top Manager");
            Shell_NotifyIconW(NIM_ADD, &nid);
            break;
            
        case WM_TRAYICON:
            if (lParam == WM_LBUTTONUP || lParam == WM_RBUTTONUP) {
                show_window_selection_menu(hwnd);
            }
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
    
    WNDCLASSW wc = {};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    
    RegisterClassW(&wc);
    
    g_hwnd = CreateWindowExW(
        0, CLASS_NAME, L"Always On Top Manager",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, hInstance, NULL
    );
    
    if (g_hwnd == NULL) {
        return 0;
    }
    
    // Don't show the main window
    // ShowWindow(g_hwnd, SW_HIDE);
    
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return 0;
}
