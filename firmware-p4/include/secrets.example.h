/**
 * @file secrets.example.h
 * @brief Template for Wi-Fi and API credentials
 *
 * INSTRUCTIONS:
 * 1. Copy this file to 'secrets.h' in the same directory
 * 2. Fill in your actual credentials
 * 3. NEVER commit secrets.h to version control!
 *
 * Command: copy include\secrets.example.h include\secrets.h
 */

#pragma once

// Wi-Fi Credentials
#define WIFI_SSID       "YourWiFiName"
#define WIFI_PASSWORD   "YourWiFiPassword"

// Google Gemini API Key
// Get yours at: https://aistudio.google.com/apikey
#define GEMINI_API_KEY  "your-gemini-api-key-here"

// Gemini Live API Configuration
#define GEMINI_MODEL    "models/gemini-3.8-live"
#define GEMINI_HOST     "generativelanguage.googleapis.com"
#define GEMINI_PORT     443
#define GEMINI_WS_PATH  "/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent"
