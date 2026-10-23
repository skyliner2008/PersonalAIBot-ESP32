package io.github.skyliner2008.pabflasher

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.hardware.usb.UsbDevice
import android.net.Uri
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
import java.util.concurrent.Executors

/**
 * The "แฟลช" page: firmware from the latest GitHub release (sha256 + the OTA signature checked) or a .bin on the
 * phone, written over USB by [FlashJob] (wiki 17). The console link lets go of the port for the flash.
 */
class FlashPage(private val a: MainActivity, root: View) {
    private val boardStatus: TextView = root.findViewById(R.id.boardStatus)
    private val releaseInfo: TextView = root.findViewById(R.id.releaseInfo)
    private val modeGroup: RadioGroup = root.findViewById(R.id.mode)
    private val fileInfo: TextView = root.findViewById(R.id.fileInfo)
    private val flashButton: Button = root.findViewById(R.id.flashButton)
    private val progress: ProgressBar = root.findViewById(R.id.progress)
    private val status: TextView = root.findViewById(R.id.status)
    private val log: TextView = root.findViewById(R.id.log)

    private val worker = Executors.newSingleThreadExecutor()
    @Volatile var busy = false
        private set
    @Volatile private var release: Release? = null
    private var localImage: ByteArray? = null
    private var localName = ""
    private var lastMode = R.id.modeUpdate
    private var lastPermille = -1

    init {
        root.findViewById<RadioButton>(R.id.modeFile).setOnClickListener { pickFile() }
        modeGroup.setOnCheckedChangeListener { _, id ->
            if (id != R.id.modeFile) lastMode = id
            fileInfo.visibility = if (id == R.id.modeFile && localImage != null) View.VISIBLE else View.GONE
        }
        releaseInfo.setOnClickListener { checkRelease() }
        flashButton.setOnClickListener { confirmAndFlash() }
        checkRelease()
    }

    fun shutdown() = worker.shutdownNow()

    fun warnBusy() = status.setText(R.string.wait_flash)

    fun showBoard(board: UsbDevice?, other: UsbDevice?) {
        boardStatus.text = when {
            board != null -> a.getString(R.string.board_found, board.vendorId, board.productId)
            other != null -> a.getString(R.string.board_other, other.vendorId, other.productId)
            else -> a.getString(R.string.board_none)
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
                a.getString(R.string.release_latest, r.tag)
            } catch (e: Exception) {
                a.getString(R.string.release_failed, Net.friendly(e))
            }
            a.runOnUiThread { releaseInfo.text = text }
        }
    }

    private fun pickFile() {
        val i = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
        @Suppress("DEPRECATION")
        a.startActivityForResult(i, REQ_FILE)
    }

    fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        if (requestCode != REQ_FILE) return
        val uri = data?.data
        if (resultCode != Activity.RESULT_OK || uri == null) {
            if (localImage == null) modeGroup.check(lastMode)
            return
        }
        status.setText(R.string.file_reading)
        worker.execute {  // a cloud file can take a while to arrive
            try {
                val bytes = readUri(uri)
                val kind = FlashPlan.detect(bytes) ?: throw IOException(a.getString(R.string.file_not_firmware))
                FlashPlan.problem(kind, bytes)?.let { throw IOException(it) }
                val name = displayName(uri)
                a.runOnUiThread {
                    localImage = bytes
                    localName = name
                    fileInfo.text = a.getString(if (kind == Kind.FACTORY) R.string.file_factory else R.string.file_app,
                        name, bytes.size)
                    fileInfo.visibility = View.VISIBLE
                    status.text = ""
                }
            } catch (e: Exception) {
                a.runOnUiThread {
                    localImage = null
                    fileInfo.visibility = View.GONE
                    modeGroup.check(lastMode)
                    showError(Net.friendly(e))
                }
            }
        }
    }

    private fun readUri(uri: Uri): ByteArray {
        val input = a.contentResolver.openInputStream(uri) ?: throw IOException("เปิดไฟล์ไม่ได้")
        return input.use {
            val b = it.readBytes()
            if (b.size > 32 * 1024 * 1024) throw IOException("ไฟล์ใหญ่เกินไป")
            b
        }
    }

    private fun displayName(uri: Uri): String =
        a.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) c.getString(0) else null
        } ?: uri.lastPathSegment ?: "firmware.bin"

    private fun confirmAndFlash() {
        if (busy) return
        val mode = modeGroup.checkedRadioButtonId
        val factory = mode == R.id.modeFactory ||
            (mode == R.id.modeFile && localImage?.let { FlashPlan.detect(it) } == Kind.FACTORY)
        if (mode == R.id.modeFile && localImage == null) { pickFile(); return }
        if (!factory) { startFlash(mode); return }
        AlertDialog.Builder(a)
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
                        (FlashPlan.detect(b) ?: throw IOException(a.getString(R.string.file_not_firmware))) to b
                    }
                    R.id.modeFactory -> Kind.FACTORY to download(Kind.FACTORY)
                    else -> Kind.APP to download(Kind.APP)
                }
                a.suspendLink()
                FlashJob(a::openBoard, object : FlashJob.Listener {
                    override fun status(text: String) = note(text)
                    override fun progress(done: Long, total: Long) = showProgress(done, total)
                }).run(kind, image)
                note(a.getString(if (kind == Kind.FACTORY) R.string.done_factory else R.string.done_app))
                a.runOnUiThread { status.setText(R.string.done) }
            } catch (e: Exception) {
                a.runOnUiThread { showError(Net.friendly(e)) }
            } finally {
                busy = false
                a.resumeLink()
                a.runOnUiThread { setBusy(false); a.refreshBoard() }
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
        val cached = File(a.cacheDir, name)
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

    private fun note(text: String) = a.runOnUiThread {
        status.text = text
        val lines = (log.text.toString() + text + "\n").lines().takeLast(40)
        log.text = lines.joinToString("\n")
    }

    private fun showProgress(done: Long, total: Long) {
        val permille = if (total <= 0) 0 else (done * 1000 / total).toInt()
        if (permille == lastPermille) return
        lastPermille = permille
        a.runOnUiThread { progress.progress = permille }
    }

    private fun showError(msg: String) {
        status.text = a.getString(R.string.error_prefix, msg)
        log.append("✖ $msg\n")
    }

    private fun setBusy(on: Boolean) {
        flashButton.isEnabled = !on
        for (i in 0 until modeGroup.childCount) modeGroup.getChildAt(i).isEnabled = !on
        progress.visibility = View.VISIBLE
        if (on) {
            progress.progress = 0
            a.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        } else {
            a.window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
        a.onFlashBusyChanged()
    }

    companion object {
        private const val REQ_FILE = 1
    }
}
