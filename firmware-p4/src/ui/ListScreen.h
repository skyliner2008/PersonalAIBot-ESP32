/**
 * @file ListScreen.h
 * @brief Scrollable touch list (settings menus, pickers)
 *
 * Items are rebuilt by a builder callback every time the screen is shown, so
 * values always reflect the current settings.
 */

#pragma once

#include <Arduino.h>

#include <functional>
#include <vector>

#include "UiManager.h"

class ListScreen : public Screen {
public:
    struct Item {
        String label;
        String value;
        std::function<void()> action;
        bool checked = false;
        bool submenu = false;
        bool hasSwatch = false;
        uint16_t swatch = 0;
        uint16_t valueColor = ui::DIM;
    };

    using Builder = std::function<void(ListScreen&)>;

    void setup(const char* title, Builder builder);
    void setTitle(const char* title) { _title = title; }

    Item& add(const String& label, const String& value, std::function<void()> action);
    Item& addSubmenu(const String& label, const String& value, std::function<void()> action);
    void clear() { _items.clear(); }
    /** Rebuild items now (e.g. after an async Wi-Fi scan). */
    void rebuild();
    /** Scroll so that item `index` is visible. */
    void reveal(int index);

    void onEnter() override;
    void onTouch(const TouchEvent& e) override;
    void tick(uint32_t now, float dt) override;
    void draw(LGFX_Sprite& c) override;

    /** Optional per-frame hook (async work such as Wi-Fi scanning). */
    std::function<void(ListScreen&, uint32_t)> onTick;

private:
    static constexpr int kItemH = 46;
    String _title;
    Builder _builder;
    std::vector<Item> _items;
    float _scroll = 0, _velocity = 0;
    int16_t _lastY = 0;
    int _pressed = -1;
    bool _dragging = false;
    bool _fingerDown = false;

    int maxScroll() const;
    int itemAt(int16_t y) const;
};
