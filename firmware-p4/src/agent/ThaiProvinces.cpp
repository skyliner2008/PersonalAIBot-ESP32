/**
 * @file ThaiProvinces.cpp
 * @brief The 77 Thai provinces (capital coordinates) and a forgiving name lookup
 */

#include "ThaiProvinces.h"

#include <string.h>

namespace thaiplace {

// Open-Meteo geocoder, feature PPLA / PPLC (the provincial capital), 2026-10-18
// code: the Thai province code (TIS 1099) ThaiWater filters by (province_code), from its own list 2026-10-25
const Province kProvinces[] = {
    {"กรุงเทพมหานคร", "Bangkok", 13.7540f, 100.5014f, 10},
    {"กระบี่", "Krabi", 8.0726f, 98.9105f, 81},
    {"กาญจนบุรี", "Kanchanaburi", 14.0041f, 99.5483f, 71},
    {"กาฬสินธุ์", "Kalasin", 16.4328f, 103.5066f, 46},
    {"กำแพงเพชร", "Kamphaeng Phet", 16.4834f, 99.5215f, 62},
    {"ขอนแก่น", "Khon Kaen", 16.4467f, 102.8330f, 40},
    {"จันทบุรี", "Chanthaburi", 12.6096f, 102.1045f, 22},
    {"ฉะเชิงเทรา", "Chachoengsao", 13.6882f, 101.0716f, 24},
    {"ชลบุรี", "Chon Buri", 13.3622f, 100.9835f, 20},
    {"ชัยนาท", "Chai Nat", 15.1864f, 100.1235f, 18},
    {"ชัยภูมิ", "Chaiyaphum", 15.8105f, 102.0288f, 36},
    {"ชุมพร", "Chumphon", 10.4957f, 99.1797f, 86},
    {"เชียงราย", "Chiang Rai", 19.9086f, 99.8325f, 57},
    {"เชียงใหม่", "Chiang Mai", 18.7904f, 98.9847f, 50},
    {"ตรัง", "Trang", 7.5563f, 99.6114f, 92},
    {"ตราด", "Trat", 12.2436f, 102.5151f, 23},
    {"ตาก", "Tak", 16.8697f, 99.1290f, 63},
    {"นครนายก", "Nakhon Nayok", 14.2046f, 101.2130f, 26},
    {"นครปฐม", "Nakhon Pathom", 13.8196f, 100.0443f, 73},
    {"นครพนม", "Nakhon Phanom", 17.4108f, 104.7786f, 48},
    {"นครราชสีมา", "Nakhon Ratchasima", 14.9707f, 102.1020f, 30},
    {"นครศรีธรรมราช", "Nakhon Si Thammarat", 8.4333f, 99.9667f, 80},
    {"นครสวรรค์", "Nakhon Sawan", 15.7047f, 100.1372f, 60},
    {"นนทบุรี", "Nonthaburi", 13.8607f, 100.5148f, 12},
    {"นราธิวาส", "Narathiwat", 6.4264f, 101.8231f, 96},
    {"น่าน", "Nan", 18.7838f, 100.7790f, 55},
    {"บึงกาฬ", "Bueng Kan", 18.3630f, 103.6519f, 38},
    {"บุรีรัมย์", "Buriram", 14.9943f, 103.1039f, 31},
    {"ปทุมธานี", "Pathum Thani", 14.0135f, 100.5305f, 13},
    {"ประจวบคีรีขันธ์", "Prachuap Khiri Khan", 11.8210f, 99.7841f, 77},
    {"ปราจีนบุรี", "Prachin Buri", 14.0499f, 101.3686f, 25},
    {"ปัตตานี", "Pattani", 6.8681f, 101.2501f, 94},
    {"พระนครศรีอยุธยา", "Phra Nakhon Si Ayutthaya", 14.3517f, 100.5774f, 14},
    {"พะเยา", "Phayao", 19.1920f, 99.8788f, 56},
    {"พังงา", "Phang Nga", 8.4509f, 98.5298f, 82},
    {"พัทลุง", "Phatthalung", 7.6179f, 100.0779f, 93},
    {"พิจิตร", "Phichit", 16.4418f, 100.3488f, 66},
    {"พิษณุโลก", "Phitsanulok", 16.8248f, 100.2586f, 65},
    {"เพชรบุรี", "Phetchaburi", 13.1119f, 99.9447f, 76},
    {"เพชรบูรณ์", "Phetchabun", 16.4190f, 101.1606f, 67},
    {"แพร่", "Phrae", 18.1459f, 100.1410f, 54},
    {"ภูเก็ต", "Phuket", 7.8906f, 98.3981f, 83},
    {"มหาสารคาม", "Maha Sarakham", 16.1848f, 103.3007f, 44},
    {"มุกดาหาร", "Mukdahan", 16.5453f, 104.7235f, 49},
    {"แม่ฮ่องสอน", "Mae Hong Son", 19.3003f, 97.9685f, 58},
    {"ยโสธร", "Yasothon", 15.7941f, 104.1451f, 35},
    {"ยะลา", "Yala", 6.5400f, 101.2813f, 95},
    {"ร้อยเอ็ด", "Roi Et", 16.0567f, 103.6531f, 45},
    {"ระนอง", "Ranong", 9.9658f, 98.6348f, 85},
    {"ระยอง", "Rayong", 12.6809f, 101.2580f, 21},
    {"ราชบุรี", "Ratchaburi", 13.5367f, 99.8171f, 70},
    {"ลพบุรี", "Lop Buri", 14.7981f, 100.6540f, 16},
    {"ลำปาง", "Lampang", 18.2923f, 99.4928f, 52},
    {"ลำพูน", "Lamphun", 18.5805f, 99.0075f, 51},
    {"เลย", "Loei", 17.4905f, 101.7275f, 42},
    {"ศรีสะเกษ", "Si Sa Ket", 15.1148f, 104.3294f, 33},
    {"สกลนคร", "Sakon Nakhon", 17.1612f, 104.1472f, 47},
    {"สงขลา", "Songkhla", 7.1988f, 100.5951f, 90},
    {"สตูล", "Satun", 6.6231f, 100.0668f, 91},
    {"สมุทรปราการ", "Samut Prakan", 13.5976f, 100.5972f, 11},
    {"สมุทรสงคราม", "Samut Songkhram", 13.4146f, 100.0026f, 75},
    {"สมุทรสาคร", "Samut Sakhon", 13.5475f, 100.2736f, 74},
    {"สระแก้ว", "Sa Kaeo", 13.8141f, 102.0722f, 27},
    {"สระบุรี", "Saraburi", 14.5333f, 100.9167f, 19},
    {"สิงห์บุรี", "Sing Buri", 14.8879f, 100.4046f, 17},
    {"สุโขทัย", "Sukhothai", 17.0078f, 99.8230f, 64},
    {"สุพรรณบุรี", "Suphan Buri", 14.4742f, 100.1222f, 72},
    {"สุราษฎร์ธานี", "Surat Thani", 9.1401f, 99.3331f, 84},
    {"สุรินทร์", "Surin", 14.8818f, 103.4936f, 32},
    {"หนองคาย", "Nong Khai", 17.8785f, 102.7420f, 43},
    {"หนองบัวลำภู", "Nong Bua Lam Phu", 17.2041f, 102.4407f, 39},
    {"อ่างทอง", "Ang Thong", 14.5884f, 100.4528f, 15},
    {"อำนาจเจริญ", "Amnat Charoen", 15.8585f, 104.6288f, 37},
    {"อุดรธานี", "Udon Thani", 17.4157f, 102.7859f, 41},
    {"อุตรดิตถ์", "Uttaradit", 17.6256f, 100.0942f, 53},
    {"อุทัยธานี", "Uthai Thani", 15.3794f, 100.0245f, 61},
    {"อุบลราชธานี", "Ubon Ratchathani", 15.2384f, 104.8487f, 34},
};
const size_t kProvinceCount = sizeof(kProvinces) / sizeof(kProvinces[0]);

namespace {

struct Alias {
    const char* name;  // already squashed (see squash)
    const char* th;    // kProvinces[].th
};
const Alias kAliases[] = {
    {"กรุงเทพ", "กรุงเทพมหานคร"},      {"กรุงเทพฯ", "กรุงเทพมหานคร"}, {"กทม", "กรุงเทพมหานคร"},
    {"krungthep", "กรุงเทพมหานคร"},    {"โคราช", "นครราชสีมา"},      {"korat", "นครราชสีมา"},
    {"อยุธยา", "พระนครศรีอยุธยา"},     {"ayutthaya", "พระนครศรีอยุธยา"}, {"อุบล", "อุบลราชธานี"},
    {"ubon", "อุบลราชธานี"},           {"อุดร", "อุดรธานี"},          {"udon", "อุดรธานี"},
    {"สุราษฎร์", "สุราษฎร์ธานี"},       {"นครศรี", "นครศรีธรรมราช"},   {"ประจวบ", "ประจวบคีรีขันธ์"},
    {"sakaew", "สระแก้ว"},             {"samutprakarn", "สมุทรปราการ"},
};

// Prefixes / suffixes people (and the model) put around a province name, squashed: "จ." -> "จ"
const char* const kPrefixes[] = {"", "จังหวัด", "จ", "อำเภอเมือง", "อเมือง", "เมือง", "changwat", "mueang", "muang"};
const char* const kSuffixes[] = {"", "province", "thailand", "ประเทศไทย"};

// "Khon-Kaen Province" -> "khonkaenprovince", " จ. ขอนแก่น" -> "จขอนแก่น": no spaces, '-', '.', '\'' or
// ',', ASCII lower-cased (Thai bytes stay as they are)
void squash(const char* in, char* out, size_t cap) {
    size_t n = 0;
    for (const unsigned char* p = (const unsigned char*)in; *p && n + 1 < cap; p++) {
        const unsigned char c = *p;
        if (c == ' ' || c == '\t' || c == '-' || c == '.' || c == '\'' || c == ',') continue;
        out[n++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    out[n] = '\0';
}

const Province* byThaiName(const char* th) {
    for (size_t i = 0; i < kProvinceCount; i++) {
        if (!strcmp(kProvinces[i].th, th)) return &kProvinces[i];
    }
    return nullptr;
}

const Province* match(const char* s) {
    if (!*s) return nullptr;
    char en[48];
    for (size_t i = 0; i < kProvinceCount; i++) {
        if (!strcmp(kProvinces[i].th, s)) return &kProvinces[i];
        squash(kProvinces[i].en, en, sizeof(en));
        if (!strcmp(en, s)) return &kProvinces[i];
    }
    for (const Alias& a : kAliases) {
        if (!strcmp(a.name, s)) return byThaiName(a.th);
    }
    return nullptr;
}

}  // namespace

const Province* findProvince(const char* name) {
    if (!name || !*name) return nullptr;
    char s[128];
    squash(name, s, sizeof(s));
    const size_t len = strlen(s);
    // the whole name first: "จันทบุรี" must not lose its "จ" as if it were "จ."
    for (const char* pre : kPrefixes) {
        const size_t pl = strlen(pre);
        if (pl >= len || strncmp(s, pre, pl) != 0) continue;
        for (const char* suf : kSuffixes) {
            const size_t sl = strlen(suf);
            if (pl + sl >= len || (sl && strcmp(s + len - sl, suf) != 0)) continue;
            char core[128];
            memcpy(core, s + pl, len - pl - sl);
            core[len - pl - sl] = '\0';
            if (const Province* p = match(core)) return p;
        }
    }
    return nullptr;
}

const Province* findProvinceInText(const char* text) {
    if (!text) return nullptr;
    const Province* best = nullptr;
    size_t bestLen = 0;
    for (size_t i = 0; i < kProvinceCount; i++) {
        const char* th = kProvinces[i].th;
        const size_t n = strlen(th);
        for (const char* at = strstr(text, th); at; at = strstr(at + 1, th)) {
            // a whole word: "ตากใบ" (Narathiwat) / "ตากฟ้า" (Nakhon Sawan) are not "ตาก" - Thai letters follow
            // (U+0E00-U+0E7F = E0 B8 xx / E0 B9 xx in UTF-8)
            const unsigned char* next = (const unsigned char*)at + n;
            const bool thaiFollows = next[0] == 0xE0 && (next[1] == 0xB8 || next[1] == 0xB9);
            if (thaiFollows) continue;
            if (n > bestLen) {  // the longest name found ("นครสวรรค์" over a shorter one inside it)
                best = &kProvinces[i];
                bestLen = n;
            }
            break;
        }
    }
    return best;
}

}  // namespace thaiplace
