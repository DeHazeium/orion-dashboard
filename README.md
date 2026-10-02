# ORION — Energy with awareness

Public repository: [DeHazeium/orion-dashboard](https://github.com/DeHazeium/orion-dashboard).

A responsive dashboard for your ESP32, PZEM-004T V3 and HuskyLens 2. It displays electrical readings, a five-second unattended-power warning, the monitored camera area, live recognized-object labels and positions, the last person seen, and camera health. Its new smart-home overview uses glass cards over original AI-generated pendant-light room art, inspired by [Imran Jakir's smart-home UI](https://dribbble.com/shots/27225846-Home-Automation-App-UI-Modern-Smart-Living-Experience). The art is decorative, not a HuskyLens camera image; there is no lighting control.

The **Air conditioner** card is a display-only concept. Based on a valid current people count inside the monitored area, it previews **OFF for 0**, **24°C for 1**, **22°C for 2**, **20°C for 3**, and **16°C for 4 or more**. If camera data is missing or stale, it shows an unknown setting. The dashboard does not send an AC command, connect to an AC, or write this preview to Firebase.

The dashboard is preconfigured for **orionai-552ed**. Firebase receives readings at **`/orion/latest`**. Only labels equal to `person` (case insensitive, with whitespace trimmed) whose **bounding-box centers are inside the configured monitored area** count as people. Other objects and people outside that area do not clear the warning. Their labels can still appear in the live vision panel.

## 1. Upload the ESP32 firmware

1. In Arduino IDE, install **esp32 by Espressif Systems**. Choose **ESP32 Dev Module** for your existing board.
2. Install **PZEM004Tv30 by Jakub Mandula**, **DFRobot_HuskylensV2**, and **ArduinoJson 7**. Use the HuskyLens **V2** library, not the original HuskyLens library. If a sensor library is absent from Library Manager, download its ZIP from the official repository linked below and use **Sketch → Include Library → Add .ZIP Library**.
3. Open `firmware/ORION_Firebase/ORION_Firebase.ino`.
4. In that same folder, copy `config.example.h` to `config.h`. Enter your Wi-Fi name and password in `config.h`. Leave the Firebase URL as supplied. If you already have a private `config.h` from the previous version, add the new `CAMERA_WIDTH`, `CAMERA_HEIGHT` and `ZONE_*` constants from `config.example.h` to it.
5. Upload, then open Serial Monitor at **115200 baud**. Internet access is required for Wi-Fi, clock synchronization and Firebase uploads. Use a 2.4 GHz Wi-Fi network supported by your ESP32.

Keep your working sensor wiring. The defaults are:

| Signal | ESP32 pin |
| --- | --- |
| PZEM TX → ESP32 RX | GPIO 16 |
| PZEM RX ← ESP32 TX | GPIO 17 |
| HuskyLens 2 SDA | GPIO 21 |
| HuskyLens 2 SCL | GPIO 22 |
| Logic ground | Common GND |

Continue powering HuskyLens 2 through its USB-C port and use its **I2C protocol**. The sketch selects **Object Recognition** and waits five seconds for the model to load. It does not use LCD, a relay, face recognition or camera image uploads. Follow the PZEM module's documented power and logic-level requirements; the table above covers signal pins only.

Defaults in `config.h`:

```cpp
constexpr float POWER_LIMIT_W = 10.0f;
constexpr uint32_t WARNING_MS = 5000;
```

The warning requires valid camera and PZEM readings, power **above 10 W**, and no detected person **inside the monitored area** for **five seconds**. A person inside the area, low power or sensor error resets the timer. The default area covers the whole 640 × 480 camera coordinate frame. To exclude a doorway or corridor, adjust `ZONE_X_MIN`, `ZONE_Y_MIN`, `ZONE_X_MAX`, and `ZONE_Y_MAX` in `config.h`, then flash again. Compare the dashboard position map against the camera screen and test with a person at every edge of your intended area. A missing detection cannot prove an entire room is empty.

`lastPersonSeenAt` is the time a person was last detected **inside** the area since the current ESP32 boot and after its clock synchronized. Object labels and coordinates come from the camera; PZEM measures only **total** power. ORION does not predict which object is switched on or attribute watts to a particular appliance. No camera frames or photos are uploaded to Firebase.

If the camera shows a person but the count stays at zero, set `DEBUG_LABELS = true` and inspect its exact `Label='...'` output in Serial Monitor. Do not assume that a learned object ID is the person class. Empty or incomplete labels pause the timer.

Wi-Fi and HTTPS uploads run in a separate ESP32 task; local monitoring continues through network outages. Uploads contain the original NTP measurement time so delayed uploads cannot make stale readings appear live. TLS certificates are verified using the supplied public Google root certificates. The device uploads about every two seconds, so the dashboard may show a warning a few seconds after the local five-second timer fires.

## 2. Firebase

The dashboard already contains your Firebase web configuration in `docs/firebase-config.js`. Web configuration is public; database rules control access.

Your supplied test rules are included as valid JSON in `database.rules.json` (without trailing commas). If they are already published in Firebase, no rule change is needed for this prototype. Nothing in this package changes your rules automatically.

**These rules allow anyone to read and overwrite your database until 30 October 2026 at 00:00 Malaysia time (29 October at 16:00 UTC).** The new telemetry includes object labels, positions and the last-person timestamp; these are publicly readable under those rules. Then both readings and uploads will be denied. Use authenticated, restricted access before permanent public use; do not merely extend public rules for production. A public reader is not prevented from writing by the fact that this dashboard itself only reads.

The ESP32 creates `/orion/latest` with its first successful upload. Do not manually add sample readings to make the live view look connected. A missing node shows “Waiting for your ESP32.” If uploads stop for more than 15 seconds, numbers clear and presence becomes unknown.

## 3. Publish with GitHub Pages

1. Create a GitHub repository, for example **orion-dashboard**.
2. Upload the contents of this project, retaining the **docs** folder. The repository should contain `docs/index.html`, not an extra enclosing `orion-dashboard` folder.
3. In the repository, open **Settings → Pages**.
4. Choose **Deploy from a branch**, then **main** and **/docs**. Save.
5. Open the Pages URL GitHub provides, normally `https://YOUR-USERNAME.github.io/orion-dashboard/`.

No npm install, build command, Firebase Hosting or GitHub Actions workflow is needed. All asset paths are relative so a GitHub project subpath works.

**Never upload `config.h` containing Wi-Fi credentials.** `.gitignore` protects normal Git operations, but GitHub's drag-and-drop uploader does not use your local ignore rules. The ZIP contains only `config.example.h` with placeholders. Only `docs` is published as the website.

## Preview and behavior

Use the site's **Explore demo** link (or append `?demo=1`) to preview person, crowded room, absent, warning, camera-error and offline states. The amber banner identifies simulated readings. Demo mode never initializes Firebase or writes to the database. **Return to live** exits it.

For local use, serve the `docs` directory over HTTP using any static server; ES modules should not be opened with `file://`. For example, with Python installed:

```sh
python -m http.server 8000 --directory docs
```

Open `http://localhost:8000`. The live view requires internet access to load Firebase's official SDK and connect to the database. Fonts have local fallbacks.

The chart displays the last **five minutes collected in this browser session**. Activity stores up to 30 status transitions in memory. Both clear when the page reloads. This version does not store historical readings in Firebase, send push notifications or switch appliance power. PZEM energy is the module's cumulative reading, not a calculated daily total.

## Telemetry contract

The firmware replaces only `/orion/latest`; it does not overwrite the database root. The browser subscribes to that node using Firebase `onValue`.

```json
{
  "schemaVersion": 1,
  "deviceId": "orion-01",
  "sampledAt": 1790760000000,
  "receivedAt": 1790760000100,
  "voltage": 233.1,
  "current": 0.348,
  "power": 77.4,
  "energy": 1.284,
  "frequency": 50.0,
  "pf": 0.96,
  "people": 1,
  "peopleOutsideZone": 0,
  "cameraOnline": true,
  "labelsValid": true,
  "pzemOnline": true,
  "lastPersonSeenAt": 1790760000000,
  "zone": { "frameWidth": 640, "frameHeight": 480, "xMin": 0, "yMin": 0, "xMax": 640, "yMax": 480 },
  "detections": [
    { "label": "person", "x": 315, "y": 231, "width": 92, "height": 270, "inZone": true },
    { "label": "TV", "x": 490, "y": 150, "width": 105, "height": 72, "inZone": true }
  ],
  "thresholdW": 10,
  "warningDelayMs": 5000,
  "unattendedMs": 0,
  "warning": false
}
```

The JSON above documents the schema; its values are illustrative, not live measurements. `sampledAt` is epoch milliseconds captured on the ESP32. `receivedAt` is a Firebase server timestamp. On faults, invalid metrics are omitted, detections are cleared, and unknown people are null (Firebase removes null-valued fields). Health flags distinguish these from valid zero readings. The browser checks freshness and health before displaying a warning, and never advances the warning timer without a new device reading. At most `MAX_RESULT_NUM` detections are transmitted in one reading. Older firmware remains compatible with basic power and presence cards, but the new vision cards show an upgrade prompt until the new fields arrive.

## Verification

Checked on 2 October 2026:

- Desktop (1440px) and phone (390px) rendering, with no horizontal overflow or browser script errors.
- All six demo scenarios, the five-second transition, the setup dialog and 17 status edge cases. The vision checks cover inside/outside detections, visible-object labels, camera health, last-person display and the display-only AC preview.
- Firmware compiled successfully for `esp32:esp32:esp32` with ESP32 core **3.3.3**, ArduinoJson **7.4.2**, PZEM library **1.2.1** and DFRobot_HuskylensV2 **1.0.9**. Flash: **1,073,127 bytes (81%)**; global RAM: **50,224 bytes (15%)**. The compile used placeholder Wi-Fi credentials.

The firmware has not been uploaded to your board or tested with your physical sensors. The live Firebase connection could not be verified from this test environment; no sample readings were written to the database. GitHub Pages uses the `/docs` publishing method above.

## References

- [Firebase web SDK from the official CDN](https://firebase.google.com/docs/web/alt-setup)
- [Firebase REST writes and server timestamps](https://firebase.google.com/docs/database/rest/save-data)
- [GitHub Pages branch and folder publishing](https://docs.github.com/en/pages/getting-started-with-github-pages/configuring-a-publishing-source-for-your-github-pages-site)
- [DFRobot HuskyLens V2 library](https://github.com/DFRobot/DFRobot_HuskylensV2)
- [PZEM-004T V3 library](https://github.com/mandulaj/PZEM-004T-v30)
- [Google Trust Services certificates](https://pki.goog/repository/)
