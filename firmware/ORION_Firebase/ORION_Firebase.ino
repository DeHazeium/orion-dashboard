/* ORION: ESP32 + PZEM-004T V3 + HuskyLens 2 + Firebase.
   Object Recognition; only the label "person" counts. Serial: 115200.
   Wi-Fi / HTTPS run in a separate task so outages do not stop local monitoring.
*/
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <PZEM004Tv30.h>
#include <DFRobot_HuskylensV2.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "config.h"
#include "google_root_ca.h"

static_assert(ZONE_X_MIN >= 0 && ZONE_X_MIN < ZONE_X_MAX && ZONE_X_MAX <= CAMERA_WIDTH, "Invalid monitored area X bounds");
static_assert(ZONE_Y_MIN >= 0 && ZONE_Y_MIN < ZONE_Y_MAX && ZONE_Y_MAX <= CAMERA_HEIGHT, "Invalid monitored area Y bounds");

struct Detection {
  char label[32];
  int16_t x, y, width, height;
  bool inZone;
};

struct Reading {
  float voltage, current, power, energy, frequency, pf;
  int people, peopleOutsideZone, detectionCount;
  Detection detections[MAX_RESULT_NUM];
  bool cameraOnline, labelsValid, pzemOnline;
  uint32_t unattendedMs;
  uint32_t capturedAtMs;
  int64_t sampledAt, lastPersonSeenAt;
};

PZEM004Tv30 pzem(Serial2, PZEM_RX_PIN, PZEM_TX_PIN);
HuskylensV2 husky;
QueueHandle_t readingsQueue = nullptr;
bool cameraReady = false;
bool timing = false;
uint32_t unattendedSince = 0;
uint32_t nextCameraAttempt = 0;
int64_t lastPersonSeenAt = 0;
const char *monitorStatus = "Starting";

int64_t epochMillis() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// A queue copies the complete reading; the uploader never accesses the sensors.
void publishReading(Reading &r) {
  r.capturedAtMs = millis();
  r.sampledAt = epochMillis();
  if (readingsQueue) xQueueOverwrite(readingsQueue, &r);
}

void uploadTask(void *parameter) {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  configTime(0, 0, "time.google.com", "pool.ntp.org");
  uint32_t lastReconnect = millis();
  bool reportedTimeWait = false;
  WiFiClientSecure secureClient;
  secureClient.setCACert(GOOGLE_ROOT_CA);
  secureClient.setHandshakeTimeout(8);
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      if (millis() - lastReconnect >= 10000) {
        Serial.println("Wi-Fi disconnected - local monitoring continues");
        WiFi.reconnect(); lastReconnect = millis();
      }
      vTaskDelay(pdMS_TO_TICKS(500)); continue;
    }
    // TLS certificate validation and sampledAt both require a synchronized clock.
    if (time(nullptr) < 1735689600) {
      if (!reportedTimeWait) { Serial.println("Wi-Fi connected - waiting for internet time"); reportedTimeWait = true; }
      vTaskDelay(pdMS_TO_TICKS(1000)); continue;
    }
    Reading r;
    if (xQueueReceive(readingsQueue, &r, pdMS_TO_TICKS(1000)) != pdTRUE) continue;
    // Never make an old measurement appear fresh after Wi-Fi reconnects.
    if (millis() - r.capturedAtMs > 8000 || r.sampledAt < 1735689600000LL) continue;
    JsonDocument doc;
    doc["schemaVersion"] = 1;
    doc["deviceId"] = "orion-01";
    doc["sampledAt"] = r.sampledAt;
    doc["receivedAt"][".sv"] = "timestamp";
    doc["cameraOnline"] = r.cameraOnline;
    doc["labelsValid"] = r.labelsValid;
    doc["pzemOnline"] = r.pzemOnline;
    doc["lastPersonSeenAt"] = r.lastPersonSeenAt > 0 ? r.lastPersonSeenAt : 0;
    doc["zone"]["frameWidth"] = CAMERA_WIDTH;
    doc["zone"]["frameHeight"] = CAMERA_HEIGHT;
    doc["zone"]["xMin"] = ZONE_X_MIN;
    doc["zone"]["yMin"] = ZONE_Y_MIN;
    doc["zone"]["xMax"] = ZONE_X_MAX;
    doc["zone"]["yMax"] = ZONE_Y_MAX;
    if (r.cameraOnline && r.labelsValid) doc["people"] = r.people;
    else doc["people"] = nullptr;
    doc["peopleOutsideZone"] = r.cameraOnline && r.labelsValid ? r.peopleOutsideZone : 0;
    JsonArray detections = doc["detections"].to<JsonArray>();
    if (r.cameraOnline && r.labelsValid) for (int i = 0; i < r.detectionCount; i++) {
      JsonObject item = detections.add<JsonObject>();
      item["label"] = r.detections[i].label;
      item["x"] = r.detections[i].x;
      item["y"] = r.detections[i].y;
      item["width"] = r.detections[i].width;
      item["height"] = r.detections[i].height;
      item["inZone"] = r.detections[i].inZone;
    }
    if (r.pzemOnline) {
      doc["voltage"] = r.voltage; doc["current"] = r.current;
      doc["power"] = r.power; doc["energy"] = r.energy;
      doc["frequency"] = r.frequency; doc["pf"] = r.pf;
    }
    doc["thresholdW"] = POWER_LIMIT_W;
    doc["warningDelayMs"] = WARNING_MS;
    doc["unattendedMs"] = r.unattendedMs;
    doc["warning"] = r.cameraOnline && r.labelsValid && r.pzemOnline && r.people == 0 && r.power > POWER_LIMIT_W && r.unattendedMs >= WARNING_MS;
    String body; serializeJson(doc, body);
    HTTPClient http;
    http.setConnectTimeout(5000); http.setTimeout(5000);
    if (http.begin(secureClient, FIREBASE_URL)) {
      http.addHeader("Content-Type", "application/json");
      int code = http.PUT(body);
      if (code != 200 && code != 204) Serial.printf("Firebase upload failed: %d (401 = check database rules)\n", code);
      http.end();
    } else Serial.println("Firebase HTTPS initialization failed");
    // Upload approximately every two seconds. Keep only the newest queued reading.
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(HUSKY_SDA_PIN, HUSKY_SCL_PIN, 100000);
  husky.retry = 1;
  readingsQueue = xQueueCreate(1, sizeof(Reading));
  if (!readingsQueue) Serial.println("Uploader unavailable: queue allocation failed");
  else if (xTaskCreate(uploadTask, "firebase", 12288, nullptr, 1, nullptr) != pdPASS) Serial.println("Uploader unavailable: task allocation failed");
  delay(2000);
  Serial.println("ORION - Person-only power monitor | 5-second warning");
}

void loop() {
  Reading r{};
  r.lastPersonSeenAt = lastPersonSeenAt;
  r.voltage = pzem.voltage(); r.current = pzem.current();
  r.power = pzem.power(); r.energy = pzem.energy();
  r.frequency = pzem.frequency(); r.pf = pzem.pf();
  r.pzemOnline = isfinite(r.voltage) && r.voltage >= 0 && isfinite(r.current) && r.current >= 0 && isfinite(r.power) && r.power >= 0 && isfinite(r.energy) && r.energy >= 0 && isfinite(r.frequency) && r.frequency >= 0 && isfinite(r.pf) && r.pf >= 0 && r.pf <= 1;

  if (!cameraReady) {
    timing = false;
    publishReading(r); // Camera is unknown while connecting/loading the model.
    if ((int32_t)(millis() - nextCameraAttempt) >= 0) {
      Serial.println("Connecting to HuskyLens 2...");
      if (husky.begin(Wire) && husky.switchAlgorithm(ALGORITHM_OBJECT_RECOGNITION)) {
        Serial.println("Loading Object Recognition - wait 5 seconds");
        delay(5000); // Required after a HuskyLens 2 algorithm switch.
        cameraReady = true;
        Serial.println("Camera ready");
      } else Serial.println("Camera handshake or algorithm selection failed");
      nextCameraAttempt = millis() + 5000;
    }
    delay(1000); return;
  }

  int objects = husky.getResult(ALGORITHM_OBJECT_RECOGNITION);
  if (objects < 0) {
    cameraReady = false; timing = false;
    nextCameraAttempt = millis() + 5000;
    Serial.println("Camera error - warning timer reset");
    publishReading(r); delay(1000); return;
  }

  r.cameraOnline = true;
  r.labelsValid = true;
  int returnedResults = 0;
  for (int i = 0; i < MAX_RESULT_NUM; i++) {
    Result *result = husky.getCachedResultByIndex(ALGORITHM_OBJECT_RECOGNITION, i);
    if (result == nullptr) continue;
    returnedResults++;
    String label = result->name; label.trim();
    if (DEBUG_LABELS) Serial.printf("ID=%u | Label='%s'\n", (unsigned)result->ID, label.c_str());
    if (label.length() == 0) r.labelsValid = false;
    else {
      Detection &d = r.detections[r.detectionCount++];
      label.toCharArray(d.label, sizeof(d.label));
      d.x = result->xCenter; d.y = result->yCenter;
      d.width = result->width; d.height = result->height;
      d.inZone = d.x >= ZONE_X_MIN && d.x <= ZONE_X_MAX && d.y >= ZONE_Y_MIN && d.y <= ZONE_Y_MAX;
      if (label.equalsIgnoreCase("person")) {
        if (d.inZone) r.people++;
        else r.peopleOutsideZone++;
      }
    }
  }
  // Incomplete / unlabeled results cannot prove the absence of a person.
  if (objects > returnedResults) r.labelsValid = false;
  if (r.people > 0) r.labelsValid = true; // A known person is sufficient to clear the warning.
  if (r.people > 0 && epochMillis() >= 1735689600000LL) {
    lastPersonSeenAt = epochMillis();
    r.lastPersonSeenAt = lastPersonSeenAt;
  }

  if (!r.labelsValid) { timing = false; monitorStatus = "Missing object labels - warning paused"; }
  else if (!r.pzemOnline) { timing = false; monitorStatus = "PZEM read failed - warning paused"; }
  else if (r.people > 0) { timing = false; monitorStatus = "Person detected - warning cleared"; }
  else if (r.power <= POWER_LIMIT_W) { timing = false; monitorStatus = "Below power threshold"; }
  else {
    if (!timing) { timing = true; unattendedSince = millis(); }
    // Saturate once the warning is reached; elapsed remains safe through millis rollover.
    uint32_t elapsed = millis() - unattendedSince;
    r.unattendedMs = elapsed >= WARNING_MS ? WARNING_MS : elapsed;
    monitorStatus = r.unattendedMs >= WARNING_MS ? "WARNING: Possible unattended power use!" : "Checking for a person...";
  }
  publishReading(r);
  Serial.printf("Power: %.1f W | People: %d | %s\n", r.power, r.people, monitorStatus);
  delay(1000);
}
