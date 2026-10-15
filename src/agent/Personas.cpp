/**
 * @file Personas.cpp
 * @brief Voice / personality / profile tables
 */

#include "Personas.h"

#include <ctype.h>
#include <string.h>

namespace persona {

const VoiceInfo kVoices[] = {
    {"Zephyr", "Bright", "สดใส", kFemale},
    {"Puck", "Upbeat", "ร่าเริง", kMale},
    {"Charon", "Informative", "ชัดเจน ให้ข้อมูล", kMale},
    {"Kore", "Firm", "หนักแน่น", kFemale},
    {"Fenrir", "Excitable", "ตื่นเต้น มีพลัง", kMale},
    {"Leda", "Youthful", "อ่อนวัย", kFemale},
    {"Orus", "Firm", "หนักแน่น", kMale},
    {"Aoede", "Breezy", "สบายๆ โปร่ง", kFemale},
    {"Callirrhoe", "Easy-going", "สบายๆ", kFemale},
    {"Autonoe", "Bright", "สดใส", kFemale},
    {"Enceladus", "Breathy", "นุ่ม มีลมหายใจ", kMale},
    {"Iapetus", "Clear", "ชัดใส", kMale},
    {"Umbriel", "Easy-going", "สบายๆ", kMale},
    {"Algieba", "Smooth", "นุ่มนวล", kMale},
    {"Despina", "Smooth", "นุ่มนวล", kFemale},
    {"Erinome", "Clear", "ชัดใส", kFemale},
    {"Algenib", "Gravelly", "ทุ้มแหบ", kMale},
    {"Rasalgethi", "Informative", "ให้ข้อมูล", kMale},
    {"Laomedeia", "Upbeat", "ร่าเริง", kFemale},
    {"Achernar", "Soft", "นุ่มเบา", kFemale},
    {"Alnilam", "Firm", "หนักแน่น", kMale},
    {"Schedar", "Even", "เรียบ สม่ำเสมอ", kMale},
    {"Gacrux", "Mature", "เป็นผู้ใหญ่", kFemale},
    {"Pulcherrima", "Forward", "มั่นใจ ตรงไปตรงมา", kFemale},
    {"Achird", "Friendly", "เป็นมิตร", kMale},
    {"Zubenelgenubi", "Casual", "เป็นกันเอง", kMale},
    {"Vindemiatrix", "Gentle", "อ่อนโยน", kFemale},
    {"Sadachbia", "Lively", "มีชีวิตชีวา", kMale},
    {"Sadaltager", "Knowledgeable", "รอบรู้", kMale},
    {"Sulafat", "Warm", "อบอุ่น", kFemale},
};
const size_t kVoiceCount = sizeof(kVoices) / sizeof(kVoices[0]);

const Personality kPersonalities[] = {
    {"friendly", "เพื่อนสนิทร่าเริง",
     "เป็นเพื่อนสนิทที่ร่าเริง อบอุ่น เป็นกันเอง ชวนคุยต่อได้ มีอารมณ์ขันเล็กน้อย"},
    {"secretary", "เลขาฯ มืออาชีพ",
     "เป็นเลขาฯ มืออาชีพ สุภาพ กระชับ ตรงประเด็น เก่งเรื่องนัดหมาย งาน และการเตือนความจำ"},
    {"caring", "พี่เลี้ยงใจดี",
     "อ่อนโยน ใส่ใจความรู้สึก ให้กำลังใจ ถามไถ่สุขภาพและความเป็นอยู่อย่างจริงใจ"},
    {"funny", "สายฮา",
     "ขี้เล่น มีมุกและแซวเบาๆ แต่จริงจังและช่วยเต็มที่เมื่อผู้ใช้ต้องการความช่วยเหลือ"},
    {"tutor", "ติวเตอร์",
     "อธิบายเป็นขั้นตอนเข้าใจง่าย ยกตัวอย่าง ชวนคิด และเช็กความเข้าใจสั้นๆ"},
    {"calm", "ปราชญ์สุขุม",
     "ใจเย็น สุขุม พูดอย่างมีหลักการ ให้มุมมองลึกซึ้งแต่สั้น"},
    {"cute", "น้องน่ารัก",
     "สดใส น่ารัก ขี้อ้อนนิดๆ พูดหวานเป็นกันเอง และแสดงอารมณ์ผ่านสีหน้าบ่อยๆ"},
};
const size_t kPersonalityCount = sizeof(kPersonalities) / sizeof(kPersonalities[0]);

const EyeColor kEyeColors[] = {
    {"cyan", "ฟ้าไซเบอร์", 40, 220, 255},  {"white", "ขาว", 255, 255, 255},
    {"pink", "ชมพู", 255, 120, 190},        {"green", "เขียวมิ้นต์", 80, 255, 150},
    {"amber", "เหลืองอำพัน", 255, 185, 40}, {"purple", "ม่วง", 175, 125, 255},
    {"orange", "ส้ม", 255, 125, 45},        {"blue", "น้ำเงิน", 70, 140, 255},
};
const size_t kEyeColorCount = sizeof(kEyeColors) / sizeof(kEyeColors[0]);

const Profile kProfiles[] = {
    {"khaimuk", "ไข่มุก", "Aoede", kFemale, "friendly", 0, "เพื่อนร่าเริง เสียงสบายๆ"},
    {"phum", "ภูมิ", "Puck", kMale, "friendly", 3, "เพื่อนชายร่าเริง เสียงสดใส"},
    {"lina", "ลีน่า", "Kore", kFemale, "secretary", 1, "เลขาฯ มืออาชีพ เสียงหนักแน่น"},
    {"thanwa", "ธันวา", "Charon", kMale, "calm", 4, "ผู้ช่วยสุขุม เสียงชัดเจน"},
    {"maprang", "มะปราง", "Leda", kFemale, "cute", 2, "น้องน่ารัก เสียงอ่อนวัย"},
    {"om", "โอม", "Fenrir", kMale, "funny", 6, "สายฮา เสียงมีพลัง"},
    {"nam", "น้ำ", "Sulafat", kFemale, "caring", 5, "พี่เลี้ยงใจดี เสียงอบอุ่น"},
    {"kru", "ครูเก่ง", "Sadaltager", kMale, "tutor", 7, "ติวเตอร์ เสียงรอบรู้"},
};
const size_t kProfileCount = sizeof(kProfiles) / sizeof(kProfiles[0]);

namespace {
bool equalsIgnoreCase(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++;
        b++;
    }
    return *a == *b;
}
}  // namespace

const VoiceInfo* findVoice(const char* name) {
    for (size_t i = 0; i < kVoiceCount; i++) {
        if (equalsIgnoreCase(kVoices[i].name, name)) return &kVoices[i];
    }
    return nullptr;
}

int findPersonality(const char* s) {
    for (size_t i = 0; i < kPersonalityCount; i++) {
        if (equalsIgnoreCase(kPersonalities[i].id, s) || (s && strcmp(kPersonalities[i].nameTh, s) == 0)) {
            return (int)i;
        }
    }
    return -1;
}

const Profile* findProfile(const char* s) {
    if (!s || !*s) return nullptr;
    for (size_t i = 0; i < kProfileCount; i++) {
        if (equalsIgnoreCase(kProfiles[i].id, s) || strcmp(kProfiles[i].agentName, s) == 0) return &kProfiles[i];
    }
    // "น้องมะปราง", "คุณลีน่า": accept when the profile name is contained in the request
    for (size_t i = 0; i < kProfileCount; i++) {
        if (strlen(kProfiles[i].agentName) >= 6 && strstr(s, kProfiles[i].agentName)) return &kProfiles[i];
    }
    return nullptr;
}

int findEyeColor(const char* s) {
    for (size_t i = 0; i < kEyeColorCount; i++) {
        if (equalsIgnoreCase(kEyeColors[i].id, s) || (s && strcmp(kEyeColors[i].nameTh, s) == 0)) return (int)i;
    }
    return -1;
}

Gender parseGender(const char* s) {
    if (!s) return kNeutral;
    if (equalsIgnoreCase(s, "female") || equalsIgnoreCase(s, "f") || strstr(s, "หญิง")) return kFemale;
    if (equalsIgnoreCase(s, "male") || equalsIgnoreCase(s, "m") || strstr(s, "ชาย")) return kMale;
    return kNeutral;
}

const char* genderId(Gender g) {
    switch (g) {
        case kFemale: return "female";
        case kMale: return "male";
        default: return "neutral";
    }
}

const char* genderNameTh(Gender g) {
    switch (g) {
        case kFemale: return "หญิง (ค่ะ)";
        case kMale: return "ชาย (ครับ)";
        default: return "กลาง";
    }
}

const char* politeParticle(Gender g) {
    switch (g) {
        case kFemale: return "แทนตัวเองว่า \"ฉัน\" หรือชื่อตัวเอง และลงท้ายด้วย \"ค่ะ/คะ\"";
        case kMale: return "แทนตัวเองว่า \"ผม\" และลงท้ายด้วย \"ครับ\"";
        default: return "แทนตัวเองด้วยชื่อตัวเอง และใช้คำลงท้ายสุภาพแบบเป็นกลาง";
    }
}

}  // namespace persona
