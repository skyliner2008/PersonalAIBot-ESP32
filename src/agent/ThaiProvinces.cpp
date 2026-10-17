/**
 * @file ThaiProvinces.cpp
 * @brief The 77 Thai provinces (capital coordinates) and a forgiving name lookup
 */

#include "ThaiProvinces.h"

#include <string.h>

namespace thaiplace {

// Open-Meteo geocoder, feature PPLA / PPLC (the provincial capital), 2026-10-18
const Province kProvinces[] = {
    {"กรุงเทพมหานคร", "Bangkok", 13.7540f, 100.5014f},
    {"กระบี่", "Krabi", 8.0726f, 98.9105f},
    {"กาญจนบุรี", "Kanchanaburi", 14.0041f, 99.5483f},
    {"กาฬสินธุ์", "Kalasin", 16.4328f, 103.5066f},
    {"กำแพงเพชร", "Kamphaeng Phet", 16.4834f, 99.5215f},
    {"ขอนแก่น", "Khon Kaen", 16.4467f, 102.8330f},
    {"จันทบุรี", "Chanthaburi", 12.6096f, 102.1045f},
    {"ฉะเชิงเทรา", "Chachoengsao", 13.6882f, 101.0716f},
    {"ชลบุรี", "Chon Buri", 13.3622f, 100.9835f},
    {"ชัยนาท", "Chai Nat", 15.1864f, 100.1235f},
    {"ชัยภูมิ", "Chaiyaphum", 15.8105f, 102.0288f},
    {"ชุมพร", "Chumphon", 10.4957f, 99.1797f},
    {"เชียงราย", "Chiang Rai", 19.9086f, 99.8325f},
    {"เชียงใหม่", "Chiang Mai", 18.7904f, 98.9847f},
    {"ตรัง", "Trang", 7.5563f, 99.6114f},
    {"ตราด", "Trat", 12.2436f, 102.5151f},
    {"ตาก", "Tak", 16.8697f, 99.1290f},
    {"นครนายก", "Nakhon Nayok", 14.2046f, 101.2130f},
    {"นครปฐม", "Nakhon Pathom", 13.8196f, 100.0443f},
    {"นครพนม", "Nakhon Phanom", 17.4108f, 104.7786f},
    {"นครราชสีมา", "Nakhon Ratchasima", 14.9707f, 102.1020f},
    {"นครศรีธรรมราช", "Nakhon Si Thammarat", 8.4333f, 99.9667f},
    {"นครสวรรค์", "Nakhon Sawan", 15.7047f, 100.1372f},
    {"นนทบุรี", "Nonthaburi", 13.8607f, 100.5148f},
    {"นราธิวาส", "Narathiwat", 6.4264f, 101.8231f},
    {"น่าน", "Nan", 18.7838f, 100.7790f},
    {"บึงกาฬ", "Bueng Kan", 18.3630f, 103.6519f},
    {"บุรีรัมย์", "Buriram", 14.9943f, 103.1039f},
    {"ปทุมธานี", "Pathum Thani", 14.0135f, 100.5305f},
    {"ประจวบคีรีขันธ์", "Prachuap Khiri Khan", 11.8210f, 99.7841f},
    {"ปราจีนบุรี", "Prachin Buri", 14.0499f, 101.3686f},
    {"ปัตตานี", "Pattani", 6.8681f, 101.2501f},
    {"พระนครศรีอยุธยา", "Phra Nakhon Si Ayutthaya", 14.3517f, 100.5774f},
    {"พะเยา", "Phayao", 19.1920f, 99.8788f},
    {"พังงา", "Phang Nga", 8.4509f, 98.5298f},
    {"พัทลุง", "Phatthalung", 7.6179f, 100.0779f},
    {"พิจิตร", "Phichit", 16.4418f, 100.3488f},
    {"พิษณุโลก", "Phitsanulok", 16.8248f, 100.2586f},
    {"เพชรบุรี", "Phetchaburi", 13.1119f, 99.9447f},
    {"เพชรบูรณ์", "Phetchabun", 16.4190f, 101.1606f},
    {"แพร่", "Phrae", 18.1459f, 100.1410f},
    {"ภูเก็ต", "Phuket", 7.8906f, 98.3981f},
    {"มหาสารคาม", "Maha Sarakham", 16.1848f, 103.3007f},
    {"มุกดาหาร", "Mukdahan", 16.5453f, 104.7235f},
    {"แม่ฮ่องสอน", "Mae Hong Son", 19.3003f, 97.9685f},
    {"ยโสธร", "Yasothon", 15.7941f, 104.1451f},
    {"ยะลา", "Yala", 6.5400f, 101.2813f},
    {"ร้อยเอ็ด", "Roi Et", 16.0567f, 103.6531f},
    {"ระนอง", "Ranong", 9.9658f, 98.6348f},
    {"ระยอง", "Rayong", 12.6809f, 101.2580f},
    {"ราชบุรี", "Ratchaburi", 13.5367f, 99.8171f},
    {"ลพบุรี", "Lop Buri", 14.7981f, 100.6540f},
    {"ลำปาง", "Lampang", 18.2923f, 99.4928f},
    {"ลำพูน", "Lamphun", 18.5805f, 99.0075f},
    {"เลย", "Loei", 17.4905f, 101.7275f},
    {"ศรีสะเกษ", "Si Sa Ket", 15.1148f, 104.3294f},
    {"สกลนคร", "Sakon Nakhon", 17.1612f, 104.1472f},
    {"สงขลา", "Songkhla", 7.1988f, 100.5951f},
    {"สตูล", "Satun", 6.6231f, 100.0668f},
    {"สมุทรปราการ", "Samut Prakan", 13.5976f, 100.5972f},
    {"สมุทรสงคราม", "Samut Songkhram", 13.4146f, 100.0026f},
    {"สมุทรสาคร", "Samut Sakhon", 13.5475f, 100.2736f},
    {"สระแก้ว", "Sa Kaeo", 13.8141f, 102.0722f},
    {"สระบุรี", "Saraburi", 14.5333f, 100.9167f},
    {"สิงห์บุรี", "Sing Buri", 14.8879f, 100.4046f},
    {"สุโขทัย", "Sukhothai", 17.0078f, 99.8230f},
    {"สุพรรณบุรี", "Suphan Buri", 14.4742f, 100.1222f},
    {"สุราษฎร์ธานี", "Surat Thani", 9.1401f, 99.3331f},
    {"สุรินทร์", "Surin", 14.8818f, 103.4936f},
    {"หนองคาย", "Nong Khai", 17.8785f, 102.7420f},
    {"หนองบัวลำภู", "Nong Bua Lam Phu", 17.2041f, 102.4407f},
    {"อ่างทอง", "Ang Thong", 14.5884f, 100.4528f},
    {"อำนาจเจริญ", "Amnat Charoen", 15.8585f, 104.6288f},
    {"อุดรธานี", "Udon Thani", 17.4157f, 102.7859f},
    {"อุตรดิตถ์", "Uttaradit", 17.6256f, 100.0942f},
    {"อุทัยธานี", "Uthai Thani", 15.3794f, 100.0245f},
    {"อุบลราชธานี", "Ubon Ratchathani", 15.2384f, 104.8487f},
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

}  // namespace thaiplace
