/**
 * @file AaIcon.cpp
 * @brief Anti-aliased icons from signed distances (T137)
 */

#include "AaIcon.h"

#include <esp_heap_caps.h>
#include <math.h>

namespace aa {

namespace {
constexpr float kRad = 57.29578f;

uint16_t mix(uint16_t a, uint16_t b, float t) {
    const int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
    const int br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
    const int r = ar + (int)lroundf((br - ar) * t), g = ag + (int)lroundf((bg - ag) * t),
              bl = ab + (int)lroundf((bb - ab) * t);
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

bool inSweep(float a, float a0, float a1) {  // a in [0, 360), a0 <= a1 (a1 may pass 360)
    if (a < a0) a += 360.0f;
    return a <= a1;
}
}  // namespace

float circle(float x, float y, float cx, float cy, float r) { return hypotf(x - cx, y - cy) - r; }

float segment(float x, float y, float ax, float ay, float bx, float by, float r) {
    const float px = x - ax, py = y - ay, dx = bx - ax, dy = by - ay;
    const float len2 = dx * dx + dy * dy;
    float t = len2 > 0 ? (px * dx + py * dy) / len2 : 0;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return hypotf(px - dx * t, py - dy * t) - r;
}

float arc(float x, float y, float cx, float cy, float r, float hw, float a0, float a1) {
    float a = atan2f(y - cy, x - cx) * kRad;
    if (a < 0) a += 360.0f;
    if (inSweep(a, a0, a1)) return fabsf(hypotf(x - cx, y - cy) - r) - hw;
    const float e0x = cx + cosf(a0 / kRad) * r, e0y = cy + sinf(a0 / kRad) * r;
    const float e1x = cx + cosf(a1 / kRad) * r, e1y = cy + sinf(a1 / kRad) * r;
    return fminf(hypotf(x - e0x, y - e0y), hypotf(x - e1x, y - e1y)) - hw;
}

float roundRect(float x, float y, float cx, float cy, float hx, float hy, float r) {
    const float qx = fabsf(x - cx) - (hx - r), qy = fabsf(y - cy) - (hy - r);
    const float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    return hypotf(ox, oy) + fminf(fmaxf(qx, qy), 0.0f) - r;
}

void Painter::layer(float d, uint16_t color) {
    const float cov = 0.5f - d;
    if (cov <= 0) return;
    _col = cov >= 1 ? color : mix(_col, color, cov);
}

void Icon::draw(LGFX_Sprite& c, int cx, int cy, int32_t key, const std::function<void(Painter&)>& paint) {
    if (!_buf) {
        _buf = (uint16_t*)heap_caps_malloc((size_t)_w * _h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!_buf) return;
    }
    if (key != _key) {
        _key = key;
        for (int j = 0; j < _h; j++) {
            for (int i = 0; i < _w; i++) {
                Painter p(i + 0.5f - _w / 2.0f, j + 0.5f - _h / 2.0f);
                paint(p);
                _buf[j * _w + i] = p.color();
            }
        }
    }
    c.pushImage(cx - _w / 2, cy - _h / 2, _w, _h, (const lgfx::rgb565_t*)_buf);
}

}  // namespace aa
