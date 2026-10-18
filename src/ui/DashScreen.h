/**
 * @file DashScreen.h
 * @brief Vehicle dash in three styles: racing bar, round digital (sport), round needles (classic)
 * @see wiki/16-Vehicle-Dash.md
 *
 *   race     ┌ ‹ ● LIVE  source            [ตั้งค่า] ┐   digital  ┌ ‹ ● LIVE  source        [ตั้งค่า] ┐
 *            │ ▮▮▮▮▮▮▮▮▮▮▮▮▮▮▮▯▯▯▯▯▯▯▯▯ rpm bar       │            │   ╭─ ▮▮▮▮▮▮▮▯▯▯ ─╮   [เครื่อง] │
 *            │ 3,250        88        [3]           │            │  ▮   3,250    ▯   [แบต  ] │
 *            │ [เครื่อง] [แบต] [อากาศ]              │            │  ▮    88 km/h ▯   [อากาศ] │
 *                                                                │   ╰──── (3) ────╯         │
 *   classic  two dials (tachometer with the red zone + gear window, speedometer + digital speed),
 *            a shift lamp between them, engine / battery / intake readouts along the bottom
 *
 * Tap the status in the header: next style (saved). Simulator "touch": hold the dash - further right =
 * more throttle. Long press: reset the peak. The dials' faces are drawn once into a PSRAM sprite.
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

    /** One reading of the three small panels: engine temperature, battery, intake air (or throttle). */
    struct Aux {
        const char* label;
        const char* shortLabel;  // for the low readouts of the classic style
        char value[12];
        const char* unit;  // "C" is drawn as °C
        vehicle::Level level;
    };

private:
    vehicle::DashModel _model;
    String _status;
    uint32_t _now = 0, _nextConfig = 0, _nextActivity = 0, _toastUntil = 0;
    bool _touchSource = false, _fingerDown = false, _needsRestart = false;
    uint8_t _source = 0;
    vehicle::DashStyle _style = vehicle::DashStyle::Race;
    int _pressed = 0;
    float _speedNeedle = 0;            // the speedometer needle moves smoothly between readings
    LGFX_Sprite* _face = nullptr;      // classic: dial faces (ticks, numbers, red zone)
    uint32_t _faceKey = 0;

    void cycleStyle();
    void auxValues(const vehicle::DashView& v, bool moto, Aux out[3]) const;
    void drawHeader(LGFX_Sprite& c, const vehicle::DashView& v);
    void drawWaiting(LGFX_Sprite& c, int y);
    void drawToast(LGFX_Sprite& c);
    void drawReadout(LGFX_Sprite& c, int x, int y, int w, int h, const Aux& a);
    void drawRace(LGFX_Sprite& c, const vehicle::DashView& v, bool moto);
    void drawDigital(LGFX_Sprite& c, const vehicle::DashView& v, bool moto);
    void drawClassic(LGFX_Sprite& c, const vehicle::DashView& v, bool moto);
    void buildClassicFace(LGFX_Sprite& c, bool moto);
};

extern DashScreen gDashUi;
