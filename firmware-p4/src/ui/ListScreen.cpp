/**
 * @file ListScreen.cpp
 * @brief Scrollable list with kinetic drag
 */

#include "ListScreen.h"

void ListScreen::setup(const char* title, Builder builder) {
    _title = title;
    _builder = builder;
}

ListScreen::Item& ListScreen::add(const String& label, const String& value, std::function<void()> action) {
    Item it;
    it.label = label;
    it.value = value;
    it.action = action;
    _items.push_back(it);
    return _items.back();
}

ListScreen::Item& ListScreen::addSubmenu(const String& label, const String& value, std::function<void()> action) {
    Item& it = add(label, value, action);
    it.submenu = true;
    return it;
}

void ListScreen::rebuild() {
    _items.clear();
    if (_builder) _builder(*this);
    if (_scroll > maxScroll()) _scroll = (float)maxScroll();
}

void ListScreen::onEnter() {
    _pressed = -1;
    _velocity = 0;
    rebuild();
}

void ListScreen::reveal(int index) {
    const int visible = ui::H - ui::kTitleH;
    const int top = index * kItemH;
    if (top < _scroll) _scroll = (float)top;
    if (top + kItemH > _scroll + visible) _scroll = (float)(top + kItemH - visible);
    if (_scroll > maxScroll()) _scroll = (float)maxScroll();
}

int ListScreen::maxScroll() const {
    const int content = (int)_items.size() * kItemH;
    const int visible = ui::H - ui::kTitleH;
    return content > visible ? content - visible : 0;
}

int ListScreen::itemAt(int16_t y) const {
    if (y < ui::kTitleH) return -1;
    const int idx = (int)((y - ui::kTitleH + _scroll) / kItemH);
    return idx >= 0 && idx < (int)_items.size() ? idx : -1;
}

void ListScreen::onTouch(const TouchEvent& e) {
    switch (e.type) {
        case TouchEvent::Down:
            _lastY = e.y;
            _velocity = 0;
            _dragging = false;
            _fingerDown = true;
            _pressed = itemAt(e.y);
            break;
        case TouchEvent::Move: {
            const float dy = (float)(_lastY - e.y);
            _scroll += dy;
            _velocity = dy;
            _lastY = e.y;
            _dragging = true;
            _pressed = -1;
            if (_scroll < 0) _scroll = 0;
            if (_scroll > maxScroll()) _scroll = (float)maxScroll();
            break;
        }
        case TouchEvent::Up:
            if (!_dragging) _velocity = 0;
            _fingerDown = false;
            _pressed = -1;
            break;
        case TouchEvent::Tap: {
            if (UiManager::hitBack(e.x, e.y)) {
                gUi.pop();
                return;
            }
            const int idx = itemAt(e.y);
            if (idx >= 0 && _items[(size_t)idx].action) {
                auto action = _items[(size_t)idx].action;  // copy: the action may rebuild the list
                action();
            }
            break;
        }
        default:
            break;
    }
}

void ListScreen::tick(uint32_t now, float dt) {
    (void)dt;
    if (onTick) onTick(*this, now);
    if (!_fingerDown && fabsf(_velocity) > 0.5f) {  // kinetic scroll after release
        _scroll += _velocity;
        _velocity *= 0.88f;
        if (_scroll < 0) _scroll = 0, _velocity = 0;
        if (_scroll > maxScroll()) _scroll = (float)maxScroll(), _velocity = 0;
    }
}

void ListScreen::draw(LGFX_Sprite& c) {
    c.fillScreen(ui::BG);
    c.setClipRect(0, ui::kTitleH, ui::W, ui::H - ui::kTitleH);
    const int first = (int)(_scroll / kItemH);
    for (int i = first; i < (int)_items.size(); i++) {
        const int y = ui::kTitleH + i * kItemH - (int)_scroll;
        if (y > ui::H) break;
        const Item& it = _items[(size_t)i];
        if (i == _pressed) c.fillRect(0, y, ui::W, kItemH, ui::PANEL_HI);
        c.drawFastHLine(12, y + kItemH - 1, ui::W - 24, ui::LINE);

        int labelX = 14;
        if (it.hasSwatch) {
            c.fillCircle(26, y + kItemH / 2, 10, it.swatch);
            labelX = 46;
        }
        int right = ui::W - 12;
        if (it.submenu) {  // ">" chevron
            for (int t = 0; t < 2; t++) {
                c.drawLine(right - 8 + t, y + 16, right - 2 + t, y + 23, ui::DIM);
                c.drawLine(right - 2 + t, y + 23, right - 8 + t, y + 30, ui::DIM);
            }
            right -= 18;
        }
        if (it.checked) {  // check mark
            for (int t = 0; t < 3; t++) {
                c.drawLine(right - 18, y + 22 + t, right - 12, y + 28 + t, ui::OK);
                c.drawLine(right - 12, y + 28 + t, right - 1, y + 15 + t, ui::OK);
            }
            right -= 26;
        }
        int valueW = 0;
        if (it.value.length()) {
            c.setFont(gUi.thai20());
            valueW = c.textWidth(it.value.c_str());
            const int maxV = (right - labelX) / 2;
            if (valueW > maxV) valueW = maxV;
            gUi.drawFitted(c, it.value.c_str(), right, y + kItemH / 2, maxV, gUi.thai20(), it.valueColor,
                           textdatum_t::middle_right);
        }
        gUi.drawFitted(c, it.label.c_str(), labelX, y + kItemH / 2, right - labelX - valueW - 10, gUi.thai26(),
                       ui::TEXT);
    }
    // scroll indicator
    if (maxScroll() > 0) {
        const int track = ui::H - ui::kTitleH - 8;
        const int content = (int)_items.size() * kItemH;
        const int barH = track * (ui::H - ui::kTitleH) / content;
        const int barY = ui::kTitleH + 4 + (int)((track - barH) * _scroll / maxScroll());
        c.fillRoundRect(ui::W - 4, barY, 3, barH, 1, ui::DIM);
    }
    c.clearClipRect();
    gUi.drawTitle(c, _title.c_str(), true);
}
