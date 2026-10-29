/**
 * @file AaIcon.h
 * @brief Anti-aliased small icons for the home screen (T137: the mic / music / dash buttons and the status bar)
 *
 * LovyanGFX has no anti-aliased arcs (fillArc is hard-edged) and its small shapes looked jagged on the board.
 * An icon here is a stack of shapes measured as signed distances per pixel (coverage = 0.5 - distance), composed
 * over the black background once per state into a small PSRAM buffer, then pushed each frame.
 */

#pragma once

#include <LovyanGFX.hpp>
#include <stdint.h>

#include <functional>

namespace aa {

/** Signed distance of the pixel centre to a shape (px, < 0 inside). */
float circle(float x, float y, float cx, float cy, float r);
/** A line from a to b with round ends, r = half the thickness. */
float segment(float x, float y, float ax, float ay, float bx, float by, float r);
/** A ring of radius r and half-thickness hw from angle a0 to a1 (degrees, 0 = right, 90 = down), round ends. */
float arc(float x, float y, float cx, float cy, float r, float hw, float a0, float a1);
/** A rounded rectangle centred on (cx, cy), half sizes hx / hy, corner radius r. */
float roundRect(float x, float y, float cx, float cy, float hx, float hy, float r);

/** Paints shapes into one pixel: each layer blends its colour by the shape's coverage. */
class Painter {
public:
    explicit Painter(float x, float y) : _x(x), _y(y) {}
    float x() const { return _x; }
    float y() const { return _y; }
    void layer(float distance, uint16_t color);
    uint16_t color() const { return _col; }

private:
    float _x, _y;
    uint16_t _col = 0;  // the black background
};

/**
 * One icon of w x h pixels, drawn again only when its key (the state it shows) changes.
 * paint() is called per pixel with coordinates relative to the icon centre.
 */
class Icon {
public:
    Icon(int w, int h) : _w(w), _h(h) {}
    void draw(LGFX_Sprite& c, int cx, int cy, int32_t key, const std::function<void(Painter&)>& paint);

private:
    int _w, _h;
    int32_t _key = INT32_MIN;
    uint16_t* _buf = nullptr;
};

}  // namespace aa
