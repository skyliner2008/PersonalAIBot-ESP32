/**
 * @file Emotion.h
 * @brief Facial expressions of the eye avatar (shared by the UI and the set_expression tool)
 */

#pragma once

#include <stdint.h>
#include <string.h>

enum class Emotion : uint8_t {
    Neutral,
    Happy,
    Excited,
    Love,
    Sad,
    Worried,
    Angry,
    Surprised,
    Sleepy,
    Thinking,
    Curious,
    Confused,
    Shy,
    Wink,
    Count
};

inline const char* const* emotionNames() {
    static const char* const names[] = {"neutral",  "happy",    "excited", "love",    "sad",
                                        "worried",  "angry",    "surprised", "sleepy", "thinking",
                                        "curious",  "confused", "shy",     "wink",    nullptr};
    return names;
}

inline const char* emotionName(Emotion e) {
    const uint8_t i = (uint8_t)e;
    return i < (uint8_t)Emotion::Count ? emotionNames()[i] : "neutral";
}

/** Returns false (and Neutral) for unknown names. */
inline bool parseEmotion(const char* s, Emotion& out) {
    out = Emotion::Neutral;
    if (!s) return false;
    const char* const* n = emotionNames();
    for (uint8_t i = 0; n[i]; i++) {
        if (strcmp(n[i], s) == 0) {
            out = (Emotion)i;
            return true;
        }
    }
    return false;
}
