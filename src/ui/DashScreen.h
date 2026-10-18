/**
 * @file DashScreen.h
 * @brief Vehicle dash: rpm bar with shift light, speed, gear, engine / battery / intake tiles
 * @see wiki/16-Vehicle-Dash.md
 *
 *   ┌ ‹  ● LIVE  OBD-II Bluetooth                 [ตั้งค่า] ┐  header: back / source + status / settings
 *   │ ▮▮▮▮▮▮▮▮▮▮▮▮▮▮▮▮▮▮▮▯▯▯▯▯▯▯▯▯▯▯                      │  rpm bar (green, yellow, red from the shift point)
 *   │ 0    1    2    3    4    5    6    7         x1000 rpm │
 *   │  3,250            88                ┌───┐             │  rpm / speed km/h / gear
 *   │  รอบ/นาที                km/h      │ 3 │             │
 *   │  สูงสุด 6,120                       └───┘             │
 *   │ [เครื่องยนต์ 87°C] [แบตเตอรี่ 13.9 V] [อากาศเข้า 42°C]   │  tiles: yellow = high, red blinking = critical
 *   └──────────────────────────────────────────────────────┘
 * Simulator "touch": hold the main area - further right = more throttle. Long press: reset the peak.
 */

#pragma once

#include "../vehicle/DashModel.h"
#include "UiManager.h"

class DashScreen : public Screen {
public:
    void onEnter() override;
    void onTouch(const TouchEvent& e) override;
    void tick(uint32_t now, float dt) override;
    void draw(LGFX_Sprite& c) override;

private:
    vehicle::DashModel _model;
    String _status;
    uint32_t _now = 0, _nextConfig = 0, _nextActivity = 0;
    bool _touchSource = false, _fingerDown = false, _needsRestart = false;
    uint8_t _source = 0;
    int _pressed = 0;

    void drawBar(LGFX_Sprite& c, const vehicle::DashView& v);
    void drawTile(LGFX_Sprite& c, int x, const char* label, const char* value, const char* unit, vehicle::Level level);
};

extern DashScreen gDashUi;
