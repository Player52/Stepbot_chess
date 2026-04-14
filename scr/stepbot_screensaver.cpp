/*
 * Stepbot Screensaver — stepbot_screensaver.cpp
 *
 * Build (MSYS2 MinGW x64):
 *   g++ -O2 -std=c++17 -mwindows -o Stepbot.scr stepbot_screensaver.cpp -lgdi32 -luser32 -lkernel32
 *
 * Install:
 *   Copy Stepbot.scr to C:\Windows\System32\
 *   Right-click desktop → Personalise → Screen saver → pick "Stepbot"
 *
 * Windows passes these args to screensavers:
 *   (none) or /p  → preview (we just show branding)
 *   /s            → full screensaver mode
 *   /c            → config dialog (we show a simple notice)
 */

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <ctime>

// ── Constants ────────────────────────────────────────────────────────────────

static const wchar_t* LIVE_FILE   = L"C:\\temp\\stepbot_live.txt";
static const wchar_t* CLASS_NAME  = L"StepbotScreensaver";
static const int      TIMER_MS    = 50;    // ~20 fps tick
static const int      DISPLAY_MS  = 6000; // ms each mode is shown before fading
static const int      FADE_MS     = 1200; // ms for a full fade in/out

// ── Globals ───────────────────────────────────────────────────────────────────

static HWND   g_hwnd       = nullptr;
static bool   g_preview    = false;
static bool   g_quitting   = false;

// Fade state
enum class Mode { Branding, Game, Split };
static Mode   g_mode       = Mode::Branding;
static Mode   g_nextMode   = Mode::Branding;
static double g_alpha      = 0.0;   // 0..1 current panel alpha
static bool   g_fadingOut  = false;
static DWORD  g_modeTimer  = 0;     // when we entered current mode (ms)
static DWORD  g_fadeStart  = 0;

// Live data from file
struct LiveData {
    std::wstring board[8];      // 8 rows of the ASCII board
    std::wstring depth, score, nodes, bestMove, fen;
    bool valid = false;
};
static LiveData g_live;
static DWORD g_lastFilePoll = 0;

// Floating particles
struct Particle { float x, y, vx, vy, a, radius; };
static std::vector<Particle> g_particles;

// ── Helpers ───────────────────────────────────────────────────────────────────

static DWORD Now() { return GetTickCount(); }

static bool StepbotRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"stepbot.exe") == 0) { found = true; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

static bool LiveFileExists() {
    return GetFileAttributesW(LIVE_FILE) != INVALID_FILE_ATTRIBUTES;
}

// Parse the live file written by Stepbot.
// Expected format (Stepbot writes this):
//   BOARD:<8 lines of board>
//   DEPTH:<n>
//   SCORE:<cp>
//   NODES:<n>
//   BESTMOVE:<move>
static void PollLiveFile() {
    if (!LiveFileExists()) { g_live.valid = false; return; }

    std::ifstream f(LIVE_FILE);
    if (!f.is_open()) { g_live.valid = false; return; }

    LiveData ld;
    std::string line;
    int boardRow = -1;

    while (std::getline(f, line)) {
        // Trim \r
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.rfind("BOARD_START", 0) == 0) { boardRow = 0; continue; }
        if (line.rfind("BOARD_END",   0) == 0) { boardRow = -1; continue; }
        if (boardRow >= 0 && boardRow < 8) {
            ld.board[boardRow] = std::wstring(line.begin(), line.end());
            boardRow++;
            continue;
        }
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        auto ws = [](std::string& s){ while(!s.empty()&&s[0]==' ')s.erase(s.begin()); };
        ws(val);
        std::wstring wval(val.begin(), val.end());
        if      (key == "DEPTH")    ld.depth    = wval;
        else if (key == "SCORE")    ld.score    = wval;
        else if (key == "NODES")    ld.nodes    = wval;
        else if (key == "BESTMOVE") ld.bestMove = wval;
    }
    ld.valid = (boardRow == -1); // only valid if we saw BOARD_END
    if (ld.valid) g_live = ld;
}

// ── Drawing ───────────────────────────────────────────────────────────────────

static void DrawCentredText(HDC hdc, const wchar_t* text, RECT rc,
                             int sizePt, bool bold, COLORREF col, int alpha255,
                             int tracking = 0)
{
    if (alpha255 <= 0) return;
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(sizePt, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = ANTIALIASED_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Courier New");
    HFONT font = CreateFontIndirectW(&lf);
    HFONT old  = (HFONT)SelectObject(hdc, font);

    // Manual alpha blend via per-pixel DIB is complex for GDI; we approximate
    // by blending text colour toward black with alpha.
    auto blend = [](COLORREF c, int a) -> COLORREF {
        int r = (GetRValue(c) * a) / 255;
        int g = (GetGValue(c) * a) / 255;
        int b = (GetBValue(c) * a) / 255;
        return RGB(r, g, b);
    };

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, blend(col, alpha255));

    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, old);
    DeleteObject(font);
}

static void DrawLeftText(HDC hdc, const wchar_t* text, int x, int y,
                          int sizePt, bool bold, COLORREF col, int alpha255)
{
    if (alpha255 <= 0) return;
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(sizePt, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = ANTIALIASED_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Courier New");
    HFONT font = CreateFontIndirectW(&lf);
    HFONT old  = (HFONT)SelectObject(hdc, font);

    int r = (GetRValue(col) * alpha255) / 255;
    int g = (GetGValue(col) * alpha255) / 255;
    int b = (GetBValue(col) * alpha255) / 255;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(r,g,b));
    TextOutW(hdc, x, y, text, (int)wcslen(text));

    SelectObject(hdc, old);
    DeleteObject(font);
}

// Draw the chess board from g_live.board[]
// squareSize in pixels, origin (ox, oy)
static void DrawBoard(HDC hdc, int ox, int oy, int squareSize, int alpha255) {
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            bool light = (row + col) % 2 == 0;
            int  bx    = ox + col * squareSize;
            int  by    = oy + row * squareSize;

            // Square colour — blend toward black
            auto bCol = [&](int base) -> int { return (base * alpha255) / 255; };
            COLORREF sqCol = light
                ? RGB(bCol(42),  bCol(42),  bCol(42))   // dark grey
                : RGB(bCol(15),  bCol(15),  bCol(15));  // near black
            HBRUSH br = CreateSolidBrush(sqCol);
            RECT sr{ bx, by, bx+squareSize, by+squareSize };
            FillRect(hdc, &sr, br);
            DeleteObject(br);

            // Piece
            if (!g_live.valid) continue;
            const std::wstring& rowStr = g_live.board[row];
            // Board rows are like "  r . . . k . . r  " with pieces separated by spaces
            // We store the raw line; extract column col character
            // Each col is 2 chars wide (piece + space) after a leading 2-char rank label
            // Actual format depends on what Stepbot writes — see stepbot_live_writer.cpp
            // Here we just render whatever char is at position col*2+2 (rank label = 2 chars)
            wchar_t piece = L' ';
            int idx = 2 + col * 2; // skip rank label "8 ", each square is "X "
            if ((int)rowStr.size() > idx) piece = rowStr[idx];

            if (piece != L'.' && piece != L' ') {
                wchar_t buf[2] = { piece, 0 };
                RECT pr{ bx, by, bx+squareSize, by+squareSize };
                int fontSize = squareSize <= 36 ? 14 : 20;
                DrawCentredText(hdc, buf, pr, fontSize, true,
                    isupper(piece) ? RGB(255,255,255) : RGB(180,180,180),
                    alpha255);
            }
        }
    }
    // Border
    HPEN pen = CreatePen(PS_SOLID, 1,
        RGB((40*alpha255)/255, (40*alpha255)/255, (40*alpha255)/255));
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, ox, oy, ox+squareSize*8+1, oy+squareSize*8+1);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void DrawInfoRow(HDC hdc, int cx, int y, int alpha255, bool small_ = false) {
    if (!g_live.valid) return;
    int sz1 = small_ ? 14 : 18;
    int sz2 = small_ ? 7  : 9;
    int gap  = small_ ? 80 : 110;

    struct { const wchar_t* lbl; std::wstring val; } items[] = {
        { L"DEPTH",     g_live.depth    },
        { L"SCORE",     g_live.score    },
        { L"NODES/S",   g_live.nodes    },
        { L"BEST MOVE", g_live.bestMove },
    };
    int n = 4;
    int totalW = gap * (n-1);
    int startX = cx - totalW/2;

    for (int i = 0; i < n; i++) {
        int x = startX + i * gap;
        RECT r{ x-gap/2, y, x+gap/2, y+40 };
        DrawCentredText(hdc, items[i].val.c_str(), r, sz1, true,
            RGB(255,255,255), alpha255);
        RECT r2{ x-gap/2, y+40, x+gap/2, y+55 };
        DrawCentredText(hdc, items[i].lbl, r2, sz2, false,
            RGB(80,80,80), alpha255);
    }
}

// Thinking bar — animated pulse
static void DrawThinkBar(HDC hdc, int cx, int y, int w, int alpha255) {
    // Background track
    RECT tr{ cx-w/2, y, cx+w/2, y+2 };
    int bg = (20*alpha255)/255;
    HBRUSH bgbr = CreateSolidBrush(RGB(bg,bg,bg));
    FillRect(hdc, &tr, bgbr);
    DeleteObject(bgbr);

    // Animated fill — position based on time
    double t = fmod((double)Now() / 1800.0, 1.0); // 0..1
    int fillW = (int)(w * 0.45);
    int pos   = (int)((w + fillW) * t) - fillW;
    pos = std::max(0, std::min(w - fillW, pos));

    int fx = cx - w/2 + pos;
    RECT fr{ fx, y, fx + fillW, y+2 };
    int fc = (220*alpha255)/255;
    HBRUSH fbr = CreateSolidBrush(RGB(fc,fc,fc));
    FillRect(hdc, &fr, fbr);
    DeleteObject(fbr);
}

static void DrawParticles(HDC hdc, int alpha255) {
    for (auto& p : g_particles) {
        int a = (int)(p.a * alpha255);
        if (a <= 5) continue;
        int r = (int)(p.radius * 1.5f);
        if (r < 1) r = 1;
        HBRUSH br = CreateSolidBrush(RGB(a,a,a));
        HPEN   pn = CreatePen(PS_NULL, 0, 0);
        HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
        HPEN   op = (HPEN)  SelectObject(hdc, pn);
        Ellipse(hdc, (int)p.x-r, (int)p.y-r, (int)p.x+r, (int)p.y+r);
        SelectObject(hdc, ob); SelectObject(hdc, op);
        DeleteObject(br); DeleteObject(pn);
    }
}

// ── Panel renderers ───────────────────────────────────────────────────────────

static void RenderBranding(HDC hdc, RECT& rc, int alpha255) {
    int cx = (rc.right  + rc.left) / 2;
    int cy = (rc.bottom + rc.top)  / 2;
    int w  =  rc.right  - rc.left;

    // Knight glyph
    RECT kr{ cx-120, cy-160, cx+120, cy-60 };
    DrawCentredText(hdc, L"\u265E", kr, 52, true, RGB(255,255,255), alpha255);

    // STEPBOT
    RECT tr{ cx-300, cy-60, cx+300, cy+20 };
    DrawCentredText(hdc, L"STEPBOT", tr, 40, true, RGB(255,255,255), alpha255);

    // Subtitle
    RECT sr{ cx-300, cy+28, cx+300, cy+58 };
    DrawCentredText(hdc, L"C++ CHESS ENGINE", sr, 11, false, RGB(70,70,70), alpha255);

    // Version / status
    bool running = g_live.valid;
    const wchar_t* status = running ? L"NNUE TRAINING IN PROGRESS" : L"v2.4";
    RECT vr{ cx-300, cy+80, cx+300, cy+105 };
    DrawCentredText(hdc, status, vr, 9, false, RGB(40,40,40), alpha255);

    // Thinking bar if training
    if (running)
        DrawThinkBar(hdc, cx, cy+110, 240, alpha255);
}

static void RenderGame(HDC hdc, RECT& rc, int alpha255) {
    int cx = (rc.right  + rc.left) / 2;
    int cy = (rc.bottom + rc.top)  / 2;
    int sq = 48;
    int bw = sq * 8;
    int bh = sq * 8;
    int ox = cx - bw/2;
    int oy = cy - bh/2 - 30;

    DrawBoard(hdc, ox, oy, sq, alpha255);

    // Info row below board
    DrawInfoRow(hdc, cx, oy + bh + 20, alpha255);
    DrawThinkBar(hdc, cx, oy + bh + 80, 320, alpha255);
}

static void RenderSplit(HDC hdc, RECT& rc, int alpha255) {
    int w  = rc.right  - rc.left;
    int h  = rc.bottom - rc.top;
    int cx = (rc.right  + rc.left) / 2;
    int cy = (rc.bottom + rc.top)  / 2;

    // Divider
    {
        int dv = (10*alpha255)/255;
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(dv,dv,dv));
        HPEN old = (HPEN)SelectObject(hdc, pen);
        MoveToEx(hdc, cx, rc.top+40, nullptr);
        LineTo(hdc, cx, rc.bottom-40);
        SelectObject(hdc, old);
        DeleteObject(pen);
    }

    // LEFT — branding
    int lcx = (rc.left + cx) / 2;
    RECT kr{ lcx-80, cy-120, lcx+80, cy-50 };
    DrawCentredText(hdc, L"\u265E", kr, 36, true, RGB(255,255,255), alpha255);
    RECT tr{ lcx-160, cy-48, lcx+160, cy+10 };
    DrawCentredText(hdc, L"STEPBOT", tr, 26, true, RGB(255,255,255), alpha255);
    RECT sr{ lcx-160, cy+14, lcx+160, cy+36 };
    DrawCentredText(hdc, L"C++ CHESS ENGINE", sr, 9, false, RGB(60,60,60), alpha255);

    // Pulse dot
    {
        int dotA = (int)(((sin((double)Now()/600.0)*0.5+0.5) * 80 + 30) * alpha255 / 255);
        HBRUSH br = CreateSolidBrush(RGB(0, dotA, 0));
        HPEN   pn = CreatePen(PS_NULL, 0, 0);
        HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
        HPEN   op = (HPEN)  SelectObject(hdc, pn);
        int dx = lcx, dy = cy + 52;
        Ellipse(hdc, dx-5, dy-5, dx+5, dy+5);
        SelectObject(hdc, ob); SelectObject(hdc, op);
        DeleteObject(br); DeleteObject(pn);

        RECT dr{ lcx-80, dy+10, lcx+80, dy+26 };
        DrawCentredText(hdc, L"NNUE TRAINING ACTIVE", dr, 7, false, RGB(30,30,30), alpha255);
    }

    // RIGHT — mini board
    int rcx = (cx + rc.right) / 2;
    int sq  = 34;
    int bw  = sq * 8;
    int bh  = sq * 8;
    int ox  = rcx - bw/2;
    int oy  = cy  - bh/2 - 20;

    DrawBoard(hdc, ox, oy, sq, alpha255);
    DrawInfoRow(hdc, rcx, oy + bh + 14, alpha255, true);
}

// ── Main paint ────────────────────────────────────────────────────────────────

static void OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right, h = rc.bottom;

    // Double-buffer
    HDC     memDC  = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    // Black background
    HBRUSH black = CreateSolidBrush(RGB(0,0,0));
    FillRect(memDC, &rc, black);
    DeleteObject(black);

    // Particles (always visible, low alpha)
    DrawParticles(memDC, 255);

    int alpha255 = (int)(g_alpha * 255.0);
    alpha255 = std::max(0, std::min(255, alpha255));

    switch (g_mode) {
        case Mode::Branding: RenderBranding(memDC, rc, alpha255); break;
        case Mode::Game:     RenderGame    (memDC, rc, alpha255); break;
        case Mode::Split:    RenderSplit   (memDC, rc, alpha255); break;
    }

    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}

// ── Timer tick ────────────────────────────────────────────────────────────────

static Mode ChooseNextMode(bool engineRunning) {
    if (!engineRunning) return Mode::Branding;
    // Cycle: Game → Split → Game → ...
    if (g_mode == Mode::Game)     return Mode::Split;
    if (g_mode == Mode::Split)    return Mode::Game;
    return Mode::Game; // from Branding → Game
}

static void OnTimer(HWND hwnd) {
    DWORD now = Now();

    // Poll file every 500ms
    if (now - g_lastFilePoll > 500) {
        g_lastFilePoll = now;
        PollLiveFile();
    }

    bool engineRunning = StepbotRunning() && LiveFileExists();

    // Update particles
    RECT rc; GetClientRect(hwnd, &rc);
    for (auto& p : g_particles) {
        p.x += p.vx; p.y += p.vy;
        if (p.x < 0) p.x = (float)rc.right;
        if (p.x > rc.right)  p.x = 0;
        if (p.y < 0) p.y = (float)rc.bottom;
        if (p.y > rc.bottom) p.y = 0;
    }

    // Fade logic
    if (!g_fadingOut) {
        // Fading in
        double elapsed = (double)(now - g_fadeStart);
        g_alpha = std::min(1.0, elapsed / FADE_MS);

        // Check if we should start fading out
        if (g_alpha >= 1.0 && (now - g_modeTimer) > (DWORD)DISPLAY_MS) {
            g_fadingOut  = true;
            g_fadeStart  = now;
            g_nextMode   = ChooseNextMode(engineRunning);
        }

        // If engine state changed while displaying branding, trigger switch sooner
        if (g_mode == Mode::Branding && engineRunning && g_alpha >= 1.0) {
            if ((now - g_modeTimer) > 2000) {
                g_fadingOut = true;
                g_fadeStart = now;
                g_nextMode  = Mode::Game;
            }
        }
        if ((g_mode == Mode::Game || g_mode == Mode::Split) && !engineRunning && g_alpha >= 1.0) {
            g_fadingOut = true;
            g_fadeStart = now;
            g_nextMode  = Mode::Branding;
        }

    } else {
        // Fading out
        double elapsed = (double)(now - g_fadeStart);
        g_alpha = std::max(0.0, 1.0 - elapsed / FADE_MS);

        if (g_alpha <= 0.0) {
            // Switch mode
            g_mode      = g_nextMode;
            g_fadingOut = false;
            g_fadeStart = now;
            g_modeTimer = now;
            g_alpha     = 0.0;
        }
    }

    InvalidateRect(hwnd, nullptr, FALSE);
}

// ── Window procedure ──────────────────────────────────────────────────────────

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            SetTimer(hwnd, 1, TIMER_MS, nullptr);
            g_modeTimer = g_fadeStart = Now();
            return 0;

        case WM_TIMER:
            OnTimer(hwnd);
            return 0;

        case WM_PAINT:
            OnPaint(hwnd);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, 1);
            PostQuitMessage(0);
            return 0;

        // Quit on any input (standard screensaver behaviour)
        case WM_KEYDOWN:
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            if (!g_preview) {
                PostMessage(hwnd, WM_CLOSE, 0, 0);
            }
            return 0;

        case WM_MOUSEMOVE:
            // Only quit on significant movement (ignore first WM_MOUSEMOVE)
            if (!g_preview) {
                static POINT last = { -1, -1 };
                POINT cur = { LOWORD(lp), HIWORD(lp) };
                if (last.x != -1 && (abs(cur.x-last.x)>4 || abs(cur.y-last.y)>4)) {
                    PostMessage(hwnd, WM_CLOSE, 0, 0);
                }
                last = cur;
            }
            return 0;

        case WM_SETCURSOR:
            if (!g_preview) SetCursor(nullptr); // hide cursor
            return TRUE;

        case WM_SYSCOMMAND:
            if (wp == SC_SCREENSAVE || wp == SC_CLOSE) return 0;
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Entry point ───────────────────────────────────────────────────────────────

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR cmdLine, int) {
    // Parse args
    std::wstring args = cmdLine ? cmdLine : L"";
    bool doConfig  = (args.find(L"/c") != std::wstring::npos ||
                      args.find(L"/C") != std::wstring::npos);
    bool doPreview = (args.find(L"/p") != std::wstring::npos ||
                      args.find(L"/P") != std::wstring::npos);
    bool doSaver   = (args.find(L"/s") != std::wstring::npos ||
                      args.find(L"/S") != std::wstring::npos);

    if (doConfig) {
        MessageBoxW(nullptr,
            L"Stepbot Screensaver\n\n"
            L"Displays Stepbot branding when idle.\n"
            L"When stepbot.exe is running and C:\\temp\\stepbot_live.txt\n"
            L"is present, shows the live engine output.\n\n"
            L"No configuration needed.",
            L"Stepbot Screensaver", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    g_preview = doPreview;

    // Seed particles
    srand((unsigned)time(nullptr));
    for (int i = 0; i < 80; i++) {
        Particle p;
        // Will be sized properly once we have a window; use big defaults
        p.x  = (float)(rand() % 1920);
        p.y  = (float)(rand() % 1080);
        p.vx = ((float)(rand()%100) - 50) / 600.f;
        p.vy = ((float)(rand()%100) - 50) / 600.f;
        p.a  = (float)(rand()%40 + 10);
        p.radius = (float)(rand()%3  +  1);
        g_particles.push_back(p);
    }

    // Register window class
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = nullptr;
    RegisterClassExW(&wc);

    HWND hwnd;
    if (doPreview) {
        // Tiny preview window in the screensaver settings dialog
        HWND parent = nullptr;
        // /p <HWND> — extract parent handle
        size_t sp = args.find(L' ');
        if (sp != std::wstring::npos) {
            parent = (HWND)(LONG_PTR)wcstoll(args.c_str() + sp + 1, nullptr, 10);
        }
        RECT pr{};
        if (parent) GetClientRect(parent, &pr);
        hwnd = CreateWindowExW(0, CLASS_NAME, L"Stepbot",
            WS_CHILD | WS_VISIBLE,
            0, 0, pr.right, pr.bottom,
            parent, nullptr, hInst, nullptr);
    } else {
        // Full-screen on every monitor
        int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        hwnd = CreateWindowExW(
            WS_EX_TOPMOST,
            CLASS_NAME, L"Stepbot",
            WS_POPUP | WS_VISIBLE,
            x, y, w, h,
            nullptr, nullptr, hInst, nullptr);
        ShowCursor(FALSE);
    }

    if (!hwnd) return 1;
    g_hwnd = hwnd;

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
