// Matrix digital rain screensaver for Windows.
//   /s        run full screen
//   /p <hwnd> draw into the small preview box of the Screen Saver Settings dialog
//   /c[:hwnd] open the settings window (also used when started without arguments)
//
// Settings live in HKCU\Software\MatrixRainSaver. When an exit password is set, waking the
// screensaver plays the "Wake up, Neo..." sequence on an Apple II style green terminal and
// asks for the password; otherwise any input closes it like a normal screensaver.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <commctrl.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../core/matrix_core.h"

// ---------------------------------------------------------------- settings

static const wchar_t* REG_KEY = L"Software\\MatrixRainSaver";

struct Settings {
    int speed10 = 10;     // 3..25  -> 0.3x .. 2.5x
    int density100 = 85;  // 30..100
    int size = 18;        // 12..32 px at 100% scaling
    std::wstring pwHash;  // empty = no password
};

static DWORD RegReadDword(HKEY k, const wchar_t* name, DWORD def) {
    DWORD v = 0, type = 0, cb = sizeof(v);
    if (RegQueryValueExW(k, name, nullptr, &type, (BYTE*)&v, &cb) == ERROR_SUCCESS && type == REG_DWORD) return v;
    return def;
}

static Settings LoadSettings() {
    Settings s;
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &k) == ERROR_SUCCESS) {
        s.speed10 = std::clamp<int>(RegReadDword(k, L"Speed", 10), 3, 25);
        s.density100 = std::clamp<int>(RegReadDword(k, L"Density", 85), 30, 100);
        s.size = std::clamp<int>(RegReadDword(k, L"FontSize", 18), 12, 32);
        wchar_t buf[128] = {};
        DWORD cb = sizeof(buf) - sizeof(wchar_t), type = 0;
        if (RegQueryValueExW(k, L"PasswordHash", nullptr, &type, (BYTE*)buf, &cb) == ERROR_SUCCESS && type == REG_SZ)
            s.pwHash = buf;
        RegCloseKey(k);
    }
    return s;
}

static void SaveSettings(const Settings& s) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    DWORD v;
    v = s.speed10;    RegSetValueExW(k, L"Speed", 0, REG_DWORD, (BYTE*)&v, sizeof(v));
    v = s.density100; RegSetValueExW(k, L"Density", 0, REG_DWORD, (BYTE*)&v, sizeof(v));
    v = s.size;       RegSetValueExW(k, L"FontSize", 0, REG_DWORD, (BYTE*)&v, sizeof(v));
    RegSetValueExW(k, L"PasswordHash", 0, REG_SZ, (const BYTE*)s.pwHash.c_str(), DWORD((s.pwHash.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
}

// SHA-256 of "zion::" + UTF-8 password, as lowercase hex (same scheme as the web prototype).
static std::wstring HashPassword(const std::wstring& pw) {
    std::string data = "zion::";
    int n = WideCharToMultiByte(CP_UTF8, 0, pw.c_str(), (int)pw.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, pw.c_str(), (int)pw.size(), utf8.data(), n, nullptr, nullptr);
    data += utf8;

    unsigned char digest[32] = {};
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0) {
            BCryptHashData(h, (PUCHAR)data.data(), (ULONG)data.size(), 0);
            BCryptFinishHash(h, digest, sizeof(digest), 0);
            BCryptDestroyHash(h);
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    static const wchar_t* hex = L"0123456789abcdef";
    std::wstring out;
    for (unsigned char b : digest) { out += hex[b >> 4]; out += hex[b & 15]; }
    return out;
}

// ---------------------------------------------------------------- platform helpers

static double NowMs() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return t.QuadPart * 1000.0 / freq.QuadPart;
}


struct Surface : PixelBuf {
    HDC dc = nullptr;
    HBITMAP bmp = nullptr, old = nullptr;
    void Create(int W, int H) {
        Destroy();
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = W;
        bi.bmiHeader.biHeight = -H;  // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        HDC screen = GetDC(nullptr);
        bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, (void**)&px, nullptr, 0);
        dc = CreateCompatibleDC(screen);
        ReleaseDC(nullptr, screen);
        old = (HBITMAP)SelectObject(dc, bmp);
        w = W; h = H;
        Clear(0);
    }
    void Destroy() {
        if (dc) { SelectObject(dc, old); DeleteDC(dc); DeleteObject(bmp); }
        dc = nullptr; bmp = nullptr; px = nullptr; w = h = 0;
    }
};

// Resample an 8-bit mask with bilinear filtering (upscaling) or box averaging (downscaling).

// ---------------------------------------------------------------- screensaver window

enum Mode { MODE_RAIN, MODE_TERM };
enum StepKind { ST_WAIT, ST_TYPE, ST_CLEAR, ST_PROMPT, ST_EXIT };
struct Step { StepKind kind; std::wstring text; int speed; int ms; };

static const wchar_t* PROMPT = L"]PASSWORD: ";
static const UINT WM_APP_ACTIVITY = WM_APP + 1;

struct Saver {
    HWND hwnd = nullptr;
    bool preview = false;
    Settings cfg;
    Surface fb;
    Rain rain;
    Terminal term;
    Mode mode = MODE_RAIN;
    double last = 0, armedAt = 0;
    bool originSet = false;
    POINT origin = {};

    // terminal script
    std::vector<Step> steps;
    size_t stepIdx = 0;
    double stepStart = 0, nextChar = 0;
    size_t typedChars = 0;
    std::wstring prefix, screen, typed;
    bool accepting = false;
    double lastInput = 0;
    bool unlocked = false;

    bool HasPassword() const { return !cfg.pwHash.empty(); }
} g;

static HHOOK g_kbHook = nullptr;

static void Quit() {
    g.unlocked = true;
    DestroyWindow(g.hwnd);
}

static void StartSteps(std::vector<Step> s) {
    g.steps = std::move(s);
    g.stepIdx = 0;
    g.stepStart = NowMs();
    g.typedChars = 0;
    g.prefix = g.screen;
    g.nextChar = g.stepStart;
}

static void OpenTerminal() {
    if (g.mode == MODE_TERM) return;
    g.mode = MODE_TERM;
    g.term.needClear = true;
    g.accepting = false;
    g.screen.clear();
    StartSteps({
        {ST_WAIT, L"", 0, 300},
        {ST_TYPE, L"Wake up, Neo...", 45, 0},
        {ST_WAIT, L"", 0, 1100},
        {ST_CLEAR, L"", 0, 0},
        {ST_WAIT, L"", 0, 250},
        {ST_TYPE, L"Follow the white rabbit.", 45, 0},
        {ST_WAIT, L"", 0, 1100},
        {ST_CLEAR, L"", 0, 0},
        {ST_WAIT, L"", 0, 250},
        {ST_PROMPT, L"", 0, 0},
    });
}

static void BackToRain() {
    g.mode = MODE_RAIN;
    g.accepting = false;
    g.steps.clear();
    g.armedAt = NowMs();
    g.originSet = false;
    g.fb.Clear(0);
}

static void ShowPrompt() {
    g.accepting = true;
    g.typed.clear();
    g.screen = PROMPT;
    g.lastInput = NowMs();
}

static void Submit() {
    g.accepting = false;
    bool ok = HashPassword(g.typed) == g.cfg.pwHash;
    g.screen = std::wstring(PROMPT) + std::wstring(g.typed.size(), L'*') + L"\n\n";
    if (ok) StartSteps({{ST_TYPE, L"ACCESS GRANTED.", 25, 0}, {ST_WAIT, L"", 0, 600}, {ST_EXIT, L"", 0, 0}});
    else StartSteps({{ST_TYPE, L"?ACCESS DENIED", 25, 0}, {ST_WAIT, L"", 0, 800}, {ST_PROMPT, L"", 0, 0}});
    g.typed.clear();
}

// Advance the terminal script. Returns true while a TYPE step is running (solid cursor).
static bool RunSteps(double now) {
    while (g.stepIdx < g.steps.size()) {
        Step& s = g.steps[g.stepIdx];
        auto advance = [&] { g.stepIdx++; g.stepStart = now; g.typedChars = 0; g.prefix = g.screen; g.nextChar = now; };
        switch (s.kind) {
        case ST_WAIT:
            if (now - g.stepStart < s.ms) return false;
            advance(); break;
        case ST_CLEAR:
            g.screen.clear(); advance(); break;
        case ST_PROMPT:
            g.steps.clear(); ShowPrompt(); return false;
        case ST_EXIT:
            g.steps.clear(); Quit(); return false;
        case ST_TYPE:
            while (g.typedChars < s.text.size() && now >= g.nextChar) {
                wchar_t c = s.text[g.typedChars++];
                g.screen = g.prefix + s.text.substr(0, g.typedChars);
                g.nextChar += s.speed * (0.5 + Rand01()) + ((c == L',' || c == L'.') ? s.speed : 0);
            }
            if (g.typedChars < s.text.size()) return true;
            advance(); break;
        }
    }
    return false;
}

static void Frame() {
    double now = NowMs();
    double dt = std::min((now - g.last) / 16.67, 3.0);
    g.last = now;

    if (g.mode == MODE_RAIN) {
        g.rain.Step(g.fb, dt);
    } else {
        bool typing = RunSteps(now);
        if (!g.hwnd || g.unlocked) return;
        if (g.accepting) {
            g.screen = std::wstring(PROMPT) + std::wstring(g.typed.size(), L'*');
            if (now - g.lastInput > 30000) { BackToRain(); return; }
        }
        g.term.SetText(g.screen, typing);
        g.term.SetCursorPhase(fmod(now, 1060.0) < 530.0);
        g.term.Render(g.fb, 0.97 + 0.03 * Rand01());
    }
    HDC dc = GetDC(g.hwnd);
    BitBlt(dc, 0, 0, g.fb.w, g.fb.h, g.fb.dc, 0, 0, SRCCOPY);
    ReleaseDC(g.hwnd, dc);
}

static void OnActivity() {
    if (g.preview || g.mode != MODE_RAIN) return;
    if (NowMs() - g.armedAt < 1000) return;
    if (!g.HasPassword()) { Quit(); return; }
    OpenTerminal();
}

static LRESULT CALLBACK KeyboardHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        auto* k = (KBDLLHOOKSTRUCT*)lp;
        bool alt = (k->flags & LLKHF_ALTDOWN) != 0;
        bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        DWORD vk = k->vkCode;
        bool block = vk == VK_LWIN || vk == VK_RWIN || vk == VK_APPS ||
                     (alt && (vk == VK_TAB || vk == VK_ESCAPE || vk == VK_F4)) ||
                     (ctrl && vk == VK_ESCAPE);
        if (block) {
            if (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) PostMessageW(g.hwnd, WM_APP_ACTIVITY, 0, 0);
            return 1;
        }
    }
    return CallNextHookEx(g_kbHook, code, wp, lp);
}

static LRESULT CALLBACK SaverProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER:
        Frame();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (g.fb.dc) BitBlt(dc, 0, 0, g.fb.w, g.fb.h, g.fb.dc, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SETCURSOR:
        if (!g.preview) { SetCursor(nullptr); return TRUE; }
        break;
    case WM_MOUSEMOVE:
        if (!g.preview && g.mode == MODE_RAIN) {
            POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
            if (!g.originSet) { g.origin = p; g.originSet = true; }
            else if (std::abs(p.x - g.origin.x) + std::abs(p.y - g.origin.y) > 12) OnActivity();
        }
        return 0;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: case WM_MOUSEWHEEL:
    case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_APP_ACTIVITY:
        OnActivity();
        return 0;  // swallow Alt so the system menu and Alt+F4 never fire
    case WM_CHAR:
        if (g.mode == MODE_TERM && g.accepting) {
            wchar_t c = (wchar_t)wp;
            g.lastInput = NowMs();
            if (c == L'\r') Submit();
            else if (c == 8) { if (!g.typed.empty()) g.typed.pop_back(); }
            else if (c == 27) g.typed.clear();
            else if (c >= 32 && g.typed.size() < 64) g.typed += c;
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_SCREENSAVE || (wp & 0xFFF0) == SC_MONITORPOWER) return 0;
        if ((wp & 0xFFF0) == SC_CLOSE && g.HasPassword() && !g.unlocked && !g.preview) return 0;
        break;
    case WM_CLOSE:
        if (!g.preview && g.HasPassword() && !g.unlocked) return 0;
        DestroyWindow(hwnd);
        return 0;
    case WM_ACTIVATEAPP:
        if (!wp && !g.preview) {
            if (!g.HasPassword()) Quit();
            else { SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); SetForegroundWindow(hwnd); }
        }
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static double SystemDpiScale() {
    HDC dc = GetDC(nullptr);
    double s = GetDeviceCaps(dc, LOGPIXELSY) / 96.0;
    ReleaseDC(nullptr, dc);
    return s;
}

static int RunSaver(HWND parent) {
    g.preview = parent != nullptr;
    g.cfg = LoadSettings();
    g_rng ^= (uint32_t)GetTickCount() * 2654435761u;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = SaverProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MatrixRainSaver";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassW(&wc);

    int x, y, w, h;
    double dpi = SystemDpiScale();
    if (g.preview) {
        RECT rc; GetClientRect(parent, &rc);
        x = 0; y = 0; w = rc.right; h = rc.bottom;
        g.hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_CHILD | WS_VISIBLE, x, y, w, h, parent, nullptr, wc.hInstance, nullptr);
    } else {
        x = GetSystemMetrics(SM_XVIRTUALSCREEN); y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        w = GetSystemMetrics(SM_CXVIRTUALSCREEN); h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        g.hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wc.lpszClassName, L"Matrix", WS_POPUP | WS_VISIBLE, x, y, w, h,
                                 nullptr, nullptr, wc.hInstance, nullptr);
    }
    if (!g.hwnd) return 1;

    g.fb.Create(w, h);
    int cell = g.preview ? std::max(5, int(g.cfg.size * h / 600.0)) : int(std::lround(g.cfg.size * dpi));
    g.rain.Init(w, h, cell, g.cfg.speed10 / 10.0, g.cfg.density100 / 100.0);
    if (!g.preview) {
        // The terminal sits on the primary monitor, whose top-left is screen (0,0).
        g.term.Init(-x, -y, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), dpi);
        SetForegroundWindow(g.hwnd);
        SetFocus(g.hwnd);
        if (g.cfg.pwHash.size()) g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHook, wc.hInstance, 0);
    }
    g.last = NowMs();
    g.armedAt = g.last;
    timeBeginPeriod(1);
    SetTimer(g.hwnd, 1, 33, nullptr);  // 30 fps is plenty for the rain and halves the CPU cost

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    timeEndPeriod(1);
    if (g_kbHook) UnhookWindowsHookEx(g_kbHook);
    g.fb.Destroy();
    return 0;
}

// ---------------------------------------------------------------- settings window

enum {
    ID_SPEED = 101, ID_DENSITY, ID_SIZE, ID_SPEED_VAL, ID_DENSITY_VAL, ID_SIZE_VAL,
    ID_PW1, ID_PW2, ID_STATUS, ID_CLEAR, ID_OK = IDOK, ID_CANCEL = IDCANCEL
};

struct ConfigState {
    Settings s;
    HFONT font = nullptr;
    double k = 1;
    bool clearPw = false;
} cs;

static void SetText(HWND dlg, int id, const std::wstring& t) { SetDlgItemTextW(dlg, id, t.c_str()); }

static void UpdateLabels(HWND dlg) {
    int sp = (int)SendDlgItemMessageW(dlg, ID_SPEED, TBM_GETPOS, 0, 0);
    int de = (int)SendDlgItemMessageW(dlg, ID_DENSITY, TBM_GETPOS, 0, 0);
    int sz = (int)SendDlgItemMessageW(dlg, ID_SIZE, TBM_GETPOS, 0, 0);
    wchar_t b[32];
    swprintf(b, 32, L"%d.%dx", sp / 10, sp % 10); SetText(dlg, ID_SPEED_VAL, b);
    swprintf(b, 32, L"%d%%", de); SetText(dlg, ID_DENSITY_VAL, b);
    swprintf(b, 32, L"%d px", sz); SetText(dlg, ID_SIZE_VAL, b);
    bool has = !cs.s.pwHash.empty() && !cs.clearPw;
    SetText(dlg, ID_STATUS, has ? L"已设置退出密码。两栏留空则沿用原密码。"
                                : L"未设置密码：屏保启动后，移动鼠标或按键会直接退出。");
}

static HWND MakeCtl(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
    double k = cs.k;
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, int(x * k), int(y * k), int(w * k), int(h * k), parent,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)cs.font, TRUE);
    return c;
}

static LRESULT CALLBACK ConfigProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        int y = 20;
        struct Row { const wchar_t* label; int id, val, lo, hi, cur; } rows[] = {
            {L"速度", ID_SPEED, ID_SPEED_VAL, 3, 25, cs.s.speed10},
            {L"密度", ID_DENSITY, ID_DENSITY_VAL, 30, 100, cs.s.density100},
            {L"字号", ID_SIZE, ID_SIZE_VAL, 12, 32, cs.s.size},
        };
        for (auto& r : rows) {
            MakeCtl(hwnd, L"STATIC", r.label, 0, 24, y + 6, 60, 22, -1);
            HWND tb = MakeCtl(hwnd, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_HORZ | TBS_NOTICKS, 84, y, 220, 32, r.id);
            SendMessageW(tb, TBM_SETRANGE, TRUE, MAKELPARAM(r.lo, r.hi));
            SendMessageW(tb, TBM_SETPOS, TRUE, r.cur);
            MakeCtl(hwnd, L"STATIC", L"", 0, 312, y + 6, 70, 22, r.val);
            y += 42;
        }
        y += 10;
        MakeCtl(hwnd, L"STATIC", L"退出密码", 0, 24, y + 4, 80, 22, -1);
        MakeCtl(hwnd, L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_PASSWORD | ES_AUTOHSCROLL, 110, y, 272, 26, ID_PW1);
        y += 36;
        MakeCtl(hwnd, L"STATIC", L"确认密码", 0, 24, y + 4, 80, 22, -1);
        MakeCtl(hwnd, L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_PASSWORD | ES_AUTOHSCROLL, 110, y, 272, 26, ID_PW2);
        y += 36;
        MakeCtl(hwnd, L"STATIC", L"", 0, 24, y, 358, 44, ID_STATUS);
        y += 56;
        MakeCtl(hwnd, L"BUTTON", L"清除密码", WS_TABSTOP | BS_PUSHBUTTON, 24, y, 100, 30, ID_CLEAR);
        MakeCtl(hwnd, L"BUTTON", L"确定", WS_TABSTOP | BS_DEFPUSHBUTTON, 192, y, 90, 30, ID_OK);
        MakeCtl(hwnd, L"BUTTON", L"取消", WS_TABSTOP | BS_PUSHBUTTON, 292, y, 90, 30, ID_CANCEL);
        UpdateLabels(hwnd);
        return 0;
    }
    case WM_HSCROLL:
        UpdateLabels(hwnd);
        return 0;
    case DM_GETDEFID:
        return MAKELRESULT(ID_OK, DC_HASDEFID);
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_CLEAR:
            cs.clearPw = true;
            SetText(hwnd, ID_PW1, L""); SetText(hwnd, ID_PW2, L"");
            UpdateLabels(hwnd);
            return 0;
        case ID_OK: {
            wchar_t a[128] = {}, b[128] = {};
            GetDlgItemTextW(hwnd, ID_PW1, a, 128);
            GetDlgItemTextW(hwnd, ID_PW2, b, 128);
            if (wcscmp(a, b) != 0) {
                MessageBoxW(hwnd, L"两次输入的密码不一致，请重新输入。", L"Matrix 数字雨", MB_ICONWARNING);
                return 0;
            }
            cs.s.speed10 = (int)SendDlgItemMessageW(hwnd, ID_SPEED, TBM_GETPOS, 0, 0);
            cs.s.density100 = (int)SendDlgItemMessageW(hwnd, ID_DENSITY, TBM_GETPOS, 0, 0);
            cs.s.size = (int)SendDlgItemMessageW(hwnd, ID_SIZE, TBM_GETPOS, 0, 0);
            if (cs.clearPw) cs.s.pwHash.clear();
            if (a[0]) cs.s.pwHash = HashPassword(a);
            SaveSettings(cs.s);
            SecureZeroMemory(a, sizeof(a)); SecureZeroMemory(b, sizeof(b));
            DestroyWindow(hwnd);
            return 0;
        }
        case ID_CANCEL:
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int RunConfig(HWND owner) {
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    cs.s = LoadSettings();
    cs.k = SystemDpiScale();
    cs.font = CreateFontW(-int(std::lround(9 * 96 * cs.k / 72)), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");

    WNDCLASSW wc = {};
    wc.lpfnWndProc = ConfigProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MatrixRainConfig";
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);

    RECT rc = {0, 0, int(406 * cs.k), int(336 * cs.k)};
    DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP;
    AdjustWindowRect(&rc, style, FALSE);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, L"Matrix 数字雨 设置", style, x, y, w, h, owner, nullptr,
                                wc.hInstance, nullptr);
    if (!hwnd) return 1;
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    if (owner) { EnableWindow(owner, TRUE); SetForegroundWindow(owner); }
    DeleteObject(cs.font);
    return 0;
}

// ---------------------------------------------------------------- entry point

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    wchar_t mode = L'c';
    HWND hwnd = nullptr;
    if (argc > 1) {
        const wchar_t* a = argv[1];
        if (*a == L'/' || *a == L'-') a++;
        mode = (wchar_t)towlower(*a);
        const wchar_t* rest = a[0] ? a + 1 : a;
        if (*rest == L':') rest++;
        if (*rest) hwnd = (HWND)(INT_PTR)_wcstoi64(rest, nullptr, 10);
        else if (argc > 2) hwnd = (HWND)(INT_PTR)_wcstoi64(argv[2], nullptr, 10);
    }
    LocalFree(argv);

    switch (mode) {
    case L's': return RunSaver(nullptr);
    case L'p': return hwnd && IsWindow(hwnd) ? RunSaver(hwnd) : 0;
    case L'a': return 0;  // Windows 9x password dialog, not used
    default: {
        // "/c" without a handle means: owned by the foreground window (the Screen Saver Settings dialog).
        HWND owner = hwnd ? hwnd : (argc > 1 ? GetForegroundWindow() : nullptr);
        return RunConfig(owner);
    }
    }
}
