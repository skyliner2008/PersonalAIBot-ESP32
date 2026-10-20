package io.github.skyliner2008.pabflasher

import android.app.AlertDialog
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.widget.AbsListView
import android.widget.BaseAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.ListView
import android.widget.TextView
import org.json.JSONObject

/** The "บอร์ด" page: live status (`diag` every 3 s), the quick buttons of Studio's top bar, and chat (`ask`). */
class BoardPage(private val a: MainActivity, root: View) {
    private val info: TextView = root.findViewById(R.id.boardInfo)
    private val chatList: ListView = root.findViewById(R.id.chatList)
    private val chatInput: EditText = root.findViewById(R.id.chatInput)
    private val chat = ArrayList<Pair<Boolean, String>>()

    private val adapter = object : BaseAdapter() {
        override fun getCount() = chat.size
        override fun getItem(p: Int) = chat[p]
        override fun getItemId(p: Int) = p.toLong()
        override fun getView(p: Int, convert: View?, parent: ViewGroup): View {
            val tv = (convert as? TextView) ?: TextView(a).apply {
                textSize = 15f
                setPadding(24, 14, 24, 14)
                setTextIsSelectable(false)
            }
            val (user, text) = chat[p]
            tv.text = a.getString(if (user) R.string.chat_you else R.string.chat_ai, text)
            tv.setBackgroundResource(if (user) R.drawable.bubble_user else R.drawable.bubble_ai)
            return tv
        }
    }

    init {
        chatList.adapter = adapter
        chatList.transcriptMode = AbsListView.TRANSCRIPT_MODE_ALWAYS_SCROLL
        info.setOnClickListener { a.send("diag", echo = false) }
        val quick = mapOf(R.id.btnMute to "mute", R.id.btnSleep to "sleep", R.id.btnWake to "wake",
            R.id.btnRetry to "retry", R.id.btnNew to "new")
        for ((id, cmd) in quick) root.findViewById<Button>(id).setOnClickListener { a.send(cmd) }
        root.findViewById<Button>(R.id.btnReboot).setOnClickListener {
            AlertDialog.Builder(a).setMessage(R.string.reboot_confirm)
                .setPositiveButton(R.string.reboot) { _, _ -> a.send("reboot") }
                .setNegativeButton(android.R.string.cancel, null).show()
        }
        root.findViewById<Button>(R.id.chatSend).setOnClickListener { ask() }
        chatInput.setOnEditorActionListener { _, id, ev ->
            if (id == EditorInfo.IME_ACTION_SEND || ev?.keyCode == KeyEvent.KEYCODE_ENTER) { ask(); true } else false
        }
    }

    private fun ask() {
        val t = chatInput.text.toString().trim()
        if (t.isNotEmpty() && a.send("ask $t")) chatInput.setText("")
    }

    fun chat(user: Boolean, text: String) {
        chat += user to text
        if (chat.size > 200) chat.removeAt(0)
        adapter.notifyDataSetChanged()
    }

    fun onDiag(d: JSONObject) {
        info.text = describe(d).joinToString("\n")
    }

    companion object {
        /** The status card, in Thai: the fields of fillDiagnostics() a person looks at. */
        fun describe(d: JSONObject): List<String> {
            val out = ArrayList<String>()
            fun has(k: String) = d.has(k) && !d.isNull(k)
            val state = Console.STATE_TH[d.optString("state")] ?: d.optString("state", "?")
            out += "สถานะ: $state • เซสชัน AI: ${d.optString("session", "?")}"
            if (has("error_text") && d.optString("error_text").isNotEmpty()) out += "⚠ ${d.optString("error_text")}"
            if (d.optBoolean("auth_error")) out += "⚠ API key ใช้ไม่ได้ — ตั้งค่าใหม่ในแท็บตั้งค่า"
            out += if (d.optBoolean("wifi")) "Wi-Fi: ${d.optInt("rssi")} dBm • IP ${d.optString("ip")}" else "Wi-Fi: ยังไม่เชื่อมต่อ"
            if (has("active_model")) {
                val pref = d.optString("preferred_model")
                val act = d.optString("active_model")
                out += "โมเดล: $act" + if (pref.isNotEmpty() && pref != act) " (หลัก $pref)" else ""
            }
            when (d.optString("search_state")) {
                "quota" -> out += "Google Search: ปิดชั่วคราว (โควตาหมด)"
                "suspect" -> out += "Google Search: กำลังตรวจ"
                else -> if (has("google_search")) out += "Google Search: " + if (d.optBoolean("google_search")) "ใช้ได้" else "ปิด"
            }
            if (d.optInt("hold_s") > 0) out += "⏸ พักการเชื่อมต่อ ${d.optInt("hold_s")} วิ (${d.optString("last_failure")})"
            val flags = ArrayList<String>()
            if (d.optBoolean("muted")) flags += "🎤 ปิดไมค์"
            if (d.optBoolean("sleeping")) flags += "💤 พักอยู่"
            if (flags.isNotEmpty()) out += flags.joinToString(" • ")
            val radio = d.optString("radio")
            if (radio.isNotEmpty() && radio != "idle") {
                val th = mapOf("playing" to "กำลังเล่น", "connecting" to "กำลังต่อสถานี", "error" to "เล่นไม่ได้")
                out += "วิทยุ: ${th[radio] ?: radio}" + (d.optString("radio_station").takeIf { it.isNotEmpty() }?.let { " — $it" } ?: "")
            }
            if (has("tokens_per_min")) out += "โทเคน/นาที: ${d.optInt("tokens_per_min")}"
            val extra = listOf("memories" to "ความจำ", "open_tasks" to "งานค้าง", "reminders" to "การแจ้งเตือน",
                "heap_kb" to "RAM ว่าง KB", "psram_kb" to "PSRAM ว่าง KB", "battery" to "แบต %")
                .filter { has(it.first) }.joinToString(" • ") { "${it.second} ${d.opt(it.first)}" }
            if (extra.isNotEmpty()) out += extra
            if (has("uptime_s")) out += "เปิดมา ${uptime(d.optLong("uptime_s"))} • เฟิร์มแวร์ ${d.optString("fw")}"
            else if (has("fw")) out += "เฟิร์มแวร์ ${d.optString("fw")}"
            return out
        }

        fun uptime(s: Long): String = when {
            s >= 86_400 -> "${s / 86_400} วัน ${s % 86_400 / 3600} ชม."
            s >= 3600 -> "${s / 3600} ชม. ${s % 3600 / 60} นาที"
            else -> "${s / 60} นาที"
        }
    }
}
