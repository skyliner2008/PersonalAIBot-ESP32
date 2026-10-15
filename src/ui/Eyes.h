/**
 * @file Eyes.h
 * @brief Cute, bouncy robot eyes (Cozmo / esp32-eyes style) for a 320x240 landscape screen
 * @see wiki/06-Avatar-and-Thai-UI.md
 *
 * Original implementation (not derived from esp32-eyes code, which is GPL-3.0):
 * every eye parameter (size, corner radius, eyelids, cheek, offset) is driven by
 * an under-damped spring, so expression changes overshoot slightly and "boing".
 *
 * Shape model per eye:
 *   rounded rectangle (w, h, radius)
 *   - top eyelid cut (lidTop = depth 0..1, tilt -1..1: + slants down to the nose = angry,
 *     - slants down outwards = sad)
 *   - flat bottom-lid cut (lidBottom 0..1)
 *   - cheek cut: an ellipse rising from below (cheek 0..1) -> happy crescents ^ ^
 * plus optional hearts (love) and blush.
 */

#pragma once

#include <LovyanGFX.hpp>
#include <stdint.h>

#include "Emotion.h"

class Eyes {
public:
    enum class Mode : uint8_t { Awake, Listening, Thinking, Speaking, Sleeping, Searching };

    void begin(int screenW, int screenH);

    /** Base expression (smoothly animated, with a little pop). */
    void setEmotion(Emotion e);
    Emotion emotion() const { return _emotion; }

    void setMode(Mode m);
    Mode mode() const { return _mode; }

    void setColor(uint16_t rgb565) { _color = rgb565; }
    void setCenterY(float y) { _centerY.target = y; }
    /** 0..1 speech level for the talking bounce. */
    void setSpeechLevel(float level) { _speech = level; }

    /** Look direction in -1..1 (overrides the random gaze for a while). */
    void lookAt(float x, float y, uint32_t holdMs = 1500);
    void blink();
    /** Bouncy "pop" (scale impulse). */
    void kick(float strength = 1.0f);

    void update(uint32_t nowMs, float dt);
    void draw(LGFX_Sprite& c);

private:
    struct Spring {
        float x = 0, v = 0, target = 0;
        void snap(float t) { x = target = t; v = 0; }
        void step(float dt, float k, float d) {
            const float a = k * (target - x) - d * v;
            v += a * dt;
            x += v * dt;
        }
    };
    struct EyeSprings {
        Spring w, h, r, lidTop, tilt, cheek, lidBottom, dy;
    };
    struct Shape {
        float w, h, r, lidTop, tilt, cheek, lidBottom, dy;
    };

    int _sw = 320, _sh = 240;
    uint16_t _color = 0x5EFF;
    Emotion _emotion = Emotion::Neutral;
    Mode _mode = Mode::Awake;

    EyeSprings _left, _right;
    Spring _lookX, _lookY, _centerY, _scale, _blush, _hearts, _sleep;

    float _speech = 0.0f, _speechSmooth = 0.0f;
    float _time = 0.0f;
    uint32_t _now = 0;

    // blink
    bool _blinking = false;
    uint32_t _blinkStart = 0, _nextBlink = 0;
    bool _doubleBlink = false;
    float _blinkFactor = 1.0f;

    // gaze
    uint32_t _nextLook = 0, _lookHoldUntil = 0;

    void applyTargets();
    void shapeFor(Emotion e, bool right, Shape& s) const;
    void stepEye(EyeSprings& e, float dt);
    void drawEye(LGFX_Sprite& c, float cx, float cy, const EyeSprings& e, bool right, float squash);
    void drawHeart(LGFX_Sprite& c, float cx, float cy, float size, uint16_t color);
    void drawZzz(LGFX_Sprite& c, float x, float y);
    void updateBlink();
    void updateGaze();
};
