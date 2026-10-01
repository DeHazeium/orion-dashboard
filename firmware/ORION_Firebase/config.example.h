#pragma once

// Copy this file to config.h and edit only that private copy.
// config.h is excluded by .gitignore. Do not upload it using GitHub's web uploader.
const char WIFI_SSID[] = "YOUR_WIFI_NAME";
const char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
const char FIREBASE_URL[] = "https://orionai-552ed-default-rtdb.asia-southeast1.firebasedatabase.app/orion/latest.json?print=silent";

constexpr int PZEM_RX_PIN = 16; // ESP32 RX <- PZEM TX
constexpr int PZEM_TX_PIN = 17; // ESP32 TX -> PZEM RX
constexpr int HUSKY_SDA_PIN = 21;
constexpr int HUSKY_SCL_PIN = 22;
constexpr float POWER_LIMIT_W = 10.0f;
constexpr uint32_t WARNING_MS = 5000;
constexpr bool DEBUG_LABELS = false;
