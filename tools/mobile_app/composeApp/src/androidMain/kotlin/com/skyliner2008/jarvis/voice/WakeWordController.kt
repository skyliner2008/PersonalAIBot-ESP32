package com.skyliner2008.jarvis.voice

import android.content.Context
import android.content.Intent
import android.os.Handler
import android.os.Looper
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * The phone's wake word and the Live session take turns on the mic (wiki 17, T091):
 *  - nothing holds the mic (no Live session, no meeting / translation) -> listen for "hey jarvis", screen on or off;
 *  - "Jarvis" -> a Live session opens (in the background if the screen is off); the listener has stopped;
 *  - the session holds the mic: no listening, the mic goes to Gemini as always;
 *  - no conversation for [WakeWordConfig.sleepAfterMinutes] -> the session closes itself -> listen again.
 * The mic in the background is allowed by JarvisService (foreground service, type microphone, started with the app).
 * Logcat tag WakeWord.
 */
object WakeWordController : WakeGate.LiveListener {
    private const val TAG = "WakeWord"
    private const val CHECK_EVERY_MS = 5_000L

    private lateinit var app: Context
    private val main = Handler(Looper.getMainLooper())
    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())
    private val owners = mutableSetOf<String>()  // who holds the mic now (main thread)
    private var installed = false
    private val startLater = Runnable { startListener() }

    fun install(context: Context) {
        if (installed) return
        installed = true
        app = context.applicationContext
        WakeGate.liveListener = this
        scope.launch {
            while (isActive) {
                delay(CHECK_EVERY_MS)
                main.post { check() }
            }
        }
    }

    // ---- the mic changes hands (called by VoiceController / SpecialistSessionController, main thread)

    override fun liveStarting(owner: String) {
        runOnMain {
            owners += owner
            main.removeCallbacks(startLater)
            if (WakeWordService.isListening) {
                WakeWordService.stopListening()
                Log.i(TAG, "$owner takes the mic - wake word stops listening")
            }
        }
    }

    override fun liveStopped(owner: String) {
        runOnMain {
            owners -= owner
            if (owners.isEmpty()) {
                main.removeCallbacks(startLater)
                main.postDelayed(startLater, 1_000)  // the old AudioRecord lets go first
            }
        }
    }

    private fun runOnMain(block: () -> Unit) {
        if (Looper.myLooper() == Looper.getMainLooper()) block() else main.post(block)
    }

    // ---- every 5 s: close a quiet session, make sure the listener runs when nothing holds the mic

    private fun check() {
        val cfg = WakeGate.config
        val control = WakeGate.sessionControl
        val live = control?.live == true
        if (!cfg.enabled) {
            if (WakeWordService.isListening) {
                WakeWordService.stopListening()
                Log.i(TAG, "wake word off in Settings - not listening")
            }
            return
        }
        if (live) {
            owners += "live"
            if (WakeGate.idleExpired(System.currentTimeMillis())) {
                Log.i(TAG, "no conversation for ${cfg.sleepAfterMinutes} min - closing the Live session")
                control?.sleep()  // -> liveStopped("live") -> the listener comes back
            }
        } else {
            owners -= "live"  // a session that ended without telling us (an error)
            if (owners.isEmpty() && !WakeWordService.isListening) startListener()
        }
    }

    private fun startListener() {
        if (!WakeGate.config.enabled || owners.isNotEmpty() || WakeWordService.isListening) return
        if (WakeGate.sessionControl?.live == true) return
        val ok = WakeWordService.startListening(app) { main.post { onHeard() } }
        Log.i(TAG, if (ok) "no Live session - listening for \"hey jarvis\"" else "listener could not start (models / mic) - retry in 5 s")
    }

    private fun onHeard() {
        val control = WakeGate.sessionControl
        if (control?.live == true) return
        runCatching { com.skyliner2008.jarvis.sound.RobotSoundPlayer.playChirpStart() }  // "I heard you"
        if (control != null) {
            Log.i(TAG, "wake word - opening a Live session")
            control.startFromWake()
            // Always Live in the background: its screen comes up as before
            val al = com.skyliner2008.jarvis.service.AlwaysLiveManager.getInstanceOrNull()
            if (al?.state?.value == com.skyliner2008.jarvis.service.AlwaysLiveManager.AlwaysLiveState.BACKGROUND_LISTEN) {
                al.onHotwordDetected()
            }
            return
        }
        Log.i(TAG, "wake word - no app screen behind it any more: opening the app")
        runCatching {
            app.startActivity(Intent(app, com.skyliner2008.jarvis.MainActivity::class.java).apply {
                flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_REORDER_TO_FRONT
                action = "com.skyliner2008.jarvis.HOTWORD_WAKE"
            })
        }
        // the listener stopped itself on the hit; with no session it comes back on the next check
    }
}
