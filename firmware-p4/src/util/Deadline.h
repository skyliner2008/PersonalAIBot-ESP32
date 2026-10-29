/**
 * @file Deadline.h
 * @brief millis()-based deadlines that survive the 32-bit wrap (every 49.7 days) - host tested
 *
 * Two traps on a board left running for weeks:
 *  - `now >= deadline` breaks at the wrap: a deadline set just before it is a huge number and
 *    millis() starts again from 0, so the check stays false for another 49.7 days (reminders,
 *    status updates and the eyes' blinking stopped).
 *  - a signed difference fixes that, but a deadline that is never refreshed goes stale: 24.8 days
 *    later it reads as "in the future" again (0 used as "off" turned the mic's follow-up window
 *    on for good once the uptime passed 24.8 days). A Timer disarms itself instead.
 */

#pragma once

#include <stdint.h>

namespace deadline {

/** True once `now` has reached `at` (both millis(), less than ~24.8 days apart). */
inline bool reached(uint32_t now, uint32_t at) { return (int32_t)(now - at) >= 0; }

/**
 * ms from `t` to `now`. A `t` stamped a little AFTER `now` was sampled (another handler ran in
 * between) counts as 0 - not as 49 days; an old `t` is the true elapsed time up to 49.7 days
 * (a signed check read anything older than 24.8 days as "just now").
 */
inline uint32_t elapsed(uint32_t now, uint32_t t) {
    const uint32_t ahead = t - now;
    return ahead != 0 && ahead < 60000u ? 0 : now - t;
}

/** A one-shot window: armed until `at`, then off for good (never reads stale). */
class Timer {
public:
    void arm(uint32_t now, uint32_t ms) {
        _at = now + ms;
        _armed = true;
    }
    void cancel() { _armed = false; }
    /** Still running at `now`? Disarms once it has run out. */
    bool active(uint32_t now) {
        if (_armed && reached(now, _at)) _armed = false;
        return _armed;
    }
    /** ms left (0 when off). */
    uint32_t remaining(uint32_t now) { return active(now) ? _at - now : 0; }

private:
    uint32_t _at = 0;
    bool _armed = false;
};

}  // namespace deadline
