/**
 * @file UiManager.h
 * @brief Screen stack, touch gestures, fonts and drawing helpers for the 320x240 landscape UI
 * @see wiki/06-Avatar-and-Thai-UI.md
 *
 * Runs entirely in the UI task: poll touch -> gestures -> top screen -> render
 * into a PSRAM sprite -> push to the LCD.
 */

#pragma once

#include <LovyanGFX.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <functional>

#include "UiTypes.h"

struct TouchEvent {
    enum Type : uint8_t { Down, Move, Up, Tap, LongPress } type;
    int16_t x, y;            // current position
    int16_t startX, startY;  // where the gesture began
    int16_t rawX, rawY;      // controller coordinates (for calibration)
};

/** 2-point-per-axis linear touch calibration (handles swapped / mirrored / offset axes). */
struct TouchCalibration {
    bool valid = false;
    uint8_t xAxis = 1, yAxis = 0;  // which raw axis drives screen X / Y (0 = raw x, 1 = raw y)
    float xScale = 1, xOffset = 0, yScale = 1, yOffset = 0;
    uint8_t rotation = 1;          // rotation the calibration was made in
};

class Screen {
public:
    virtual ~Screen() = default;
    virtual void onEnter() {}
    virtual void onTouch(const TouchEvent& e) { (void)e; }
    virtual void tick(uint32_t now, float dt) { (void)now; (void)dt; }
    virtual void draw(LGFX_Sprite& c) = 0;
};

namespace ui {
constexpr int W = 320;
constexpr int H = 240;
constexpr int kTitleH = 34;

// Palette (RGB565)
constexpr uint16_t BG = 0x0000;
constexpr uint16_t PANEL = 0x18E3;     // dark gray
constexpr uint16_t PANEL_HI = 0x3186;  // pressed / selected
constexpr uint16_t LINE = 0x2945;
constexpr uint16_t TEXT = 0xFFFF;
constexpr uint16_t DIM = 0x9CD3;
constexpr uint16_t ACCENT = 0x2EDF;    // cyan-blue
constexpr uint16_t OK = 0x07E8;
constexpr uint16_t WARN = 0xFE60;
constexpr uint16_t ERR = 0xF8A2;
constexpr uint16_t USER = 0x9F3F;      // user speech
}  // namespace ui

class UiManager {
public:
    bool begin(LGFX_Device* lcd, uint8_t rotation, UiHost* host);
    void setRotation(uint8_t rotation);
    void setBrightness(uint8_t percent);
    /** Panel colour inversion (INVON). The ES3C28P IPS panel needs it for a black background. */
    void setInvert(bool on);
    /** Raw touch correction: bit0 swap X/Y, bit1 mirror X, bit2 mirror Y (panel coordinates). */
    void setTouchTransform(uint8_t t) { _touchTransform = t & 7; }
    void setCalibration(const TouchCalibration& c) { _cal = c; }
    const TouchCalibration& calibration() const { return _cal; }

    // Screen stack
    void push(Screen* s);
    void pop();
    void popToRoot();
    Screen* top() const { return _depth ? _stack[_depth - 1] : nullptr; }
    bool atRoot() const { return _depth <= 1; }

    /** Feed one raw FT6336 sample (portrait panel coordinates). */
    void handleRawTouch(bool down, int16_t rawX, int16_t rawY, uint32_t now);
    /** Console helpers: synthesize gestures in screen coordinates. */
    void injectTap(int16_t x, int16_t y);
    void injectLongPress(int16_t x, int16_t y);
    void injectDrag(int16_t x0, int16_t y0, int16_t x1, int16_t y1);

    /** Tick + draw the top screen and push it to the LCD. */
    void frame(uint32_t now);

    LGFX_Sprite& canvas() { return *_canvas; }
    const lgfx::IFont* thai26() const { return _thai26Ok ? &_thai26 : (const lgfx::IFont*)&fonts::Font2; }
    const lgfx::IFont* thai20() const { return _thai20Ok ? &_thai20 : (const lgfx::IFont*)&fonts::Font2; }
    UiHost* host() const { return _host; }
    const uint8_t* frameBuffer(size_t& len) const;

    // ---- Drawing helpers ----
    /** Title bar with an optional back arrow; returns true if (x,y) hits the back area. */
    void drawTitle(LGFX_Sprite& c, const char* title, bool back);
    static bool hitBack(int16_t x, int16_t y) { return x < 60 && y < ui::kTitleH; }
    void drawButton(LGFX_Sprite& c, int x, int y, int w, int h, const char* label, uint16_t bg,
                    uint16_t fg = ui::TEXT, const lgfx::IFont* font = nullptr);
    /** Draw `text` clipped to maxWidth, adding "..." when it does not fit. */
    void drawFitted(LGFX_Sprite& c, const char* text, int x, int y, int maxWidth, const lgfx::IFont* font,
                    uint16_t color, textdatum_t datum = textdatum_t::middle_left);

private:
    LGFX_Device* _lcd = nullptr;
    LGFX_Sprite* _canvas = nullptr;
    UiHost* _host = nullptr;
    uint8_t _rotation = 1;
    uint8_t _touchTransform = 0;
    TouchCalibration _cal;
    int16_t _rawX = 0, _rawY = 0;

    lgfx::VLWfont _thai26, _thai20;
    lgfx::PointerWrapper _thai26Data, _thai20Data;
    bool _thai26Ok = false, _thai20Ok = false;

    static constexpr int kMaxDepth = 8;
    Screen* _stack[kMaxDepth] = {};
    int _depth = 0;
    uint32_t _lastFrame = 0;

    // Gesture state
    bool _down = false, _dragging = false, _longFired = false;
    int16_t _sx = 0, _sy = 0, _lx = 0, _ly = 0;
    uint32_t _downAt = 0;

    void mapTouch(int16_t rawX, int16_t rawY, int16_t& x, int16_t& y) const;
    void send(TouchEvent::Type t, int16_t x, int16_t y);
};

extern UiManager gUi;
