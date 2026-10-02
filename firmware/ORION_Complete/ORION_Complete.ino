/* ORION COMPLETE: One-file ESP32 + PZEM-004T V3 + HuskyLens 2 + Firebase.
   Edit WIFI_SSID and WIFI_PASSWORD below before uploading.
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

// Keep your edited copy with real Wi-Fi credentials private.
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

// HuskyLens 2 Object Recognition coordinates use a 640 x 480 frame.
// Start with the full frame. Narrow this rectangle after checking where
// people appear on the camera screen at your exhibition booth.
constexpr int CAMERA_WIDTH = 640;
constexpr int CAMERA_HEIGHT = 480;
constexpr int ZONE_X_MIN = 0;
constexpr int ZONE_Y_MIN = 0;
constexpr int ZONE_X_MAX = 640;
constexpr int ZONE_Y_MAX = 480;
// Google Trust Services roots R1-R4, from https://pki.goog/roots.pem
// Downloaded 2026-09-30. Used for TLS verification; these are public certificates.
static const char GOOGLE_ROOT_CA[] PROGMEM = R"PEM(
-----BEGIN CERTIFICATE-----
MIIFVzCCAz+gAwIBAgINAgPlk28xsBNJiGuiFzANBgkqhkiG9w0BAQwFADBHMQsw
CQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEU
MBIGA1UEAxMLR1RTIFJvb3QgUjEwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAw
MDAwWjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZp
Y2VzIExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjEwggIiMA0GCSqGSIb3DQEBAQUA
A4ICDwAwggIKAoICAQC2EQKLHuOhd5s73L+UPreVp0A8of2C+X0yBoJx9vaMf/vo
27xqLpeXo4xL+Sv2sfnOhB2x+cWX3u+58qPpvBKJXqeqUqv4IyfLpLGcY9vXmX7w
Cl7raKb0xlpHDU0QM+NOsROjyBhsS+z8CZDfnWQpJSMHobTSPS5g4M/SCYe7zUjw
TcLCeoiKu7rPWRnWr4+wB7CeMfGCwcDfLqZtbBkOtdh+JhpFAz2weaSUKK0Pfybl
qAj+lug8aJRT7oM6iCsVlgmy4HqMLnXWnOunVmSPlk9orj2XwoSPwLxAwAtcvfaH
szVsrBhQf4TgTM2S0yDpM7xSma8ytSmzJSq0SPly4cpk9+aCEI3oncKKiPo4Zor8
Y/kB+Xj9e1x3+naH+uzfsQ55lVe0vSbv1gHR6xYKu44LtcXFilWr06zqkUspzBmk
MiVOKvFlRNACzqrOSbTqn3yDsEB750Orp2yjj32JgfpMpf/VjsPOS+C12LOORc92
wO1AK/1TD7Cn1TsNsYqiA94xrcx36m97PtbfkSIS5r762DL8EGMUUXLeXdYWk70p
aDPvOmbsB4om3xPXV2V4J95eSRQAogB/mqghtqmxlbCluQ0WEdrHbEg8QOB+DVrN
VjzRlwW5y0vtOUucxD/SVRNuJLDWcfr0wbrM7Rv1/oFB2ACYPTrIrnqYNxgFlQID
AQABo0IwQDAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4E
FgQU5K8rJnEaK0gnhS9SZizv8IkTcT4wDQYJKoZIhvcNAQEMBQADggIBAJ+qQibb
C5u+/x6Wki4+omVKapi6Ist9wTrYggoGxval3sBOh2Z5ofmmWJyq+bXmYOfg6LEe
QkEzCzc9zolwFcq1JKjPa7XSQCGYzyI0zzvFIoTgxQ6KfF2I5DUkzps+GlQebtuy
h6f88/qBVRRiClmpIgUxPoLW7ttXNLwzldMXG+gnoot7TiYaelpkttGsN/H9oPM4
7HLwEXWdyzRSjeZ2axfG34arJ45JK3VmgRAhpuo+9K4l/3wV3s6MJT/KYnAK9y8J
ZgfIPxz88NtFMN9iiMG1D53Dn0reWVlHxYciNuaCp+0KueIHoI17eko8cdLiA6Ef
MgfdG+RCzgwARWGAtQsgWSl4vflVy2PFPEz0tv/bal8xa5meLMFrUKTX5hgUvYU/
Z6tGn6D/Qqc6f1zLXbBwHSs09dR2CQzreExZBfMzQsNhFRAbd03OIozUhfJFfbdT
6u9AWpQKXCBfTkBdYiJ23//OYb2MI3jSNwLgjt7RETeJ9r/tSQdirpLsQBqvFAnZ
0E6yove+7u7Y/9waLd64NnHi/Hm3lCXRSHNboTXns5lndcEZOitHTtNCjv0xyBZm
2tIMPNuzjsmhDYAPexZ3FL//2wmUspO8IFgV6dtxQ/PeEMMA3KgqlbbC1j+Qa3bb
bP6MvPJwNQzcmRk13NfIRmPVNnGuV/u3gm3c
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIFVzCCAz+gAwIBAgINAgPlrsWNBCUaqxElqjANBgkqhkiG9w0BAQwFADBHMQsw
CQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEU
MBIGA1UEAxMLR1RTIFJvb3QgUjIwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAw
MDAwWjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZp
Y2VzIExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjIwggIiMA0GCSqGSIb3DQEBAQUA
A4ICDwAwggIKAoICAQDO3v2m++zsFDQ8BwZabFn3GTXd98GdVarTzTukk3LvCvpt
nfbwhYBboUhSnznFt+4orO/LdmgUud+tAWyZH8QiHZ/+cnfgLFuv5AS/T3KgGjSY
6Dlo7JUle3ah5mm5hRm9iYz+re026nO8/4Piy33B0s5Ks40FnotJk9/BW9BuXvAu
MC6C/Pq8tBcKSOWIm8Wba96wyrQD8Nr0kLhlZPdcTK3ofmZemde4wj7I0BOdre7k
RXuJVfeKH2JShBKzwkCX44ofR5GmdFrS+LFjKBC4swm4VndAoiaYecb+3yXuPuWg
f9RhD1FLPD+M2uFwdNjCaKH5wQzpoeJ/u1U8dgbuak7MkogwTZq9TwtImoS1mKPV
+3PBV2HdKFZ1E66HjucMUQkQdYhMvI35ezzUIkgfKtzra7tEscszcTJGr61K8Yzo
dDqs5xoic4DSMPclQsciOzsSrZYuxsN2B6ogtzVJV+mSSeh2FnIxZyuWfoqjx5RW
Ir9qS34BIbIjMt/kmkRtWVtd9QCgHJvGeJeNkP+byKq0rxFROV7Z+2et1VsRnTKa
G73VululycslaVNVJ1zgyjbLiGH7HrfQy+4W+9OmTN6SpdTi3/UGVN4unUu0kzCq
gc7dGtxRcw1PcOnlthYhGXmy5okLdWTK1au8CcEYof/UVKGFPP0UJAOyh9OktwID
AQABo0IwQDAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4E
FgQUu//KjiOfT5nK2+JopqUVJxce2Q4wDQYJKoZIhvcNAQEMBQADggIBAB/Kzt3H
vqGf2SdMC9wXmBFqiN495nFWcrKeGk6c1SuYJF2ba3uwM4IJvd8lRuqYnrYb/oM8
0mJhwQTtzuDFycgTE1XnqGOtjHsB/ncw4c5omwX4Eu55MaBBRTUoCnGkJE+M3DyC
B19m3H0Q/gxhswWV7uGugQ+o+MePTagjAiZrHYNSVc61LwDKgEDg4XSsYPWHgJ2u
NmSRXbBoGOqKYcl3qJfEycel/FVL8/B/uWU9J2jQzGv6U53hkRrJXRqWbTKH7QMg
yALOWr7Z6v2yTcQvG99fevX4i8buMTolUVVnjWQye+mew4K6Ki3pHrTgSAai/Gev
HyICc/sgCq+dVEuhzf9gR7A/Xe8bVr2XIZYtCtFenTgCR2y59PYjJbigapordwj6
xLEokCZYCDzifqrXPW+6MYgKBesntaFJ7qBFVHvmJ2WZICGoo7z7GJa7Um8M7YNR
TOlZ4iBgxcJlkoKM8xAfDoqXvneCbT+PHV28SSe9zE8P4c52hgQjxcCMElv924Sg
JPFI/2R80L5cFtHvma3AH/vLrrw4IgYmZNralw4/KBVEqE8AyvCazM90arQ+POuV
7LXTWtiBmelDGDfrs7vRWGJB82bSj6p4lVQgw1oudCvV0b4YacCs1aTPObpRhANl
6WLAYv7YTVWW4tAR+kg0Eeye7QUd5MjWHYbL
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPluILrIPglJ209ZjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjMwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjMwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AAQfTzOHMymKoYTey8chWEGJ6ladK0uFxh1MJ7x/JlFyb+Kf1qPKzEUURout736G
jOyxfi//qXGdGIRFBEFVbivqJn+7kAHjSxm65FSWRQmx1WyRRK2EE46ajA2ADDL2
4CejQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBTB8Sa6oC2uhYHP0/EqEr24Cmf9vDAKBggqhkjOPQQDAwNpADBmAjEA9uEglRR7
VKOQFhG/hMjqb2sXnh5GmCCbn9MN2azTL818+FsuVbu/3ZL3pAzcMeGiAjEA/Jdm
ZuVDFhOD3cffL74UOO0BzrEXGhF16b0DjyZ+hOXJYKaV11RZt+cRLInUue4X
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi
QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR
HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D
9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8
p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD
-----END CERTIFICATE-----
)PEM";

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
