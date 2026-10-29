/**
 * @file NoiseFloor.h
 * @brief Adaptive background-noise estimate for the local VAD (header-only, host tested)
 *
 * speech = level > noise x factor (never below minLevel). Below that the estimate follows the
 * background. It used to follow ONLY below it: a background that jumps above factor x the old
 * estimate (a fan or an air purifier switched on next to the board) then counted as speech for
 * ever - the mic streamed non-stop, the assistant never went to sleep and woke itself up again
 * right after. Real speech has short pauses; a level that stays above the threshold for
 * `riseAfterChunks` chunks in a row is background, so the estimate climbs towards it slowly.
 */

#pragma once

#include <stdint.h>

namespace vad {

class NoiseFloor {
public:
    static constexpr float kFollow = 0.05f;  // per chunk below the threshold
    static constexpr float kRise = 0.01f;    // per chunk once a loud run counts as background (~4 s at 40 ms)

    NoiseFloor(float initial, float minLevel, float factor, uint32_t riseAfterChunks)
        : _noise(initial), _minLevel(minLevel), _factor(factor), _riseAfter(riseAfterChunks) {}

    float threshold() const {
        const float t = _noise * _factor;
        return t < _minLevel ? _minLevel : t;
    }

    /**
     * One chunk's RMS level. Returns true for speech (decided on the threshold BEFORE this chunk).
     * `allowRise` false (e.g. the radio is sounding: its leak is handled by the echo estimate) keeps
     * the old behaviour: the estimate only follows quieter chunks.
     */
    bool update(float level, bool allowRise) {
        const bool voice = level > threshold();
        if (!voice) {
            _noise += (level - _noise) * kFollow;
            _loudRun = 0;
        } else if (!allowRise) {
            _loudRun = 0;
        } else if (_loudRun < _riseAfter) {
            _loudRun++;
        } else {
            _noise += (level - _noise) * kRise;
        }
        return voice;
    }

    float noise() const { return _noise; }
    uint32_t loudRun() const { return _loudRun; }

private:
    float _noise;
    float _minLevel;  // (not "_min": a macro in Arduino.h)
    float _factor;
    uint32_t _riseAfter;
    uint32_t _loudRun = 0;
};

}  // namespace vad
