/**
 * @file KeyboardScreen.h
 * @brief On-screen Thai / English keyboard for 320x240 touch
 *
 * Pages (bottom row switches): abc / ABC, 123 / #+=, ก-ฝ, พ-ฮ, สระ (vowels & tone marks).
 * Thai is grouped in alphabetical order (easier to find than Kedmanee with
 * 32-px keys). Backspace removes one code point; long-press on it clears all.
 */

#pragma once

#include <Arduino.h>

#include <functional>

#include "UiManager.h"

class KeyboardScreen : public Screen {
public:
    struct Options {
        const char* title = "";
        const char* initial = "";
        size_t maxBytes = 64;
        bool password = false;   // masked, with a show/hide toggle
        bool asciiOnly = false;  // hide Thai pages (API keys, SSIDs)
    };

    void open(const Options& opt, std::function<void(const String&)> onDone);

    /** Console helper: type text directly (as if typed on the keys). */
    void typeText(const char* utf8);

    void onEnter() override;
    void onTouch(const TouchEvent& e) override;
    void tick(uint32_t now, float dt) override;
    void draw(LGFX_Sprite& c) override;

private:
    enum Page : uint8_t { EN, EN_UP, SYM, SYM2, TH1, TH2, TH3, kPages };
    enum Special : int8_t { K_NONE = 0, K_SHIFT = -1, K_BKSP = -2, K_SPACE = -3, K_OK = -4, K_PAGE = -5 };

    struct Key {
        int16_t x, y, w, h;
        const char* label;  // UTF-8 character or label
        int8_t special;
        uint8_t page;       // target page for K_PAGE
    };

    Options _opt;
    String _title;
    String _text;
    std::function<void(const String&)> _onDone;
    Page _page = EN;
    bool _reveal = false;
    Key _keys[48];
    int _keyCount = 0;
    int _pressed = -1;
    uint32_t _now = 0;

    void buildKeys();
    int keyAt(int16_t x, int16_t y) const;
    void press(const Key& k);
    void backspace();
    void insert(const char* s);
    void drawKey(LGFX_Sprite& c, const Key& k, bool pressed);
};

extern KeyboardScreen gKeyboard;
