#include <windows.h>
#include <stdio.h>

#define APPNAME "HELLO_WIN"

char szAppName[] = APPNAME; // The name of this application
char szTitle[] = APPNAME; // The title bar text

void CenterWindow(HWND hWnd);

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        CenterWindow(hwnd);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_RBUTTONUP:
        DestroyWindow(hwnd);
        break;
    case WM_KEYDOWN:
        if (VK_ESCAPE == wParam)
            DestroyWindow(hwnd);
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc;
        RECT rc;
        hdc = BeginPaint(hwnd, &ps);

        GetClientRect(hwnd, &rc);
        SetTextColor(hdc, RGB(240, 240, 96));
        SetBkMode(hdc, TRANSPARENT);
        DrawTextA(hdc, "Hello Windows!", -1, &rc, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

        EndPaint(hwnd, &ps);
        break;
    }
    default:
        return DefWindowProcA(hwnd, message, wParam, lParam);
    }

    return 0;
}

#if 0
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
#else
int main(void)
{
#endif
    MSG msg;
    WNDCLASSA wc;
    HWND hwnd;

    // Fill in window class structure with parameters that describe
    // the main window.

    ZeroMemory(&wc, sizeof wc);
    wc.hInstance = NULL;
    wc.lpszClassName = szAppName;
    wc.lpfnWndProc = (WNDPROC)WndProc;
    wc.style = CS_DBLCLKS | CS_VREDRAW | CS_HREDRAW;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hIcon = LoadIconA(NULL, (LPCSTR)IDI_APPLICATION);
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);

    if (FALSE == RegisterClassA(&wc)) {
        return 0;
    }

    // create the browser
    hwnd = CreateWindowExA(0,
        wc.lpszClassName,
        szTitle,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        360, // CW_USEDEFAULT,
        240, // CW_USEDEFAULT,
        0,
        0,
        NULL,
        0);

    if (NULL == hwnd) {
        return 0;
    }

    // Main message loop:
    printf("Entering main loop\n");
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return msg.wParam;
}

void CenterWindow(HWND hwnd_self)
{
    HWND hwnd_parent;
    RECT rw_self, rc_parent, rw_parent;
    int xpos, ypos;

    hwnd_parent = GetParent(hwnd_self);
    if (NULL == hwnd_parent) {
        hwnd_parent = GetDesktopWindow();
    }

    GetWindowRect(hwnd_parent, &rw_parent);
    GetClientRect(hwnd_parent, &rc_parent);
    GetWindowRect(hwnd_self, &rw_self);

    xpos = rw_parent.left + (rc_parent.right + rw_self.left - rw_self.right) / 2;
    ypos = rw_parent.top + (rc_parent.bottom + rw_self.top - rw_self.bottom) / 2;

    SetWindowPos(hwnd_self, NULL, xpos, ypos, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}
