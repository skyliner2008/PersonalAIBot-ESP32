package io.github.skyliner2008.pabflasher

import android.app.AlertDialog
import android.os.Handler
import android.os.Looper
import android.text.InputType
import android.view.View
import android.view.ViewGroup
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.Spinner
import android.widget.Switch
import android.widget.TextView
import org.json.JSONArray
import org.json.JSONObject
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * The "ตั้งค่า" page: the settings of Studio's settings tab, read with `config` / `options` and written with
 * `set <field> <value>` (the board saves each one to NVS at once). Secrets are write-only: the board only tells
 * whether a Wi-Fi password / API key is set and a hint of the key.
 */
class SettingsPage(private val a: MainActivity, root: View) {
    private val body: LinearLayout = root.findViewById(R.id.settingsBody)
    private val main = Handler(Looper.getMainLooper())
    private var options: BoardOptions? = null
    private var base: Map<String, String> = emptyMap()
    private var config: JSONObject? = null
    private var profileShown = -1

    private val note: TextView
    private val ssid: EditText
    private val pass: EditText
    private val passHint: TextView
    private val key: EditText
    private val keyHint: TextView
    private val apiResult: TextView
    private val agent: EditText
    private val user: EditText
    private val profile: Spinner
    private val voice: Spinner
    private val personality: Spinner
    private val gender: Spinner
    private val eye: Spinner
    private val model: Spinner
    private val thinking: Spinner
    private val textModel: Spinner
    private val search: Switch
    private val consolidate: Switch
    private val seeks = LinkedHashMap<String, Pair<SeekBar, Int>>()  // field -> (bar, lowest value)
    private val wake: Spinner
    private val sleep: EditText
    private val clock: EditText
    private val rotation: Switch
    private val finance: Switch

    init {
        note = text(a.getString(R.string.set_not_read), dim = true)
        row(button(R.string.set_reload) { a.send("config"); a.send("options", echo = false) },
            button(R.string.set_save_all) { saveAll() })

        section(R.string.set_wifi)
        ssid = edit(R.string.set_ssid, InputType.TYPE_CLASS_TEXT)
        row(button(R.string.set_scan) { if (a.send("wifiscan")) note.setText(R.string.set_scanning) })
        pass = edit(R.string.set_pass, InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD)
        passHint = text("", dim = true)
        row(button(R.string.set_save_wifi) { saveWifi() })

        section(R.string.set_key)
        key = edit(R.string.set_key_new, InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD)
        keyHint = text("", dim = true)
        row(button(R.string.set_key_test) { if (a.send("apitest")) apiResult.setText(R.string.set_key_testing) },
            button(R.string.set_save_key) { saveKey() })
        apiResult = text("", dim = true)
        text(a.getString(R.string.set_key_help), dim = true)

        section(R.string.set_assistant)
        agent = edit(R.string.set_agent, InputType.TYPE_CLASS_TEXT)
        user = edit(R.string.set_user, InputType.TYPE_CLASS_TEXT)
        profile = spinner(R.string.set_profile)
        voice = spinner(R.string.set_voice)
        personality = spinner(R.string.set_personality)
        gender = spinner(R.string.set_gender)
        eye = spinner(R.string.set_eye)
        profile.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(p: AdapterView<*>?, v: View?, pos: Int, id: Long) = onProfilePicked(pos)
            override fun onNothingSelected(p: AdapterView<*>?) = Unit
        }

        section(R.string.set_models)
        model = spinner(R.string.set_model)
        thinking = spinner(R.string.set_thinking)
        textModel = spinner(R.string.set_textmodel)
        search = switch(R.string.set_search)
        row(button(R.string.set_search_retry) { a.send("searchretry") })
        consolidate = switch(R.string.set_consolidate)

        section(R.string.set_device)
        seek("volume", R.string.set_volume, 0, 100)
        seek("aivolume", R.string.set_aivolume, 10, 100)
        seek("brightness", R.string.set_brightness, 5, 100)
        seek("micgain", R.string.set_micgain, 0, 7)
        text(a.getString(R.string.set_micgain_help), dim = true)
        wake = spinner(R.string.set_wake)
        fill(wake, SettingsForm.WAKE_MODES)
        sleep = edit(R.string.set_sleep, InputType.TYPE_CLASS_NUMBER)
        clock = edit(R.string.set_clock, InputType.TYPE_CLASS_NUMBER)
        seek("dimlevel", R.string.set_dimlevel, 1, 60)
        rotation = switch(R.string.set_rotation)

        section(R.string.set_finance)
        finance = switch(R.string.set_finance_on)
        row(button(R.string.set_save_all) { saveAll() })
    }

    // ---------------------------------------------------------------- replies from the board

    fun onOptions(o: BoardOptions) {
        options = o
        fill(profile, o.profiles.map { "${it.name} — ${it.summary}" } + a.getString(R.string.set_custom))
        fill(voice, o.voices.map { it.label })
        fill(personality, o.personalities.map { it.label })
        fill(gender, o.genders.map { it.label })
        fill(eye, o.eyes.map { it.name })
        fill(model, o.liveModels.map { it.label })
        fill(thinking, o.thinking)
        fill(textModel, o.textModels)
        config?.let { onConfig(it) }
    }

    fun onConfig(c: JSONObject) {
        config = c
        base = SettingsForm.fromConfig(c)
        ssid.setText(c.optString("ssid"))
        pass.setText("")
        passHint.setText(if (c.optBoolean("passSet")) R.string.set_pass_kept else R.string.set_pass_none)
        key.setText("")
        keyHint.text = if (c.optBoolean("keySet")) {
            val where = a.getString(if (c.optString("secretsStore") == "sd") R.string.set_store_sd else R.string.set_store_flash)
            a.getString(R.string.set_key_on_board, c.optString("keyHint"), c.optInt("keyLen"), where)
        } else a.getString(R.string.set_key_none)
        agent.setText(base["agent"])
        user.setText(base["user"])
        options?.let { o ->
            val pi = o.profiles.indexOfFirst { it.id == base["profile"] }.let { if (it < 0) o.profiles.size else it }
            profileShown = pi
            select(profile, pi)
            select(voice, o.voices.indexOfFirst { it.id == base["voice"] })
            select(personality, o.personalities.indexOfFirst { it.id == base["personality"] })
            select(gender, o.genders.indexOfFirst { it.id == base["gender"] })
            select(eye, o.eyes.indexOfFirst { it.id == base["eye"] })
            select(model, base["model"]!!.toInt())
            select(thinking, base["thinking"]!!.toInt())
            select(textModel, base["textmodel"]!!.toInt())
        }
        search.isChecked = base["search"] == "1"
        consolidate.isChecked = base["consolidate"] == "1"
        for ((field, bar) in seeks) bar.first.progress = (base[field]?.toIntOrNull() ?: bar.second) - bar.second
        select(wake, base["wake"]!!.toInt())
        sleep.setText(base["sleep"])
        clock.setText(base["clock"])
        rotation.isChecked = base["rotation"] == "3"
        finance.isChecked = base["finance"] == "1"
        note.text = a.getString(R.string.set_read_at, SimpleDateFormat("HH:mm:ss", Locale.US).format(Date()), c.optString("fw"))
    }

    fun onWifiScan(json: String) {
        val arr = JSONArray(json)
        val nets = (0 until arr.length()).map { arr.getJSONObject(it) }
            .filter { it.optString("ssid").isNotEmpty() }
            .sortedByDescending { it.optInt("rssi", -100) }
            .distinctBy { it.optString("ssid") }
        note.text = a.getString(R.string.set_scan_found, nets.size)
        if (nets.isEmpty()) return
        val labels = nets.map {
            "${it.optString("ssid")}  (${it.optInt("rssi")} dBm${if (it.optBoolean("secure", true)) "" else ", เปิด"})"
        }.toTypedArray()
        AlertDialog.Builder(a).setTitle(R.string.set_scan_pick)
            .setItems(labels) { _, i -> ssid.setText(nets[i].optString("ssid")); pass.requestFocus() }
            .setNegativeButton(android.R.string.cancel, null).show()
    }

    fun onApiTest(d: JSONObject) {
        apiResult.text = describeApiTest(d)
    }

    // ---------------------------------------------------------------- writing

    private fun form(): Map<String, String> {
        val v = LinkedHashMap<String, String>()
        val o = options
        v["agent"] = agent.text.toString().trim()
        v["user"] = user.text.toString().trim()
        if (o != null) {
            val pi = profile.selectedItemPosition
            v["profile"] = o.profiles.getOrNull(pi)?.id ?: "custom"
            o.voices.getOrNull(voice.selectedItemPosition)?.let { v["voice"] = it.id }
            o.personalities.getOrNull(personality.selectedItemPosition)?.let { v["personality"] = it.id }
            o.genders.getOrNull(gender.selectedItemPosition)?.let { v["gender"] = it.id }
            o.eyes.getOrNull(eye.selectedItemPosition)?.let { v["eye"] = it.id }
            if (model.selectedItemPosition >= 0) v["model"] = model.selectedItemPosition.toString()
            if (thinking.selectedItemPosition >= 0) v["thinking"] = thinking.selectedItemPosition.toString()
            if (textModel.selectedItemPosition >= 0) v["textmodel"] = textModel.selectedItemPosition.toString()
        }
        v["search"] = SettingsForm.flag(search.isChecked)
        v["consolidate"] = SettingsForm.flag(consolidate.isChecked)
        for ((field, bar) in seeks) v[field] = (bar.first.progress + bar.second).toString()
        if (wake.selectedItemPosition >= 0) v["wake"] = wake.selectedItemPosition.toString()
        minutes(sleep)?.let { v["sleep"] = it }
        minutes(clock)?.let { v["clock"] = it }
        v["rotation"] = if (rotation.isChecked) "3" else "1"
        v["finance"] = SettingsForm.flag(finance.isChecked)
        return v
    }

    private fun minutes(e: EditText): String? = e.text.toString().trim().toIntOrNull()?.coerceIn(0, 120)?.toString()

    private fun saveAll() {
        if (config == null) { note.setText(R.string.set_not_read); a.send("config"); return }
        val cmds = SettingsForm.commands(base, form(), options)
        if (cmds.isEmpty()) { note.setText(R.string.set_nothing_changed); return }
        var sent = 0
        for (c in cmds) if (a.send(c)) sent++
        note.text = a.getString(R.string.set_sent, sent)
        readBackSoon()
    }

    private fun saveWifi() {
        val s = Console.clean(ssid.text.toString())
        if (s.isEmpty()) { note.setText(R.string.set_ssid_empty); return }
        a.send("set ssid $s")
        val p = pass.text.toString()
        if (p.isNotEmpty()) a.send("set pass ${Console.clean(p)}")
        pass.setText("")
        readBackSoon()
    }

    private fun saveKey() {
        val k = key.text.toString().trim()
        when {
            k.length < 20 -> { keyHint.setText(R.string.set_key_short); return }
            k.any { it.isWhitespace() } -> { keyHint.setText(R.string.set_key_space); return }
        }
        a.send("set key $k")
        key.setText("")
        readBackSoon()
    }

    private fun readBackSoon() = main.postDelayed({ a.send("config", echo = false) }, 800)

    /** A ready-made profile picked by hand: show what it sets (like Studio); "save all" sends it. */
    private fun onProfilePicked(pos: Int) {
        if (pos == profileShown) return
        profileShown = pos
        val o = options ?: return
        val p = o.profiles.getOrNull(pos) ?: return
        agent.setText(p.name)
        select(voice, o.voices.indexOfFirst { it.id == p.voice })
        select(personality, o.personalities.indexOfFirst { it.id == p.personality })
        select(gender, o.genders.indexOfFirst { it.id == p.gender })
        select(eye, o.eyes.indexOfFirst { it.id == p.eye })
    }

    // ---------------------------------------------------------------- building the form

    private fun lp() = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)

    private fun section(title: Int) {
        val tv = TextView(a, null, 0, R.style.Section)
        tv.setText(title)
        body.addView(tv, lp().apply { topMargin = dp(20) })
    }

    private fun label(text: Int) = TextView(a).apply {
        setText(text)
        textSize = 13f
        setTextColor(a.getColor(R.color.muted))
        body.addView(this, lp().apply { topMargin = dp(8) })
    }

    private fun text(s: String, dim: Boolean): TextView = TextView(a).apply {
        text = s
        textSize = if (dim) 13f else 15f
        if (dim) setTextColor(a.getColor(R.color.muted))
        body.addView(this, lp().apply { topMargin = dp(4) })
    }

    private fun edit(label: Int, type: Int): EditText {
        label(label)
        return EditText(a).apply {
            inputType = type
            setSingleLine()
            body.addView(this, lp())
        }
    }

    private fun spinner(label: Int): Spinner {
        label(label)
        return Spinner(a).apply { body.addView(this, lp()) }
    }

    private fun switch(label: Int): Switch = Switch(a).apply {
        setText(label)
        textSize = 15f
        body.addView(this, lp().apply { topMargin = dp(10) })
    }

    /** A slider that is sent as soon as it is let go (like Studio's volume / brightness). */
    private fun seek(field: String, label: Int, lo: Int, hi: Int) {
        val title = label(label)
        val name = a.getString(label)
        val bar = SeekBar(a).apply { max = hi - lo }
        bar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(s: SeekBar, p: Int, fromUser: Boolean) { title.text = "$name: ${p + lo}" }
            override fun onStartTrackingTouch(s: SeekBar) = Unit
            override fun onStopTrackingTouch(s: SeekBar) {
                val v = (s.progress + lo).toString()
                if (a.send("set $field $v")) base = base + (field to v)
            }
        })
        body.addView(bar, lp())
        seeks[field] = bar to lo
    }

    private fun button(text: Int, onClick: () -> Unit) = Button(a).apply {
        setText(text)
        isAllCaps = false
        setOnClickListener { onClick() }
    }

    private fun row(vararg buttons: Button) {
        val r = LinearLayout(a).apply { orientation = LinearLayout.HORIZONTAL }
        for (b in buttons) r.addView(b, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        body.addView(r, lp().apply { topMargin = dp(4) })
    }

    private fun fill(s: Spinner, items: List<String>) {
        s.adapter = ArrayAdapter(a, android.R.layout.simple_spinner_dropdown_item, items)
    }

    private fun select(s: Spinner, i: Int) {
        if (i >= 0 && i < (s.adapter?.count ?: 0)) s.setSelection(i, false)
    }

    private fun dp(v: Int) = (v * a.resources.displayMetrics.density).toInt()

    companion object {
        /** The reply to `apitest` as lines a person reads (the rules of Studio's show_apitest). */
        fun describeApiTest(d: JSONObject): String {
            val out = ArrayList<String>()
            val err = d.opt("error")
            if (err is String) return "ทดสอบไม่ได้: $err"
            out += "key บนบอร์ด ${d.optString("keyLen", "?")} ตัวอักษร"
            d.optJSONArray("models")?.let { m ->
                for (i in 0 until m.length()) {
                    val o = m.getJSONObject(i)
                    out += (if (o.optInt("http") == 200) "✔ มองเห็น " else "✘ มองไม่เห็น ") + "${o.optString("id")} (HTTP ${o.optInt("http")})"
                }
            }
            d.optJSONArray("generate")?.let { g ->
                for (i in 0 until g.length()) {
                    val o = g.getJSONObject(i)
                    val h = o.optInt("http")
                    val id = o.optString("id")
                    out += when {
                        h == 200 -> "✔ เรียก $id ได้"
                        h == 400 && "live" in id -> "• $id: ใช้ได้เฉพาะ Live (WebSocket) — ปกติ"
                        else -> {
                            val e = o.optJSONObject("error")
                            "✘ $id HTTP $h ${e?.optString("status") ?: ""} ${e?.optString("message")?.take(120) ?: ""}".trim()
                        }
                    }
                }
            }
            d.optJSONObject("search")?.let { s ->
                out += if (s.optInt("http") == 200) "✔ Google Search ใช้ได้" else "✘ Google Search HTTP ${s.optInt("http")}"
            }
            return out.joinToString("\n")
        }
    }
}
