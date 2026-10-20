/**
 * @file NavScreen.h
 * @brief Turn-by-turn from Google Maps on the phone: arrow, distance, instruction, arrival (wiki 18)
 *
 *   ┌ ‹  นำทาง                     Wi-Fi ┐
 *   │   ▲          เลี้ยวขวาเข้า           │
 *   │  ─┘          ถ.พระราม 4               │
 *   │ 350 ม.                              │
 *   │ ถึง 16:40 • อีก 5.4 กม. • 12 นาที   42 km/h │
 *
 * Opens by itself when navigation starts over the face (the dash shows a strip instead), closes when it ends.
 * The arrows are drawn in code (no images): NavScreen::drawIcon is also used small on the dash header.
 */

#pragma once

#include "../phone/PhoneProtocol.h"
#include "UiManager.h"

class NavScreen : public Screen {
public:
    void onEnter() override;
    void onTouch(const TouchEvent& e) override;
    void tick(uint32_t now, float dt) override;
    void draw(LGFX_Sprite& c) override;

    /** The manoeuvre arrow in a box of `size` px centred on (cx, cy). */
    static void drawIcon(LGFX_Sprite& c, phone::Icon icon, int cx, int cy, int size, uint16_t color, uint16_t bg);

private:
    phone::Nav _nav;
    phone::Gps _gps;
    String _place;
    const char* _via = "none";
    bool _connected = false, _pressedBack = false;
    uint32_t _now = 0, _nextRead = 0;

    void drawWrapped(LGFX_Sprite& c, const char* text, int x, int y, int w, int maxLines, int lineH);
};

extern NavScreen gNavUi;
