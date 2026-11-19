/* ============ THE CORRIDOR —— 打包产物内置的真实恐怖小游戏（Win32 + GDI 软件渲染） ============
 * 打包面板【开始打包】按平台编译的本体：第一视角走廊 + 手电筒 + 逼近的怪物 + 惊吓镜头。
 * 纯 WinAPI，无任何外部依赖；应用头像（资源 ID 1）用于窗口图标与惊吓镜头的大脸。
 * 编译：x86_64-w64-mingw32-gcc game_horror_win.c [app_res.o] -o 产品.exe -mwindows -static -lgdi32 -luser32 -lm -O2
 * 注：Wine 无中文字体，界面文案用英文；800x600 窗口模式。
 */
#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define W 800
#define H 600
#define CORRIDOR_LEN 24.0
#define HALF_W 1.5
#define CEIL_H 2.4
#define EYE 1.6
#define FOCAL 600.0

static BITMAPINFO g_bi;
static unsigned *g_fb = NULL;
static HICON g_icon;
static HBITMAP g_iconBmp = NULL;

/* 游戏状态 */
static int g_started = 0, g_over = 0, g_flash = 1, g_fHeld = 0;
static double g_px = 0.0, g_pz = 0.6;          /* 玩家位置 */
static double g_time = 0.0, g_surv = 0.0, g_bat = 100.0;
static int g_mActive = 0; static double g_mz = 0.0, g_mx = 0.0;
static int g_scareT = 0;

static const char *g_pakLine = "assets.pak NOT found (no encrypted pack)";

static void resetGame(void) {
    g_started = 0; g_over = 0; g_flash = 1; g_fHeld = 0;
    g_px = 0.0; g_pz = 0.6; g_time = 0.0; g_surv = 0.0; g_bat = 100.0;
    g_mActive = 0; g_mz = 0.0; g_mx = 0.0; g_scareT = 0;
}

static void renderScene(int flickerOn) {
    const int horizon = H / 2;
    for (int sy = 0; sy < H; ++sy) {
        double dy = (sy - horizon) / FOCAL;
        unsigned *row = g_fb + sy * W;
        for (int sx = 0; sx < W; ++sx) {
            double dx = (sx - W / 2.0) / FOCAL;
            double il = 1.0 / sqrt(dx * dx + dy * dy + 1.0);
            double rx = dx * il, ry = dy * il, rz = il;
            double t = 1e9, wx = 0, wy = 0, wz = 0;
            int what = 0;                     /* 0地板 1天花板 2左墙 3右墙 4尽头墙 */
            if (ry < -1e-6) {
                double tt = (0 - EYE) / ry, X = g_px + rx * tt, Z = g_pz + rz * tt;
                if (Z > g_pz - 0.1 && Z < CORRIDOR_LEN && X > -1.6 && X < 1.6 && tt < t) { t = tt; wx = X; wy = 0; wz = Z; what = 0; }
            }
            if (ry > 1e-6) {
                double tt = (CEIL_H - EYE) / ry, X = g_px + rx * tt, Z = g_pz + rz * tt;
                if (Z > g_pz - 0.1 && Z < CORRIDOR_LEN && X > -1.6 && X < 1.6 && tt < t) { t = tt; wx = X; wy = CEIL_H; wz = Z; what = 1; }
            }
            if (rx < -1e-6) {
                double tt = (-HALF_W - g_px) / rx, Y = EYE + ry * tt, Z = g_pz + rz * tt;
                if (Y > 0 && Y < CEIL_H && Z > g_pz && Z < CORRIDOR_LEN && tt < t) { t = tt; wx = -HALF_W; wy = Y; wz = Z; what = 2; }
            }
            if (rx > 1e-6) {
                double tt = (HALF_W - g_px) / rx, Y = EYE + ry * tt, Z = g_pz + rz * tt;
                if (Y > 0 && Y < CEIL_H && Z > g_pz && Z < CORRIDOR_LEN && tt < t) { t = tt; wx = HALF_W; wy = Y; wz = Z; what = 3; }
            }
            if (rz > 1e-6) {
                double tt = (CORRIDOR_LEN - g_pz) / rz, X = g_px + rx * tt, Y = EYE + ry * tt;
                if (X > -HALF_W && X < HALF_W && Y > 0 && Y < CEIL_H && tt < t) { t = tt; wx = X; wy = Y; wz = CORRIDOR_LEN; what = 4; }
            }
            int r = 12, g = 10, b = 12;
            if (what == 0) {                  /* 棋盘格地板 */
                int c = ((int)(wx * 2) + (int)(wz * 2)) & 1;
                r = c ? 74 : 42; g = c ? 62 : 36; b = c ? 52 : 32;
            } else if (what == 1) { r = 28; g = 26; b = 30; }
            else if (what == 2 || what == 3) {/* 砖墙 */
                double u = (what == 2) ? wz : -wz;
                int row = (int)(wy / 0.35), col = (int)(u / 0.8);
                if (((col + (row & 1)) & 1)) { r = 88; g = 54; b = 42; } else { r = 60; g = 38; b = 32; }
                if (wy - row * 0.35 < 0.045) { r = 46; g = 30; b = 26; }
            } else {                          /* 尽头墙 + 黑门 */
                r = 32; g = 28; b = 32;
                if (wy < 0.95 && wy > 0.15 && fabs(wx) < 0.45) { r = 6; g = 5; b = 7; }
            }
            /* 光照：微弱环境光 + 尽头顶灯 + 手电筒锥形光 */
            double vx = wx - g_px, vy = wy - EYE, vz = wz - g_pz;
            double inv = 1.0 / sqrt(vx * vx + vy * vy + vz * vz + 1e-9);
            double ang = vz * inv;
            if (ang < 0) ang = 0;
            double L = 0.045;
            if (what == 0 || what == 1) L += 0.85 * exp(-(wz - CORRIDOR_LEN) * (wz - CORRIDOR_LEN) / 26.0)
                                       * (flickerOn ? 1.0 : 0.15);
            if (g_flash && g_bat > 0) {
                double fk = (g_bat < 25) ? (0.30 + 0.70 * ((rand() % 100) / 100.0)) : 1.0;
                L += (0.95 * pow(ang, 22.0) + 0.38 * pow(ang, 3.5)) * fk;
            }
            double fog = exp(-t * 0.09);
            L *= 0.22 + 0.78 * fog;
            if (L > 1) L = 1;
            unsigned char R = (unsigned char)(r * L), G = (unsigned char)(g * L), B = (unsigned char)(b * L);
            row[sx] = 0xFF000000u | (R << 16) | (G << 8) | B;
        }
    }
}

/* 怪物：贴在场景上的黑影 + 一对红眼（黑暗里也发亮） */
static void drawMonster(void) {
    if (!g_mActive || g_over) return;
    double dz = g_mz - g_pz;
    if (dz < 0.4) return;
    double sx = W / 2.0 + (g_mx - g_px) * FOCAL / dz;
    double feetY = H / 2.0 + (EYE - 0.0) * FOCAL / dz;
    double headY = H / 2.0 + (EYE - 1.75) * FOCAL / dz;
    double halfW = 0.32 * FOCAL / dz;
    int x0 = (int)(sx - halfW), x1 = (int)(sx + halfW);
    int y0 = (int)(headY), y1 = (int)(feetY);
    if (x0 < 0) x0 = 0; if (x1 >= W) x1 = W - 1;
    if (y0 < 0) y0 = 0; if (y1 >= H) y1 = H - 1;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            double nx = (x - sx) / halfW;
            double ny = (y - y0) / (double)(y1 - y0 + 1);
            if (nx * nx + (ny < 0.35 ? (0.35 - ny) * 2 : (ny - 0.35) * 0.6) > 1.0) continue; /* 头+身剪影 */
            g_fb[y * W + x] = 0xFF000000u | (14 << 16) | (10 << 8) | 12;
        }
    /* 红眼（微光常亮） */
    int ey = y0 + (int)((y1 - y0) * 0.12), esep = (int)(halfW * 0.36);
    int esz = (int)(halfW * 0.14) + 1;
    for (int s = -1; s <= 1; s += 2)
        for (int dy2 = 0; dy2 < esz; ++dy2)
            for (int dx2 = 0; dx2 < esz; ++dx2) {
                int xx = (int)(sx + s * esep) + dx2 - esz / 2, yy = ey + dy2;
                if (xx >= 0 && xx < W && yy >= 0 && yy < H) g_fb[yy * W + xx] = 0xFF000000u | (235 << 16) | (45 << 8) | 30;
            }
}

static void update(double dt) {
    if (!g_started) {
        if (GetAsyncKeyState(VK_SPACE) & 0x8000) { g_started = 1; g_time = 0; }
        return;
    }
    if (g_over) {
        if (g_scareT > 0) { g_scareT -= dt; return; }
        if (GetAsyncKeyState(VK_SPACE) & 0x8000) resetGame();
        return;
    }
    g_time += dt; g_surv += dt;
    if (g_flash) { g_bat -= dt * 1.15; if (g_bat <= 0) { g_bat = 0; g_flash = 0; } }
    if ((GetAsyncKeyState('F') & 0x8000) && !g_fHeld) { g_flash ^= 1; g_fHeld = 1; }
    else if (!(GetAsyncKeyState('F') & 0x8000)) g_fHeld = 0;
    double spd = 2.3 * dt, lat = 1.4 * dt;
    if (GetAsyncKeyState('W') & 0x8000) g_pz += spd;
    if (GetAsyncKeyState('S') & 0x8000) g_pz -= spd;
    if (GetAsyncKeyState('A') & 0x8000) g_px -= lat;
    if (GetAsyncKeyState('D') & 0x8000) g_px += lat;
    if (g_pz < 0.3) g_pz = 0.3; if (g_pz > CORRIDOR_LEN - 2.0) g_pz = CORRIDOR_LEN - 2.0;
    if (g_px < -1.05) g_px = -1.05; if (g_px > 1.05) g_px = 1.05;
    /* 怪物：4 秒后现身；手电关掉它就扑上来 */
    if (!g_mActive && g_time > 4.0) { g_mActive = 1; g_mz = g_pz + 8.0; g_mx = 0.0; }
    if (g_mActive) {
        g_mx += (g_px * 0.4 - g_mx) * dt * 0.5;
        g_mz -= (g_flash ? 0.32 : 1.25) * dt;
        if (g_mz - g_pz < 0.85) { g_over = 1; g_scareT = (int)(2.2 / dt) + 1; }
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER) {
        update(0.033);
        if (!g_started || g_over) memset(g_fb, 0x00, W * H * 4);
        else renderScene(rand() % 100 < 6 ? 0 : 1);   /* 顶灯偶尔抽风 */
        drawMonster();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps);
        SetDIBitsToDevice(dc, 0, 0, W, H, 0, 0, 0, H, g_fb, &g_bi, DIB_RGB_COLORS);
        if (g_iconBmp) {
            if (g_over && g_scareT > 0) {           /* 惊吓镜头：红屏 + 头像大脸 */
                RECT rc = {0, 0, W, H};
                SetDCBrushColor(dc, RGB(120, 8, 8));
                HBRUSH rb = (HBRUSH)GetStockObject(DC_BRUSH);
                FillRect(dc, &rc, rb);
                SetStretchBltMode(dc, COLORONCOLOR);
                BITMAP bm; GetObject(g_iconBmp, sizeof(bm), &bm);
                HDC mdc = CreateCompatibleDC(dc);
                HBITMAP old = (HBITMAP)SelectObject(mdc, g_iconBmp);
                StretchBlt(dc, (W - 440) / 2, (H - 460) / 2, 440, 460, mdc, 0, bm.bmHeight, bm.bmWidth, -bm.bmHeight, SRCCOPY);
                SelectObject(mdc, old); DeleteDC(mdc);
            }
        }
        SetBkMode(dc, TRANSPARENT);
        HFONT f = CreateFontA(22, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, 0, 0, 0, 0, "Arial");
        HFONT big = CreateFontA(52, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, 0, 0, 0, 0, "Arial");
        char buf[160];
        if (!g_started) {
            SetTextColor(dc, RGB(230, 225, 210));
            SelectObject(dc, big);
            TextOutA(dc, 190, 120, "T H E   C O R R I D O R", 23);
            SelectObject(dc, f);
            SetTextColor(dc, RGB(160, 150, 140));
            TextOutA(dc, 235, 330, "W/S walk   A/D strafe   F flashlight", 36);
            TextOutA(dc, 255, 365, "TURN THE LIGHT OFF AND IT COMES.", 32);
            SetTextColor(dc, RGB(230, 210, 120));
            TextOutA(dc, 300, 430, "PRESS SPACE TO START", 20);
            SetTextColor(dc, RGB(110, 105, 100));
            TextOutA(dc, 215, 520, g_pakLine, (int)strlen(g_pakLine));
        } else if (g_over) {
            if (g_scareT <= 0) {
                SetTextColor(dc, RGB(255, 60, 40));
                SelectObject(dc, big);
                TextOutA(dc, 290, 200, "GAME OVER", 9);
                SelectObject(dc, f);
                SetTextColor(dc, RGB(200, 190, 180));
                sprintf(buf, "You survived %.1f seconds.  [SPACE] retry", g_surv);
                TextOutA(dc, 190, 300, buf, (int)strlen(buf));
            }
        } else {
            SelectObject(dc, f);
            SetTextColor(dc, RGB(190, 185, 170));
            sprintf(buf, "Battery %d%%   Survived %.1fs   [F] flashlight", (int)g_bat, g_surv);
            TextOutA(dc, 20, 14, buf, (int)strlen(buf));
            RECT bar = {20, 44, 20 + (int)(2.2 * g_bat), 54};
            SetDCBrushColor(dc, RGB(g_bat < 25 ? 200 : 90, g_bat < 25 ? 40 : 160, 60));
            HBRUSH bb = (HBRUSH)GetStockObject(DC_BRUSH);
            FillRect(dc, &bar, bb);
        }
        DeleteObject(f); DeleteObject(big);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    (void)hPrev; (void)lpCmd;
    /* 同目录加密资源包检测（沿用打包产物约定） */
    char pakPath[MAX_PATH];
    GetModuleFileNameA(NULL, pakPath, MAX_PATH);
    char *slash = strrchr(pakPath, '\\');
    if (slash) *(slash + 1) = '\0';
    strcat(pakPath, "assets.pak");
    FILE *pf = fopen(pakPath, "rb");
    if (pf) { fclose(pf); g_pakLine = "assets.pak is ready (encrypted pack)"; }

    g_icon = (HICON)LoadImageA(hInst, MAKEINTRESOURCEA(1), IMAGE_ICON, 64, 64, LR_DEFAULTCOLOR);
    ICONINFO ii;
    if (g_icon && GetIconInfo(g_icon, &ii) && ii.hbmColor) g_iconBmp = ii.hbmColor;

    memset(&g_bi, 0, sizeof(g_bi));
    g_bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    g_bi.bmiHeader.biWidth = W; g_bi.bmiHeader.biHeight = -H;
    g_bi.bmiHeader.biPlanes = 1; g_bi.bmiHeader.biBitCount = 32; g_bi.bmiHeader.biCompression = BI_RGB;
    g_fb = (unsigned *)malloc(W * H * 4);

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = g_icon;
    wc.lpszClassName = "VNHorrorCorridor";
    RegisterClassA(&wc);
    RECT rr = {0, 0, W, H};
    AdjustWindowRect(&rr, WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX), FALSE);
    HWND hwnd = CreateWindowA("VNHorrorCorridor", "My Horror Game", WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX),
                              CW_USEDEFAULT, CW_USEDEFAULT, rr.right - rr.left, rr.bottom - rr.top,
                              NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, SW_SHOWNORMAL);
    SetTimer(hwnd, 1, 33, NULL);
    MSG m;
    while (GetMessage(&m, NULL, 0, 0) > 0) { TranslateMessage(&m); DispatchMessage(&m); }
    return 0;
}
#endif
