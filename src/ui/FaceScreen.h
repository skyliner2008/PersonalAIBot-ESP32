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
 *   └──────────────────────────────────────────┘
 *
 * Touch: mic = mic on/off · gear = settings menu · tap elsewhere = stop speaking / wake / poke /
 *        retry after a pause · long press = new session
 */

#pragma once

#include <functional>

#include "Emotion.h"
#include "Eyes.h"
#include "UiManager.h"

class FaceScreen : public Screen {
public:
    void begin(std::function<void()> openMenu);

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
    std::function<void()> _openMenu;
    SemaphoreHandle_t _lock = nullptr;

    volatile AvatarState _state = AVATAR_BOOT;
    volatile float _speechLevel = 0;
    volatile bool _pokePending = false;
    volatile bool _powerSave = false;
    void drawClock(LGFX_Sprite& c);
    void drawRadioIcon(LGFX_Sprite& c, int cx, int cy, uint16_t color);
    UiStatus _status;

    static constexpr size_t kTextCap = 512;
    char _text[kTextCap] = "";
    char _role = 's';
    uint32_t _textUntil = 0;
    bool _textDirty = false;
    char _hint[96] = "";
    uint32_t _hintUntil = 0;
    Emotion _expr = Emotion::Neutral;
    uint32_t _exprUntil = 0;
    uint32_t _now = 0;

    // Layout cache (UI task)
    char _render[kTextCap] = "";
    char _renderRole = 's';
    struct Line { uint16_t start, len; };
    Line _lines[2];
    int _lineCount = 0;
    bool _subtitleVisible = false;

    void layout();
    void drawStatus(LGFX_Sprite& c, const UiStatus& s);
    void drawGear(LGFX_Sprite& c, int cx, int cy, uint16_t color);
    void drawMic(LGFX_Sprite& c, int cx, int cy, bool muted, bool hearing);
};

extern FaceScreen gFace;
