package com.skyliner2008.jarvis.update

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInfo
import android.content.pm.PackageInstaller
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.provider.Settings
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import com.skyliner2008.jarvis.ui.AppUpdateLogic
import com.skyliner2008.jarvis.ui.AppUpdateState
import io.github.skyliner2008.pabflasher.Net
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest
import kotlin.coroutines.cancellation.CancellationException

/**
 * The app updates itself from the latest GitHub release (T097): the version and signer come from
 * PersonalAIBot-Mobile.json, the APK is streamed to the cache with its sha256 checked, then handed to Android's
 * PackageInstaller (Android always asks the user to confirm). One per process: the settings card and the daily
 * check at start-up share it.
 */
object AppSelfUpdate {
    private const val TAG = "AppUpdate"
    private const val PREFS = "app_update"
    private const val DAY_MS = 24 * 60 * 60 * 1000L
    private const val CHANNEL = "app_update"

    private val _state = MutableStateFlow<AppUpdateState>(AppUpdateState.Idle)
    val state: StateFlow<AppUpdateState> = _state.asStateFlow()

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var job: Job? = null
    private var found: Pair<AppUpdateLogic.Release, AppUpdateLogic.Manifest>? = null

    fun currentVersion(context: Context): String = runCatching { packageInfo(context, 0).versionName }.getOrNull() ?: "?"

    fun check(context: Context) = start {
        _state.value = AppUpdateState.Checking
        _state.value = fetch(context.applicationContext)
    }

    /** Once a day at start-up: a notification when there is a version not announced yet. */
    fun autoCheck(context: Context) {
        val app = context.applicationContext
        val prefs = app.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        cleanCache(app)
        if (System.currentTimeMillis() - prefs.getLong("last_check", 0L) < DAY_MS) return
        start {
            val result = fetch(app)
            prefs.edit().putLong("last_check", System.currentTimeMillis()).apply()
            if (_state.value == AppUpdateState.Idle) _state.value = result  // nobody is looking at the card
            val manifest = found?.second
            if (result is AppUpdateState.Available && manifest != null &&
                prefs.getLong("announced", 0L) != manifest.versionCode) {
                notifyAvailable(app, result.version)
                prefs.edit().putLong("announced", manifest.versionCode).apply()
            }
        }
    }

    fun update(context: Context) {
        val app = context.applicationContext
        val (release, manifest) = found ?: return check(app)
        val apkUrl = release.apkUrl ?: return fail("Release ${release.tag} ไม่มีไฟล์ ${AppUpdateLogic.APK_NAME}")
        if (!AppUpdateLogic.sameSigner(manifest, installedSigners(app))) {
            return fail("แอปนี้เซ็นด้วยกุญแจคนละชุดกับเวอร์ชันใหม่ (build จาก PC เครื่องอื่น) Android จะไม่ยอมติดตั้งทับ — " +
                "ถอนแอปเดิมแล้วติดตั้ง ${AppUpdateLogic.APK_NAME} จาก GitHub ใหม่ (ข้อมูลในแอปจะหาย ควรสำรองก่อน)")
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && !app.packageManager.canRequestPackageInstalls()) {
            // asked before the ~200 MB download, not after it
            app.startActivity(Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:${app.packageName}"))
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            return fail("อนุญาต \"ติดตั้งแอปที่ไม่รู้จัก\" ให้แอปนี้ แล้วกดอัปเดตอีกครั้ง")
        }
        start {
            val apk = download(app, apkUrl, manifest)
            _state.value = AppUpdateState.Installing
            install(app, apk)
        }
    }

    fun cancel() {
        job?.cancel()
        _state.value = AppUpdateState.Idle
    }

    /** From [AppUpdateReceiver]: Android's installer answered (success never arrives - the app is replaced). */
    internal fun installResult(context: Context, status: Int, message: String?) {
        Log.i(TAG, "installer status $status ${message.orEmpty()}")
        when (status) {
            PackageInstaller.STATUS_PENDING_USER_ACTION -> Unit  // Android's confirm screen is up: still Installing
            PackageInstaller.STATUS_SUCCESS -> _state.value = AppUpdateState.Idle
            PackageInstaller.STATUS_FAILURE_ABORTED -> _state.value = AppUpdateState.Idle  // the user said no
            PackageInstaller.STATUS_FAILURE_CONFLICT, PackageInstaller.STATUS_FAILURE_INCOMPATIBLE ->
                fail("ติดตั้งทับแอปเดิมไม่ได้ (กุญแจเซ็นต่างกัน) — ถอนแอปเดิมแล้วติดตั้งใหม่")
            PackageInstaller.STATUS_FAILURE_STORAGE -> fail("พื้นที่ในเครื่องไม่พอสำหรับติดตั้ง")
            else -> fail("ติดตั้งไม่สำเร็จ${message?.let { ": $it" } ?: ""}")
        }
        if (status != PackageInstaller.STATUS_PENDING_USER_ACTION) cleanCache(context, force = true)
    }

    // ---- steps ----

    private fun fetch(context: Context): AppUpdateState {
        val release = AppUpdateLogic.parseRelease(Net.text(AppUpdateLogic.LATEST_URL))
        val manifestUrl = release.manifestUrl ?: run {
            Log.i(TAG, "latest ${release.tag}: no ${AppUpdateLogic.MANIFEST_NAME} (released before T097)")
            return AppUpdateState.Failed("Release ล่าสุด (${release.tag}) ยังไม่มีข้อมูลเวอร์ชันแอป — รอ release ถัดไป")
        }
        val manifest = AppUpdateLogic.parseManifest(Net.text(manifestUrl, accept = "application/octet-stream"))
            ?: return AppUpdateState.Failed("ข้อมูลเวอร์ชันแอปใน ${release.tag} อ่านไม่ได้")
        found = release to manifest
        val result = AppUpdateLogic.decide(manifest, installedCode(context), installedSigners(context))
        Log.i(TAG, "latest ${release.tag}: app ${manifest.versionName} (${manifest.versionCode}), " +
            "installed ${currentVersion(context)} (${installedCode(context)}) -> $result")
        return result
    }

    private suspend fun download(context: Context, url: String, manifest: AppUpdateLogic.Manifest): File {
        val file = apkFile(context).apply { parentFile?.mkdirs(); delete() }
        val digest = MessageDigest.getInstance("SHA-256")
        val c = URL(url).openConnection() as HttpURLConnection
        try {
            c.connectTimeout = 15_000
            c.readTimeout = 30_000
            c.instanceFollowRedirects = true
            c.setRequestProperty("Accept", "application/octet-stream")
            if (c.responseCode != 200) throw IOException("ดาวน์โหลดไม่สำเร็จ (HTTP ${c.responseCode})")
            val total = if (manifest.size > 0) manifest.size else c.contentLengthLong
            var done = 0L
            var shown = 0L
            val buf = ByteArray(256 * 1024)
            c.inputStream.use { input ->
                file.outputStream().use { out ->
                    while (true) {
                        currentCoroutineContext().ensureActive()  // cancel() stops the download here
                        val n = input.read(buf)
                        if (n < 0) break
                        out.write(buf, 0, n)
                        digest.update(buf, 0, n)
                        done += n
                        if (done - shown >= 1_000_000 || done == total) {
                            shown = done
                            _state.value = AppUpdateState.Downloading(done, total)
                        }
                    }
                }
            }
            if (manifest.size > 0 && done != manifest.size) throw IOException("ไฟล์ที่ได้ไม่ครบ (${done} / ${manifest.size} ไบต์)")
        } finally {
            c.disconnect()
        }
        val sha = digest.digest().joinToString("") { "%02x".format(it) }
        if (sha != manifest.sha256) {
            file.delete()
            throw IOException("ไฟล์ที่ดาวน์โหลดไม่ตรงกับ sha256 ใน release — ไม่ติดตั้ง")
        }
        return file
    }

    private fun install(context: Context, apk: File) {
        val installer = context.packageManager.packageInstaller
        val params = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL)
        params.setAppPackageName(context.packageName)
        val id = installer.createSession(params)
        installer.openSession(id).use { session ->
            session.openWrite("app.apk", 0, apk.length()).use { out ->
                apk.inputStream().use { it.copyTo(out, 256 * 1024) }
                session.fsync(out)
            }
            val flags = PendingIntent.FLAG_UPDATE_CURRENT or
                (if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0)
            val callback = PendingIntent.getBroadcast(
                context, id, Intent(context, AppUpdateReceiver::class.java).setPackage(context.packageName), flags)
            session.commit(callback.intentSender)
        }
    }

    // ---- helpers ----

    private fun start(block: suspend () -> Unit) {
        if (job?.isActive == true) return
        job = scope.launch {
            try {
                block()
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                Log.w(TAG, "update failed: ${e.message}", e)
                fail(Net.friendly(e))
            }
        }
    }

    private fun fail(message: String) {
        _state.value = AppUpdateState.Failed(message)
    }

    private fun apkFile(context: Context) = File(File(context.cacheDir, "app-update"), AppUpdateLogic.APK_NAME)

    /** A finished or abandoned download is ~200 MB of cache: dropped at start-up and after the installer answered. */
    private fun cleanCache(context: Context, force: Boolean = false) {
        if (!force && job?.isActive == true) return
        runCatching { apkFile(context).delete() }
    }

    private fun packageInfo(context: Context, flags: Int): PackageInfo {
        val pm = context.packageManager
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            pm.getPackageInfo(context.packageName, PackageManager.PackageInfoFlags.of(flags.toLong()))
        } else {
            @Suppress("DEPRECATION")
            pm.getPackageInfo(context.packageName, flags)
        }
    }

    private fun installedCode(context: Context): Long = runCatching {
        val info = packageInfo(context, 0)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) info.longVersionCode
        else @Suppress("DEPRECATION") info.versionCode.toLong()
    }.getOrDefault(0L)

    /** SHA-256 of this app's signing certificate(s), lowercase hex - what apksigner prints for the release APK. */
    private fun installedSigners(context: Context): Set<String> = runCatching {
        val sigs = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            val info = packageInfo(context, PackageManager.GET_SIGNING_CERTIFICATES).signingInfo ?: return emptySet()
            if (info.hasMultipleSigners()) info.apkContentsSigners else info.signingCertificateHistory
        } else {
            @Suppress("DEPRECATION")
            packageInfo(context, PackageManager.GET_SIGNATURES).signatures
        }
        sigs.orEmpty().map { s ->
            MessageDigest.getInstance("SHA-256").digest(s.toByteArray()).joinToString("") { "%02x".format(it) }
        }.toSet()
    }.getOrDefault(emptySet())

    private fun notifyAvailable(context: Context, version: String) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.getSystemService(NotificationManager::class.java).createNotificationChannel(
                NotificationChannel(CHANNEL, "อัปเดตแอป", NotificationManager.IMPORTANCE_DEFAULT))
        }
        val launch = context.packageManager.getLaunchIntentForPackage(context.packageName) ?: return
        val open = PendingIntent.getActivity(context, 0, launch, PendingIntent.FLAG_IMMUTABLE)
        val n = NotificationCompat.Builder(context, CHANNEL)
            .setSmallIcon(android.R.drawable.stat_sys_download_done)
            .setContentTitle("มีแอปเวอร์ชันใหม่ $version")
            .setContentText("ตั้งค่า → อัปเดตแอป เพื่อดาวน์โหลดและติดตั้ง")
            .setContentIntent(open)
            .setAutoCancel(true)
            .build()
        runCatching { NotificationManagerCompat.from(context).notify(CHANNEL.hashCode(), n) }  // no permission = no notice
    }
}

/** Android's installer reports here: ask the user to confirm, or tell [AppSelfUpdate] how it ended. */
class AppUpdateReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE)
        if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
            val confirm = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                intent.getParcelableExtra(Intent.EXTRA_INTENT, Intent::class.java)
            } else {
                @Suppress("DEPRECATION")
                intent.getParcelableExtra(Intent.EXTRA_INTENT)
            }
            confirm?.let { context.startActivity(it.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) }
        }
        AppSelfUpdate.installResult(context, status, intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE))
    }
}
