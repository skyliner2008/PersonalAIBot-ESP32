/**
 * @file RadioResampler.h
 * @brief Decoder PCM (any rate, mono/stereo) -> mono 24 kHz for the speaker mixer
 *
 * Stereo is averaged to mono, a 2nd-order Butterworth low-pass (RBJ biquad) removes what would
 * alias above the 12 kHz Nyquist limit, then linear interpolation resamples to the output rate.
 * Good enough for a 1-2 W speaker; header-only so the host tests can check it.
 */

#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>

class RadioResampler {
public:
    void reset(uint32_t inRate, uint32_t outRate = 24000) {
        _inRate = inRate ? inRate : outRate;
        _outRate = outRate;
        _step = (float)_inRate / (float)_outRate;  // input samples per output sample
        _t = 0.0f;
        _prev = _cur = 0.0f;
        _z1 = _z2 = 0.0f;
        // low-pass just under the smaller Nyquist frequency
        const float nyq = 0.5f * (float)(_inRate < _outRate ? _inRate : _outRate);
        const float fc = nyq * 0.9f;
        const float w0 = 2.0f * 3.14159265f * fc / (float)_inRate;
        const float alpha = sinf(w0) / (2.0f * 0.7071f);
        const float cw = cosf(w0);
        const float a0 = 1.0f + alpha;
        _b0 = (1.0f - cw) * 0.5f / a0;
        _b1 = (1.0f - cw) / a0;
        _b2 = _b0;
        _a1 = -2.0f * cw / a0;
        _a2 = (1.0f - alpha) / a0;
        _bypassFilter = _inRate <= _outRate;  // nothing above Nyquist to remove when upsampling
    }

    uint32_t inRate() const { return _inRate; }

    /** Upper bound of output samples for `frames` input frames. */
    size_t maxOutput(size_t frames) const { return (size_t)((float)frames / _step) + 2; }

    /**
     * `in`: `frames` frames of `channels` (1 or 2) interleaved int16. Writes mono samples to `out`
     * (at most outCap) and returns how many.
     */
    size_t process(const int16_t* in, size_t frames, int channels, int16_t* out, size_t outCap) {
        size_t n = 0;
        for (size_t i = 0; i < frames; i++) {
            float x = channels == 2 ? 0.5f * ((float)in[2 * i] + (float)in[2 * i + 1]) : (float)in[i];
            if (!_bypassFilter) {  // transposed direct form II
                const float y = _b0 * x + _z1;
                _z1 = _b1 * x - _a1 * y + _z2;
                _z2 = _b2 * x - _a2 * y;
                x = y;
            }
            _prev = _cur;
            _cur = x;
            while (_t < 1.0f) {
                if (n >= outCap) return n;
                float v = _prev + (_cur - _prev) * _t;
                if (v > 32767.0f) v = 32767.0f;
                if (v < -32768.0f) v = -32768.0f;
                out[n++] = (int16_t)lrintf(v);
                _t += _step;
            }
            _t -= 1.0f;
        }
        return n;
    }

private:
    uint32_t _inRate = 24000, _outRate = 24000;
    float _step = 1.0f, _t = 0.0f, _prev = 0.0f, _cur = 0.0f;
    float _b0 = 1, _b1 = 0, _b2 = 0, _a1 = 0, _a2 = 0, _z1 = 0, _z2 = 0;
    bool _bypassFilter = true;
};
