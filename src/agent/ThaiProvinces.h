/**
 * @file ThaiProvinces.h
 * @brief The 77 Thai provinces with the coordinates of their capitals (get_weather without geocoding)
 * @see wiki/11-Data-Tools-and-Cards.md
 *
 * Each HTTPS request costs a TLS handshake to a server ~200 ms away (Open-Meteo, Germany): a weather
 * question for a province used to need a geocoding request before the forecast, and Thai names the
 * geocoder does not know ("อุบลราชธานี", "เพชรบุรี") a second one with the English name (T072).
 * Coordinates: Open-Meteo's own geocoder (GeoNames PPLA / PPLC), so answers match the old lookups.
 */

#pragma once

#include <stddef.h>

namespace thaiplace {

struct Province {
    const char* th;  // "ขอนแก่น" (also the card / result label)
    const char* en;  // "Khon Kaen"
    float lat;
    float lon;
};

extern const Province kProvinces[];
extern const size_t kProvinceCount;

/**
 * Province for a Thai or English name, or nullptr: "ขอนแก่น", "จังหวัดขอนแก่น", "จ.ขอนแก่น",
 * "เมืองขอนแก่น", "Khon Kaen", "khonkaen", "Khon Kaen Province", short forms ("กทม", "โคราช", "Korat").
 * Districts and other places ("หาดใหญ่", "Pattaya") are not provinces: the caller geocodes those.
 */
const Province* findProvince(const char* name);

}  // namespace thaiplace
