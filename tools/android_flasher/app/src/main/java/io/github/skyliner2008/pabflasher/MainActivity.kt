package io.github.skyliner2008.pabflasher

import android.annotation.SuppressLint
import android.app.Activity
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.View
import android.widget.Button
import android.widget.TextView
import org.json.JSONObject
import java.io.IOException
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

/**
 * ESP32-AI Flasher: the board's console on a phone (status, log, settings, chat - the serial protocol of
 * ESP32-AI Studio, wiki 10) and the USB flasher (wiki 17). One USB port: the console lets go of it while flashing.
 */
class MainActivity : Activity() {
    lateinit var usb: UsbManager
        private set
    private lateinit var linkStatus: TextView
    private lateinit var linkButton: Button
    private lateinit var pages: List<View>
    private lateinit var tabs: List<TextView>

    lateinit var flashPage: FlashPage
        private set
    private lateinit var boardPage: BoardPage
    private lateinit var logPage: LogPage
    private lateinit var settingsPage: SettingsPage

    private val main = Handler(Looper.getMainLooper())
    private val linkWorker = Executors.newSingleThreadExecutor()
    @Volatile private var link: BoardLink? = null
    @Volatile private var linkSuspended = false
    @Volatile private var permissionLatch: CountDownLatch? = null
    private var tab = 0
    var diag: JSONObject? = null
        private set
    var config: JSONObject? = null
        private set

    private val usbReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                ACTION_USB_PERMISSION -> permissionLatch?.countDown()
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> { refreshBoard(); main.postDelayed({ autoConnect() }, 800) }
                else -> refreshBoard()
            }
        }
    }

    private val diagTick = object : Runnable {
        override fun run() {
            if (link != null && tab == TAB_BOARD) send("diag", echo = false)
            main.postDelayed(this, DIAG_EVERY_MS)
        }
    }

    @SuppressLint("UnspecifiedRegisterReceiverFlag")  // the flag exists from API 33, used there
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        usb = getSystemService(UsbManager::class.java)
        linkStatus = findViewById(R.id.linkStatus)
        linkButton = findViewById(R.id.linkButton)
        pages = listOf(findViewById(R.id.pageBoard), findViewById(R.id.pageLog), findViewById(R.id.pageSettings),
            findViewById(R.id.pageFlash))
        tabs = listOf(findViewById(R.id.tabBoard), findViewById(R.id.tabLog), findViewById(R.id.tabSettings),
            findViewById(R.id.tabFlash))
        boardPage = BoardPage(this, pages[TAB_BOARD])
        logPage = LogPage(this, pages[TAB_LOG])
        settingsPage = SettingsPage(this, pages[TAB_SETTINGS])
        flashPage = FlashPage(this, pages[TAB_FLASH])

        val filter = IntentFilter().apply {
            addAction(ACTION_USB_PERMISSION)
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
        }
        if (Build.VERSION.SDK_INT >= 33) registerReceiver(usbReceiver, filter, RECEIVER_NOT_EXPORTED)
        else registerReceiver(usbReceiver, filter)

        tabs.forEachIndexed { i, t -> t.setOnClickListener { showTab(i) } }
        linkButton.setOnClickListener { if (link != null) disconnect() else connect(ask = true) }
        val saved = try { getPreferences(MODE_PRIVATE).getInt("tab", TAB_BOARD) } catch (_: Exception) { TAB_BOARD }
        showTab(saved.coerceIn(0, pages.size - 1))
        main.postDelayed(diagTick, DIAG_EVERY_MS)
        updateLinkBar()
    }

    override fun onResume() {
        super.onResume()
        refreshBoard()
        autoConnect()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        refreshBoard()  // the board was plugged in while the app was open
        main.postDelayed({ autoConnect() }, 800)
    }

    override fun onDestroy() {
        main.removeCallbacksAndMessages(null)
        unregisterReceiver(usbReceiver)
        link?.close()
        link = null
        linkWorker.shutdownNow()
        flashPage.shutdown()
        super.onDestroy()
    }

    @Deprecated("Activity result API of the framework")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        @Suppress("DEPRECATION")
        super.onActivityResult(requestCode, resultCode, data)
        flashPage.onActivityResult(requestCode, resultCode, data)
    }

    @Deprecated("The flash must not be interrupted half way")
    override fun onBackPressed() {
        if (flashPage.busy) { flashPage.warnBusy(); return }
        @Suppress("DEPRECATION")
        super.onBackPressed()
    }

    private fun showTab(i: Int) {
        tab = i
        pages.forEachIndexed { k, p -> p.visibility = if (k == i) View.VISIBLE else View.GONE }
        tabs.forEachIndexed { k, t -> t.isSelected = k == i }
        try { getPreferences(MODE_PRIVATE).edit().putInt("tab", i).apply() } catch (_: Exception) {}
        if (i == TAB_BOARD && link != null) send("diag", echo = false)
        if (i == TAB_SETTINGS && link != null && config == null) send("config", echo = false)
    }

    fun refreshBoard() {
        flashPage.showBoard(UsbCdcPort.findBoard(usb), usb.deviceList.values.firstOrNull())
        updateLinkBar()
    }

    // ---------------------------------------------------------------- the console link

    val connected get() = link != null

    /** Connect without asking when the permission is already there (plugged in, app opened). */
    private fun autoConnect() {
        if (link != null || linkSuspended || flashPage.busy) return
        val dev = UsbCdcPort.findBoard(usb) ?: return
        if (usb.hasPermission(dev)) connect(ask = false)
    }

    private fun connect(ask: Boolean) {
        if (link != null || linkSuspended || flashPage.busy) return
        linkStatus.setText(R.string.link_connecting)
        linkWorker.execute {
            try {
                val dev = UsbCdcPort.findBoard(usb) ?: throw IOException(getString(R.string.board_none_short))
                if (!usb.hasPermission(dev) && (!ask || !askPermission(dev))) throw IOException(getString(R.string.no_permission))
                if (link != null || linkSuspended) return@execute
                val port = UsbCdcPort.open(usb, dev)
                if (linkSuspended) { port.close(); return@execute }  // a flash started meanwhile: the port is its
                val l = BoardLink(port, { ev -> main.post { onEvent(ev) } }, { e -> main.post { onLost(e) } })
                link = l
                l.start()
                main.post {
                    updateLinkBar()
                    logPage.local(getString(R.string.link_connected_log))
                    for (c in listOf("config", "options", "diag")) send(c, echo = false)
                }
            } catch (e: Exception) {
                main.post {
                    updateLinkBar()
                    if (ask) logPage.local("✖ " + (e.message ?: e.toString()))
                }
            }
        }
    }

    private fun disconnect() {
        val l = link ?: return
        link = null
        linkWorker.execute { l.close() }
        updateLinkBar()
        logPage.local(getString(R.string.link_closed_log))
    }

    private fun onLost(e: IOException) {
        val l = link ?: return
        link = null
        linkWorker.execute { l.close() }
        logPage.local(getString(R.string.link_lost_log, e.message ?: ""))
        updateLinkBar()
        // a reboot re-enumerates the port: try again for a while
        for (k in 1..8) main.postDelayed({ autoConnect() }, 1500L * k)
    }

    /** Before a flash: the flasher needs the port. Called from the flash worker thread. */
    fun suspendLink() {
        linkSuspended = true
        try {  // a connect in flight finishes first (it then sees linkSuspended and lets go of the port)
            linkWorker.submit {}.get(5, TimeUnit.SECONDS)
        } catch (_: Exception) {
        }
        val l = link ?: return
        link = null
        l.close()
        main.post { updateLinkBar(); logPage.local(getString(R.string.link_flash_log)) }
    }

    /** After a flash: the board reboots into the new firmware, then the console comes back. */
    fun resumeLink() {
        linkSuspended = false
        for (k in 2..10) main.postDelayed({ autoConnect() }, 1000L * k)
    }

    /** Send a console command; [echo] shows it in the log (secrets masked). */
    fun send(cmd: String, echo: Boolean = true): Boolean {
        val l = link ?: run {
            if (echo) logPage.local(getString(R.string.not_connected))
            return false
        }
        if (echo) logPage.command(Console.echo(Console.clean(cmd)))
        linkWorker.execute {
            try {
                l.send(cmd)
            } catch (e: IOException) {
                main.post { onLost(e) }
            }
        }
        return true
    }

    private fun onEvent(ev: LineStream.Event) {
        when (ev) {
            is LineStream.Event.Blob -> logPage.local(getString(if (ev.ok) R.string.blob_ok else R.string.blob_bad, ev.header))
            is LineStream.Event.Line -> {
                val text = Console.mask(ev.text)
                logPage.line(text)
                Console.chat(text)?.let { (user, msg) -> boardPage.chat(user, msg) }
                val t = Console.parseTag(text) ?: return
                try {
                    when (t.tag) {
                        "CONFIG" -> { config = JSONObject(t.json); settingsPage.onConfig(config!!); updateLinkBar() }
                        "OPTIONS" -> settingsPage.onOptions(BoardOptions(t.json))
                        "DIAG" -> { diag = JSONObject(t.json); boardPage.onDiag(diag!!); updateLinkBar() }
                        "WIFISCAN" -> settingsPage.onWifiScan(t.json)
                        "APITEST" -> settingsPage.onApiTest(JSONObject(t.json))
                    }
                } catch (e: Exception) {
                    logPage.local("✖ ${t.tag}: ${e.message}")
                }
            }
        }
    }

    private fun updateLinkBar() {
        val l = link
        linkButton.setText(if (l != null) R.string.link_disconnect else R.string.link_connect)
        linkButton.isEnabled = !flashPage.busy
        linkStatus.isActivated = l != null
        linkStatus.text = when {
            flashPage.busy -> getString(R.string.link_flashing)
            l == null && UsbCdcPort.findBoard(usb) == null -> getString(R.string.board_none_short)
            l == null -> getString(R.string.link_off)
            else -> {
                val d = diag
                val parts = mutableListOf(getString(R.string.link_on))
                (d?.optString("fw") ?: config?.optString("fw"))?.takeIf { it.isNotEmpty() }?.let { parts += "fw $it" }
                if (d != null) {
                    parts += Console.STATE_TH[d.optString("state")] ?: d.optString("state")
                    parts += if (d.optBoolean("wifi")) "Wi-Fi ${d.optInt("rssi")} dBm" else "Wi-Fi ✘"
                }
                parts.joinToString(" • ")
            }
        }
    }

    // ---------------------------------------------------------------- USB permission

    /** PortProvider for the flasher: wait for the board on USB (it may re-enumerate after a reset) + permission. */
    fun openBoard(): SerialPort {
        val deadline = SystemClock.elapsedRealtime() + 15_000
        var last: Exception? = null
        while (SystemClock.elapsedRealtime() < deadline) {
            val dev = UsbCdcPort.findBoard(usb)
            if (dev == null) { Thread.sleep(250); continue }
            if (!usb.hasPermission(dev) && !askPermission(dev)) throw IOException(getString(R.string.no_permission))
            try {
                return UsbCdcPort.open(usb, dev)
            } catch (e: IOException) {
                last = e
                Thread.sleep(300)
            }
        }
        throw last ?: IOException(getString(R.string.board_none))
    }

    /** Blocks a worker thread until the user answers the system's USB permission dialog. */
    fun askPermission(dev: UsbDevice): Boolean {
        val latch = CountDownLatch(1)
        permissionLatch = latch
        val intent = Intent(ACTION_USB_PERMISSION).setPackage(packageName)
        val flags = if (Build.VERSION.SDK_INT >= 31) PendingIntent.FLAG_MUTABLE else 0
        runOnUiThread { usb.requestPermission(dev, PendingIntent.getBroadcast(this, 0, intent, flags)) }
        latch.await(60, TimeUnit.SECONDS)
        return usb.hasPermission(dev)
    }

    fun onFlashBusyChanged() = main.post { updateLinkBar() }

    companion object {
        private const val ACTION_USB_PERMISSION = "io.github.skyliner2008.pabflasher.USB_PERMISSION"
        const val TAB_BOARD = 0
        const val TAB_LOG = 1
        const val TAB_SETTINGS = 2
        const val TAB_FLASH = 3
        private const val DIAG_EVERY_MS = 3000L
    }
}
