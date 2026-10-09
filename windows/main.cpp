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

#include "glyphs.h"

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

// ---------------------------------------------------------------- helpers

static uint32_t g_rng = 0x9E3779B9u;
static inline uint32_t Rand32() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static inline double Rand01() { return (Rand32() & 0xFFFFFF) / double(0x1000000); }

static double NowMs() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return t.QuadPart * 1000.0 / freq.QuadPart;
}

struct Surface {
    HDC dc = nullptr;
    HBITMAP bmp = nullptr, old = nullptr;
    uint32_t* px = nullptr;
    int w = 0, h = 0;
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
    void Clear(uint32_t c) { std::fill(px, px + size_t(w) * h, c); }
    void Destroy() {
        if (dc) { SelectObject(dc, old); DeleteDC(dc); DeleteObject(bmp); }
        dc = nullptr; bmp = nullptr; px = nullptr; w = h = 0;
    }
};

// Resample an 8-bit mask with bilinear filtering (upscaling) or box averaging (downscaling).
static std::vector<uint8_t> ResampleMask(const unsigned char* src, int sw, int sh, int dw, int dh) {
    std::vector<uint8_t> out(size_t(dw) * dh);
    double fx = double(sw) / dw, fy = double(sh) / dh;
    for (int y = 0; y < dh; y++) {
        for (int x = 0; x < dw; x++) {
            double v;
            if (fx > 1.0 || fy > 1.0) {
                int x0 = int(x * fx), x1 = std::max(x0 + 1, int((x + 1) * fx));
                int y0 = int(y * fy), y1 = std::max(y0 + 1, int((y + 1) * fy));
                x1 = std::min(x1, sw); y1 = std::min(y1, sh);
                double sum = 0; int n = 0;
                for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) { sum += src[yy * sw + xx]; n++; }
                v = n ? sum / n : 0;
            } else {
                double sx = (x + 0.5) * fx - 0.5, sy = (y + 0.5) * fy - 0.5;
                int ix = (int)std::floor(sx), iy = (int)std::floor(sy);
                double ax = sx - ix, ay = sy - iy;
                auto at = [&](int xx, int yy) -> double {
                    xx = std::clamp(xx, 0, sw - 1); yy = std::clamp(yy, 0, sh - 1);
                    return src[yy * sw + xx];
                };
                v = (at(ix, iy) * (1 - ax) + at(ix + 1, iy) * ax) * (1 - ay) + (at(ix, iy + 1) * (1 - ax) + at(ix + 1, iy + 1) * ax) * ay;
            }
            out[size_t(y) * dw + x] = (uint8_t)std::clamp(v + 0.5, 0.0, 255.0);
        }
    }
    return out;
}

// Separable box blur, run twice for a soft glow.
static void BoxBlur(std::vector<float>& a, int w, int h, int r) {
    if (r < 1) return;
    std::vector<float> tmp(a.size());
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < h; y++) {
            float sum = 0; int n = 0;
            const float* row = &a[size_t(y) * w];
            for (int x = -r; x <= r; x++) if (x >= 0 && x < w) { sum += row[x]; n++; }
            for (int x = 0; x < w; x++) {
                tmp[size_t(y) * w + x] = sum / (2 * r + 1);
                int out = x - r, in = x + r + 1;
                if (out >= 0) sum -= row[out];
                if (in < w) sum += row[in];
            }
        }
        for (int x = 0; x < w; x++) {
            float sum = 0;
            for (int y = -r; y <= r; y++) if (y >= 0 && y < h) sum += tmp[size_t(y) * w + x];
            for (int y = 0; y < h; y++) {
                a[size_t(y) * w + x] = sum / (2 * r + 1);
                int out = y - r, in = y + r + 1;
                if (out >= 0) sum -= tmp[size_t(out) * w + x];
                if (in < h) sum += tmp[size_t(in) * w + x];
            }
        }
    }
}

// ---------------------------------------------------------------- digital rain

struct RainGlyph { std::vector<uint8_t> mask, glow; };

struct Rain {
    struct Drop { float x, y, v; int len; bool active; std::vector<uint8_t> chars; };
    int W = 0, H = 0, s = 18, pad = 0;
    double speed = 1, density = 0.85;
    std::vector<RainGlyph> glyphs;
    std::vector<Drop> drops;

    void Init(int w, int h, int cell, double spd, double dens) {
        W = w; H = h; s = std::max(5, cell); speed = spd; density = dens;
        pad = std::max(2, s / 2);
        glyphs.clear();
        for (int i = 0; i < RAIN_COUNT; i++) {
            RainGlyph g;
            g.mask = ResampleMask(RAIN_GLYPHS[i], RAIN_CELL, RAIN_CELL, s, s);
            int gs = s + 2 * pad;
            std::vector<float> f(size_t(gs) * gs, 0.f);
            for (int y = 0; y < s; y++) for (int x = 0; x < s; x++) f[size_t(y + pad) * gs + x + pad] = g.mask[size_t(y) * s + x];
            BoxBlur(f, gs, gs, std::max(1, s / 5));
            g.glow.resize(f.size());
            for (size_t k = 0; k < f.size(); k++) g.glow[k] = (uint8_t)std::min(255.f, f[k] * 2.2f);
            glyphs.push_back(std::move(g));
        }
        int cols = (W + s - 1) / s;
        drops.assign(cols, Drop{});
        for (int i = 0; i < cols; i++) Reset(drops[i], float(i * s), false);
    }

    void Reset(Drop& d, float x, bool above) {
        d.x = x;
        d.y = above ? float(-Rand01() * H * 1.5) : float(Rand01() * H);
        d.v = float(0.55 + Rand01() * 0.9);
        d.len = 8 + int(Rand01() * 26);
        d.chars.clear();
        d.active = Rand01() < density;
    }

    static uint8_t Pick() { return uint8_t(Rand32() % RAIN_COUNT); }

    // Alpha-blend a mask in a solid colour. alpha is 0..256.
    static void Blend(Surface& fb, const uint8_t* m, int mw, int mh, int x0, int y0, int r, int g, int b, int alpha, bool additive) {
        for (int yy = 0; yy < mh; yy++) {
            int y = y0 + yy;
            if (y < 0 || y >= fb.h) continue;
            uint32_t* row = fb.px + size_t(y) * fb.w;
            const uint8_t* mr = m + size_t(yy) * mw;
            for (int xx = 0; xx < mw; xx++) {
                int x = x0 + xx;
                if (x < 0 || x >= fb.w) continue;
                int a = (mr[xx] * alpha) >> 8;
                if (!a) continue;
                uint32_t p = row[x];
                int pr = (p >> 16) & 255, pg = (p >> 8) & 255, pb = p & 255;
                if (additive) {
                    pr = std::min(255, pr + ((r * a) >> 8));
                    pg = std::min(255, pg + ((g * a) >> 8));
                    pb = std::min(255, pb + ((b * a) >> 8));
                } else {
                    pr += ((r - pr) * a) >> 8;
                    pg += ((g - pg) * a) >> 8;
                    pb += ((b - pb) * a) >> 8;
                }
                row[x] = (uint32_t(pr) << 16) | (uint32_t(pg) << 8) | uint32_t(pb);
            }
        }
    }

    void Step(Surface& fb, double dt) {
        // Fade the previous frame for the trailing glow (0.91 per 60 Hz frame).
        uint32_t f = (uint32_t)std::lround(256.0 * std::pow(0.91, dt));
        uint32_t* p = fb.px;
        size_t n = size_t(fb.w) * fb.h;
        for (size_t i = 0; i < n; i++) {
            uint32_t v = p[i];
            p[i] = ((((v & 0x00FF00FFu) * f) >> 8) & 0x00FF00FFu) | ((((v & 0x0000FF00u) * f) >> 8) & 0x0000FF00u);
        }

        for (Drop& d : drops) {
            if (!d.active) {
                if (Rand01() < 0.004 * density * dt) { Reset(d, d.x, true); d.y = float(-s); d.active = true; }
                continue;
            }
            int prevRow = (int)std::floor(d.y / s);
            d.y += float(d.v * speed * s * 0.35 * dt);
            int row = (int)std::floor(d.y / s);
            for (int r = prevRow + 1; r <= row && r <= prevRow + d.len; r++) d.chars.insert(d.chars.begin(), Pick());
            if ((int)d.chars.size() > d.len) d.chars.resize(d.len);
            if (d.chars.size() > 2 && Rand01() < 0.06 * dt) d.chars[1 + Rand32() % (d.chars.size() - 1)] = Pick();

            int nch = (int)d.chars.size();
            for (int i = 1; i < nch; i++) {
                int y = (row - i) * s;
                if (y < -s || y > H) continue;
                double a = 1.0 - double(i) / nch;
                Blend(fb, glyphs[d.chars[i]].mask.data(), s, s, (int)d.x, y, 0, 150 + int(a * 105), 30 + int(a * 35), int((0.35 + a * 0.65) * 256), false);
            }
            if (nch && row * s < H) {
                const RainGlyph& g = glyphs[d.chars[0]];
                Blend(fb, g.glow.data(), s + 2 * pad, s + 2 * pad, (int)d.x - pad, row * s - pad, 0, 255, 65, 256, true);
                Blend(fb, g.mask.data(), s, s, (int)d.x, row * s, 232, 255, 232, 256, false);
            }
            if ((row - d.len) * s > H) {
                bool on = Rand01() < density;
                Reset(d, d.x, true);
                d.active = on;
            }
        }
    }
};

// ---------------------------------------------------------------- Apple II terminal

struct Terminal {
    int ox = 0, oy = 0, PW = 0, PH = 0;  // primary monitor rect inside the window
    double fontPx = 52, adv = 20, lineH = 60;
    int padX = 0, padY = 0, cols = 40;
    std::vector<std::vector<uint8_t>> glyphs;  // scaled VT323 masks
    int gw = 0, gh = 0;
    std::vector<uint8_t> text, glow, shade;  // text layer, glow layer, scanline*vignette
    std::wstring shown;
    bool cursorOn = true, cursorSolid = false;
    bool dirty = true;

    void Init(int originX, int originY, int w, int h, double dpiScale) {
        ox = originX; oy = originY; PW = w; PH = h;
        fontPx = std::clamp(0.046 * PW, 22.0 * dpiScale, 52.0 * dpiScale);
        double k = fontPx / TERM_BASE;
        adv = TERM_ADVANCE * k;
        lineH = fontPx * 1.15;
        padX = int(PW * 0.06); padY = int(PH * 0.06);
        cols = std::max(10, int((PW - 2 * padX) / adv));
        gw = std::max(1, int(std::lround(TERM_W * k)));
        gh = std::max(1, int(std::lround(TERM_H * k)));
        glyphs.clear();
        for (int i = 0; i < TERM_COUNT; i++) glyphs.push_back(ResampleMask(TERM_GLYPHS[i], TERM_W, TERM_H, gw, gh));

        text.assign(size_t(PW) * PH, 0);
        glow.assign(size_t(PW) * PH, 0);
        shade.assign(size_t(PW) * PH, 0);
        // Scanlines: a dark band every 4 CSS px. Vignette: darken toward the tube edges.
        int period = std::max(3, int(std::lround(4 * dpiScale)));
        for (int y = 0; y < PH; y++) {
            double ny = (y + 0.5) / PH * 2 - 1;
            double scan = (y % period) == period - 2 ? 0.62 : 1.0;
            for (int x = 0; x < PW; x++) {
                double nx = (x + 0.5) / PW * 2 - 1;
                double r = std::sqrt((nx * nx + ny * ny) / 2.0);  // 1.0 at the corners
                double t = std::clamp((r - 0.55) / 0.45, 0.0, 1.0);
                double vig = 1.0 - 0.6 * t * t;
                shade[size_t(y) * PW + x] = (uint8_t)std::lround(255 * scan * vig);
            }
        }
        dirty = true;
    }

    void SetText(const std::wstring& s, bool solid) {
        if (s != shown || solid != cursorSolid) { shown = s; cursorSolid = solid; dirty = true; }
    }
    void SetCursorPhase(bool on) { if (on != cursorOn) { cursorOn = on; dirty = true; } }

    void DrawMask(const uint8_t* m, int x0, int y0) {
        for (int yy = 0; yy < gh; yy++) {
            int y = y0 + yy;
            if (y < 0 || y >= PH) continue;
            for (int xx = 0; xx < gw; xx++) {
                int x = x0 + xx;
                if (x < 0 || x >= PW) continue;
                uint8_t& t = text[size_t(y) * PW + x];
                t = std::max(t, m[size_t(yy) * gw + xx]);
            }
        }
    }

    void Rebuild() {
        std::fill(text.begin(), text.end(), 0);
        // Lay out lines, wrapping at the terminal width.
        int line = 0, col = 0;
        auto newline = [&] { line++; col = 0; };
        for (wchar_t c : shown) {
            if (c == L'\n') { newline(); continue; }
            if (col >= cols) newline();
            if (c >= TERM_FIRST && c < TERM_FIRST + TERM_COUNT && c != L' ')
                DrawMask(glyphs[c - TERM_FIRST].data(), padX + int(std::lround(col * adv)), padY + int(std::lround(line * lineH)));
            col++;
        }
        if (col >= cols) newline();
        if (cursorSolid || cursorOn) {
            int cx = padX + int(std::lround(col * adv)), cy = padY + int(std::lround(line * lineH + fontPx * 0.12));
            int cw = int(fontPx * 0.5), chh = int(fontPx * 0.95);
            for (int y = std::max(0, cy); y < std::min(PH, cy + chh); y++)
                for (int x = std::max(0, cx); x < std::min(PW, cx + cw); x++) text[size_t(y) * PW + x] = 255;
        }
        // Phosphor glow: blur a quarter-resolution copy at two radii, then scale it back up.
        int lw = std::max(1, PW / 4), lh = std::max(1, PH / 4);
        std::vector<float> lo(size_t(lw) * lh, 0.f);
        for (int y = 0; y < lh; y++)
            for (int x = 0; x < lw; x++) {
                float sum = 0;
                for (int yy = 0; yy < 4; yy++) for (int xx = 0; xx < 4; xx++) {
                    int sx = x * 4 + xx, sy = y * 4 + yy;
                    if (sx < PW && sy < PH) sum += text[size_t(sy) * PW + sx];
                }
                lo[size_t(y) * lw + x] = sum / 16.f;
            }
        std::vector<float> wide = lo;
        BoxBlur(lo, lw, lh, std::max(1, int(fontPx / 24)));
        BoxBlur(wide, lw, lh, std::max(2, int(fontPx / 9)));
        std::vector<int> mix(lo.size());
        for (size_t i = 0; i < lo.size(); i++) mix[i] = (int)std::min(255.f, lo[i] * 1.1f + wide[i] * 0.9f);
        UpsampleGlow(mix, lw, lh);
        dirty = false;
    }

    // Fast bilinear upscale of the quarter-resolution glow (fixed point, separable).
    void UpsampleGlow(const std::vector<int>& lo, int lw, int lh) {
        std::vector<int> x0(PW), wx(PW);
        for (int x = 0; x < PW; x++) {
            double sx = std::clamp((x + 0.5) / 4.0 - 0.5, 0.0, double(lw - 1));
            x0[x] = std::min((int)sx, lw - 1);
            wx[x] = int((sx - x0[x]) * 256);
        }
        std::vector<int> rows(size_t(lh) * PW);  // each low-res row stretched to full width
        for (int y = 0; y < lh; y++) {
            const int* src = &lo[size_t(y) * lw];
            int* dst = &rows[size_t(y) * PW];
            for (int x = 0; x < PW; x++) {
                int a = src[x0[x]], b = src[std::min(x0[x] + 1, lw - 1)];
                dst[x] = a + (((b - a) * wx[x]) >> 8);
            }
        }
        for (int y = 0; y < PH; y++) {
            double sy = std::clamp((y + 0.5) / 4.0 - 0.5, 0.0, double(lh - 1));
            int y0 = std::min((int)sy, lh - 1), y1 = std::min(y0 + 1, lh - 1);
            int wy = int((sy - y0) * 256);
            const int* r0 = &rows[size_t(y0) * PW];
            const int* r1 = &rows[size_t(y1) * PW];
            uint8_t* out = &glow[size_t(y) * PW];
            for (int x = 0; x < PW; x++) out[x] = (uint8_t)(r0[x] + (((r1[x] - r0[x]) * wy) >> 8));
        }
    }

    void Render(Surface& fb, double flicker) {
        if (dirty) Rebuild();
        fb.Clear(0);
        const int bgR = 2, bgG = 10, bgB = 3;      // tube black
        const int fgR = 57, fgG = 255, fgB = 90;   // Apple II monitor green
        int fl = int(flicker * 256);
        for (int y = 0; y < PH; y++) {
            int fy = oy + y;
            if (fy < 0 || fy >= fb.h) continue;
            uint32_t* row = fb.px + size_t(fy) * fb.w;
            const uint8_t* t = &text[size_t(y) * PW];
            const uint8_t* g = &glow[size_t(y) * PW];
            const uint8_t* sh = &shade[size_t(y) * PW];
            for (int x = 0; x < PW; x++) {
                int fx = ox + x;
                if (fx < 0 || fx >= fb.w) continue;
                int v = std::min(255, t[x] + ((g[x] * 150) >> 8));
                int m = (sh[x] * fl) >> 8;  // 0..255
                int r = ((bgR + ((fgR * v) >> 8)) * m) >> 8;
                int gg = ((bgG + ((fgG * v) >> 8)) * m) >> 8;
                int b = ((bgB + ((fgB * v) >> 8)) * m) >> 8;
                // White-hot core where the text is brightest, like the CSS text-shadow stack.
                int core = (t[x] * 24) >> 8;
                r = std::min(255, r + core); b = std::min(255, b + core / 2);
                row[fx] = (uint32_t(r) << 16) | (uint32_t(std::min(255, gg)) << 8) | uint32_t(b);
            }
        }
    }
};

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
    SetTimer(g.hwnd, 1, 16, nullptr);

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
