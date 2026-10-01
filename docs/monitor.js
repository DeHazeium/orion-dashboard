export const STALE_MS = 15000;
export const finite = value => typeof value === 'number' && Number.isFinite(value);
export function measurementAge(data, now = Date.now()) {
  // sampledAt is the ESP32's NTP clock, not the time Firebase received an old upload.
  return finite(data?.sampledAt) ? now - data.sampledAt : Infinity;
}
export function classify(data, { now = Date.now(), connected = true, error = '' } = {}) {
  const state = (key, tone, title, detail, code) => ({ key, tone, title, detail, code });
  if (error) return state('error', 'bad', 'Firebase connection unavailable', error, 'CHECK CONNECTION');
  if (!connected) return state('disconnected', 'neutral', 'Connecting to Firebase', 'Live updates are paused while the connection is restored.', 'RECONNECTING');
  if (!data) return state('waiting', 'neutral', 'Waiting for your ESP32', 'Upload the included firmware to start sending your readings.', 'STANDBY');
  const age = measurementAge(data, now);
  if (!finite(data.sampledAt) || age < -5000) return state('invalid', 'bad', 'Check device data', 'The measurement timestamp is missing or the device clock is incorrect.', 'DATA ERROR');
  if (age > STALE_MS) return state('offline', 'neutral', 'Device is offline', 'No fresh measurement for 15 seconds. Presence and warning status are unknown.', 'NO SIGNAL');
  if (data.schemaVersion !== 1) return state('invalid', 'bad', 'Check device data', 'Upload the matching ORION Firebase firmware.', 'DATA ERROR');
  if (data.cameraOnline !== true) return state('camera', 'bad', 'Camera unavailable', 'Person detection is unknown. The unattended timer is paused.', 'CAMERA ERROR');
  if (data.labelsValid !== true) return state('labels', 'bad', 'Object labels unavailable', 'Check HuskyLens object recognition. The unattended timer is paused.', 'VISION ERROR');
  if (!Number.isInteger(data.people) || data.people < 0) return state('invalid', 'bad', 'Check person count', 'A valid camera reading is required before the warning can run.', 'DATA ERROR');
  if (data.pzemOnline !== true || !finite(data.power) || data.power < 0) return state('pzem', 'bad', 'Power sensor unavailable', 'Check the PZEM connection. The unattended timer is paused.', 'SENSOR ERROR');
  if (!finite(data.thresholdW) || data.thresholdW < 0 || !finite(data.warningDelayMs) || data.warningDelayMs <= 0 || !finite(data.unattendedMs) || data.unattendedMs < 0) return state('invalid', 'bad', 'Check warning configuration', 'The device needs valid threshold and timer values.', 'DATA ERROR');
  if (data.people > 0) return state('present', 'good', 'Someone’s here. All in view.', 'A person is detected. The unattended warning is cleared.', 'PERSON PRESENT');
  if (data.power <= data.thresholdW) return state('idle', 'good', 'Low power. No warning.', `Consumption is at or below the ${data.thresholdW} W threshold.`, 'LOW POWER');
  if (data.unattendedMs >= data.warningDelayMs) return state('warning', 'warn', 'Possible unattended power use', `Power is above ${data.thresholdW} W with no person detected for ${data.warningDelayMs / 1000} seconds.`, 'ATTENTION');
  return state('countdown', 'warn', 'Power is on. No person in view.', `Checking for ${data.warningDelayMs / 1000} seconds before raising a warning.`, 'OBSERVING');
}
