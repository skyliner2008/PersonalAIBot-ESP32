package io.github.skyliner2008.pabflasher

/**
 * Google Maps' navigation notification -> the next step for the board (wiki 18).
 *
 * Maps does not document it and the layout differs between versions and languages, e.g.
 *   title "350 ม."                       text "เลี้ยวขวาเข้า ถ.พระราม 4"   sub "12 นาที · 5.4 กม. · ถึง 16:40"
 *   title "Turn right onto Rama IV Rd"   text "350 m"                     sub "12 min · 5.4 km · 4:40 PM ETA"
 *   title "อีก 350 ม. เลี้ยวขวา"          text "ถ.พระราม 4"
 * so each field is classified by what it looks like, not by where it is. Unknown layouts still give
 * the instruction text; the app keeps the raw fields so a new layout can be added from a real sample.
 */
object MapsParser {
    const val MAPS_PACKAGE = "com.google.android.apps.maps"

    data class Step(
        val icon: String,
        val text: String,
        val distM: Int,      // -1 = unknown
        val eta: String,     // "" = unknown
        val leftKm: Float,   // -1 = unknown
        val leftMin: Int,    // -1 = unknown
    )

    private const val NUM = "(\\d+(?:[.,]\\d+)?)"
    private val DIST_UNIT = "(กิโลเมตร|กม\\.?|km|เมตร|ม\\.?|m|ไมล์|mi|ฟุต|ft)"
    /** A distance at the start: "350 ม.", "1.2 km", "อีก 350 ม. ..." */
    private val LEADING_DIST = Regex("^\\s*(?:อีก|in)?\\s*$NUM\\s*$DIST_UNIT(?![a-zA-Zก-๙])", RegexOption.IGNORE_CASE)
    private val ANY_DIST = Regex("$NUM\\s*$DIST_UNIT(?![a-zA-Zก-๙])", RegexOption.IGNORE_CASE)
    private val TIME = Regex("(?<!\\d)([01]?\\d|2[0-3])[:.]([0-5]\\d)(?!\\d)\\s*(AM|PM|น\\.)?", RegexOption.IGNORE_CASE)
    private val MINUTES = Regex("(\\d+)\\s*(นาที|min)", RegexOption.IGNORE_CASE)
    private val HOURS = Regex("(\\d+)\\s*(ชั่วโมง|ชม\\.?|hr|h)(?![a-zA-Z])", RegexOption.IGNORE_CASE)

    /** The fields of the notification (title, text, subText, bigText ...); null = not navigation. */
    fun parse(fields: List<String?>): Step? {
        val parts = fields.mapNotNull { it?.trim()?.takeIf { s -> s.isNotEmpty() } }.distinct()
        if (parts.isEmpty()) return null
        // the summary line: has an arrival time or "min · km"
        val summary = parts.firstOrNull { TIME.containsMatchIn(it) || (MINUTES.containsMatchIn(it) && it.contains('·')) }
        var distM = -1
        var instruction: String? = null
        for (p in parts) {
            if (p == summary) continue
            val m = LEADING_DIST.find(p)
            if (m != null && distM < 0) {
                distM = metres(m.groupValues[1], m.groupValues[2])
                val rest = p.substring(m.range.last + 1).trim().trimStart('·', '-', ',', ' ')
                if (rest.isNotEmpty() && instruction == null) instruction = rest
            } else if (instruction == null) {
                instruction = p
            }
        }
        // "เลี้ยวขวา" + "ถ.พระราม 4" in two fields: join a bare road name to a bare manoeuvre
        val others = parts.filter { it != summary && it != instruction && LEADING_DIST.find(it) == null }
        if (instruction != null && others.isNotEmpty() && icon(instruction) != "none" && icon(others[0]) == "none" &&
            instruction.length < 20) {
            instruction = "$instruction ${others[0]}"
        }
        if (instruction == null && distM < 0) return null
        var eta = ""
        var leftKm = -1f
        var leftMin = -1
        if (summary != null) {
            TIME.find(summary)?.let { t ->
                var h = t.groupValues[1].toInt()
                val ampm = t.groupValues[3].uppercase()
                if (ampm == "PM" && h < 12) h += 12
                if (ampm == "AM" && h == 12) h = 0
                eta = "%02d:%s".format(h, t.groupValues[2])
            }
            ANY_DIST.find(summary)?.let { leftKm = metres(it.groupValues[1], it.groupValues[2]) / 1000f }
            val mins = MINUTES.find(summary)?.groupValues?.get(1)?.toIntOrNull()
            val hours = HOURS.find(summary)?.groupValues?.get(1)?.toIntOrNull()
            if (mins != null || hours != null) leftMin = (hours ?: 0) * 60 + (mins ?: 0)
        }
        val text = (instruction ?: "").take(150)
        return Step(icon(text), text, distM, eta, leftKm, leftMin)
    }

    fun metres(num: String, unit: String): Int {
        val v = num.replace(',', '.').toDoubleOrNull() ?: return -1
        val u = unit.lowercase().trimEnd('.')
        return when (u) {
            "กิโลเมตร", "กม", "km" -> (v * 1000).toInt()
            "ไมล์", "mi" -> (v * 1609.34).toInt()
            "ฟุต", "ft" -> (v * 0.3048).toInt()
            else -> v.toInt()  // เมตร, ม., m
        }
    }

    /** The arrow for an instruction (the board's phone::Icon keys). Specific words before general ones. */
    fun icon(text: String): String {
        val t = text.lowercase()
        fun has(vararg w: String) = w.any { t.contains(it) }
        return when {
            has("ถึงที่หมาย", "ถึงจุดหมาย", "จุดหมายอยู่", "arrive", "destination", "ถึงแล้ว") -> "arrive"
            has("กลับรถ", "u-turn", "make a u") -> "uturn"
            has("วงเวียน", "roundabout", "rotary") -> "roundabout"
            has("หักซ้าย", "sharp left") -> "sharp_left"
            has("หักขวา", "sharp right") -> "sharp_right"
            has("เบี่ยงซ้าย", "slight left", "bear left") -> "slight_left"
            has("เบี่ยงขวา", "slight right", "bear right") -> "slight_right"
            has("ชิดซ้าย", "keep left", "ทางแยกซ้าย", "fork left") -> "keep_left"
            has("ชิดขวา", "keep right", "ทางแยกขวา", "fork right") -> "keep_right"
            has("merge", "รวมเข้า", "บรรจบ") -> "merge"
            has("เลี้ยวซ้าย", "turn left", "left onto", "เลี้ยว ซ้าย") -> "left"
            has("เลี้ยวขวา", "turn right", "right onto", "เลี้ยว ขวา") -> "right"
            has("ตรงไป", "continue", "straight", "มุ่งหน้า", "head ") -> "straight"
            else -> "none"
        }
    }
}
