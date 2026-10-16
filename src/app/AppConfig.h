/**
 * @file AppConfig.h
 * @brief Compile-time tunables of the ESP32-AI agent
 * @see wiki/04-System-Architecture.md
 *
 * User-facing settings (Wi-Fi, API key, names, voice, personality, model, ...)
 * live in NVS and are edited from the touch menu: see src/app/Settings.h.
 * include/secrets.h only provides first-boot defaults.
 */

#pragma once

#define FIRMWARE_VERSION        "3.4.4"

// ============================================================
// Prompt labels
// ============================================================
#define AGENT_USER_LABEL        "ผู้ใช้"           // speaker labels in the recent-context prompt
#define AGENT_SELF_LABEL        "ผู้ช่วย"

// ============================================================
// Time
// ============================================================
#define TIME_TZ                 "ICT-7"           // POSIX TZ for Asia/Bangkok
#define TIME_UTC_OFFSET_SEC     (7 * 3600)
#define TIME_NTP_1              "pool.ntp.org"
#define TIME_NTP_2              "time.google.com"
#define TIME_NTP_3              "time.cloudflare.com"

// ============================================================
// Audio
// ============================================================
#define AUDIO_I2S_RATE          24000   // one clock for mic + speaker (Gemini output rate)
#define AUDIO_UPLOAD_RATE       16000   // Gemini Live input rate (mic is resampled 3:2)
#define AUDIO_MIC_CHUNK_MS      40      // size of each realtimeInput audio message
#define AUDIO_PLAYBACK_BUFFER   (1024 * 1024)   // PSRAM, ~21 s of 24 kHz speech
#define AUDIO_CAPTURE_BUFFER    (256 * 1024)    // PSRAM, ~8 s of 16 kHz mic: the agent loop can stall ~1.7 s
                                                // while the Live TLS handshake runs (wake-up = when people talk)
#define AUDIO_RADIO_BUFFER      (192 * 1024)    // PSRAM, ~4 s of 24 kHz mono radio (absorbs network jitter)
#define AUDIO_RADIO_PREBUFFER_MS 1000           // radio starts sounding once this much is queued
#define RADIO_DUCK_GAIN         0.12f           // radio level while the assistant listens / talks
#define RADIO_RESUME_MS         6000            // idle this long with the radio on -> back to sleep, radio up
#define RADIO_WAKE_FACTOR       3.0f            // while the radio plays, wake on sound this many times the expected radio echo
#define RADIO_WAKE_MIN_LEVEL    0.02f           // ... and at least this loud (mic RMS; speech ~0.05-0.12, radio leak ~0.005-0.01)
// Jitter buffer for the assistant's voice. Gemini streams 24 kHz PCM as base64 (~64 KB/s); the
// framework's lwIP has a 5.7 KB TCP window, so over a phone hotspot (RTT ~100 ms) audio can arrive
// SLOWER than it plays. The buffer starts at AUDIO_PREBUFFER_MS, doubles after a gap mid-reply,
// and after each reply is set from the measured arrival rate (what that reply would have needed
// to play without a gap); it eases back down after clean replies. Ceiling AUDIO_PREBUFFER_MAX_MS.
#define AUDIO_PREBUFFER_MS      200
#define AUDIO_PREBUFFER_MAX_MS  3000
#define AUDIO_DEFAULT_VOLUME    70      // 0-100 (ES8311 DAC)
#define AUDIO_MIC_GAIN          5       // boot value of the ES8311 ADC scale (0-7, x6 dB); NVS "micgain" overrides

// Echo control: speaker and mic are centimetres apart and there is no AEC, so
// the mic is NOT streamed while the assistant speaks (+ a short tail).
// Tap the screen (or BOOT) to stop the assistant instead of talking over it.
#define AGENT_HALF_DUPLEX       1
#define AGENT_ECHO_TAIL_MS      350

// Local voice activity detection ("hybrid VAD" as recommended for the Live API): the device
// decides when to start streaming and sends audioStreamEnd when speech ends, the server VAD
// finds the exact start inside the pre-roll. Silence is not streamed on a 65k TPM budget,
// except for a short follow-up window after each reply (users answer quickly and softly).
#define AGENT_LOCAL_VAD         1
#define AGENT_VAD_MIN_LEVEL     0.004f  // RMS 0-1; adaptive threshold never goes below this (v3.1: 0.006)
#define AGENT_VAD_NOISE_FACTOR  2.5f    // speech = level > noise floor x factor (v3.1: 3.0)
#define AGENT_VAD_NOISE_RISE_MS 10000   // above the threshold this long without a pause = background, not speech
#define AGENT_VAD_HANGOVER_MS   1500   // keep streaming this long after the last voiced chunk (Thai pauses)
#define AGENT_VAD_PREROLL_MS    600     // audio sent from before the detected onset (was 320: clipped words)
#define AGENT_VAD_ONSET_CHUNKS  2       // voiced 40-ms chunks in a row that open the stream
#define AGENT_FOLLOWUP_MS       6000    // after a reply, stream the mic this long without waiting for onset
#define AGENT_NO_REPLY_WARN_MS  8000    // log a warning if speech was sent but nothing came back
#define AGENT_NO_REPLY_SEARCH_MS 25000  // ... when it heard us and may be running Google Search (server side, often 10-20 s)
#define AGENT_WAKE_CHUNKS       4       // voiced 40-ms chunks needed to wake from sleep
#define AGENT_SERVER_VAD_TUNING 1       // realtimeInputConfig.automaticActivityDetection in the setup

// Mic AGC: quiet speech (far from the board) is lifted towards a target level before upload.
#define AGENT_AGC_TARGET        0.08f   // speech RMS aimed for (about -22 dBFS)
#define AGENT_AGC_MAX_GAIN      8.0f    // at most +18 dB
#define AGENT_MIC_RECORD_MAX_S  8       // "rec" console command: longest diagnostic recording

// ============================================================
// Gemini Live session (token budget: 65k tokens / minute)
// ============================================================
#define AGENT_CTX_TRIGGER_TOKENS  12000  // compress the context above this ...
#define AGENT_CTX_TARGET_TOKENS   5000   // ... down to this (system prompt + tools stay)
#define AGENT_EXPRESSION_HOLD_MS  6000   // set_expression lasts this long

// ============================================================
// Long-term memory (standalone, on SD card or internal flash)
// ============================================================
#define MEMORY_DIR_NAME         "agent"
#define MEMORY_MAX_RECORDS      2000
#define MEMORY_CORE_PROMPT_BYTES   1500  // core facts injected into each new session
#define MEMORY_RECENT_TURNS        6     // previous-conversation turns for a new session
#define MEMORY_RECENT_PROMPT_BYTES 1200
#define MEMORY_EPISODE_KEEP_DAYS_SD     365
#define MEMORY_EPISODE_KEEP_DAYS_FLASH  30

// Background consolidation with Gemini Flash-Lite (15 RPM / 500 RPD on this account)
#define CONSOLIDATE_MIN_TURNS        4     // new turns before a run is worth it
#define CONSOLIDATE_IDLE_MS          45000 // run only after the conversation went quiet
#define CONSOLIDATE_MIN_INTERVAL_SEC 300   // at most one call every 5 minutes
#define CONSOLIDATE_MAX_PER_DAY      150   // well below 500 RPD
