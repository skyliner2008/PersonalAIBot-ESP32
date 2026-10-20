package io.github.skyliner2008.pabflasher

import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.CopyOnWriteArrayList

/**
 * Everything the phone tells the board and the board asks of the phone (wiki 18), one per app process:
 * the pairing token, the transport (USB console when plugged in, else Wi-Fi), GPS, the Google Maps step,
 * the board's requests ("navigate" / "stop"). No Android classes here: the service, the notification
 * listener and the screens feed it; [Actions] does the phone side.
 */
class PhoneHub(
    private val store: Store,
    private val actions: Actions,
    app: String,
    wifiPort: Int = PhoneProtocol.TCP_PORT,
    announcePort: Int = PhoneProtocol.UDP_PORT,
    private val now: () -> Long = { System.currentTimeMillis() },
) : WifiBoardServer.Listener {
    interface Store {
        fun loadToken(): String?
        fun saveToken(token: String)
        var paired: Boolean  // a board took our token over USB at least once
    }

    interface Actions {
        /** null = done, else why not (Thai, said by the board's assistant). */
        fun navigate(to: String): String?
        fun stop(): String?
    }

    fun interface Observer {
        fun onHubChanged()
    }

    val token: String = store.loadToken()?.takeIf { it.length == 32 } ?: PhoneProtocol.newToken().also { store.saveToken(it) }
    val wifi = WifiBoardServer({ token }, app, this, wifiPort, announcePort)
    private val observers = CopyOnWriteArrayList<Observer>()
    private val events = ArrayDeque<String>()
    private val clock = SimpleDateFormat("HH:mm:ss", Locale.US)

    @Volatile private var usbSender: ((String) -> Unit)? = null
    @Volatile var wifiBoard: String? = null
        private set
    @Volatile var step: MapsParser.Step? = null
        private set
    @Volatile var rawMaps: String = ""
        private set
    @Volatile var lastFix: String = ""
        private set
    @Volatile var lastFixAt = 0L
        private set
    @Volatile var place: String = ""
        private set
    private var lastSentAt = 0L
    private var lastNavSentAt = 0L

    val paired get() = store.paired
    val via get() = when {
        usbSender != null -> "usb"
        wifi.connected -> "wifi"
        else -> "none"
    }

    fun observe(o: Observer) = observers.add(o)
    fun unobserve(o: Observer) = observers.remove(o)
    private fun changed() = observers.forEach { it.onHubChanged() }

    fun recentEvents(): List<String> = synchronized(events) { events.toList() }

    private fun note(text: String) {
        synchronized(events) {
            events.addLast("${clock.format(Date(now()))} $text")
            while (events.size > 40) events.removeFirst()
        }
        changed()
    }

    // ---------------------------------------------------------------- transports

    /** The USB console is up (MainActivity): pair, then the phone messages go this way. */
    fun usbConnected(sender: (String) -> Unit) {
        usbSender = sender
        sender(PhoneProtocol.pair(token))
        store.paired = true
        note("เชื่อมบอร์ดทาง USB แล้ว — จับคู่ให้ (ใช้ Wi-Fi ได้ต่อจากนี้)")
    }

    fun usbGone() {
        if (usbSender == null) return
        usbSender = null
        note("USB หลุด")
    }

    fun startWifi() {
        try {
            wifi.start()
            note("รอบอร์ดทาง Wi-Fi (พอร์ต ${PhoneProtocol.TCP_PORT})")
        } catch (e: Exception) {
            note("เปิดรับบอร์ดทาง Wi-Fi ไม่ได้: ${e.message}")
        }
    }

    fun stopWifi() = wifi.stop()

    override fun onBoardConnected(address: String) {
        wifiBoard = address
        note("บอร์ดต่อเข้ามาทาง Wi-Fi ($address) — ยืนยันการจับคู่แล้ว")
        step?.let { send(PhoneProtocol.nav(it)) }
    }

    override fun onBoardRequest(json: String) = handleRequest(json)

    override fun onBoardGone(why: String) {
        wifiBoard = null
        note("Wi-Fi: $why")
    }

    /** One "phone {...}" line to the board, USB first. */
    fun send(line: String): Boolean {
        val usb = usbSender
        val ok = if (usb != null) { usb(line); true } else wifi.send(line)
        if (ok) lastSentAt = now()
        return ok
    }

    // ---------------------------------------------------------------- what the phone knows

    fun onGps(lat: Double, lon: Double, speedKmh: Float, heading: Float, accuracy: Float) {
        send(PhoneProtocol.gps(lat, lon, speedKmh, heading, accuracy))
        lastFixAt = now()
        lastFix = "%.0f km/h • แม่นยำ %.0f ม.".format(speedKmh, accuracy)
        changed()
    }

    /** The Maps step (null = navigation ended). Sent when it changes, and again every 5 s while navigating. */
    fun onStep(s: MapsParser.Step?, raw: String) {
        if (raw.isNotEmpty()) rawMaps = raw
        val before = step
        step = s
        if (s == null) {
            if (before != null) {
                send(PhoneProtocol.navEnd())
                note("Google Maps จบการนำทาง")
            }
        } else if (s != before || now() - lastNavSentAt > 5000) {
            send(PhoneProtocol.nav(s))
            lastNavSentAt = now()
            if (before == null) note("Google Maps เริ่มนำทาง")
        }
        changed()
    }

    fun onPlace(name: String) {
        if (name.isEmpty() || name == place) return
        place = name
        send(PhoneProtocol.place(name))
        changed()
    }

    /** Every ~2 s from the service: the board drops a silent phone after 10 s. */
    fun keepAlive() {
        val t = now()
        step?.let { if (t - lastNavSentAt > 5000) onStep(it, "") }
        if (t - lastSentAt > 2000) send(PhoneProtocol.ping())
    }

    // ---------------------------------------------------------------- what the board asks

    fun handleRequest(json: String) {
        val r = PhoneProtocol.parseRequest(json) ?: return
        val why = when (r.what) {
            "navigate" -> if (r.to.isBlank()) "ไม่ได้บอกจุดหมาย" else actions.navigate(r.to)
            "stop" -> actions.stop()
            else -> "มือถือไม่รู้จักคำสั่ง ${r.what}"
        }
        send(PhoneProtocol.result(r.id, why == null, why ?: "ok"))
        note(when {
            why != null -> "บอร์ดขอ ${r.what} ${r.to} — ทำไม่ได้: $why"
            r.what == "navigate" -> "บอร์ดสั่งนำทางไป ${r.to} → เปิด Google Maps"
            else -> "บอร์ดสั่งหยุดนำทาง"
        })
    }
}
