// Shared drawing code for the Matrix screensaver (Windows and macOS).
// Renders into a plain 32-bit 0x00RRGGBB pixel buffer, so each platform only has to
// put the pixels on screen and feed it time and input.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "glyphs.h"

// ---------------------------------------------------------------- helpers

static uint32_t g_rng = 0x9E3779B9u;
static inline uint32_t Rand32() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static inline double Rand01() { return (Rand32() & 0xFFFFFF) / double(0x1000000); }

struct PixelBuf {
    uint32_t* px = nullptr;
    int w = 0, h = 0;
    void Clear(uint32_t c) { std::fill(px, px + size_t(w) * h, c); }
};


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
    // Screen split into s*s tiles; each holds an upper bound on its brightest byte, so the fade
    // can skip tiles that are already black (most of the screen at any moment).
    int tw = 0, th = 0;
    std::vector<uint8_t> tiles;

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
        tw = (W + s - 1) / s; th = (H + s - 1) / s;
        tiles.assign(size_t(tw) * th, 255);
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
    static void Blend(PixelBuf& fb, const uint8_t* m, int mw, int mh, int x0, int y0, int r, int g, int b, int alpha, bool additive) {
        int xa = std::max(0, -x0), xb = std::min(mw, fb.w - x0);
        int ya = std::max(0, -y0), yb = std::min(mh, fb.h - y0);
        for (int yy = ya; yy < yb; yy++) {
            uint32_t* row = fb.px + size_t(y0 + yy) * fb.w + x0;
            const uint8_t* mr = m + size_t(yy) * mw;
            for (int xx = xa; xx < xb; xx++) {
                if (!mr[xx]) continue;  // most of a glyph cell is empty
                int a = (mr[xx] * alpha) >> 8;
                uint32_t p = row[xx];
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
                row[xx] = (uint32_t(pr) << 16) | (uint32_t(pg) << 8) | uint32_t(pb);
            }
        }
    }

    void Mark(int x0, int y0, int w, int h) {
        int ta = std::max(0, x0 / s), tb = std::min(tw - 1, (x0 + w - 1) / s);
        int ua = std::max(0, y0 / s), ub = std::min(th - 1, (y0 + h - 1) / s);
        for (int ty = ua; ty <= ub; ty++) for (int tx = ta; tx <= tb; tx++) tiles[size_t(ty) * tw + tx] = 255;
    }

    void Step(PixelBuf& fb, double dt) {
        // Fade the previous frame for the trailing glow (0.91 per 60 Hz frame).
        // Done per byte so the compiler turns it into SIMD; this pass touches every pixel and is
        // most of the frame's cost. The unused top byte is 0 and stays 0.
        uint16_t f = (uint16_t)std::lround(256.0 * std::pow(0.91, dt));
        for (int ty = 0; ty < th; ty++) {
            int ya = ty * s, yb = std::min(fb.h, ya + s);
            for (int tx = 0; tx < tw; tx++) {
                uint8_t& t = tiles[size_t(ty) * tw + tx];
                if (!t) continue;
                t = uint8_t((t * f) >> 8);
                int xa = tx * s, n = (std::min(fb.w, xa + s) - xa) * 4;
                for (int y = ya; y < yb; y++) {
                    uint8_t* p = reinterpret_cast<uint8_t*>(fb.px + size_t(y) * fb.w + xa);
                    for (int i = 0; i < n; i++) p[i] = uint8_t((p[i] * f) >> 8);
                }
            }
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
                Mark((int)d.x, y, s, s);
                Blend(fb, glyphs[d.chars[i]].mask.data(), s, s, (int)d.x, y, 0, 150 + int(a * 105), 30 + int(a * 35), int((0.35 + a * 0.65) * 256), false);
            }
            if (nch && row * s < H) {
                const RainGlyph& g = glyphs[d.chars[0]];
                Mark((int)d.x - pad, row * s - pad, s + 2 * pad, s + 2 * pad);
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
    bool needClear = true;  // set when the terminal opens, so other monitors are blanked once, not every frame

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

    void Render(PixelBuf& fb, double flicker) {
        if (dirty) Rebuild();
        if (needClear) { fb.Clear(0); needClear = false; }
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
