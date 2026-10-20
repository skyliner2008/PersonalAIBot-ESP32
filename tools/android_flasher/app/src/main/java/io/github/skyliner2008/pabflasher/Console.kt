package io.github.skyliner2008.pabflasher

import java.io.IOException

/**
 * The firmware's serial console, the same one ESP32-AI Studio uses (wiki 10): one command per line, log lines
 * ("[Agent] ...", "[Chat] ai: ...", "E (123) ..."), replies as one line "<TAG> <json>", and two binary replies
 * ("SCREENSHOT w h n" / "RECORD rate n" / "RECPAIRS rate n" + n bytes + "\nEND\n") that must not be read as text.
 */
object Console {
    val TAGS = setOf("CONFIG", "OPTIONS", "TOOLS", "DIAG", "WIFISCAN", "APITEST", "RECINFO", "RADIOLIST", "TOOL",
        "OTA", "MEM", "WAKEWORD")

    class Tagged(val tag: String, val name: String?, val json: String)

    /** "DIAG {...}" -> DIAG + json; "TOOL get_weather {...}" -> TOOL, get_weather; anything else null. */
    fun parseTag(line: String): Tagged? {
        val sp = line.indexOf(' ')
        if (sp <= 0) return null
        val tag = line.substring(0, sp)
        if (tag !in TAGS) return null
        var rest = line.substring(sp + 1).trim()
        var name: String? = null
        if (tag == "TOOL") {
            val s2 = rest.indexOf(' ')
            if (s2 <= 0) return null
            name = rest.substring(0, s2)
            rest = rest.substring(s2 + 1).trim()
        }
        if (!rest.startsWith("{") && !rest.startsWith("[")) return null
        return Tagged(tag, name, rest)
    }

    enum class Kind { ERR, WARN, USER, CHAT, GEMINI, WIFI, TOUCH, HEALTH, OK, MIC, INFO, CMD, LOCAL, REPLY }

    private val ERR_RE = Regex("^E \\(|ERROR|error|failed|FAIL|Disconnect reason")

    /** The colours of Studio's log tab, same rules. */
    fun classify(t: String): Kind = when {
        parseTag(t) != null -> Kind.REPLY
        ERR_RE.containsMatchIn(t) -> Kind.ERR
        t.startsWith("[Mic]") -> if ("⚠" in t) Kind.WARN else Kind.MIC
        t.startsWith("[Fallback]") || "quota" in t || t.startsWith("W (") -> Kind.WARN
        t.startsWith("[Chat] user") -> Kind.USER
        t.startsWith("[Chat]") -> Kind.CHAT
        t.startsWith("[Gemini]") -> Kind.GEMINI
        t.startsWith("[WiFi]") -> Kind.WIFI
        t.startsWith("[Touch]") -> Kind.TOUCH
        t.startsWith("[Health]") -> Kind.HEALTH
        t.startsWith("OK ") || "Connected" in t || "session ready" in t -> Kind.OK
        else -> Kind.INFO
    }

    private val SECRET_RE = Regex("(key=)[^&\\s\"]+")

    /** The firmware never prints a key, but a URL in a log line could carry one. */
    fun mask(t: String): String = SECRET_RE.replace(t, "$1***")

    /** What the log shows for a command we sent: never the Wi-Fi password or the API key. */
    fun echo(cmd: String): String {
        val m = Regex("^set\\s+(key|pass)\\s+.*$").find(cmd.trim()) ?: return cmd
        return "set ${m.groupValues[1]} ***"
    }

    /** A command is one line: newlines would split it into two commands on the board. */
    fun clean(cmd: String): String = cmd.replace('\r', ' ').replace('\n', ' ').trim()

    /** "[Chat] user: hi" -> (true, "hi"), "[Chat] ai: ..." -> (false, ...); other lines null. */
    fun chat(line: String): Pair<Boolean, String>? = when {
        line.startsWith("[Chat] user: ") -> true to line.substring(13)
        line.startsWith("[Chat] ai: ") -> false to line.substring(11)
        else -> null
    }

    val STATE_TH = mapOf("boot" to "กำลังบูต", "connecting" to "กำลังเชื่อมต่อ", "idle" to "พร้อมคุย",
        "listening" to "กำลังฟัง", "thinking" to "กำลังคิด", "speaking" to "กำลังพูด", "error" to "มีปัญหา",
        "sleep" to "พักอยู่", "setup" to "รอตั้งค่า")
}

/** Splits what the board sends into lines, skipping the binary replies (and their END trailer). */
class LineStream {
    sealed class Event {
        class Line(val text: String) : Event()
        /** A binary reply went by: [ok] = it ended with "\nEND\n" (nothing else was printed into it). */
        class Blob(val header: String, val ok: Boolean) : Event()
    }

    private var buf = ByteArray(16 * 1024)
    private var len = 0
    private var blobLeft = 0
    private var blobHeader: String? = null
    private var awaitEnd = false

    fun feed(data: ByteArray, n: Int = data.size): List<Event> {
        ensure(len + n)
        data.copyInto(buf, len, 0, n)
        len += n
        val out = ArrayList<Event>()
        var pos = 0
        while (true) {
            if (blobLeft > 0) {
                val take = minOf(blobLeft, len - pos)
                pos += take
                blobLeft -= take
                if (blobLeft > 0) break
                awaitEnd = true
                continue
            }
            if (awaitEnd) {
                if (len - pos < 5) break
                awaitEnd = false
                if (matchesEnd(pos)) {
                    pos += 5
                    out += Event.Blob(blobHeader ?: "", true)
                } else {  // something else was printed into the binary stream: resync on the trailer
                    out += Event.Blob(blobHeader ?: "", false)
                    var k = pos
                    while (k <= len - 5 && !matchesEnd(k)) k++
                    if (k <= len - 5) pos = k + 5
                }
                continue
            }
            var nl = pos
            while (nl < len && buf[nl] != '\n'.code.toByte()) nl++
            if (nl >= len) {
                if (len - pos > MAX_LINE) pos = len  // no newline in 256 KB: not a console line
                break
            }
            val text = String(buf, pos, nl - pos, Charsets.UTF_8).trimEnd('\r')
            pos = nl + 1
            val size = blobSize(text)
            if (size != null) {
                blobHeader = text
                blobLeft = size
                if (size == 0) awaitEnd = true
                continue
            }
            if (text == "END" || text.isBlank()) continue
            out += Event.Line(text)
        }
        buf.copyInto(buf, 0, pos, len)
        len -= pos
        return out
    }

    private fun matchesEnd(at: Int) = buf[at] == '\n'.code.toByte() && buf[at + 1] == 'E'.code.toByte() &&
        buf[at + 2] == 'N'.code.toByte() && buf[at + 3] == 'D'.code.toByte() && buf[at + 4] == '\n'.code.toByte()

    private fun ensure(size: Int) {
        if (size > buf.size) buf = buf.copyOf(maxOf(size, buf.size * 2))
    }

    companion object {
        const val MAX_LINE = 256 * 1024

        /** Byte count of a binary reply header: SCREENSHOT w h n, RECORD rate n, RECPAIRS rate n. */
        fun blobSize(line: String): Int? {
            val p = line.split(' ')
            val n = when (p[0]) {
                "SCREENSHOT" -> if (p.size == 4) p[3] else null
                "RECORD", "RECPAIRS" -> if (p.size == 3) p[2] else null
                else -> null
            } ?: return null
            return n.toIntOrNull()?.takeIf { it in 0..64 * 1024 * 1024 }
        }
    }
}

/**
 * The console session over the board's USB port: a reader thread turns bytes into [LineStream] events,
 * [send] writes one command line. Opening it never touches DTR/RTS, so the board is not reset (like Studio).
 */
class BoardLink(
    private val port: SerialPort,
    private val onEvent: (LineStream.Event) -> Unit,
    private val onLost: (IOException) -> Unit,
) {
    @Volatile private var running = true
    private val writeLock = Any()
    private val thread = Thread({ loop() }, "board-link")

    fun start() = thread.start()

    fun send(cmd: String) {
        val line = Console.clean(cmd)
        if (line.isEmpty()) return
        synchronized(writeLock) { port.write((line + "\n").toByteArray(Charsets.UTF_8), 2000) }
    }

    fun close() {
        running = false
        if (Thread.currentThread() !== thread) thread.join(1500)
        port.close()
    }

    private fun loop() {
        val buf = ByteArray(16 * 1024)
        val lines = LineStream()
        try {
            while (running) {
                val n = port.read(buf, 200)
                if (n > 0) lines.feed(buf, n).forEach(onEvent)
            }
        } catch (e: IOException) {
            if (running) onLost(e)
        }
    }
}
