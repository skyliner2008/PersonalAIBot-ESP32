/**
 * @file Personas.h
 * @brief Catalog of Gemini Live voices, personalities and ready-made assistant profiles
 * @see wiki/09-Assistant-Profiles-and-Tools.md
 *
 * Voices: the 30 prebuilt Gemini voices (any of them works with native-audio
 * Live models). The style words come from the Gemini docs; the gender is the
 * commonly perceived voice tone (not an API field).
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace persona {

enum Gender : uint8_t { kFemale = 0, kMale = 1, kNeutral = 2, kGenderCount };

struct VoiceInfo {
    const char* name;     // API voice name, e.g. "Aoede"
    const char* styleEn;  // "Breezy"
    const char* styleTh;  // "สบายๆ โปร่ง"
    Gender gender;
};

struct Personality {
    const char* id;      // "friendly"
    const char* nameTh;  // "เพื่อนสนิทร่าเริง"
    const char* prompt;  // behaviour instructions (Thai)
};

struct Profile {
    const char* id;          // "khaimuk"
    const char* agentName;   // "ไข่มุก"
    const char* voice;       // "Aoede"
    Gender gender;
    const char* personality; // personality id
    uint8_t eyeColor;        // index into kEyeColors
    const char* summaryTh;   // one-line description
};

struct EyeColor {
    const char* id;
    const char* nameTh;
    uint8_t r, g, b;
};

extern const VoiceInfo kVoices[];
extern const size_t kVoiceCount;
extern const Personality kPersonalities[];
extern const size_t kPersonalityCount;
extern const Profile kProfiles[];
extern const size_t kProfileCount;
extern const EyeColor kEyeColors[];
extern const size_t kEyeColorCount;

/** Case-insensitive lookups; return nullptr / -1 when unknown. */
const VoiceInfo* findVoice(const char* name);
int findPersonality(const char* idOrThaiName);
const Profile* findProfile(const char* idOrAgentName);
int findEyeColor(const char* idOrThaiName);
Gender parseGender(const char* s);  // "female"/"หญิง" -> kFemale ... (unknown -> kNeutral)

const char* genderId(Gender g);    // "female" / "male" / "neutral"
const char* genderNameTh(Gender g);
/** Polite sentence-ending instruction for Thai speech. */
const char* politeParticle(Gender g);

}  // namespace persona
