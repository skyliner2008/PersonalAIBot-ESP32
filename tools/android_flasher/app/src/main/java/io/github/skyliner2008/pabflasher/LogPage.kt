package io.github.skyliner2008.pabflasher

import android.content.Intent
import android.graphics.Typeface
import android.os.Handler
import android.os.Looper
import android.text.Editable
import android.text.TextWatcher
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.widget.AbsListView
import android.widget.BaseAdapter
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.ListView
import android.widget.TextView
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** The "Log" page: the board's log live, coloured like Studio's, a filter, and a console command line. */
class LogPage(private val a: MainActivity, root: View) {
    private class Entry(val time: Long, val text: String, val kind: Console.Kind)

    private val list: ListView = root.findViewById(R.id.logList)
    private val filter: EditText = root.findViewById(R.id.logFilter)
    private val hideNoise: CheckBox = root.findViewById(R.id.logHideNoise)
    private val showReplies: CheckBox = root.findViewById(R.id.logShowReplies)
    private val follow: CheckBox = root.findViewById(R.id.logFollow)
    private val cmd: EditText = root.findViewById(R.id.cmdInput)

    private val all = ArrayDeque<Entry>()
    private val shown = ArrayList<Entry>()
    private val main = Handler(Looper.getMainLooper())
    private var notifyPending = false
    private val history = ArrayList<String>()
    private val clock = SimpleDateFormat("HH:mm:ss", Locale.US)

    private val adapter = object : BaseAdapter() {
        override fun getCount() = shown.size
        override fun getItem(p: Int) = shown[p]
        override fun getItemId(p: Int) = p.toLong()
        override fun getView(p: Int, convert: View?, parent: ViewGroup): View {
            val tv = (convert as? TextView) ?: TextView(a).apply {
                typeface = Typeface.MONOSPACE
                textSize = 11f
                setPadding(12, 2, 12, 2)
                setTextIsSelectable(false)
            }
            val e = shown[p]
            tv.text = "${clock.format(Date(e.time))} ${e.text}"
            tv.setTextColor(color(e.kind))
            return tv
        }
    }

    init {
        list.adapter = adapter
        list.transcriptMode = AbsListView.TRANSCRIPT_MODE_ALWAYS_SCROLL
        list.setOnItemLongClickListener { _, _, p, _ -> share(listOf(shown[p])); true }
        follow.setOnCheckedChangeListener { _, on ->
            list.transcriptMode = if (on) AbsListView.TRANSCRIPT_MODE_ALWAYS_SCROLL else AbsListView.TRANSCRIPT_MODE_DISABLED
        }
        hideNoise.setOnCheckedChangeListener { _, _ -> refilter() }
        showReplies.setOnCheckedChangeListener { _, _ -> refilter() }
        filter.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, st: Int, c: Int, af: Int) = Unit
            override fun onTextChanged(s: CharSequence?, st: Int, b: Int, c: Int) = Unit
            override fun afterTextChanged(s: Editable?) = refilter()
        })
        root.findViewById<Button>(R.id.logClear).setOnClickListener { all.clear(); refilter() }
        root.findViewById<Button>(R.id.logShare).setOnClickListener { share(shown) }
        root.findViewById<Button>(R.id.logHelp).setOnClickListener { a.send("help") }
        root.findViewById<Button>(R.id.cmdSend).setOnClickListener { sendCommand() }
        root.findViewById<Button>(R.id.cmdHistory).setOnClickListener { pickHistory() }
        cmd.setOnEditorActionListener { _, id, ev ->
            if (id == EditorInfo.IME_ACTION_SEND || ev?.keyCode == KeyEvent.KEYCODE_ENTER) { sendCommand(); true } else false
        }
    }

    fun line(text: String) = add(Entry(System.currentTimeMillis(), text, Console.classify(text)))
    fun command(text: String) = add(Entry(System.currentTimeMillis(), "> $text", Console.Kind.CMD))
    fun local(text: String) = add(Entry(System.currentTimeMillis(), "· $text", Console.Kind.LOCAL))

    private fun add(e: Entry) {
        all.addLast(e)
        if (all.size > MAX_LINES) {
            val gone = all.removeFirst()
            if (shown.isNotEmpty() && shown[0] === gone) shown.removeAt(0)
        }
        if (passes(e)) shown += e
        if (!notifyPending) {  // a burst of lines: one redraw
            notifyPending = true
            main.postDelayed({ notifyPending = false; adapter.notifyDataSetChanged() }, 80)
        }
    }

    private fun passes(e: Entry): Boolean {
        if (hideNoise.isChecked && (e.kind == Console.Kind.TOUCH || e.kind == Console.Kind.HEALTH)) return false
        if (!showReplies.isChecked && e.kind == Console.Kind.REPLY) return false
        val f = filter.text.toString().trim()
        return f.isEmpty() || e.text.contains(f, ignoreCase = true)
    }

    private fun refilter() {
        shown.clear()
        all.filterTo(shown) { passes(it) }
        adapter.notifyDataSetChanged()
    }

    private fun sendCommand() {
        val c = cmd.text.toString().trim()
        if (c.isEmpty()) return
        if (a.send(c)) {
            history.remove(c)
            history += c
            if (history.size > 30) history.removeAt(0)
            cmd.setText("")
        }
    }

    private fun pickHistory() {
        if (history.isEmpty()) return
        val items = history.reversed().map { Console.echo(it) }.toTypedArray()
        android.app.AlertDialog.Builder(a).setTitle(R.string.log_history)
            .setItems(items) { _, i -> cmd.setText(history.reversed()[i]); cmd.setSelection(cmd.text.length) }
            .show()
    }

    private fun share(entries: List<Entry>) {
        if (entries.isEmpty()) return
        val text = entries.joinToString("\n") { "${clock.format(Date(it.time))} ${it.text}" }
        val send = Intent(Intent.ACTION_SEND).setType("text/plain").putExtra(Intent.EXTRA_TEXT, text)
            .putExtra(Intent.EXTRA_SUBJECT, "ESP32-AI log")
        a.startActivity(Intent.createChooser(send, a.getString(R.string.log_share)))
    }

    private fun color(k: Console.Kind): Int = a.getColor(when (k) {
        Console.Kind.ERR -> R.color.log_err
        Console.Kind.WARN -> R.color.log_warn
        Console.Kind.USER -> R.color.log_user
        Console.Kind.CHAT, Console.Kind.OK -> R.color.log_ok
        Console.Kind.GEMINI -> R.color.log_gemini
        Console.Kind.WIFI -> R.color.log_wifi
        Console.Kind.TOUCH, Console.Kind.HEALTH, Console.Kind.REPLY -> R.color.log_dim
        Console.Kind.MIC -> R.color.log_mic
        Console.Kind.CMD -> R.color.log_cmd
        Console.Kind.LOCAL -> R.color.muted
        Console.Kind.INFO -> R.color.log_info
    })

    companion object {
        const val MAX_LINES = 3000
    }
}
