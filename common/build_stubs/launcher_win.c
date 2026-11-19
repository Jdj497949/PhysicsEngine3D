// ============ 打包发布产物：游戏启动器（Win32 / Linux 双平台） ============
// 打包面板「开始打包」后按平台编译的产物：
//   - 窗口标题 = 产品名称；窗口图标/客户区大图 = 应用头像（.ico 内嵌资源，数字 ID 1）
//   - 启动时检测同目录 assets.pak（加密资源包）并显示状态
// 编译：Windows -> x86_64-w64-mingw32-gcc launcher_win.c [app_res.o] -o 产品.exe -mwindows
//       Linux    -> gcc launcher_win.c -o 产品
// 注：Wine 沙箱无中文字体，界面文案用英文；图标资源用数字 ID 1（windres 通用）
#ifdef _WIN32
#include <windows.h>
#include <stdio.h>

static HICON g_icon;
static const char *g_pakLine = "assets.pak NOT found (no encrypted pack)";

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        // 客户区一列画大图标（应用头像，由大到小）
        if (g_icon) {
            for (int s = 128; s >= 48; s -= 8)
                DrawIconEx(dc, (rc.right - s) / 2, 30, g_icon, s, s, 0, NULL, DI_NORMAL);
        }
        SetTextColor(dc, RGB(230, 235, 245));
        SetBkMode(dc, TRANSPARENT);
        const char *lines[] = {
            "Launcher runs OK!",
            g_pakLine,
            "Window icon = app avatar (yellow ball)",
            "== PhysicsEngine3D Build & Publish =="
        };
        HFONT f = CreateFontA(20, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                              DEFAULT_PITCH, "Arial");
        HFONT old = (HFONT)SelectObject(dc, f);
        int y = rc.bottom - 120;
        for (int i = 0; i < 4; ++i) {
            int len = (int)strlen(lines[i]);
            SIZE sz; GetTextExtentPoint32A(dc, lines[i], len, &sz);
            TextOutA(dc, (rc.right - sz.cx) / 2, y, lines[i], len);
            y += 26;
        }
        SelectObject(dc, old); DeleteObject(f);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    (void)hPrev; (void)lpCmd;
    // 检测同目录加密资源包
    char pakPath[MAX_PATH];
    GetModuleFileNameA(NULL, pakPath, MAX_PATH);
    char *slash = strrchr(pakPath, '\\');
    if (slash) *(slash + 1) = '\0';
    strcat(pakPath, "assets.pak");
    FILE *pf = fopen(pakPath, "rb");
    if (pf) { fclose(pf); g_pakLine = "assets.pak is ready (encrypted pack)"; }

    // 内嵌应用头像：数字 ID 1，任意尺寸加载
    g_icon = (HICON)LoadImageA(hInst, MAKEINTRESOURCEA(1), IMAGE_ICON, 128, 128, LR_DEFAULTCOLOR);
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hIcon = (HICON)LoadImageA(hInst, MAKEINTRESOURCEA(1), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(24, 28, 36));
    wc.lpszClassName = "BuildLauncher";
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(0, "BuildLauncher", "My Horror Game",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 420, 460,
                                NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}
#else
// Linux 版：同一打包链路的原生 ELF（图标不可见属正常，按用户要求不纠结）
#include <stdio.h>
int main(void) {
    FILE *pf = fopen("assets.pak", "rb");
    if (pf) { fseek(pf, 0, SEEK_END); long kb = ftell(pf) / 1024; fclose(pf);
        printf("启动器运行正常！加密资源包 assets.pak 已就绪（%ld KB）\n", kb);
    } else {
        printf("启动器运行正常！未找到 assets.pak\n");
    }
    return 0;
}
#endif
