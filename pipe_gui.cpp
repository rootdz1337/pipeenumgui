#define UNICODE
#define _UNICODE

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")

// Control IDs
#define IDC_LISTVIEW    1001
#define IDC_REFRESH     1002
#define IDC_STATUS      1003
#define IDC_FILTER      1004
#define IDC_FILTER_LBL  1005
#define IDC_DETAILS     1006
#define IDC_KILL        1007
#define IDC_COPY        1008

const wchar_t* WINDOW_CLASS = L"PipeDetectorWindow";

HWND g_hWnd = nullptr;
HWND g_hListView = nullptr;
HWND g_hRefreshBtn = nullptr;
HWND g_hStatus = nullptr;
HWND g_hFilterEdit = nullptr;
HWND g_hFilterLabel = nullptr;
HWND g_hDetails = nullptr;
HWND g_hKillBtn = nullptr;
HWND g_hCopyBtn = nullptr;

// ============================================================================
// Data structure
// ============================================================================
struct PipeInfo {
    std::wstring name;
    DWORD        instances;
};

std::vector<PipeInfo> g_allPipes;

// ============================================================================
// Enumerate named pipes
// ============================================================================
std::vector<PipeInfo> EnumerateNamedPipes() {
    std::vector<PipeInfo> pipes;
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(L"\\\\.\\pipe\\*", &fd);

    if (hFind == INVALID_HANDLE_VALUE) return pipes;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 ||
            wcscmp(fd.cFileName, L"..") == 0)
            continue;

        PipeInfo pi;
        pi.name = fd.cFileName;
        pi.instances = 0;

        // Count instances by opening repeatedly
        std::wstring fullPath = L"\\\\.\\pipe\\" + pi.name;
        while (true) {
            HANDLE hPipe = CreateFileW(
                fullPath.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0, NULL, OPEN_EXISTING, 0, NULL);

            if (hPipe == INVALID_HANDLE_VALUE) break;

            pi.instances++;
            CloseHandle(hPipe);

            // Prevent infinite loop on message-mode pipes that stay open
            if (pi.instances > 100) break;
        }

        pipes.push_back(pi);
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);

    // Sort alphabetically
    std::sort(pipes.begin(), pipes.end(),
        [](const PipeInfo& a, const PipeInfo& b) {
            return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
        });

    return pipes;
}

// ============================================================================
// ListView setup
// ============================================================================
void SetupListView(HWND hList) {
    ListView_SetExtendedListViewStyle(hList,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    LVCOLUMN lvc = { 0 };
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    lvc.iSubItem = 0;
    lvc.pszText = (LPWSTR)L"#";
    lvc.cx = 45;
    ListView_InsertColumn(hList, 0, &lvc);

    lvc.iSubItem = 1;
    lvc.pszText = (LPWSTR)L"Pipe Name";
    lvc.cx = 450;
    ListView_InsertColumn(hList, 1, &lvc);

    lvc.iSubItem = 2;
    lvc.pszText = (LPWSTR)L"Instances";
    lvc.cx = 80;
    ListView_InsertColumn(hList, 2, &lvc);
}

// ============================================================================
// Populate list (with optional filter)
// ============================================================================
void PopulateList(const std::wstring& filter) {
    ListView_DeleteAllItems(g_hListView);

    std::wstring lf = filter;
    std::transform(lf.begin(), lf.end(), lf.begin(), ::towlower);

    int row = 0;
    for (const auto& p : g_allPipes) {
        if (!lf.empty()) {
            std::wstring ln = p.name;
            std::transform(ln.begin(), ln.end(), ln.begin(), ::towlower);
            if (ln.find(lf) == std::wstring::npos) continue;
        }

        LVITEM lvi = { 0 };
        lvi.mask = LVIF_TEXT;
        lvi.iItem = row;
        lvi.iSubItem = 0;

        wchar_t idx[16];
        swprintf(idx, 16, L"%d", row + 1);
        lvi.pszText = idx;
        int r = ListView_InsertItem(g_hListView, &lvi);

        ListView_SetItemText(g_hListView, r, 1, (LPWSTR)p.name.c_str());

        wchar_t inst[16];
        swprintf(inst, 16, L"%lu", p.instances);
        ListView_SetItemText(g_hListView, r, 2, inst);

        row++;
    }

    wchar_t status[128];
    swprintf(status, 128, L"Showing %d of %zu named pipe(s).",
        row, g_allPipes.size());
    SetWindowTextW(g_hStatus, status);
}

// ============================================================================
// Refresh
// ============================================================================
void RefreshPipes() {
    SetWindowTextW(g_hStatus, L"Enumerating named pipes...");
    UpdateWindow(g_hStatus);

    g_allPipes = EnumerateNamedPipes();

    wchar_t filter[256] = { 0 };
    GetWindowTextW(g_hFilterEdit, filter, 256);
    PopulateList(filter);
}

// ============================================================================
// Show details of selected pipe
// ============================================================================
void ShowSelectedDetails() {
    int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
    if (sel < 0) {
        SetWindowTextW(g_hDetails, L"(no selection)");
        return;
    }

    wchar_t name[512] = { 0 };
    ListView_GetItemText(g_hListView, sel, 1, name, 512);

    // Find in g_allPipes
    for (const auto& p : g_allPipes) {
        if (p.name == name) {
            wchar_t buf[1024];
            swprintf(buf, 1024,
                L"Name:      \\\\.\\pipe\\%s\r\n"
                L"Instances: %lu\r\n"
                L"Full path: \\\\.\\pipe\\%s",
                p.name.c_str(), p.instances, p.name.c_str());
            SetWindowTextW(g_hDetails, buf);
            return;
        }
    }
}

// ============================================================================
// WndProc
// ============================================================================
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_CREATE: {
        HINSTANCE hInst = ((LPCREATESTRUCT)lParam)->hInstance;

        // --- Top row: filter ---
        g_hFilterLabel = CreateWindowExW(
            0, L"STATIC", L"Filter:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            10, 14, 50, 22, hWnd, (HMENU)IDC_FILTER_LBL, hInst, NULL);

        g_hFilterEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            65, 10, 300, 26, hWnd, (HMENU)IDC_FILTER, hInst, NULL);

        g_hRefreshBtn = CreateWindowExW(
            0, L"BUTTON", L"Refresh",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            380, 10, 100, 26, hWnd, (HMENU)IDC_REFRESH, hInst, NULL);

        // --- ListView ---
        g_hListView = CreateWindowExW(
            0, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_BORDER,
            10, 45, 880, 360, hWnd, (HMENU)IDC_LISTVIEW, hInst, NULL);
        SetupListView(g_hListView);

        // --- Bottom buttons ---
        g_hKillBtn = CreateWindowExW(
            0, L"BUTTON", L"Copy Name",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 415, 110, 28, hWnd, (HMENU)IDC_COPY, hInst, NULL);

        g_hCopyBtn = CreateWindowExW(
            0, L"BUTTON", L"Open Pipe",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            130, 415, 110, 28, hWnd, (HMENU)IDC_KILL, hInst, NULL);

        g_hStatus = CreateWindowExW(
            0, L"STATIC", L"Ready.",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            250, 420, 640, 22, hWnd, (HMENU)IDC_STATUS, hInst, NULL);

        // --- Details box ---
        g_hDetails = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"(no selection)",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY |
            WS_VSCROLL | ES_AUTOVSCROLL,
            10, 450, 880, 70, hWnd, (HMENU)IDC_DETAILS, hInst, NULL);

        // Fonts
        HFONT hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        EnumChildWindows(hWnd, [](HWND h, LPARAM lp) -> BOOL {
            SendMessage(h, WM_SETFONT, (WPARAM)lp, TRUE);
            return TRUE;
        }, (LPARAM)hFont);

        // Monospace font for details
        HFONT hMono = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        SendMessage(g_hDetails, WM_SETFONT, (WPARAM)hMono, TRUE);

        RefreshPipes();
        return 0;
    }

    case WM_SIZE: {
        RECT rc;
        GetClientRect(hWnd, &rc);

        int margin = 10;
        int topH = 26;
        int btnH = 28;
        int detailsH = 70;
        int statusH = 22;

        int listTop = margin + topH + 9;
        int listH = rc.bottom - listTop - margin - btnH - 8 - detailsH - 8 - statusH - 4;

        // Top row
        SetWindowPos(g_hFilterLabel, NULL, margin, margin + 4, 50, 22, SWP_NOZORDER);
        SetWindowPos(g_hFilterEdit, NULL, margin + 55, margin, 300, topH, SWP_NOZORDER);
        SetWindowPos(g_hRefreshBtn, NULL, margin + 365, margin, 100, topH, SWP_NOZORDER);

        // ListView
        SetWindowPos(g_hListView, NULL,
            margin, listTop, rc.right - margin * 2, listH, SWP_NOZORDER);

        // Bottom buttons
        int btnY = listTop + listH + 8;
        SetWindowPos(g_hCopyBtn, NULL, margin, btnY, 110, btnH, SWP_NOZORDER);
        SetWindowPos(g_hKillBtn, NULL, margin + 120, btnY, 110, btnH, SWP_NOZORDER);
        SetWindowPos(g_hStatus, NULL, margin + 240, btnY + 4,
            rc.right - margin * 2 - 240, statusH, SWP_NOZORDER);

        // Details
        int detailsY = btnY + btnH + 8;
        SetWindowPos(g_hDetails, NULL, margin, detailsY,
            rc.right - margin * 2, detailsH, SWP_NOZORDER);

        // Column widths
        int totalW = rc.right - margin * 2;
        if (totalW > 200) {
            ListView_SetColumnWidth(g_hListView, 0, 45);
            ListView_SetColumnWidth(g_hListView, 1, totalW - 45 - 90 - 25);
            ListView_SetColumnWidth(g_hListView, 2, 90);
        }
        return 0;
    }

    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case IDC_REFRESH:
            RefreshPipes();
            break;

        case IDC_FILTER:
            if (HIWORD(wParam) == EN_CHANGE) {
                wchar_t filter[256] = { 0 };
                GetWindowTextW(g_hFilterEdit, filter, 256);
                PopulateList(filter);
            }
            break;

        case IDC_COPY: {
            int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
            if (sel < 0) {
                MessageBoxW(hWnd, L"Select a pipe first.",
                    L"Copy Name", MB_OK | MB_ICONINFORMATION);
                break;
            }
            wchar_t name[512] = { 0 };
            ListView_GetItemText(g_hListView, sel, 1, name, 512);
            std::wstring full = std::wstring(L"\\\\.\\pipe\\") + name;

            if (OpenClipboard(hWnd)) {
                EmptyClipboard();
                size_t sz = (full.size() + 1) * sizeof(wchar_t);
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sz);
                if (hMem) {
                    memcpy(GlobalLock(hMem), full.c_str(), sz);
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_UNICODETEXT, hMem);
                }
                CloseClipboard();
            }
            SetWindowTextW(g_hStatus, L"Pipe path copied to clipboard.");
            break;
        }

        case IDC_KILL: {
            int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
            if (sel < 0) {
                MessageBoxW(hWnd, L"Select a pipe first.",
                    L"Open Pipe", MB_OK | MB_ICONINFORMATION);
                break;
            }
            wchar_t name[512] = { 0 };
            ListView_GetItemText(g_hListView, sel, 1, name, 512);
            std::wstring full = std::wstring(L"\\\\.\\pipe\\") + name;

            // Try to open (non-blocking, with short timeout)
            HANDLE hPipe = CreateFileW(
                full.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0, NULL, OPEN_EXISTING, 0, NULL);

            if (hPipe == INVALID_HANDLE_VALUE) {
                // Try read-only access
                hPipe = CreateFileW(
                    full.c_str(),
                    GENERIC_READ,
                    0, NULL, OPEN_EXISTING, 0, NULL);
            }

            if (hPipe != INVALID_HANDLE_VALUE) {
                DWORD mode = PIPE_READMODE_MESSAGE;
                SetNamedPipeHandleState(hPipe, &mode, NULL, NULL);
                CloseHandle(hPipe);
                SetWindowTextW(g_hStatus, L"Successfully opened pipe.");
            } else {
                wchar_t buf[256];
                swprintf(buf, 256,
                    L"Could not open pipe. Error: %lu", GetLastError());
                SetWindowTextW(g_hStatus, buf);
            }
            break;
        }
        }
        return 0;
    }

    case WM_NOTIFY: {
        LPNMHDR lpnmh = (LPNMHDR)lParam;
        if (lpnmh->idFrom == IDC_LISTVIEW) {
            if (lpnmh->code == LVN_ITEMCHANGED) {
                ShowSelectedDetails();
            } else if (lpnmh->code == NM_DBLCLK) {
                ShowSelectedDetails();
                SendMessage(hWnd, WM_COMMAND, IDC_COPY, 0);
            }
        }
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================================
// WinMain
// ============================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
    PWSTR pCmdLine, int nCmdShow) {

    INITCOMMONCONTROLSEX icex = { 0 };
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = WINDOW_CLASS;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL, L"Window registration failed.",
            L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    g_hWnd = CreateWindowExW(
        0, WINDOW_CLASS,
        L"Named Pipe Detector",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 920, 600,
        NULL, NULL, hInstance, NULL);

    if (!g_hWnd) {
        MessageBoxW(NULL, L"Window creation failed.",
            L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessage(g_hWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    return (int)msg.wParam;
}
