/**
 * @file UiManager.cpp
 * @brief Screen stack, gesture recognition, fonts and shared drawing helpers
 */

#include "UiManager.h"

#include <Arduino.h>
#include <string.h>

#include "../display/fonts/ThaiFont20.h"
#include "../display/fonts/ThaiFont26.h"
#include "../util/Utf8.h"
#include "pins_config.h"

UiManager gUi;

namespace {
constexpr uint32_t kLongPressMs = 1500;
constexpr int kDragThreshold = 10;
}  // namespace

bool UiManager::begin(LGFX_Device* lcd, uint8_t rotation, UiHost* host) {
    _lcd = lcd;
    _host = host;
    setRotation(rotation);

    _canvas = new LGFX_Sprite(lcd);
    _canvas->setColorDepth(16);
    _canvas->setPsram(true);
    if (!_canvas->createSprite(ui::W, ui::H)) {
        Serial.println("[UI] ERROR: sprite allocation failed");
        return false;
    }
    _thai26Data.set(ThaiFont26, ThaiFont26_size);
    _thai26Ok = _thai26.loadFont(&_thai26Data);
    _thai20Data.set(ThaiFont20, ThaiFont20_size);
    _thai20Ok = _thai20.loadFont(&_thai20Data);
    Serial.printf("[UI] 320x240 landscape (rotation %u), Thai fonts %s/%s (line %d/%d px)\n", rotation,
                  _thai26Ok ? "26" : "-", _thai20Ok ? "20" : "-", (int)_thai26.yAdvance, (int)_thai20.yAdvance);
    return true;
}

void UiManager::setRotation(uint8_t rotation) {
    _rotation = (rotation == 3) ? 3 : 1;
    if (_lcd) _lcd->setRotation(_rotation);
}

void UiManager::setBrightness(uint8_t percent) {
    if (_lcd) _lcd->setBrightness((uint8_t)(percent * 255 / 100));
}

void UiManager::setInvert(bool on) {
    if (_lcd) _lcd->invertDisplay(on);
}

const uint8_t* UiManager::frameBuffer(size_t& len) const {
    len = _canvas ? (size_t)ui::W * ui::H * 2 : 0;
    return _canvas ? (const uint8_t*)_canvas->getBuffer() : nullptr;
}

// ============================================================
// Screen stack
// ============================================================

void UiManager::push(Screen* s) {
    if (!s || _depth >= kMaxDepth) return;
    _stack[_depth++] = s;
    s->onEnter();
}

void UiManager::pop() {
    if (_depth <= 1) return;
    _depth--;
    if (top()) top()->onEnter();
}

void UiManager::popToRoot() {
    if (_depth <= 1) return;
    _depth = 1;
    top()->onEnter();
}

// ============================================================
// Touch
// ============================================================

// Same transform LovyanGFX applies for its own touch drivers (Panel_Device::convertRawXY),
// assuming the FT6336 reports portrait coordinates aligned with the panel (0..239, 0..319).
void UiManager::mapTouch(int16_t rx, int16_t ry, int16_t& x, int16_t& y) const {
    if (_cal.valid) {
        float fx = (_cal.xAxis ? ry : rx) * _cal.xScale + _cal.xOffset;
        float fy = (_cal.yAxis ? ry : rx) * _cal.yScale + _cal.yOffset;
        if (_cal.rotation != _rotation) {  // calibrated in the other landscape orientation
            fx = ui::W - 1 - fx;
            fy = ui::H - 1 - fy;
        }
        x = (int16_t)constrain((int)lroundf(fx), 0, ui::W - 1);
        y = (int16_t)constrain((int)lroundf(fy), 0, ui::H - 1);
        return;
    }
    // board-specific correction of the controller's axes (see settings "touch")
    if (_touchTransform & 1) std::swap(rx, ry);
    if (_touchTransform & 2) rx = (int16_t)(ui::H - 1 - rx);
    if (_touchTransform & 4) ry = (int16_t)(ui::W - 1 - ry);
    int32_t tx = rx, ty = ry;
    // Measured on the ES3C28P (3-point calibration, 2026-10-14): the FT6336 is mounted
    // rotated 180° relative to the LCD, so use the opposite landscape rotation.
    const uint8_t r = _rotation ^ 2;
    const bool vflip = (1 << r) & 0b10010110;
    if (r & 1) std::swap(tx, ty);
    if (r & 2) tx = (ui::W - 1) - tx;
    if (vflip) ty = (ui::H - 1) - ty;
    x = (int16_t)tx;
    y = (int16_t)ty;
}

void UiManager::send(TouchEvent::Type t, int16_t x, int16_t y) {
    if (!top()) return;
    TouchEvent e{t, x, y, _sx, _sy, _rawX, _rawY};
    top()->onTouch(e);
}

void UiManager::handleRawTouch(bool down, int16_t rawX, int16_t rawY, uint32_t now) {
    int16_t x = _lx, y = _ly;
    if (down) {
        mapTouch(rawX, rawY, x, y);
        _rawX = rawX;
        _rawY = rawY;
    }

    if (down && !_down) {
        Serial.printf("[Touch] raw %d,%d -> screen %d,%d\n", rawX, rawY, x, y);
        _down = true;
        _dragging = false;
        _longFired = false;
        _sx = _lx = x;
        _sy = _ly = y;
        _downAt = now;
        send(TouchEvent::Down, x, y);
        return;
    }
    if (down && _down) {
        if (!_dragging && (abs(x - _sx) > kDragThreshold || abs(y - _sy) > kDragThreshold)) _dragging = true;
        if (_dragging && (x != _lx || y != _ly)) send(TouchEvent::Move, x, y);
        _lx = x;
        _ly = y;
        if (!_dragging && !_longFired && now - _downAt >= kLongPressMs) {
            _longFired = true;
            send(TouchEvent::LongPress, x, y);
        }
        return;
    }
    if (!down && _down) {
        _down = false;
        send(TouchEvent::Up, _lx, _ly);
        if (!_dragging && !_longFired) send(TouchEvent::Tap, _lx, _ly);
    }
}

void UiManager::injectTap(int16_t x, int16_t y) {
    _sx = _lx = x;
    _sy = _ly = y;
    send(TouchEvent::Down, x, y);
    send(TouchEvent::Up, x, y);
    send(TouchEvent::Tap, x, y);
}

void UiManager::injectLongPress(int16_t x, int16_t y) {
    _sx = _lx = x;
    _sy = _ly = y;
    send(TouchEvent::Down, x, y);
    send(TouchEvent::LongPress, x, y);
    send(TouchEvent::Up, x, y);
}

void UiManager::injectDrag(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    _sx = x0;
    _sy = y0;
    send(TouchEvent::Down, x0, y0);
    for (int i = 1; i <= 8; i++) {
        send(TouchEvent::Move, (int16_t)(x0 + (x1 - x0) * i / 8), (int16_t)(y0 + (y1 - y0) * i / 8));
    }
    send(TouchEvent::Up, x1, y1);
}

// ============================================================
// Frame
// ============================================================

void UiManager::frame(uint32_t now) {
    if (!_canvas || !top()) return;
    const float dt = _lastFrame ? (now - _lastFrame) / 1000.0f : 0.033f;
    _lastFrame = now;
    Screen* s = top();
    s->tick(now, dt);
    _canvas->setTextSize(1);
    s->draw(*_canvas);
    if (_lcd && (_lcd->width() != ui::W || _lcd->height() != ui::H)) {
        // the 800x480 panel: the 320x240 UI at a whole 2x (640x480), centred - the same zoom on both axes (2.5 x 2
        // stretched the eyes and the Thai text 25 % wide). The 80-px side bands stay black. TouchGT911 maps back.
        const int zx = _lcd->width() / ui::W, zy = _lcd->height() / ui::H;
        const int z = zx < zy ? zx : zy;
        _canvas->setPivot(0, 0);
        _canvas->pushRotateZoom((float)((_lcd->width() - ui::W * z) / 2), (float)((_lcd->height() - ui::H * z) / 2), 0.0f,
                                (float)z, (float)z);
    } else {
        _canvas->pushSprite(0, 0);
    }
}

// ============================================================
// Drawing helpers
// ============================================================

void UiManager::drawTitle(LGFX_Sprite& c, const char* title, bool back) {
    c.fillRect(0, 0, ui::W, ui::kTitleH, ui::PANEL);
    c.drawFastHLine(0, ui::kTitleH - 1, ui::W, ui::LINE);
    if (back) {  // chevron "<"
        for (int t = 0; t < 3; t++) {
            c.drawLine(22 + t, 8, 12 + t, 17, ui::TEXT);
            c.drawLine(12 + t, 17, 22 + t, 26, ui::TEXT);
        }
    }
    drawFitted(c, title, back ? 40 : 12, ui::kTitleH / 2, ui::W - (back ? 52 : 24), thai26(), ui::TEXT);
}

void UiManager::drawButton(LGFX_Sprite& c, int x, int y, int w, int h, const char* label, uint16_t bg,
                           uint16_t fg, const lgfx::IFont* font) {
    c.fillSmoothRoundRect(x, y, w, h, 8, bg);
    c.setFont(font ? font : thai20());
    c.setTextColor(fg);
    c.setTextDatum(textdatum_t::middle_center);
    c.drawString(label, x + w / 2, y + h / 2);
}

void UiManager::drawFitted(LGFX_Sprite& c, const char* text, int x, int y, int maxWidth,
                           const lgfx::IFont* font, uint16_t color, textdatum_t datum) {
    c.setFont(font);
    c.setTextColor(color);
    c.setTextDatum(datum);
    if (c.textWidth(text) <= maxWidth) {
        c.drawString(text, x, y);
        return;
    }
    // Trim code points from the end until "<prefix>..." fits
    char buf[160];
    memcpy(buf, "...", 4);
    size_t len = utf8::clampToBoundary(text, strlen(text), sizeof(buf) - 4);
    while (len > 0) {
        len = utf8::clampToBoundary(text, len, len - 1);
        memcpy(buf, text, len);
        memcpy(buf + len, "...", 4);
        if (c.textWidth(buf) <= maxWidth) break;
    }
    c.drawString(buf, x, y);
}
