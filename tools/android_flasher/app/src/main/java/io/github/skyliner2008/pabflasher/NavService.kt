package io.github.skyliner2008.pabflasher

import android.content.ActivityNotFoundException
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.location.Geocoder
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.net.Uri
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.provider.Settings
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import java.util.Locale
import java.util.concurrent.Executors

/** The app-wide [PhoneHub] (one per process), with the Android side of it. */
object Hub {
    @Volatile private var hub: PhoneHub? = null
    /** A screen of the app is in front: Android lets it open Google Maps without the overlay permission. */
    @Volatile var uiVisible = false

    fun get(ctx: Context): PhoneHub = hub ?: synchronized(this) {
        hub ?: run {
            val app = ctx.applicationContext
            val prefs = app.getSharedPreferences("phone_link", Context.MODE_PRIVATE)
            val store = object : PhoneHub.Store {
                override fun loadToken() = prefs.getString("token", null)
                override fun saveToken(token: String) { prefs.edit().putString("token", token).apply() }
                override var paired: Boolean
                    get() = prefs.getBoolean("paired", false)
                    set(v) { prefs.edit().putBoolean("paired", v).apply() }
            }
            PhoneHub(store, MapsActions(app), BuildConfig.VERSION_NAME).also { hub = it }
        }
    }
}

/** What the board may ask of the phone: Google Maps only (navigate / stop). */
class MapsActions(private val ctx: Context) : PhoneHub.Actions {
    override fun navigate(to: String): String? {
        // Android 10+ blocks activities started from the background, unless the app may draw over others
        if (!Hub.uiVisible && !Settings.canDrawOverlays(ctx)) {
            return "มือถือยังไม่อนุญาต \"แสดงทับแอปอื่น\" ให้ ESP32-AI Flasher (แอป → แท็บนำทาง → สิทธิ์)"
        }
        val uri = Uri.parse("google.navigation:q=" + Uri.encode(to) + "&mode=d")
        val i = Intent(Intent.ACTION_VIEW, uri).setPackage(MapsParser.MAPS_PACKAGE).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        return try {
            ctx.startActivity(i)
            null
        } catch (_: ActivityNotFoundException) {
            "ไม่พบแอป Google Maps บนมือถือ"
        } catch (e: Exception) {
            "เปิด Google Maps ไม่ได้: ${e.message}"
        }
    }

    override fun stop(): String? = MapsListener.exitNavigation()
}

/**
 * Reads Google Maps' ongoing navigation notification (needs "notification access" for this app). Android 13+ hides
 * that switch for apps installed from a file until "Allow restricted settings" in the app info.
 */
class MapsListener : NotificationListenerService() {
    companion object {
        @Volatile private var instance: MapsListener? = null
        @Volatile private var navKey: String? = null

        fun enabled(ctx: Context): Boolean {
            val flat = Settings.Secure.getString(ctx.contentResolver, "enabled_notification_listeners") ?: return false
            val me = ComponentName(ctx, MapsListener::class.java)
            return flat.split(':').any { ComponentName.unflattenFromString(it) == me }
        }

        /** Press the "exit navigation" button of Maps' notification. null = done. */
        fun exitNavigation(): String? {
            val l = instance ?: return "มือถือยังไม่ได้เปิดสิทธิ์ \"อ่านการแจ้งเตือน\" ให้ ESP32-AI Flasher"
            val sbn = try { l.activeNotifications?.firstOrNull { it.key == navKey } } catch (_: Exception) { null }
                ?: return "ไม่ได้นำทางอยู่"
            val exit = sbn.notification.actions?.firstOrNull {
                Regex("ออก|หยุด|สิ้นสุด|exit|stop|end|close", RegexOption.IGNORE_CASE).containsMatchIn(it.title ?: "")
            } ?: return "ปุ่มออกจากการนำทางของ Google Maps ไม่อยู่ในการแจ้งเตือน — กด \"ออก\" ใน Maps"
            return try {
                exit.actionIntent.send()
                null
            } catch (e: PendingIntent.CanceledException) {
                "Google Maps ไม่รับคำสั่ง"
            }
        }
    }

    override fun onListenerConnected() {
        instance = this
        try { activeNotifications?.forEach { onNotificationPosted(it) } } catch (_: Exception) {}
    }

    override fun onListenerDisconnected() {
        instance = null
    }

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        if (sbn.packageName != MapsParser.MAPS_PACKAGE) return
        val n = sbn.notification
        val nav = n.category == "navigation" || sbn.isOngoing  // Notification.CATEGORY_NAVIGATION (API 28)
        if (!nav) return
        val e = n.extras
        val fields = listOf(Notification.EXTRA_TITLE, Notification.EXTRA_TEXT, Notification.EXTRA_SUB_TEXT,
            Notification.EXTRA_BIG_TEXT, Notification.EXTRA_INFO_TEXT, Notification.EXTRA_SUMMARY_TEXT)
            .map { e.getCharSequence(it)?.toString() }
        val step = MapsParser.parse(fields) ?: return
        navKey = sbn.key
        Hub.get(this).onStep(step, fields.filterNotNull().joinToString(" | "))
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification) {
        if (sbn.key != navKey) return
        navKey = null
        Hub.get(this).onStep(null, "")
    }
}

/**
 * Foreground service while "ส่งให้บอร์ด" is on: GPS once a second, the place name now and then, the Wi-Fi server
 * the board connects to, a keep-alive. Survives the screen going off (HONOR / Samsung: also allow the battery).
 */
class NavService : Service(), LocationListener {
    companion object {
        @Volatile var running = false
            private set
        private const val CHANNEL = "nav_link"

        fun start(ctx: Context) {
            ctx.startForegroundService(Intent(ctx, NavService::class.java))
        }

        fun stop(ctx: Context) = ctx.stopService(Intent(ctx, NavService::class.java))
    }

    private val main = Handler(Looper.getMainLooper())
    private val geo = Executors.newSingleThreadExecutor()
    private lateinit var hub: PhoneHub
    private var lastPlaceAt = 0L
    private var lastPlaceLoc: Location? = null

    private val tick = object : Runnable {
        override fun run() {
            hub.keepAlive()
            main.postDelayed(this, 2000)
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        hub = Hub.get(this)
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(CHANNEL, getString(R.string.nav_channel), NotificationManager.IMPORTANCE_LOW))
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        val note = Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentTitle(getString(R.string.nav_service_title))
            .setContentText(getString(R.string.nav_service_text))
            .setContentIntent(open)
            .setOngoing(true)
            .build()
        if (Build.VERSION.SDK_INT >= 29) startForeground(1, note, ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION)
        else startForeground(1, note)
        running = true
        hub.startWifi()
        try {
            getSystemService(LocationManager::class.java)
                .requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000L, 0f, this, Looper.getMainLooper())
        } catch (_: SecurityException) {
            // no location permission: the link still runs (Maps steps), the นำทาง tab shows what is missing
        }
        main.postDelayed(tick, 2000)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int) = START_STICKY

    override fun onDestroy() {
        running = false
        main.removeCallbacksAndMessages(null)
        try { getSystemService(LocationManager::class.java).removeUpdates(this) } catch (_: Exception) {}
        hub.stopWifi()
        geo.shutdownNow()
        super.onDestroy()
    }

    override fun onLocationChanged(loc: Location) {
        val kmh = if (loc.hasSpeed()) loc.speed * 3.6f else 0f
        val hdg = if (loc.hasBearing() && kmh > 3) loc.bearing else -1f
        hub.onGps(loc.latitude, loc.longitude, kmh, hdg, if (loc.hasAccuracy()) loc.accuracy else 0f)
        maybePlace(loc)
    }

    @Deprecated("Deprecated in Java")
    override fun onStatusChanged(provider: String?, status: Int, extras: android.os.Bundle?) = Unit
    override fun onProviderEnabled(provider: String) = Unit
    override fun onProviderDisabled(provider: String) = Unit

    /** "ถ.พระราม 4 ลุมพินี ปทุมวัน" for "ตอนนี้อยู่ไหน" - every minute or 300 m, from the phone's geocoder. */
    private fun maybePlace(loc: Location) {
        val now = System.currentTimeMillis()
        val moved = lastPlaceLoc?.distanceTo(loc) ?: Float.MAX_VALUE
        if (now - lastPlaceAt < 60_000 && moved < 300) return
        if (!Geocoder.isPresent()) return
        lastPlaceAt = now
        lastPlaceLoc = loc
        geo.execute {
            try {
                @Suppress("DEPRECATION")
                val a = Geocoder(this, Locale("th")).getFromLocation(loc.latitude, loc.longitude, 1)?.firstOrNull()
                    ?: return@execute
                val name = listOf(a.thoroughfare, a.subLocality, a.locality ?: a.subAdminArea, a.adminArea)
                    .filter { !it.isNullOrBlank() }.distinct().joinToString(" ")
                if (name.isNotEmpty()) main.post { hub.onPlace(name) }
            } catch (_: Exception) {
            }
        }
    }
}
