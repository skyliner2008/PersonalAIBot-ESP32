/**
 * @file SearchGate.h
 * @brief When the Live session may offer Google Search (grounding has its own quota)
 * @see wiki/07-Troubleshooting-Log.md (T028, T045, T048)
 *
 * The Live API closes a session with the same "exceeded your current quota" text for a Search
 * grounding allowance the project does not have (or used up) and for other limits - notably a
 * session still open on the server after a reboot without a close handshake (power cut, RESET,
 * flashing). A REST check cannot tell them apart: on the free tier Search grounding is "not
 * available" for the REST models (always 429) while it is "supported" for the Live models. So the
 * Live connection itself decides:
 *   quota close while Search was offered       -> SUSPECT: reconnect at once without Search
 *   that session gets ready                    -> CONFIRMED: Search off for 6 h, 12 h, 24 h, 48 h
 *                                                 (doubling while it keeps failing), kept across reboots
 *   that one also closes with quota            -> not Search (stale session / other limit): cleared
 *   a session WITH Search gets ready           -> the doubling starts over
 * Header-only and free of Arduino calls (host tested).
 */

#pragma once

#include <stdint.h>

namespace search {

class Gate {
public:
    static constexpr uint32_t kConfirmedSec = 6 * 3600;  // first confirmed pause, doubles up to 48 h
    static constexpr uint8_t kMaxDoublings = 3;
    static constexpr uint32_t kSuspectSec = 10 * 60;     // a suspicion nothing settled expires by itself

    /** Boot: the deadline (unix epoch, 0 = none) and failure streak saved before the reboot. */
    void restore(uint32_t offUntilEpoch, uint8_t strikes) {
        _offUntilEpoch = offUntilEpoch;
        _strikes = strikes;
    }

    /** Live quota close while the setup offered Search. True the first time (log it). */
    bool onLiveQuota(uint32_t mono) {
        const bool fresh = !suspect(mono);
        _suspectUntil = mono + kSuspectSec;
        return fresh;
    }

    /** Live quota close WITHOUT Search while suspect: Search was not the cause. True if it was suspect. */
    bool onQuotaWithoutSearch(uint32_t mono) {
        if (!suspect(mono)) return false;
        _suspectUntil = 0;
        return true;
    }

    /**
     * A session WITHOUT Search got ready. If Search was suspect, that confirms it: returns true and
     * fills what to save (`persistEpoch` 0 when the clock is unknown - see needsSave()).
     */
    bool onReadyWithoutSearch(uint32_t mono, uint32_t epoch, uint32_t& persistEpoch, uint8_t& persistStrikes,
                              uint32_t& pauseSec) {
        if (!suspect(mono)) return false;
        _suspectUntil = 0;
        pauseSec = kConfirmedSec << (_strikes < kMaxDoublings ? _strikes : kMaxDoublings);
        if (_strikes < 255) _strikes++;
        _confirmedUntil = mono + pauseSec;
        _offUntilEpoch = epoch ? epoch + pauseSec : 0;
        persistEpoch = _offUntilEpoch;
        persistStrikes = _strikes;
        return true;
    }

    /** A session WITH Search got ready: Search works. True if a saved streak must be cleared. */
    bool onReadyWithSearch() {
        const bool had = _strikes != 0 || _offUntilEpoch != 0;
        _strikes = 0;
        _offUntilEpoch = 0;
        _confirmedUntil = 0;
        _suspectUntil = 0;
        return had;
    }

    /** User asked to try Search again now. True if the saved state must be cleared. */
    bool reset() { return onReadyWithSearch(); }

    /** May the next setup offer Search? `epoch` 0 = clock not synced yet. */
    bool available(uint32_t mono, uint32_t epoch) const {
        if (suspect(mono) || mono < _confirmedUntil) return false;
        if (_offUntilEpoch) return epoch != 0 && epoch >= _offUntilEpoch;  // unknown clock: keep it off
        return true;
    }

    /** Confirmed before the clock was synced: now that it is, the deadline can be saved. */
    bool needsSave(uint32_t mono, uint32_t epoch, uint32_t& persist) {
        if (!epoch || _offUntilEpoch || mono >= _confirmedUntil) return false;
        _offUntilEpoch = epoch + (_confirmedUntil - mono);
        persist = _offUntilEpoch;
        return true;
    }

    /** The saved deadline has passed (clear it from flash; the streak stays). */
    bool savedExpired(uint32_t epoch) const { return _offUntilEpoch && epoch && epoch >= _offUntilEpoch; }
    void clearSaved() { _offUntilEpoch = 0; }

    bool suspect(uint32_t mono) const { return mono < _suspectUntil; }
    bool confirmed(uint32_t mono, uint32_t epoch) const {
        return mono < _confirmedUntil || (_offUntilEpoch && (!epoch || epoch < _offUntilEpoch));
    }
    uint8_t strikes() const { return _strikes; }

    /** Seconds until Search is offered again (0 = available or unknown clock). */
    uint32_t secondsLeft(uint32_t mono, uint32_t epoch) const {
        uint32_t left = 0;
        if (mono < _suspectUntil) left = _suspectUntil - mono;
        if (mono < _confirmedUntil && _confirmedUntil - mono > left) left = _confirmedUntil - mono;
        if (_offUntilEpoch && epoch && epoch < _offUntilEpoch && _offUntilEpoch - epoch > left) {
            left = _offUntilEpoch - epoch;
        }
        return left;
    }

private:
    uint32_t _suspectUntil = 0;    // monoSec
    uint32_t _confirmedUntil = 0;  // monoSec (also covers a confirmation made before the clock synced)
    uint32_t _offUntilEpoch = 0;   // unix seconds, survives reboots (NVS "rt"/"srchoff")
    uint8_t _strikes = 0;          // confirmations in a row (NVS "rt"/"srchn"): the pause doubles
};

}  // namespace search
