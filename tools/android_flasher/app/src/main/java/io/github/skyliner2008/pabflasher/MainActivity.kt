package io.github.skyliner2008.pabflasher

import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.provider.OpenableColumns
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.ProgressBar
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.TextView
import java.io.File
import java.io.IOException
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

/**
 * ESP32-AI Flasher: put PersonalAIBot-ESP32 on the board from a phone over a USB-C (OTG) cable.
 * Firmware comes from the latest GitHub release (sha256 + the OTA signature checked) or from a .bin on the phone.
 */
class MainActivity : Activity() {
    private lateinit var usb: UsbManager
    private lateinit var boardStatus: TextView
    private lateinit var releaseInfo: TextView
    private lateinit var modeGroup: RadioGroup
    private lateinit var fileInfo: TextView
    private lateinit var flashButton: Button
    private lateinit var progress: ProgressBar
    private lateinit var status: TextView
    private lateinit var log: TextView

    private val worker = Executors.newSingleThreadExecutor()
    @Volatile private var busy = false
    @Volatile private var release: Release? = null
    private var localImage: ByteArray? = null
    private var localName = ""
    private var lastMode = R.id.modeUpdate
    @Volatile private var permissionLatch: CountDownLatch? = null
    private var lastPermille = -1

    private val usbReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                ACTION_USB_PERMISSION -> permissionLatch?.countDown()
                else -> refreshBoard()
            }
        }
    }

    @SuppressLint("UnspecifiedRegisterReceiverFlag")  // the flag exists from API 33, used there
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        usb = getSystemService(UsbManager::class.java)
        boardStatus = findViewById(R.id.boardStatus)
        releaseInfo = findViewById(R.id.releaseInfo)
        modeGroup = findViewById(R.id.mode)
        fileInfo = findViewById(R.id.fileInfo)
        flashButton = findViewById(R.id.flashButton)
        progress = findViewById(R.id.progress)
        status = findViewById(R.id.status)
        log = findViewById(R.id.log)

        val filter = IntentFilter().apply {
            addAction(ACTION_USB_PERMISSION)
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
        }
        if (Build.VERSION.SDK_INT >= 33) registerReceiver(usbReceiver, filter, RECEIVER_NOT_EXPORTED)
        else registerReceiver(usbReceiver, filter)

        findViewById<RadioButton>(R.id.modeFile).setOnClickListener { pickFile() }
        modeGroup.setOnCheckedChangeListener { _, id ->
            if (id != R.id.modeFile) lastMode = id
            fileInfo.visibility = if (id == R.id.modeFile && localImage != null) View.VISIBLE else View.GONE
        }
        releaseInfo.setOnClickListener { checkRelease() }
        flashButton.setOnClickListener { confirmAndFlash() }
        checkRelease()
    }

    override fun onResume() {
        super.onResume()
        refreshBoard()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        refreshBoard()  // the board was plugged in while the app was open
    }

    override fun onDestroy() {
        unregisterReceiver(usbReceiver)
        worker.shutdownNow()
        super.onDestroy()
    }

    private fun refreshBoard() {
        val board = UsbCdcPort.findBoard(usb)
        val other = usb.deviceList.values.firstOrNull()
        boardStatus.text = when {
            board != null -> getString(R.string.board_found, board.vendorId, board.productId)
            other != null -> getString(R.string.board_other, other.vendorId, other.productId)
            else -> getString(R.string.board_none)
        }
        boardStatus.isActivated = board != null
    }

    private fun checkRelease() {
        if (busy) return
        releaseInfo.setText(R.string.release_checking)
        worker.execute {
            val text = try {
                val r = Releases.parse(Net.text(Releases.latestUrl(BuildConfig.REPO)))
                release = r
                getString(R.string.release_latest, r.tag)
            } catch (e: Exception) {
                getString(R.string.release_failed, Net.friendly(e))
            }
            runOnUiThread { releaseInfo.text = text }
        }
    }

    private fun pickFile() {
        val i = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
        @Suppress("DEPRECATION")
        startActivityForResult(i, REQ_FILE)
    }

    @Deprecated("Activity result API of the framework")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        @Suppress("DEPRECATION")
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != REQ_FILE) return
        val uri = data?.data
        if (resultCode != RESULT_OK || uri == null) {
            if (localImage == null) modeGroup.check(lastMode)
            return
        }
        status.setText(R.string.file_reading)
        worker.execute {  // a cloud file can take a while to arrive
            try {
                val bytes = readUri(uri)
                val kind = FlashPlan.detect(bytes) ?: throw IOException(getString(R.string.file_not_firmware))
                FlashPlan.problem(kind, bytes)?.let { throw IOException(it) }
                val name = displayName(uri)
                runOnUiThread {
                    localImage = bytes
                    localName = name
                    fileInfo.text = getString(if (kind == Kind.FACTORY) R.string.file_factory else R.string.file_app,
                        name, bytes.size)
                    fileInfo.visibility = View.VISIBLE
                    status.text = ""
                }
            } catch (e: Exception) {
                runOnUiThread {
                    localImage = null
                    fileInfo.visibility = View.GONE
                    modeGroup.check(lastMode)
                    showError(Net.friendly(e))
                }
            }
        }
    }

    private fun readUri(uri: Uri): ByteArray {
        val input = contentResolver.openInputStream(uri) ?: throw IOException("เปิดไฟล์ไม่ได้")
        return input.use {
            val b = it.readBytes()
            if (b.size > 32 * 1024 * 1024) throw IOException("ไฟล์ใหญ่เกินไป")
            b
        }
    }

    private fun displayName(uri: Uri): String =
        contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) c.getString(0) else null
        } ?: uri.lastPathSegment ?: "firmware.bin"

    private fun confirmAndFlash() {
        if (busy) return
        val mode = modeGroup.checkedRadioButtonId
        val factory = mode == R.id.modeFactory ||
            (mode == R.id.modeFile && localImage?.let { FlashPlan.detect(it) } == Kind.FACTORY)
        if (mode == R.id.modeFile && localImage == null) { pickFile(); return }
        if (!factory) { startFlash(mode); return }
        AlertDialog.Builder(this)
            .setTitle(R.string.factory_title)
            .setMessage(R.string.factory_message)
            .setPositiveButton(R.string.factory_go) { _, _ -> startFlash(mode) }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun startFlash(mode: Int) {
        busy = true
        setBusy(true)
        log.text = ""
        lastPermille = -1
        worker.execute {
            try {
                val (kind, image) = when (mode) {
                    R.id.modeFile -> {
                        val b = localImage ?: throw IOException("ยังไม่ได้เลือกไฟล์")
                        note("ไฟล์ในเครื่อง: $localName")
                        (FlashPlan.detect(b) ?: throw IOException(getString(R.string.file_not_firmware))) to b
                    }
                    R.id.modeFactory -> Kind.FACTORY to download(Kind.FACTORY)
                    else -> Kind.APP to download(Kind.APP)
                }
                FlashJob(::openBoard, object : FlashJob.Listener {
                    override fun status(text: String) = note(text)
                    override fun progress(done: Long, total: Long) = showProgress(done, total)
                }).run(kind, image)
                note(getString(if (kind == Kind.FACTORY) R.string.done_factory else R.string.done_app))
                runOnUiThread { status.setText(R.string.done) }
            } catch (e: Exception) {
                runOnUiThread { showError(Net.friendly(e)) }
            } finally {
                busy = false
                runOnUiThread { setBusy(false); refreshBoard() }
            }
        }
    }

    /** The release file, checked against its .sha256 (and the OTA signature for the app); cached once verified. */
    private fun download(kind: Kind): ByteArray {
        val r = release ?: Releases.parse(Net.text(Releases.latestUrl(BuildConfig.REPO))).also { release = it }
        val name = r.fileName(kind) ?: throw IOException("Release ${r.tag} ไม่มีไฟล์ ${if (kind == Kind.APP) "-app.bin" else "-factory.bin"}")
        note("เฟิร์มแวร์ ${r.tag}: $name")
        val shaUrl = r.url("$name.sha256") ?: throw IOException("Release ${r.tag} ไม่มีไฟล์ $name.sha256")
        val want = Releases.parseSha256(Net.text(shaUrl, "text/plain"), name)
            ?: throw IOException("ไฟล์ $name.sha256 อ่านไม่ได้")
        val cached = File(cacheDir, name)
        var bytes = if (cached.exists()) cached.readBytes() else null
        if (bytes == null || Releases.sha256Hex(bytes) != want) {
            note("กำลังดาวน์โหลด…")
            bytes = Net.bytes(r.url(name)!!) { got, total -> if (total > 0) showProgress(got, total) }
            if (Releases.sha256Hex(bytes) != want) throw IOException("ไฟล์ที่ดาวน์โหลดเสีย (sha256 ไม่ตรง) ลองใหม่")
            cached.parentFile?.listFiles { f -> f.name.endsWith(".bin") }?.forEach { it.delete() }
            cached.writeBytes(bytes)
        }
        note("sha256 ตรงกับ release ✔")
        if (kind == Kind.APP && BuildConfig.OTA_PUBKEY_PEM.isNotEmpty()) {
            val sigUrl = r.url("$name.sig") ?: throw IOException("Release นี้ไม่มีลายเซ็น (.sig) — ไม่แฟลช")
            if (!Releases.signatureOk(bytes, Net.text(sigUrl, "text/plain"), BuildConfig.OTA_PUBKEY_PEM)) {
                throw IOException("ลายเซ็นไม่ถูกต้อง — ไฟล์นี้ไม่ได้มาจากเจ้าของโปรเจค ไม่แฟลช")
            }
            note("ลายเซ็น OTA ถูกต้อง ✔")
        }
        return bytes
    }

    /** PortProvider: wait for the board on USB (it may re-enumerate after a reset) and for the permission. */
    private fun openBoard(): SerialPort {
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

    private fun askPermission(dev: UsbDevice): Boolean {
        val latch = CountDownLatch(1)
        permissionLatch = latch
        val intent = Intent(ACTION_USB_PERMISSION).setPackage(packageName)
        val flags = if (Build.VERSION.SDK_INT >= 31) PendingIntent.FLAG_MUTABLE else 0
        runOnUiThread { usb.requestPermission(dev, PendingIntent.getBroadcast(this, 0, intent, flags)) }
        note("รออนุญาตให้แอปใช้ USB…")
        latch.await(60, TimeUnit.SECONDS)
        return usb.hasPermission(dev)
    }

    private fun note(text: String) = runOnUiThread {
        status.text = text
        val lines = (log.text.toString() + text + "\n").lines().takeLast(40)
        log.text = lines.joinToString("\n")
    }

    private fun showProgress(done: Long, total: Long) {
        val permille = if (total <= 0) 0 else (done * 1000 / total).toInt()
        if (permille == lastPermille) return
        lastPermille = permille
        runOnUiThread { progress.progress = permille }
    }

    private fun showError(msg: String) {
        status.text = getString(R.string.error_prefix, msg)
        log.append("✖ $msg\n")
    }

    private fun setBusy(on: Boolean) {
        flashButton.isEnabled = !on
        for (i in 0 until modeGroup.childCount) modeGroup.getChildAt(i).isEnabled = !on
        progress.visibility = View.VISIBLE
        if (on) {
            progress.progress = 0
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        } else {
            window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
    }

    @Deprecated("The flash must not be interrupted half way")
    override fun onBackPressed() {
        if (busy) { status.setText(R.string.wait_flash); return }
        @Suppress("DEPRECATION")
        super.onBackPressed()
    }

    companion object {
        private const val ACTION_USB_PERMISSION = "io.github.skyliner2008.pabflasher.USB_PERMISSION"
        private const val REQ_FILE = 1
    }
}
