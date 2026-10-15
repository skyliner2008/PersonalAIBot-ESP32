/**
 * @file KeyboardScreen.cpp
 * @brief Thai / English touch keyboard
 */

#include "KeyboardScreen.h"

#include <string.h>

#include "../util/Utf8.h"

KeyboardScreen gKeyboard;

namespace {

// Three rows of ten cells per page. Row 3 cell 0 is SHIFT on the latin pages,
// row 3 cell 9 is always BACKSPACE. Strings are space-separated UTF-8 tokens.
const char* const kRows[7][3] = {
    {"q w e r t y u i o p", "a s d f g h j k l -", "^ z x c v b n m . <"},
    {"Q W E R T Y U I O P", "A S D F G H J K L _", "^ Z X C V B N M , <"},
    {"1 2 3 4 5 6 7 8 9 0", "@ # $ % & * + = ( )", "^ ! ? : ; ' \" / , <"},
    {"~ ` | \\ ^ { } [ ] <", "> _ - . , ? ! @ # $", "^ 0 1 2 3 4 5 6 7 <"},
    {"ก ข ฃ ค ฅ ฆ ง จ ฉ ช", "ซ ฌ ญ ฎ ฏ ฐ ฑ ฒ ณ ด", "ต ถ ท ธ น บ ป ผ ฝ <"},
    {"พ ฟ ภ ม ย ร ล ว ศ ษ", "ส ห ฬ อ ฮ ฤ ฦ ๆ ฯ ฿", "๑ ๒ ๓ ๔ ๕ ๖ ๗ ๘ ๙ <"},
    {"ะ ั า ำ ิ ี ึ ื ุ ู", "เ แ โ ใ ไ ๅ ็ ่ ้ ๊", "๋ ์ ํ ฺ ๐ . , ! ? <"},
};

constexpr int kFieldY = 36, kFieldH = 38;
constexpr int kKeysY = 78;
constexpr int kRowH = 40;

bool isCombining(const char* s) {
    size_t i = 0;
    const uint32_t cp = utf8::next(s, strlen(s), i);
    return utf8::isThaiCombining(cp);
}

}  // namespace

void KeyboardScreen::open(const Options& opt, std::function<void(const String&)> onDone) {
    _opt = opt;
    _title = opt.title;
    _text = opt.initial ? opt.initial : "";
    _onDone = onDone;
    _reveal = !opt.password;
    _page = EN;
    if (!opt.asciiOnly && !opt.password) {
        // Start on Thai if the existing text is Thai
        for (size_t i = 0; i < _text.length(); i++) {
            if ((uint8_t)_text[i] >= 0xE0) {
                _page = TH1;
                break;
            }
        }
    }
    gUi.push(this);
}

void KeyboardScreen::onEnter() {
    _pressed = -1;
    buildKeys();
}

void KeyboardScreen::buildKeys() {
    _keyCount = 0;
    static char tokens[7][3][10][8];  // split once per build (labels must outlive the keys)
    const int cellW = ui::W / 10;
    for (int r = 0; r < 3; r++) {
        // tokenize the row
        const char* row = kRows[_page][r];
        int col = 0;
        const char* p = row;
        while (*p && col < 10) {
            const char* sp = strchr(p, ' ');
            const size_t n = sp ? (size_t)(sp - p) : strlen(p);
            memcpy(tokens[_page][r][col], p, n < 7 ? n : 7);
            tokens[_page][r][col][n < 7 ? n : 7] = '\0';
            col++;
            p = sp ? sp + 1 : p + n;
        }
        for (int i = 0; i < col; i++) {
            Key k{(int16_t)(i * cellW), (int16_t)(kKeysY + r * kRowH), (int16_t)cellW, (int16_t)kRowH,
                  tokens[_page][r][i], K_NONE, 0};
            const bool latin = _page <= SYM2;
            if (r == 2 && i == 9) k.special = K_BKSP;
            else if (r == 2 && i == 0 && latin) k.special = K_SHIFT;
            _keys[_keyCount++] = k;
        }
    }
    // Bottom row: page switches, space, OK
    struct B {
        const char* label;
        int8_t special;
        uint8_t page;
        int w;
        bool thai;
    };
    const B bottom[] = {
        {_page == EN ? "ABC" : "abc", K_PAGE, (uint8_t)(_page == EN ? EN_UP : EN), 42, false},
        {"123", K_PAGE, (uint8_t)SYM, 40, false},
        {"ก-ฝ", K_PAGE, (uint8_t)TH1, 42, true},
        {"พ-ฮ", K_PAGE, (uint8_t)TH2, 42, true},
        {"สระ", K_PAGE, (uint8_t)TH3, 42, true},
        {"เว้นวรรค", K_SPACE, 0, 54, false},
        {"ตกลง", K_OK, 0, 58, false},
    };
    int x = 0;
    int extra = 0;
    if (_opt.asciiOnly) extra = (42 * 3) / 3;  // redistribute the Thai keys' width
    for (const B& b : bottom) {
        if (b.thai && _opt.asciiOnly) continue;
        const int w = b.w + (b.thai ? 0 : (b.special == K_PAGE ? 0 : extra));
        _keys[_keyCount++] = Key{(int16_t)x, (int16_t)(kKeysY + 3 * kRowH), (int16_t)w, (int16_t)kRowH, b.label,
                                 b.special, b.page};
        x += w;
    }
    // stretch the last key to the edge
    _keys[_keyCount - 1].w = (int16_t)(ui::W - _keys[_keyCount - 1].x);
}

int KeyboardScreen::keyAt(int16_t x, int16_t y) const {
    for (int i = 0; i < _keyCount; i++) {
        const Key& k = _keys[i];
        if (x >= k.x && x < k.x + k.w && y >= k.y && y < k.y + k.h) return i;
    }
    return -1;
}

void KeyboardScreen::insert(const char* s) {
    const size_t add = strlen(s);
    if (_text.length() + add > _opt.maxBytes) return;
    if (_opt.asciiOnly) {
        for (size_t i = 0; i < add; i++) {
            if ((uint8_t)s[i] >= 0x80) return;
        }
    }
    _text += s;
}

void KeyboardScreen::backspace() {
    const char* s = _text.c_str();
    size_t n = _text.length();
    if (!n) return;
    n--;
    while (n > 0 && utf8::isContinuation((uint8_t)s[n])) n--;
    _text.remove(n);
}

void KeyboardScreen::typeText(const char* utf8s) { insert(utf8s); }

void KeyboardScreen::press(const Key& k) {
    switch (k.special) {
        case K_SHIFT:
            _page = _page == EN ? EN_UP : _page == EN_UP ? EN : _page == SYM ? SYM2 : SYM;
            buildKeys();
            break;
        case K_BKSP:
            backspace();
            break;
        case K_SPACE:
            insert(" ");
            break;
        case K_PAGE:
            _page = (Page)k.page;
            buildKeys();
            break;
        case K_OK: {
            String result = _text;
            result.trim();
            auto done = _onDone;
            gUi.pop();
            if (done) done(result);
            break;
        }
        default:
            insert(k.label);
            if (_page == EN_UP) {  // one-shot shift
                _page = EN;
                buildKeys();
            }
            break;
    }
}

void KeyboardScreen::onTouch(const TouchEvent& e) {
    if (e.type == TouchEvent::Down) {
        _pressed = keyAt(e.x, e.y);
        return;
    }
    if (e.type == TouchEvent::Up) {
        _pressed = -1;
        return;
    }
    if (e.type == TouchEvent::LongPress) {
        const int i = keyAt(e.x, e.y);
        if (i >= 0 && _keys[i].special == K_BKSP) _text = "";
        return;
    }
    if (e.type != TouchEvent::Tap) return;
    if (UiManager::hitBack(e.x, e.y)) {
        gUi.pop();  // cancel
        return;
    }
    if (_opt.password && e.y >= kFieldY && e.y < kFieldY + kFieldH && e.x > ui::W - 70) {
        _reveal = !_reveal;
        return;
    }
    const int i = keyAt(e.x, e.y);
    if (i >= 0) press(_keys[i]);
}

void KeyboardScreen::tick(uint32_t now, float dt) {
    (void)dt;
    _now = now;
}

void KeyboardScreen::drawKey(LGFX_Sprite& c, const Key& k, bool pressed) {
    uint16_t bg = pressed ? ui::PANEL_HI : ui::PANEL;
    if (k.special == K_OK) bg = pressed ? 0x0420 : 0x0560;
    if (k.special == K_PAGE && k.page == _page) bg = ui::PANEL_HI;
    c.fillSmoothRoundRect(k.x + 2, k.y + 2, k.w - 4, k.h - 4, 6, bg);
    const int cx = k.x + k.w / 2, cy = k.y + k.h / 2;

    if (k.special == K_BKSP) {  // backspace arrow
        c.fillTriangle(cx - 11, cy, cx - 4, cy - 7, cx - 4, cy + 7, ui::TEXT);
        c.fillRect(cx - 4, cy - 3, 14, 6, ui::TEXT);
        return;
    }
    if (k.special == K_SHIFT) {  // up arrow
        const uint16_t col = (_page == EN_UP || _page == SYM2) ? ui::ACCENT : ui::TEXT;
        c.fillTriangle(cx, cy - 9, cx - 8, cy, cx + 8, cy, col);
        c.fillRect(cx - 3, cy, 7, 8, col);
        return;
    }
    const bool small = k.special != K_NONE;
    c.setFont(small ? gUi.thai20() : gUi.thai26());
    c.setTextDatum(textdatum_t::middle_center);
    if (!small && isCombining(k.label)) {
        // Thai vowel/tone mark: draw it over a dim "อ" so it is visible on its own
        c.setTextColor(0x52AA);
        c.drawString("อ", cx, cy);
        c.setTextColor(ui::TEXT);
        c.setTextDatum(textdatum_t::middle_left);
        c.drawString(k.label, cx + c.textWidth("อ") / 2, cy);
        return;
    }
    c.setTextColor(ui::TEXT);
    c.drawString(k.label, cx, cy);
}

void KeyboardScreen::draw(LGFX_Sprite& c) {
    c.fillScreen(ui::BG);
    gUi.drawTitle(c, _title.c_str(), true);

    // Text field
    c.fillSmoothRoundRect(4, kFieldY, ui::W - 8, kFieldH, 8, 0x10A2);
    const int fieldRight = _opt.password ? ui::W - 74 : ui::W - 14;
    String shown;
    if (_reveal) {
        shown = _text;
    } else {
        size_t i = 0;
        const char* s = _text.c_str();
        while (i < _text.length()) {
            utf8::next(s, _text.length(), i);
            shown += "*";
        }
    }
    c.setFont(gUi.thai26());
    c.setTextColor(ui::TEXT);
    c.setTextDatum(textdatum_t::middle_left);
    // show the tail if the text is wider than the field
    const char* p = shown.c_str();
    size_t off = 0;
    while (c.textWidth(p + off) > fieldRight - 14 && off < shown.length()) {
        off++;
        while (off < shown.length() && utf8::isContinuation((uint8_t)p[off])) off++;
    }
    c.drawString(p + off, 14, kFieldY + kFieldH / 2);
    if ((_now / 500) % 2) {  // cursor
        const int cxp = 14 + c.textWidth(p + off) + 2;
        c.fillRect(cxp, kFieldY + 8, 2, kFieldH - 16, ui::ACCENT);
    }
    if (_opt.password) {
        gUi.drawButton(c, ui::W - 68, kFieldY + 5, 60, kFieldH - 10, _reveal ? "ซ่อน" : "แสดง", ui::PANEL_HI);
    }
    // byte counter (limits are in bytes; Thai characters take 3)
    char cnt[16];
    snprintf(cnt, sizeof(cnt), "%u/%u", (unsigned)_text.length(), (unsigned)_opt.maxBytes);
    c.setFont(&fonts::Font0);
    c.setTextColor(ui::DIM);
    c.setTextDatum(textdatum_t::top_right);
    c.drawString(cnt, ui::W - 6, 2);

    for (int i = 0; i < _keyCount; i++) drawKey(c, _keys[i], i == _pressed);
}
