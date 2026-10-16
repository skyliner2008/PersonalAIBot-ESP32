/**
 * @file Eyes.cpp
 * @brief Spring-animated robot eyes
 */

#include "Eyes.h"

#include <Arduino.h>
#include <math.h>

#include "../util/Deadline.h"

namespace {

constexpr float kBaseW = 84.0f;
constexpr float kBaseH = 100.0f;
constexpr float kSpacing = 68.0f;
constexpr uint16_t kBg = 0x0000;

float frand(float a, float b) { return a + (b - a) * (float)random(0, 10001) / 10000.0f; }
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

float easeOutBack(float u) {
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    const float t = u - 1.0f;
    return 1.0f + c3 * t * t * t + c1 * t * t;
}

uint16_t rgb565(float r, float g, float b) {
    const uint16_t R = (uint16_t)clampf(r, 0, 255), G = (uint16_t)clampf(g, 0, 255), B = (uint16_t)clampf(b, 0, 255);
    return (uint16_t)(((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3));
}

}  // namespace

void Eyes::begin(int screenW, int screenH) {
    _sw = screenW;
    _sh = screenH;
    Shape s;
    shapeFor(Emotion::Neutral, false, s);
    for (EyeSprings* e : {&_left, &_right}) {
        e->w.snap(s.w);
        e->h.snap(s.h);
        e->r.snap(s.r);
        e->lidTop.snap(0);
        e->tilt.snap(0);
        e->cheek.snap(0);
        e->lidBottom.snap(0);
        e->dy.snap(0);
    }
    _centerY.snap(_sh * 0.45f);
    _scale.snap(1.0f);
    _sleep.snap(1.0f);  // start with closed eyes; waking up is the first animation
    _sleep.target = 0.0f;
    _nextBlink = millis() + 1500;
}

// ============================================================
// Expression presets (viewer's left eye = right == false)
// ============================================================

void Eyes::shapeFor(Emotion e, bool right, Shape& s) const {
    s = Shape{1.0f, 1.0f, 26.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    switch (e) {
        case Emotion::Neutral:
            break;
        case Emotion::Happy:
            s.h = 0.95f; s.r = 30; s.cheek = 0.55f;
            break;
        case Emotion::Excited:
            s.w = 1.1f; s.h = 1.08f; s.r = 32; s.cheek = 0.45f;
            break;
        case Emotion::Love:
            s.r = 30; s.cheek = 0.4f;  // shown as hearts, this is the fallback shape
            break;
        case Emotion::Sad:
            s.w = 0.95f; s.h = 0.88f; s.r = 22; s.lidTop = 0.32f; s.tilt = -0.85f; s.dy = 8;
            break;
        case Emotion::Worried:
            s.w = 0.95f; s.h = 0.95f; s.r = 22; s.lidTop = 0.22f; s.tilt = -0.55f; s.lidBottom = 0.08f; s.dy = 4;
            break;
        case Emotion::Angry:
            s.h = 0.82f; s.r = 16; s.lidTop = 0.40f; s.tilt = 0.9f; s.dy = 2;
            break;
        case Emotion::Surprised:
            s.w = 1.16f; s.h = 1.22f; s.r = 44;
            break;
        case Emotion::Sleepy:
            s.h = 0.95f; s.r = 20; s.lidTop = 0.60f; s.tilt = -0.15f; s.lidBottom = 0.08f; s.dy = 8;
            break;
        case Emotion::Thinking:
            if (!right) { s.h = 0.9f; s.lidTop = 0.28f; s.tilt = 0.2f; }
            else { s.lidTop = 0.06f; }
            break;
        case Emotion::Curious:
            if (!right) { s.w = 1.12f; s.h = 1.16f; s.r = 34; }
            else { s.w = 0.92f; s.h = 0.86f; s.lidTop = 0.22f; s.tilt = 0.3f; }
            break;
        case Emotion::Confused:
            if (!right) { s.lidTop = 0.12f; s.tilt = -0.35f; s.dy = -2; }
            else { s.w = 0.86f; s.h = 0.78f; s.lidTop = 0.2f; s.tilt = 0.45f; s.dy = 4; }
            break;
        case Emotion::Shy:
            s.w = 0.92f; s.h = 0.85f; s.cheek = 0.35f; s.lidTop = 0.12f;
            break;
        case Emotion::Wink:
            if (!right) { s.r = 30; s.cheek = 0.45f; }
            else { s.r = 30; s.cheek = 1.05f; }
            break;
        default:
            break;
    }
}

void Eyes::applyTargets() {
    Shape s;
    shapeFor(_emotion, false, s);
    _left.w.target = s.w; _left.h.target = s.h; _left.r.target = s.r; _left.lidTop.target = s.lidTop;
    _left.tilt.target = s.tilt; _left.cheek.target = s.cheek; _left.lidBottom.target = s.lidBottom;
    _left.dy.target = s.dy;
    shapeFor(_emotion, true, s);
    _right.w.target = s.w; _right.h.target = s.h; _right.r.target = s.r; _right.lidTop.target = s.lidTop;
    _right.tilt.target = s.tilt; _right.cheek.target = s.cheek; _right.lidBottom.target = s.lidBottom;
    _right.dy.target = s.dy;

    switch (_emotion) {
        case Emotion::Happy: _blush.target = 0.25f; break;
        case Emotion::Excited: _blush.target = 0.35f; break;
        case Emotion::Love: _blush.target = 0.6f; break;
        case Emotion::Shy: _blush.target = 0.9f; break;
        default: _blush.target = 0.0f; break;
    }
    _hearts.target = _emotion == Emotion::Love ? 1.0f : 0.0f;
}

void Eyes::setEmotion(Emotion e) {
    if (e == _emotion) return;
    _emotion = e;
    applyTargets();
    kick(e == Emotion::Surprised || e == Emotion::Excited ? 1.2f : 0.6f);
    if (e == Emotion::Shy) lookAt(frand(-0.6f, 0.6f), 0.5f, 2500);
}

void Eyes::setMode(Mode m) {
    if (m == _mode) return;
    const Mode prev = _mode;
    _mode = m;
    _sleep.target = m == Mode::Sleeping ? 1.0f : 0.0f;
    _scale.target = m == Mode::Listening ? 1.06f : 1.0f;
    if (prev == Mode::Sleeping) {  // waking up: pop open
        kick(1.4f);
        _nextBlink = _now + 400;
    }
    _nextLook = 0;
}

void Eyes::lookAt(float x, float y, uint32_t holdMs) {
    _lookX.target = clampf(x, -1, 1);
    _lookY.target = clampf(y, -1, 1);
    _lookHoldUntil = _now + holdMs;
}

void Eyes::blink() {
    if (!_blinking) {
        _blinking = true;
        _blinkStart = _now;
    }
}

void Eyes::kick(float strength) { _scale.v += strength * 1.2f; }

// ============================================================
// Simulation
// ============================================================

void Eyes::stepEye(EyeSprings& e, float dt) {
    const float k = 170.0f, d = 13.0f;  // under-damped: ~15% overshoot = bouncy
    e.w.step(dt, k, d);
    e.h.step(dt, k, d);
    e.r.step(dt, k, d);
    e.lidTop.step(dt, k, d);
    e.tilt.step(dt, k, d);
    e.cheek.step(dt, k, d);
    e.lidBottom.step(dt, k, d);
    e.dy.step(dt, k, d);
}

void Eyes::updateBlink() {
    if (_mode == Mode::Sleeping) {
        _blinkFactor = 1.0f;
        _nextBlink = _now + 1000;  // kept fresh: a deadline left for weeks reads as far ahead (millis wrap)
        return;
    }
    if (!_blinking && deadline::reached(_now, _nextBlink)) {
        _blinking = true;
        _blinkStart = _now;
    }
    if (!_blinking) {
        _blinkFactor = 1.0f;
        return;
    }
    const float t = (_now - _blinkStart) / 170.0f;
    if (t < 0.4f) {
        _blinkFactor = 1.0f - 0.92f * (t / 0.4f);
    } else if (t < 1.0f) {
        _blinkFactor = 0.08f + 0.92f * easeOutBack((t - 0.4f) / 0.6f);
    } else {
        _blinking = false;
        _blinkFactor = 1.0f;
        if (_doubleBlink) {
            _doubleBlink = false;
            _nextBlink = _now + 120;
        } else {
            const float slow = _mode == Mode::Listening ? 1.6f : 1.0f;
            _nextBlink = _now + (uint32_t)(frand(2200, 5600) * slow);
            _doubleBlink = random(0, 5) == 0;
        }
    }
}

void Eyes::updateGaze() {
    // deadlines that ran out follow `now`, so they never go stale (millis wrap - see Deadline.h)
    if (deadline::reached(_now, _lookHoldUntil)) _lookHoldUntil = _now;
    if (deadline::reached(_now, _nextLook)) _nextLook = _now;
    switch (_mode) {
        case Mode::Searching:
            _lookX.target = sinf(_time * 1.4f) * 0.75f;
            _lookY.target = -0.1f;
            return;
        case Mode::Sleeping:
            _lookX.target = 0;
            _lookY.target = 0.25f;
            return;
        case Mode::Listening:
            if (deadline::reached(_now, _lookHoldUntil)) {
                _lookX.target = 0;
                _lookY.target = 0.12f;
            }
            return;
        default:
            break;
    }
    if (!deadline::reached(_now, _lookHoldUntil) || !deadline::reached(_now, _nextLook)) return;

    if (_mode == Mode::Thinking) {
        _lookX.target = -0.55f + frand(-0.15f, 0.15f);
        _lookY.target = -0.6f + frand(-0.1f, 0.1f);
        _nextLook = _now + (uint32_t)frand(600, 1100);
        return;
    }
    const float amp = _mode == Mode::Speaking ? 0.35f : 1.0f;
    float nx = 0, ny = 0;
    if (random(0, 100) >= 45) {
        nx = frand(-0.75f, 0.75f) * amp;
        ny = frand(-0.45f, 0.35f) * amp;
    }
    if (fabsf(nx - _lookX.target) > 0.4f && random(0, 4) == 0) blink();  // saccade blink
    _lookX.target = nx;
    _lookY.target = ny;
    _nextLook = _now + (uint32_t)frand(1200, 3600);
}

void Eyes::update(uint32_t nowMs, float dt) {
    _now = nowMs;
    _time += dt;
    if (dt > 0.1f) dt = 0.1f;

    // Talking bounce: follow the voice level, pop on strong syllables
    static uint32_t lastPop = 0;
    const float prev = _speechSmooth;
    _speechSmooth += (_speech - _speechSmooth) * 0.35f;
    if (_mode == Mode::Speaking && _speechSmooth > 0.45f && _speechSmooth - prev > 0.08f && nowMs - lastPop > 260) {
        kick(0.35f);
        lastPop = nowMs;
    }

    updateBlink();
    updateGaze();

    const float h = dt / 2.0f;
    for (int i = 0; i < 2; i++) {
        stepEye(_left, h);
        stepEye(_right, h);
        _lookX.step(h, 220.0f, 22.0f);
        _lookY.step(h, 220.0f, 22.0f);
        _centerY.step(h, 140.0f, 16.0f);
        _scale.step(h, 200.0f, 11.0f);
        _blush.step(h, 60.0f, 14.0f);
        _hearts.step(h, 90.0f, 14.0f);
        _sleep.step(h, 40.0f, 12.0f);
    }
}

// ============================================================
// Rendering
// ============================================================

void Eyes::drawEye(LGFX_Sprite& c, float ecx, float ecy, const EyeSprings& e, bool right, float squash) {
    const float scale = _scale.x;
    const float sleep = clampf(_sleep.x, 0, 1);
    const float w = kBaseW * e.w.x * scale * (1.0f + 0.05f * squash);
    float h = kBaseH * e.h.x * scale * (1.0f - 0.10f * squash) * _blinkFactor * (1.0f - 0.94f * sleep);
    if (h < 5) h = 5;
    const float ycen = ecy + e.dy.x;
    const float x0 = ecx - w / 2, y0 = ycen - h / 2;

    if (h < 12) {  // closed: a soft line (sleep / mid-blink)
        c.fillSmoothRoundRect((int32_t)x0, (int32_t)(ycen - 3), (int32_t)w, 6, 3, _color);
        return;
    }
    const float r = fminf(fminf(e.r.x * scale, w / 2), h / 2);
    c.fillSmoothRoundRect((int32_t)x0, (int32_t)y0, (int32_t)w, (int32_t)h, (int32_t)r, _color);

    // Top eyelid: deeper on the inner side when tilt > 0 (angry), outer side when < 0 (sad)
    const float d = clampf(e.lidTop.x, 0, 1);
    const float tilt = clampf(e.tilt.x, -1, 1);
    const float dInner = clampf(d * (1 + tilt), 0, 1.05f) * h;
    const float dOuter = clampf(d * (1 - tilt), 0, 1.05f) * h;
    const float dL = right ? dInner : dOuter;  // the inner side of the left eye is its right edge
    const float dR = right ? dOuter : dInner;
    if (dL > 0.5f || dR > 0.5f) {
        const int32_t xa = (int32_t)(x0 - 2), xb = (int32_t)(x0 + w + 2), ya = (int32_t)(y0 - 2);
        c.fillTriangle(xa, ya, xb, ya, xb, (int32_t)(y0 + dR), kBg);
        c.fillTriangle(xa, ya, xb, (int32_t)(y0 + dR), xa, (int32_t)(y0 + dL), kBg);
    }

    const float lb = clampf(e.lidBottom.x, 0, 1) * h;
    if (lb > 0.5f) c.fillRect((int32_t)(x0 - 2), (int32_t)(y0 + h - lb), (int32_t)(w + 4), (int32_t)(lb + 3), kBg);

    // Cheek pushes up from below -> happy crescent
    const float ch = clampf(e.cheek.x, 0, 1.1f);
    if (ch > 0.02f) {
        const float cyc = y0 + h + h * 0.62f - ch * h * 0.85f;
        c.fillEllipse((int32_t)ecx, (int32_t)cyc, (int32_t)(w * 0.8f), (int32_t)(h * 0.72f), kBg);
    }
}

void Eyes::drawHeart(LGFX_Sprite& c, float cx, float cy, float s, uint16_t color) {
    const float r = s * 0.28f;
    c.fillCircle((int32_t)(cx - s * 0.24f), (int32_t)(cy - s * 0.12f), (int32_t)r, color);
    c.fillCircle((int32_t)(cx + s * 0.24f), (int32_t)(cy - s * 0.12f), (int32_t)r, color);
    c.fillTriangle((int32_t)(cx - s * 0.50f), (int32_t)(cy - s * 0.02f), (int32_t)(cx + s * 0.50f),
                   (int32_t)(cy - s * 0.02f), (int32_t)cx, (int32_t)(cy + s * 0.55f), color);
}

void Eyes::drawZzz(LGFX_Sprite& c, float x, float y) {
    c.setFont(&fonts::Font2);
    c.setTextDatum(textdatum_t::middle_center);
    for (int i = 0; i < 3; i++) {
        const float phase = fmodf(_time * 0.45f + i / 3.0f, 1.0f);
        const float alpha = phase < 0.8f ? 1.0f : (1.0f - phase) / 0.2f;
        const uint8_t r = (uint8_t)(((_color >> 11) & 0x1F) << 3), g = (uint8_t)(((_color >> 5) & 0x3F) << 2),
                      b = (uint8_t)((_color & 0x1F) << 3);
        c.setTextColor(rgb565(r * alpha * 0.8f, g * alpha * 0.8f, b * alpha * 0.8f));
        c.setTextSize(1.0f + phase * 1.2f);
        c.drawString("z", (int32_t)(x + phase * 30 + i * 4), (int32_t)(y - phase * 40));
    }
    c.setTextSize(1);
}

void Eyes::draw(LGFX_Sprite& c) {
    const float breathe = sinf(_time * 6.2832f / 3.4f);
    const float cx = _sw / 2.0f + _lookX.x * 24.0f;
    const float cy = _centerY.x + breathe * 1.6f + _lookY.x * 14.0f;
    const float spacing = kSpacing * _scale.x;
    const float squash = _mode == Mode::Speaking ? _speechSmooth : 0.0f;

    if (_hearts.x > 0.5f && _sleep.x < 0.5f) {
        const float pulse = 1.0f + 0.07f * sinf(_time * 8.0f);
        const float size = 92.0f * _scale.x * pulse * clampf(_hearts.x, 0.5f, 1.1f);
        drawHeart(c, cx - spacing, cy, size, rgb565(255, 70, 120));
        drawHeart(c, cx + spacing, cy, size, rgb565(255, 70, 120));
    } else {
        drawEye(c, cx - spacing, cy, _left, false, squash);
        drawEye(c, cx + spacing, cy, _right, true, squash);
    }

    const float blush = clampf(_blush.x, 0, 1);
    if (blush > 0.05f && _sleep.x < 0.5f) {
        const uint16_t col = rgb565(255 * blush * 0.85f, 70 * blush, 120 * blush);
        const float by = cy + kBaseH * 0.5f * _scale.x + 6;
        c.fillEllipse((int32_t)(cx - spacing - 10), (int32_t)by, 20, 7, col);
        c.fillEllipse((int32_t)(cx + spacing + 10), (int32_t)by, 20, 7, col);
    }

    if (_sleep.x > 0.6f) drawZzz(c, cx + spacing + 40, cy - 30);
}
