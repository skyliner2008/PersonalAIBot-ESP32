/**
 * @file RadioScreen.h
 * @brief Touch radio player: now-playing panel (tap = talk to the assistant), station presets, volume
 * @see wiki/12-Internet-Radio.md
 *
 *   ┌ ‹  วิทยุออนไลน์                  12:30   ⏹ ┐  header: back / stop or play
 *   │ ▮▮▮  COOL Fahrenheit 93                    │  now playing — TAP = talk to the AI (like the face)
 *   │      Song - Artist                          │
 *   │      แตะตรงนี้เพื่อคุยกับ AI                 │
 *   │ [ลูกทุ่ง 90] [มิติข่าว] [Smooth] [94 ...] → │  station chips (drag sideways, tap to play)
 *   └ [⏮]   [ − ]    70%    [ + ]   [⏭]         ┘  previous / volume / next
 */

#pragma once

#include "UiManager.h"

class AudioManager;

class RadioScreen : public Screen {
public:
    void begin(AudioManager* audio);

    void onEnter() override;
    void onTouch(const TouchEvent& e) override;
    void tick(uint32_t now, float dt) override;
    void draw(LGFX_Sprite& c) override;

private:
    AudioManager* _audio = nullptr;
    float _scroll = 0, _velocity = 0;
    int16_t _lastX = 0;
    bool _dragging = false, _fingerDown = false, _inChips = false;
    int _pressed = -1;  // hit id while the finger is down (for the pressed look)
    float _bars[6] = {0};
    uint32_t _now = 0, _nextSettings = 0;
    int _volume = 70;
    int _current = -1;  // station playing (index in the list), else the last one

    int hitTest(int x, int y) const;
    int chipAt(int x) const;
    float maxScroll() const;
    void scrollTo(int index);
    void playIndex(int index);
};

extern RadioScreen gRadioUi;
