/**
 * @file EchoWakeGate.h
 * @brief Waking by voice while the speaker plays (radio): "louder than the expected echo" (host tested)
 *
 * Asleep with the radio on, the speaker leaks into the mic. Per 40-ms chunk the agent knows the mic
 * level and the level of what the speaker played during the same samples (the ES8311 DAC loopback on
 * the right I2S slot - sample-aligned; before that a level measured in software, ~100 ms off). It
 * learns coupling = mic / reference on chunks without speech and wakes only on sound well above
 * coupling x reference.
 * The room keeps sounding after loud music stops (reverberation, ~0.3 s): the reference is followed
 * by an envelope that decays per chunk, so the tail is expected, not "speech".
 * Header-only, no Arduino calls.
 */

#pragma once

#include <math.h>

namespace echo {

struct WakeParams {
    float factor = 3.0f;      // wake on sound this many times the expected echo
    float minLevel = 0.02f;   // ... and at least this loud while something plays (speech ~0.05-0.12)
    float decay = 0.6f;       // envelope per chunk (reverb tail)
    float learnMin = 0.003f;  // reference below this: too quiet to learn the coupling from
    float clipLevel = 0.35f;  // mic this loud while playing = the speaker saturates it (volume 85: 0.5 RMS):
                              // the echo is no longer proportional and no voice can be told apart
};

class WakeGate {
public:
    using Params = WakeParams;

    WakeGate() = default;
    explicit WakeGate(const Params& p) : _p(p) {}

    /**
     * The speaker gain changed by a known factor (codec volume, applied after the reference): carry the
     * learned coupling over instead of starting from nothing (relearning in a quiet passage once read
     * the next loud one as 19x the echo). The caller still settles so it adjusts quickly.
     */
    void scaleCoupling(float gain) {
        _coupling *= gain;
        _eMic *= gain * gain;
    }

    /** Speaker level changed / new stream: learn the coupling again (the caller also settles). */
    void relearn() {
        _coupling = _eMic = _eRef = 0.0f;
        _learned = false;
    }

    /**
     * One chunk. `playing`: a radio session is on (its buffer may run dry for a moment, the reference
     * may be ~0 between songs - the strict rules stay). `settling`: right after a change - learn fast,
     * never wake. `ambient`: the room's own noise level (mic RMS with nothing playing): it adds to the
     * echo, and in quiet passages it is most of what the mic hears (measured: 3-9x "echo" peaks without it).
     * Returns true for wake-worthy sound.
     */
    bool update(float mic, float ref, float threshold, bool playing, bool settling, float ambient = 0.0f) {
        _env = ref > _env ? ref : _env * _p.decay;
        const bool active = playing || _env > _p.learnMin;
        if (!active) return mic > threshold;  // nothing playing: the plain voice threshold
        if (mic > _p.clipLevel) {            // saturated by the speaker: neither a wake nor a sample to learn from
            _expected = mic;
            return false;
        }
        const float echo = _coupling * _env;
        _expected = sqrtf(echo * echo + ambient * ambient);  // independent sources add as energy
        const bool loud = !settling && _learned && mic > threshold && mic > _expected * _p.factor && mic > _p.minLevel;
        if (!loud && _env > _p.learnMin) {
            // energy averages (not an average of ratios): loud passages, where mic/ref is reliable, weigh most
            const float a = settling ? 0.2f : 0.03f;
            float m2 = mic * mic - ambient * ambient;
            if (m2 < 0.0f) m2 = 0.0f;
            const float r2 = _env * _env;
            if (!_learned) {
                _eMic = m2;
                _eRef = r2;
                _learned = true;
            } else {
                _eMic += (m2 - _eMic) * a;
                _eRef += (r2 - _eRef) * a;
            }
            _coupling = _eRef > 0.0f ? sqrtf(_eMic / _eRef) : 0.0f;
        }
        // diag: how close music alone comes to the wake factor (only chunks loud enough to matter)
        if (!settling && _expected > 0.0f && mic > _p.minLevel && mic > threshold) {
            const float r = mic / _expected;
            if (r > _peakRatio) _peakRatio = r;
        }
        return loud;
    }

    float coupling() const { return _coupling; }
    float envelope() const { return _env; }
    float expected() const { return _expected; }
    /** Highest mic / expected-echo ratio since the last call (no speech -> how much margin the factor has). */
    float takePeakRatio() {
        const float r = _peakRatio;
        _peakRatio = 0.0f;
        return r;
    }

private:
    Params _p;
    float _env = 0.0f, _coupling = 0.0f, _expected = 0.0f, _peakRatio = 0.0f;
    float _eMic = 0.0f, _eRef = 0.0f;  // energy averages behind the coupling
    bool _learned = false;
};

}  // namespace echo
