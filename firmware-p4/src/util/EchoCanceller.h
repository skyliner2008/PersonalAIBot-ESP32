/**
 * @file EchoCanceller.h
 * @brief Small NLMS echo canceller on the ES8311 DAC-loopback reference (header-only, host tested)
 * @see wiki/14-AEC-WakeWord-Plan.md, T057
 *
 * The right I2S slot carries exactly what the speaker plays, sample-aligned with the mic (left slot).
 * Measured on the board (radio, volume 70): the echo arrives 1.4 ms after the reference and a 128-tap
 * (8 ms) NLMS filter removes 27 dB of it - what remains per 40-ms chunk is <= 0.004 while speech near
 * the board is 0.04-0.09. Comparing loudness alone (EchoWakeGate) needed speech 3x louder than the
 * echo and never woke on normal speech.
 *
 *   out = mic - w * ref        (always; with no reference playing the output is the mic itself)
 *   w adapts only while the reference plays and the chunk is not double talk: a first pass with
 *   frozen w measures the residual; if it is far above the residual floor (someone talks) the chunk
 *   keeps that output and w is left alone, else a second pass adapts. `force` (just after the radio
 *   started / the volume changed) adapts regardless, so it converges before it trusts itself.
 */

#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#if defined(__XTENSA__)
#include "dsps_dotprod.h"  // esp-dsp (in the Arduino SDK): Xtensa FPU assembly, any alignment
#endif
// Arduino builds with -Os: the per-sample loops ran at 10.5 ms per 40-ms chunk (26 % of a core)
#define ECHO_HOT __attribute__((optimize("O3")))
#else
#define ECHO_HOT
#endif

namespace echo {

class Canceller {
public:
    static constexpr size_t kTaps = 128;
    static_assert(kTaps % 4 == 0 && (kTaps & (kTaps - 1)) == 0, "unrolled by 4, energy recount uses a mask");
    static constexpr size_t kMaxChunk = 640;  // one agent chunk (40 ms @ 16 kHz)
    static constexpr float kMu = 0.3f;  // 0.5 learned quiet talk the double-talk test missed (board: filter went off)
    static constexpr float kDtFactor = 3.0f;   // residual this many times the floor = someone talks
    static constexpr float kDtMin = 0.008f;    // ... and at least this loud (mic RMS 0-1)
    // ... and at least this share of the predicted echo: what imperfect cancellation leaves is 5-20 % of
    // the echo, a voice is comparable or louder. Without it a loud echo (micgain 6: 0.06-0.17) left more
    // than kDtMin after a good 20 dB, counted as talk, froze the filter and it drifted off (board: dt 113,
    // bypass 76 per 45 s)
    static constexpr float kDtEchoRatio = 0.3f;
    static constexpr int kClipLevel = 31000;  // |sample| this high = clipped / about to clip
    static constexpr float kRefQuiet = 0.002f; // reference below this: nothing to learn from
    static constexpr int kDtMaxRun = 100;      // "talk" for 4 s on end = the echo path changed: adapt again
    // NLMS regularisation: a quiet reference must not turn the step into a jump (as if RMS 0.003)
    static constexpr float kEnergyFloor = kTaps * 1e-5f;

    // Both signals are high-passed first: this small speaker barely plays bass (measured: 200 Hz reaches
    // the mic at 0.5 % of the reference, 1 kHz at 27 %), so bass-heavy music made the reference loud
    // while the mic heard little - the filter over-predicted and went off (replay of a board recording:
    // every bypass was a bass hit). Speech intelligibility is above it as well.
    static constexpr float kHpfHz = 250.0f;
    static constexpr float kFs = 16000.0f;

    Canceller() {
        const float w0 = 2.0f * 3.14159265f * kHpfHz / kFs, c = cosf(w0), al = sinf(w0) / (2.0f * 0.7071f);
        const float a0 = 1.0f + al;
        _b0 = (1.0f + c) / 2.0f / a0;
        _b1 = -(1.0f + c) / a0;
        _b2 = _b0;
        _a1 = -2.0f * c / a0;
        _a2 = (1.0f - al) / a0;
        reset();
    }

    void reset() {
        memset(_w, 0, sizeof(_w));
        memset(_x, 0, sizeof(_x));
        _pos = 0;
        _energy = 0.0f;
        _floor = 0.0f;
        _idle = true;
        _dtRun = 0;
        _badRun = 0;
        _hpMic[0] = _hpMic[1] = _hpRef[0] = _hpRef[1] = 0.0f;
    }

    /** The speaker gain changed by a known factor (codec volume, after the reference): the echo scales. */
    void scale(float gain) {
        for (size_t i = 0; i < kTaps; i++) _w[i] *= gain;
    }

    /**
     * n samples (<= kMaxChunk): out = mic - echo estimate (out may be the mic buffer). Returns the
     * residual RMS (0-1). See micRms(), refRms(), doubleTalk(), adapted().
     */
    ECHO_HOT float process(const int16_t* mic, const int16_t* ref, int16_t* out, size_t n, bool force) {
        if (n > kMaxChunk) n = kMaxChunk;
        float rAcc = 0.0f, mAcc = 0.0f, rawAcc = 0.0f;
        bool anyRef = false;
        int peak = 0;
        for (size_t i = 0; i < n; i++) {
            const int a = mic[i] < 0 ? -(int)mic[i] : mic[i];
            if (a > peak) peak = a;
            const float raw = mic[i] * kScale;
            rawAcc += raw * raw;
            const float m = hpf(raw, _hpMic);  // always run: the filter state stays continuous
            _hm[i] = m;
            mAcc += m * m;
            if (ref[i]) anyRef = true;
            const float r = hpf(ref[i] * kScale, _hpRef);
            _hr[i] = r;
            rAcc += r * r;
        }
        _micRms = n ? sqrtf(mAcc / n) : 0.0f;  // high-passed: what the canceller can work on
        // the speaker drove the ADC to full scale (micgain 6 + radio: peaks 0.6-0.8 and more): the echo
        // is no longer linear - nothing to learn from, and its leftover is not a voice
        _clipped = peak >= kClipLevel;
        _refRms = n ? sqrtf(rAcc / n) : 0.0f;
        _doubleTalk = false;
        _adapted = false;
        _bypassed = false;
        _echoRms = 0.0f;
        _passthrough = !anyRef && _idle;
        if (_passthrough) {  // nothing played for a whole filter length: the mic is the residual
            if (out != mic) memcpy(out, mic, n * sizeof(int16_t));
            return n ? sqrtf(rawAcc / n) : 0.0f;
        }
        _idle = !anyRef;  // all-zero chunk: after it the history is zeros (cleared below)

        // pass 1: frozen filter (from a saved state, so pass 2 can start over)
        memcpy(_xSave, _x, sizeof(_x));
        const size_t pos0 = _pos;
        float eAcc = 0.0f, yAcc1 = 0.0f;
        for (size_t i = 0; i < n; i++) {
            push(_hr[i]);
            const float y = dot();
            const float e = _hm[i] - y;
            _e1[i] = e;
            eAcc += e * e;
            yAcc1 += y * y;
        }
        float res = n ? sqrtf(eAcc / n) : 0.0f;
        const float echo1 = n ? sqrtf(yAcc1 / n) : 0.0f;
        const bool learnable = _refRms > kRefQuiet && !_clipped;
        _doubleTalk = learnable && !force && _floor > 0.0f && res > kDtFactor * _floor && res > kDtMin &&
                      res > kDtEchoRatio * echo1;
        // a filter frozen as "talk" cannot follow a changed echo path (seen right after going to sleep:
        // the residual stayed above the mic) - nobody talks 4 s without a pause
        if (_doubleTalk && ++_dtRun > kDtMaxRun) {
            _doubleTalk = false;
            force = true;
            _dtRun = 0;
        } else if (!_doubleTalk) {
            _dtRun = 0;
        }
        if (learnable && (force || !_doubleTalk)) {  // pass 2: adapt
            memcpy(_x, _xSave, sizeof(_x));
            _pos = pos0;
            resyncEnergy();
            eAcc = 0.0f;
            for (size_t i = 0; i < n; i++) {
                push(_hr[i]);
                // the running sum drifts (float add/subtract for minutes): after loud music the error
                // could exceed a quiet passage's true energy -> a huge step -> the filter diverged and
                // added a fake echo (residual 0.024 with the mic at 0.004). Recount it exactly.
                if ((i & (kTaps - 1)) == kTaps - 1) resyncEnergy();
                const float e = _hm[i] - dot();
                _e1[i] = e;
                eAcc += e * e;
                const float k = kMu * e / (_energy + kEnergyFloor);
                const float* x = &_x[_pos];
                for (size_t t = 0; t < kTaps; t += 4) {  // kTaps is a multiple of 4
                    _w[t] += k * x[t];
                    _w[t + 1] += k * x[t + 1];
                    _w[t + 2] += k * x[t + 2];
                    _w[t + 3] += k * x[t + 3];
                }
            }
            res = n ? sqrtf(eAcc / n) : 0.0f;
            _floor = _floor <= 0.0f ? res : _floor + (res - _floor) * 0.1f;
            _adapted = true;
        }
        // never worse than nothing: a residual louder than the mic means the filter is wrong - pass the
        // mic through, and start over if it keeps happening
        if (res > _micRms * 1.25f && res > kDtMin * 0.5f) {
            _bypasses++;
            _bypassed = true;
            // reset only when clearly wrong: with someone talking the residual is about the mic itself,
            // and resetting a good filter then (seen on the board) threw away what it had learned
            if (res > _micRms * 2.0f && ++_badRun >= 3) {
                memset(_w, 0, sizeof(_w));
                _floor = 0.0f;
                _badRun = 0;
                _resets++;
            }
            if (out != mic) memcpy(out, mic, n * sizeof(int16_t));
            res = _micRms;
        } else {
            _badRun = 0;
            float yAcc = 0.0f;
            for (size_t i = 0; i < n; i++) {
                const float y = _hm[i] - _e1[i];  // the echo estimate that was removed
                yAcc += y * y;
                float v = _e1[i] * 32768.0f;
                out[i] = (int16_t)(v > 32767.0f ? 32767 : (v < -32768.0f ? -32768 : v));
            }
            _echoRms = n ? sqrtf(yAcc / n) : 0.0f;
        }
        if (_idle) {  // the reference went silent: start the next one from a clean history
            memset(_x, 0, sizeof(_x));
            _energy = 0.0f;
        }
        return res;
    }

    /** High-passed mic level of the last chunk (the same band as the residual - compare levels with it). */
    float micRms() const { return _micRms; }
    /** Nothing was playing: the last chunk went out raw (not high-passed). */
    bool passthrough() const { return _passthrough; }
    float refRms() const { return _refRms; }
    float residualFloor() const { return _floor; }
    bool doubleTalk() const { return _doubleTalk; }
    bool adapted() const { return _adapted; }
    /** The last chunk was passed through raw (the filter was wrong): its level is the echo, not the room. */
    bool bypassed() const { return _bypassed; }
    /** The last chunk had samples at full scale (non-linear echo). */
    bool clipped() const { return _clipped; }
    /** RMS of the echo estimate removed from the last chunk (what a voice has to stand out against). */
    float echoRms() const { return _echoRms; }
    /** Echo removed on the last adapted chunk, dB (diag). */
    float erleDb() const { return _adapted && _floor > 0.0f && _micRms > 0.0f ? 20.0f * log10f(_micRms / _floor) : 0.0f; }
    /** Chunks passed through because the filter made them louder / filter restarts (diag, reset on read). */
    uint32_t takeBypasses() { const uint32_t v = _bypasses; _bypasses = 0; return v; }
    uint32_t takeResets() { const uint32_t v = _resets; _resets = 0; return v; }

private:
    static constexpr float kScale = 1.0f / 32768.0f;
    float _w[kTaps];
    float _x[2 * kTaps];      // reference history, duplicated: the window &_x[_pos] is always contiguous
    float _xSave[2 * kTaps];
    float _e1[kMaxChunk];
    float _hm[kMaxChunk], _hr[kMaxChunk];  // high-passed mic / reference of the current chunk
    float _b0 = 1, _b1 = 0, _b2 = 0, _a1 = 0, _a2 = 0;
    float _hpMic[2] = {0, 0}, _hpRef[2] = {0, 0};  // biquad states (transposed direct form II)
    size_t _pos = 0;
    float _energy = 0.0f;     // sum of squares over the window (NLMS normalisation)
    float _floor = 0.0f;      // residual RMS on adapted chunks (the double-talk yardstick)
    float _micRms = 0.0f, _refRms = 0.0f, _echoRms = 0.0f;
    bool _doubleTalk = false, _adapted = false, _idle = true, _bypassed = false, _clipped = false, _passthrough = true;
    int _dtRun = 0, _badRun = 0;
    uint32_t _bypasses = 0, _resets = 0;

    float hpf(float x, float* z) const {
        const float y = _b0 * x + z[0];
        z[0] = _b1 * x - _a1 * y + z[1];
        z[1] = _b2 * x - _a2 * y;
        return y;
    }

    void push(float s) {
        _pos = (_pos + kTaps - 1) % kTaps;
        const float leaving = _x[_pos + kTaps];  // oldest sample of the previous window
        _energy += s * s - leaving * leaving;
        if (_energy < 0.0f) _energy = 0.0f;
        _x[_pos] = s;
        _x[_pos + kTaps] = s;
    }

    ECHO_HOT void resyncEnergy() {
        const float* x = &_x[_pos];
        float e = 0.0f;
#if defined(ESP_PLATFORM) && defined(__XTENSA__)
        dsps_dotprod_f32_ae32(x, x, &e, (int)kTaps);
#else
        for (size_t t = 0; t < kTaps; t++) e += x[t] * x[t];
#endif
        _energy = e;
    }

    ECHO_HOT float dot() const {
        const float* x = &_x[_pos];
        float y = 0.0f;
#if defined(ESP_PLATFORM) && defined(__XTENSA__)
        dsps_dotprod_f32_ae32(_w, x, &y, (int)kTaps);
#else
        for (size_t t = 0; t < kTaps; t++) y += _w[t] * x[t];
#endif
        return y;
    }
};

}  // namespace echo
