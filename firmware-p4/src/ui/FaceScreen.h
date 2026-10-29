/**
 * @file FaceScreen.h
 * @brief Home screen: animated eyes, status icons, Thai subtitles, settings gear
 * @see wiki/06-Avatar-and-Thai-UI.md
 *
 *   ┌──────────────────────────────────────────┐
 *   │ (🎤) ▂▄▆        14:05          95% ▮  ⚙  │  mic button + status (22 px)
 *   │                                          │
 *   │        ████            ████              │  eyes (move up while a
 *   │        ████            ████              │  subtitle is shown)
 *   │                                          │
 *   │  ผมจำได้ว่าคุณชอบกาแฟดำไม่ใส่น้ำตาล          │  subtitle, 2 lines
 *   │                                     (◠) │  dash shortcut (hidden under a subtitle)
 *   └──────────────────────────────────────────┘
 *
 * Touch: mic = mic on/off · gear = settings menu · gauge (bottom right) = vehicle dash ·
 *        tap elsewhere = stop speaking / wake / poke / retry after a pause · long press = new session
 */

#pragma once

#include <functional>

#include "../util/Deadline.h"
#include "Emotion.h"
#include "Eyes.h"
#include "UiManager.h"

class FaceScreen : public Screen {
public:
    void begin(std::function<void()> openMenu, std::function<void()> openRadio = nullptr,
               std::function<void()> openDash = nullptr);

    // ---- Thread-safe (called from the agent task) ----
    void setState(AvatarState s);
    void setStatus(const UiStatus& s);
    void pushTranscript(char role, const char* fragment);  // 'u' user, 'a' assistant
    void setSystemText(const char* text, uint32_t holdMs = 8000);
    void setHint(const char* text, uint32_t durationMs);
    void setExpression(Emotion e, uint32_t holdMs);
    void setSpeechLevel(float level) { _speechLevel = level; }
    void setEyeColor(uint16_t c) { _eyes.setColor(c); }
    void poke() { _pokePending = true; }  // bouncy reaction to a tap (applied in tick)
    /** Power save: a dim clock face instead of the eyes (the agent also lowers the backlight). */
    void setPowerSave(bool on) { _powerSave = on; }
    bool powerSave() const { return _powerSave; }

    // ---- Screen ----
    void onTouch(const TouchEvent& e) override;
    void tick(uint32_t now, float dt) override;
    void draw(LGFX_Sprite& c) override;

    Eyes& eyes() { return _eyes; }

private:
    Eyes _eyes;
    std::function<void()> _openMenu, _openRadio, _openDash;
    SemaphoreHandle_t _lock = nullptr;

    volatile AvatarState _state = AVATAR_BOOT;
    volatile float _speechLevel = 0;
    volatile bool _pokePending = false;
    volatile bool _powerSave = false;
    void drawClock(LGFX_Sprite& c);
    void drawRadioIcon(LGFX_Sprite& c, int cx, int cy, uint16_t color);
    void drawRadioButton(LGFX_Sprite& c, int cx, int cy, bool active);
    void drawDashButton(LGFX_Sprite& c, int cx, int cy);
    UiStatus _status;

    static constexpr size_t kTextCap = 512;
    char _text[kTextCap] = "";
    char _role = 's';
    deadline::Timer _textTimer;   // subtitle shown until it runs out ...
    bool _textForever = false;    // ... or for good (setSystemText with holdMs 0: errors, setup hints)
    bool _textDirty = false;
    char _hint[96] = "";
    deadline::Timer _hintTimer;
    Emotion _expr = Emotion::Neutral;
    deadline::Timer _exprTimer;
    uint32_t _now = 0;

    // Layout cache (UI task)
    char _render[kTextCap] = "";
    char _renderRole = 's';
    struct Line { uint16_t start, len; };
    Line _lines[2];
    int _lineCount = 0;
    bool _subtitleVisible = false;
    volatile bool _radioShown = false;     // "now playing" line drawn (tap on it opens the radio player)
    volatile bool _radioBtnShown = false;  // radio/MP3 shortcut button drawn (next to dash button)
    volatile bool _dashShown = false;      // dash shortcut drawn (bottom right)

    void layout();
    void drawStatus(LGFX_Sprite& c, const UiStatus& s);
    void drawGear(LGFX_Sprite& c, int cx, int cy, uint16_t color);
    void drawMic(LGFX_Sprite& c, int cx, int cy, bool muted, bool hearing);
};

extern FaceScreen gFace;
