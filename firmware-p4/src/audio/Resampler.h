/**
 * @file Resampler.h
 * @brief Polyphase 24 kHz -> 16 kHz downsampler for the microphone path
 *
 * The ES8311 shares one I2S clock for ADC and DAC, and Gemini speaks at
 * 24 kHz, so the whole I2S bus runs at 24 kHz. The Live API expects 16 kHz
 * input, so the mic stream is converted with a 2/3 rational resampler:
 *   upsample x2 (zero stuffing) -> low-pass FIR @ 7 kHz -> keep every 3rd sample
 * implemented in polyphase form (only non-zero taps are evaluated).
 */

#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

class Downsampler24to16 {
public:
    static constexpr int kTaps = 48;            // FIR length at the 48 kHz virtual rate
    static constexpr int kHistory = kTaps / 2 + 2;
    static constexpr double kPi = 3.14159265358979323846;

    Downsampler24to16() {
        designFilter();
        reset();
    }

    void reset() {
        memset(_hist, 0, sizeof(_hist));
        _histPos = 0;
        _inCount = 0;
        _outCount = 0;
    }

    /** Max output samples produced for n input samples. */
    static constexpr size_t maxOutput(size_t n) { return (n * 2) / 3 + 2; }

    /**
     * Convert n samples (24 kHz) to 16 kHz. `out` must hold maxOutput(n).
     * Returns the number of samples written. State carries across calls.
     */
    size_t process(const int16_t* in, size_t n, int16_t* out) {
        size_t produced = 0;
        for (size_t s = 0; s < n; s++) {
            _hist[_histPos] = (float)in[s];
            const uint64_t newest = _inCount;  // index of the sample just stored
            _inCount++;

            // Output j sits at virtual index 3j; it is computable once 3j <= 2*newest
            while (3 * _outCount <= 2 * newest) {
                out[produced++] = saturate(computeOutput(3 * _outCount, newest));
                _outCount++;
            }
            _histPos = (_histPos + 1) % kHistory;
        }
        return produced;
    }

private:
    float _taps[kTaps];
    float _hist[kHistory];
    int _histPos;
    uint64_t _inCount;
    uint64_t _outCount;

    void designFilter() {
        // Windowed-sinc low-pass at 7 kHz for a 48 kHz virtual rate (Hamming)
        const double fc = 7000.0 / 48000.0;
        const double mid = (kTaps - 1) / 2.0;
        double sumEven = 0.0, sumOdd = 0.0;
        double h[kTaps];
        for (int k = 0; k < kTaps; k++) {
            const double x = k - mid;
            const double sinc = (x == 0.0) ? 2.0 * fc : sin(2.0 * kPi * fc * x) / (kPi * x);
            const double w = 0.54 - 0.46 * cos(2.0 * kPi * k / (kTaps - 1));
            h[k] = sinc * w;
            if (k % 2 == 0) sumEven += h[k];
            else sumOdd += h[k];
        }
        // Each output only sees one tap parity; normalise both phases to unity DC gain
        for (int k = 0; k < kTaps; k++) {
            _taps[k] = (float)(h[k] / ((k % 2 == 0) ? sumEven : sumOdd));
        }
    }

    // Virtual 48 kHz index t; newest input sample has 24 kHz index `newest`
    float computeOutput(uint64_t t, uint64_t newest) const {
        float acc = 0.0f;
        // x48[m] is non-zero only for even m = 2i, so k must have the parity of t
        for (int k = (int)(t & 1); k < kTaps; k += 2) {
            if ((uint64_t)k > t) break;
            const uint64_t m = t - (uint64_t)k;  // even
            const uint64_t i = m / 2;            // 24 kHz sample index
            const uint64_t age = newest - i;
            if (age >= (uint64_t)kHistory) break;  // older than history (start-up)
            const int idx = (int)((_histPos + kHistory - (int)age) % kHistory);
            acc += _taps[k] * _hist[idx];
        }
        return acc;
    }

    static int16_t saturate(float v) {
        if (v > 32767.0f) return 32767;
        if (v < -32768.0f) return -32768;
        return (int16_t)lrintf(v);
    }
};
