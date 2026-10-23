package io.github.skyliner2008.pabflasher

import org.json.JSONArray
import org.json.JSONObject

/** What the board offers (reply to `options`): profiles, voices, personalities, eye colours, models. */
class BoardOptions(json: String) {
    class Item(val id: String, val label: String)
    class Profile(val id: String, val name: String, val voice: String, val gender: String, val personality: String,
                  val eye: String, val summary: String)
    class Eye(val id: String, val name: String, val rgb: String)

    val profiles: List<Profile>
    val voices: List<Item>         // id = voice name, label = name — gender — style
    val personalities: List<Item>
    val genders: List<Item>
    val eyes: List<Eye>
    val liveModels: List<Item>     // index = the `set model` value
    val textModels: List<String>
    val thinking: List<String>

    init {
        val o = JSONObject(json)
        val genderName = HashMap<String, String>()
        genders = objects(o, "genders") { Item(it.getString("id"), it.getString("name")) }
        genders.forEach { genderName[it.id] = it.label }
        profiles = objects(o, "profiles") {
            Profile(it.getString("id"), it.getString("name"), it.getString("voice"), it.optString("gender"),
                it.optString("personality"), it.optString("eye"), it.optString("summary"))
        }
        voices = objects(o, "voices") {
            val g = it.optString("gender")
            Item(it.getString("name"), "${it.getString("name")} — ${genderName[g] ?: g} — ${it.optString("style")}")
        }
        personalities = objects(o, "personalities") { Item(it.getString("id"), it.getString("name")) }
        eyes = objects(o, "eyes") { Eye(it.getString("id"), it.getString("name"), it.optString("rgb", "#FFFFFF")) }
        liveModels = objects(o, "liveModels") { Item(it.getString("id"), it.optString("label", it.getString("id"))) }
        textModels = strings(o.optJSONArray("textModels"))
        thinking = strings(o.optJSONArray("thinking"))
    }

    private fun <T> objects(o: JSONObject, key: String, map: (JSONObject) -> T): List<T> {
        val a = o.optJSONArray(key) ?: return emptyList()
        return (0 until a.length()).map { map(a.getJSONObject(it)) }
    }

    private fun strings(a: JSONArray?): List<String> = if (a == null) emptyList() else (0 until a.length()).map { a.getString(it) }
}

/**
 * The settings form as `set <field> <value>` strings (the fields of handleSet() in src/main.cpp), and which
 * commands a change needs - the same rules as Studio's "save all" button.
 */
object SettingsForm {
    /** Fields this app edits, in the order they are sent (the profile first: it sets voice, eye ...). */
    val FIELDS = listOf("profile", "agent", "user", "voice", "personality", "gender", "eye", "model", "thinking",
        "textmodel", "search", "consolidate", "volume", "aivolume", "brightness", "micgain", "wake", "sleep", "clock",
        "dimlevel", "rotation", "finance")

    val WAKE_MODES = listOf("ระดับเสียง (แบบเดิม)", "คำปลุกอย่างเดียว", "คำปลุก + ระดับเสียง")

    /** The reply to `config` as form values. */
    fun fromConfig(c: JSONObject): Map<String, String> = linkedMapOf(
        "profile" to c.optString("profile", "custom"),
        "agent" to c.optString("agent"),
        "user" to c.optString("user"),
        "voice" to c.optString("voice"),
        "personality" to c.optString("personality"),
        "gender" to c.optString("gender"),
        "eye" to c.optString("eye"),
        "model" to c.optInt("model", 0).toString(),
        "thinking" to c.optInt("thinking", 0).toString(),
        "textmodel" to c.optInt("textmodel", 0).toString(),
        "search" to flag(c.optBoolean("search", false)),
        "consolidate" to flag(c.optBoolean("consolidate", true)),
        "volume" to c.optInt("volume", 70).toString(),
        "aivolume" to c.optInt("aivolume", 80).toString(),
        "brightness" to c.optInt("brightness", 80).toString(),
        "micgain" to c.optInt("micgain", 5).toString(),
        "wake" to c.optInt("wake", 0).toString(),
        "sleep" to c.optInt("sleep", 3).toString(),
        "clock" to c.optInt("clock", 5).toString(),
        "dimlevel" to c.optInt("dimlevel", 8).toString(),
        "rotation" to (if (c.optInt("rotation", 1) == 3) "3" else "1"),
        "finance" to flag(c.optBoolean("finance", true)),
    )

    fun flag(on: Boolean) = if (on) "1" else "0"

    /**
     * Commands that take the board from [base] (what it reported) to [now] (the form). A new ready-made profile is
     * sent first and its own name / voice / personality / gender / eye count as already set; empty names are not sent.
     */
    fun commands(base: Map<String, String>, now: Map<String, String>, options: BoardOptions?): List<String> {
        val out = ArrayList<String>()
        var b = base
        val profile = now["profile"]
        if (profile != null && profile != "custom" && profile != base["profile"]) {
            out += "set profile $profile"
            options?.profiles?.firstOrNull { it.id == profile }?.let { p ->
                b = base + mapOf("profile" to p.id, "agent" to p.name, "voice" to p.voice,
                    "personality" to p.personality, "gender" to p.gender, "eye" to p.eye)
            }
        }
        for (k in FIELDS) {
            if (k == "profile") continue
            val v = now[k]?.let { Console.clean(it) } ?: continue
            if (b[k] == v) continue
            if ((k == "agent" || k == "user") && v.isEmpty()) continue
            out += "set $k $v"
        }
        return out
    }
}
