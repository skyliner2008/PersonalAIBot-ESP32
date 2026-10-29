/**
 * @file EmotionCue.h
 * @brief The eyes follow what the assistant says (T140, host tested)
 *
 * set_expression is no longer declared to the model: 3.8 Live sometimes wrote the call as text and said
 * "set_expression{emotion:thinking}" out loud (T138/T140) - spoken audio cannot be taken back. The board picks
 * the expression from words in the reply instead. The first group in the table that matches wins (specific first).
 */

#pragma once

#include <string.h>

#include "../ui/Emotion.h"

namespace emotioncue {

struct Cue {
    Emotion emotion;
    const char* const* words;  // nullptr-terminated, UTF-8 / lower-case ASCII
};

inline const Cue* cues() {
    static const char* const kSleepy[] = {"ฝันดี", "ราตรีสวัสดิ์", "ง่วง", "good night", nullptr};
    static const char* const kLove[] = {"รัก", "คิดถึง", "love", nullptr};
    static const char* const kSad[] = {"เสียใจ", "เศร้า", "น่าเสียดาย", "sorry to hear", nullptr};
    static const char* const kWorried[] = {"ขอโทษ", "ขออภัย", "ระวัง", "อันตราย", "น่าเป็นห่วง", "ไม่สำเร็จ", "sorry", nullptr};
    static const char* const kAngry[] = {"โกรธ", "โมโห", nullptr};
    static const char* const kSurprised[] = {"ว้าว", "โอ้โห", "จริงเหรอ", "จริงหรอ", "ไม่น่าเชื่อ", "wow", nullptr};
    static const char* const kExcited[] = {"สุดยอด", "เยี่ยม", "ยินดีด้วย", "เย้", "awesome", "great", nullptr};
    static const char* const kShy[] = {"เขิน", "อายจัง", nullptr};
    static const char* const kWink[] = {"ล้อเล่น", "แอบบอก", "just kidding", nullptr};
    static const char* const kConfused[] = {"ไม่แน่ใจ", "งง", "ไม่เข้าใจ", nullptr};
    static const char* const kThinking[] = {"ขอเช็ค", "ขอดู", "ขอคิด", "แป๊บนึง", "สักครู่", "let me check", nullptr};
    static const char* const kCurious[] = {"น่าสนใจ", "อยากรู้", "interesting", nullptr};
    static const char* const kHappy[] = {"ขอบคุณ", "ยินดี", "ดีใจ", "สวัสดี", "ได้เลย", "ฮ่าๆ", "555", "สนุก",
                                         "thank", "hello", nullptr};
    static const Cue kCues[] = {
        {Emotion::Sleepy, kSleepy},   {Emotion::Love, kLove},       {Emotion::Sad, kSad},
        {Emotion::Worried, kWorried}, {Emotion::Angry, kAngry},     {Emotion::Surprised, kSurprised},
        {Emotion::Excited, kExcited}, {Emotion::Shy, kShy},         {Emotion::Wink, kWink},
        {Emotion::Confused, kConfused}, {Emotion::Thinking, kThinking}, {Emotion::Curious, kCurious},
        {Emotion::Happy, kHappy},     {Emotion::Neutral, nullptr},
    };
    return kCues;
}

/** The expression for a piece of the reply; false = nothing in it (keep the eyes as they are). */
inline bool find(const char* text, Emotion& out) {
    if (!text || !*text) return false;
    for (const Cue* c = cues(); c->words; c++) {
        for (const char* const* w = c->words; *w; w++) {
            if (strstr(text, *w)) {
                out = c->emotion;
                return true;
            }
        }
    }
    return false;
}

}  // namespace emotioncue
