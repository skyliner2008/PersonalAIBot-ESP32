package io.github.skyliner2008.pabflasher

import android.Manifest
import android.annotation.SuppressLint
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.provider.Settings
import android.text.InputType
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView

/**
 * The "นำทาง" tab (wiki 18): the link to the board (USB / Wi-Fi), GPS and Google Maps as the app sees them,
 * start / stop sending, the permissions it needs (each with a button to its settings page), a local test.
 */
class NavPage(private val a: MainActivity, root: View) : PhoneHub.Observer {
    private val body: LinearLayout = root.findViewById(R.id.navBody)
    private val main = Handler(Looper.getMainLooper())
    private val hub = Hub.get(a)
    private val status: TextView
    private val toggle: Button
    private val perms = ArrayList<Pair<TextView, () -> Boolean>>()
    private val dest: EditText
    private val raw: TextView
    private val events: TextView
    private var refreshQueued = false

    init {
        status = card()
        toggle = button(R.string.nav_start) {
            if (NavService.running) NavService.stop(a) else {
                if (!has(Manifest.permission.ACCESS_FINE_LOCATION)) askRuntime()
                NavService.start(a)
            }
            main.postDelayed({ refresh() }, 300)
        }
        text(a.getString(R.string.nav_how), dim = true)

        section(R.string.nav_perms)
        perm(R.string.nav_perm_location, { has(Manifest.permission.ACCESS_FINE_LOCATION) }) { askRuntime() }
        if (Build.VERSION.SDK_INT >= 33) {
            perm(R.string.nav_perm_notify, { has(Manifest.permission.POST_NOTIFICATIONS) }) { askRuntime() }
        }
        perm(R.string.nav_perm_listener, { MapsListener.enabled(a) }) {
            open(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS))
        }
        perm(R.string.nav_perm_overlay, { Settings.canDrawOverlays(a) }) {
            open(Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, Uri.parse("package:" + a.packageName)))
        }
        perm(R.string.nav_perm_battery, { a.getSystemService(PowerManager::class.java).isIgnoringBatteryOptimizations(a.packageName) }) {
            @SuppressLint("BatteryLife")  // a navigation link must not be put to sleep with the screen
            val i = Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, Uri.parse("package:" + a.packageName))
            open(i)
        }
        text(a.getString(R.string.nav_perm_help), dim = true)
        button(R.string.nav_app_info) {
            open(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:" + a.packageName)))
        }

        section(R.string.nav_test)
        dest = EditText(a).apply {
            setHint(R.string.nav_test_hint)
            inputType = InputType.TYPE_CLASS_TEXT
            setSingleLine()
            importantForAutofill = View.IMPORTANT_FOR_AUTOFILL_NO
            body.addView(this, lp())
        }
        row(button(R.string.nav_test_go, add = false) {
            val to = dest.text.toString().trim()
            if (to.isNotEmpty()) MapsActions(a).navigate(to)?.let { note(it) }
        }, button(R.string.nav_test_stop, add = false) { MapsListener.exitNavigation()?.let { note(it) } })
        raw = text("", dim = true)
        section(R.string.nav_events)
        events = text("", dim = true)
        refresh()
    }

    fun onShow() {
        hub.observe(this)
        refresh()
    }

    fun onHide() = hub.unobserve(this)

    override fun onHubChanged() {
        if (refreshQueued) return
        refreshQueued = true
        main.postDelayed({ refreshQueued = false; refresh() }, 200)
    }

    private fun note(t: String) {
        raw.text = t
    }

    fun refresh() {
        val lines = ArrayList<String>()
        lines += when (hub.via) {
            "usb" -> a.getString(R.string.nav_link_usb)
            "wifi" -> a.getString(R.string.nav_link_wifi, hub.wifiBoard ?: "")
            else -> a.getString(if (NavService.running) R.string.nav_link_waiting else R.string.nav_link_off)
        }
        lines += a.getString(if (hub.paired) R.string.nav_paired else R.string.nav_not_paired)
        val fixAge = (System.currentTimeMillis() - hub.lastFixAt) / 1000
        lines += if (hub.lastFixAt > 0 && fixAge < 5) "GPS: ${hub.lastFix}" else a.getString(R.string.nav_no_fix)
        if (hub.place.isNotEmpty()) lines += hub.place
        val s = hub.step
        lines += if (s != null) {
            val d = when {
                s.distM < 0 -> ""
                s.distM < 1000 -> "${s.distM} ม. "
                else -> "%.1f กม. ".format(s.distM / 1000f)
            }
            "Google Maps: $d${s.text}" + (if (s.eta.isNotEmpty()) " (ถึง ${s.eta})" else "")
        } else a.getString(R.string.nav_maps_idle)
        status.text = lines.joinToString("\n")
        status.isActivated = hub.via != "none"
        toggle.setText(if (NavService.running) R.string.nav_stop else R.string.nav_start)
        for ((tv, ok) in perms) {
            val good = ok()
            tv.text = (if (good) "✔ " else "✖ ") + tv.tag
            tv.setTextColor(a.getColor(if (good) R.color.log_ok else R.color.log_err))
        }
        if (hub.rawMaps.isNotEmpty()) raw.text = a.getString(R.string.nav_raw, hub.rawMaps)
        events.text = hub.recentEvents().takeLast(12).joinToString("\n")
    }

    private fun has(p: String) = a.checkSelfPermission(p) == PackageManager.PERMISSION_GRANTED

    private fun askRuntime() {
        val want = mutableListOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION)
        if (Build.VERSION.SDK_INT >= 33) want += Manifest.permission.POST_NOTIFICATIONS
        a.requestPermissions(want.toTypedArray(), 7)
    }

    private fun open(i: Intent) {
        try {
            a.startActivity(i)
        } catch (_: Exception) {
            a.startActivity(Intent(Settings.ACTION_SETTINGS))
        }
    }

    // ---------------------------------------------------------------- building

    private fun lp() = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
    private fun dp(v: Int) = (v * a.resources.displayMetrics.density).toInt()

    private fun card(): TextView = TextView(a, null, 0, R.style.Card).apply {
        setBackgroundResource(R.drawable.card)
        setPadding(dp(12), dp(12), dp(12), dp(12))
        textSize = 15f
        body.addView(this, lp().apply { topMargin = dp(8) })
    }

    private fun section(title: Int) {
        val tv = TextView(a, null, 0, R.style.Section)
        tv.setText(title)
        body.addView(tv, lp().apply { topMargin = dp(20) })
    }

    private fun text(s: String, dim: Boolean): TextView = TextView(a).apply {
        text = s
        textSize = if (dim) 13f else 15f
        if (dim) setTextColor(a.getColor(R.color.muted))
        body.addView(this, lp().apply { topMargin = dp(6) })
    }

    private fun button(label: Int, add: Boolean = true, onClick: () -> Unit) = Button(a).apply {
        setText(label)
        isAllCaps = false
        setOnClickListener { onClick() }
        if (add) body.addView(this, lp().apply { topMargin = dp(8) })
    }

    private fun row(vararg buttons: Button) {
        val r = LinearLayout(a).apply { orientation = LinearLayout.HORIZONTAL }
        for (b in buttons) r.addView(b, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        body.addView(r, lp().apply { topMargin = dp(4) })
    }

    /** "✔ ตำแหน่ง (GPS)" + a button to where it is granted. */
    private fun perm(label: Int, ok: () -> Boolean, fix: () -> Unit) {
        val r = LinearLayout(a).apply { orientation = LinearLayout.HORIZONTAL; gravity = android.view.Gravity.CENTER_VERTICAL }
        val tv = TextView(a).apply { tag = a.getString(label); textSize = 14f }
        r.addView(tv, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        r.addView(Button(a).apply { setText(R.string.nav_perm_fix); isAllCaps = false; setOnClickListener { fix() } })
        body.addView(r, lp().apply { topMargin = dp(4) })
        perms += tv to ok
    }
}
