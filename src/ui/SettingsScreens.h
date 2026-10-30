/**
 * @file SettingsScreens.h
 * @brief Touch settings menu: profiles, voice, personality, names, Wi-Fi, API key, model, ...
 * @see wiki/09-Assistant-Profiles-and-Tools.md
 */

#pragma once

#include <Arduino.h>

#include <functional>

#include "UiManager.h"

/** Yes/No dialog. */
class ConfirmScreen : public Screen {
public:
    void ask(const char* title, const char* message, const char* okLabel, std::function<void()> onYes);
    void onTouch(const TouchEvent& e) override;
    void draw(LGFX_Sprite& c) override;

private:
    String _title, _message, _ok;
    std::function<void()> _onYes;
};

/** Live system information (refreshes every second). */
class InfoScreen : public Screen {
public:
    void onEnter() override;
    void onTouch(const TouchEvent& e) override;
    void tick(uint32_t now, float dt) override;
    void draw(LGFX_Sprite& c) override;

private:
    String _text;
    uint32_t _next = 0;
};

/** 3-target touch calibration (fixes swapped / mirrored touch axes of a board). */
class CalibrationScreen : public Screen {
public:
    void onEnter() override;
    void onTouch(const TouchEvent& e) override;
    void tick(uint32_t now, float dt) override;
    void draw(LGFX_Sprite& c) override;

private:
    static constexpr int kPoints = 3;
    int _index = 0;
    int16_t _raw[kPoints][2] = {};
    int16_t _candidate[2] = {};
    bool _haveCandidate = false;
    uint32_t _doneAt = 0;
    bool _failed = false;
    void finish();
};

namespace settingsui {
/** Show touch calibration (also opened automatically when the board was never calibrated). */
void openCalibration();
/** Open the main settings menu (call from the UI task). */
void openMenu();
/** T141: reminders, timers and price alerts - view, edit, delete (the bell on the face). */
void openAlerts();
/** Open a specific page by name: "menu", "wifi", "apikey", "agent", "user", "profile", "voice", "info". */
bool openPage(const char* page);
}  // namespace settingsui
