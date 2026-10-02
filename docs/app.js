import { firebaseConfig, TELEMETRY_PATH } from './firebase-config.js';
import { classify, finite, measurementAge, STALE_MS } from './monitor.js';
import { previewTemperature } from './ac-preview.js';

const $ = id => document.getElementById(id);
if (location.search) history.replaceState(null, '', location.pathname + location.hash);
let latest = null, connected = false, error = '', serverOffset = 0, lastState = '';
let points = [], activity = [], lastSample = null;
const time = value => new Date(value).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
const number = (value, digits = 1) => finite(value) && value >= 0 ? value.toLocaleString(undefined, { minimumFractionDigits: digits, maximumFractionDigits: digits }) : '—';
const setText = (id, value) => { $(id).textContent = value; };
const now = () => Date.now() + serverOffset;

$('today').textContent = new Date().toLocaleDateString(undefined, { weekday: 'short', day: 'numeric', month: 'long', year: 'numeric' });
$('setup-button').addEventListener('click', () => $('setup-dialog').showModal());
$('setup-dialog').addEventListener('click', event => { if (event.target === $('setup-dialog')) { const r = event.target.getBoundingClientRect(); if (event.clientX < r.left || event.clientX > r.right || event.clientY < r.top || event.clientY > r.bottom) event.target.close(); } });

function recordActivity(state) {
  if (state.key === lastState) return;
  lastState = state.key;
  activity.unshift({ text: state.title, tone: state.tone, at: Date.now() });
  activity = activity.slice(0, 30);
  $('activity-list').replaceChildren(...activity.map(item => {
    const li = document.createElement('li');
    const dot = document.createElement('span'); dot.className = `activity-dot ${item.tone}`;
    const text = document.createElement('span'); text.textContent = item.text;
    const stamp = document.createElement('time'); stamp.textContent = time(item.at); stamp.dateTime = new Date(item.at).toISOString();
    li.append(dot, text, stamp); return li;
  }));
}

function health(id, text, tone = 'neutral') { setText(id, text); $(id).className = `health ${tone}`; }

function renderVision(fresh, cameraOK) {
  const zone = latest?.zone;
  const validZone = zone && ['frameWidth', 'frameHeight', 'xMin', 'yMin', 'xMax', 'yMax'].every(key => Number.isInteger(zone[key]))
    && zone.frameWidth > 0 && zone.frameHeight > 0 && zone.xMin >= 0 && zone.yMin >= 0
    && zone.xMin < zone.xMax && zone.xMax <= zone.frameWidth && zone.yMin < zone.yMax && zone.yMax <= zone.frameHeight;
  const hasDetectionData = Array.isArray(latest?.detections);
  const usable = fresh && cameraOK && validZone && hasDetectionData;
  const fullFrame = validZone && zone.xMin === 0 && zone.yMin === 0 && zone.xMax === zone.frameWidth && zone.yMax === zone.frameHeight;
  setText('zone-mode', validZone ? fullFrame ? 'Full frame' : 'Custom area' : 'Awaiting setup');
  setText('zone-frame-size', validZone ? `${zone.frameWidth} × ${zone.frameHeight}` : 'Camera frame');
  $('zone-inner').hidden = !validZone;
  if (validZone) {
    const inner = $('zone-inner');
    inner.style.left = `${zone.xMin / zone.frameWidth * 100}%`;
    inner.style.top = `${zone.yMin / zone.frameHeight * 100}%`;
    inner.style.right = `${(1 - zone.xMax / zone.frameWidth) * 100}%`;
    inner.style.bottom = `${(1 - zone.yMax / zone.frameHeight) * 100}%`;
  }
  const detections = usable ? latest.detections.slice(0, 10).filter(item => item && typeof item.label === 'string' && item.label.trim()
    && finite(item.x) && finite(item.y) && item.x >= 0 && item.x <= zone.frameWidth && item.y >= 0 && item.y <= zone.frameHeight) : [];
  const markers = detections.map(item => {
    const marker = document.createElement('span');
    const person = item.label.trim().toLowerCase() === 'person';
    const inside = item.x >= zone.xMin && item.x <= zone.xMax && item.y >= zone.yMin && item.y <= zone.yMax;
    marker.className = `detection-marker ${person ? inside ? 'person' : 'outside' : 'object'}`;
    marker.style.left = `${item.x / zone.frameWidth * 100}%`;
    marker.style.top = `${item.y / zone.frameHeight * 100}%`;
    marker.title = `${item.label.trim()} · ${person ? inside ? 'inside monitored area' : 'outside monitored area' : 'visible object'}`;
    marker.setAttribute('aria-label', marker.title);
    return marker;
  });
  $('detection-markers').replaceChildren(...markers);
  const outsideCount = usable && Number.isInteger(latest.peopleOutsideZone) ? latest.peopleOutsideZone : 0;
  setText('zone-detail', !fresh ? 'Waiting for a fresh device reading.' : !cameraOK ? 'Camera unavailable. Position and occupancy are unknown.' : !usable ? 'Upload the updated firmware to show object positions and the monitored area.' : `${latest.people} ${latest.people === 1 ? 'person' : 'people'} inside · ${outsideCount} outside · ${detections.length} total detections`);

  const grouped = new Map();
  for (const item of detections) {
    const name = item.label.trim().slice(0, 31);
    const key = name.toLowerCase();
    grouped.set(key, { name, count: (grouped.get(key)?.count || 0) + 1 });
  }
  setText('object-total', usable ? `${detections.length} detected` : '—');
  setText('quick-objects', usable ? String(detections.length) : '—');
  setText('quick-camera', usable ? 'in camera view' : cameraOK ? 'Labels unavailable' : 'Camera unavailable');
  if (!usable || grouped.size === 0) {
    const message = document.createElement('p');
    message.className = 'muted';
    message.textContent = !fresh ? 'Waiting for live camera labels.' : !cameraOK ? 'Camera unavailable. Object labels are unknown.' : !usable ? 'Upload the updated firmware to see detected labels.' : 'No objects recognized in the current view.';
    $('objects-list').replaceChildren(message);
  } else {
    $('objects-list').replaceChildren(...Array.from(grouped.values(), ({ name, count }) => {
      const chip = document.createElement('span'); chip.className = 'object-chip';
      chip.append(document.createTextNode(name));
      if (count > 1) { const quantity = document.createElement('b'); quantity.textContent = `× ${count}`; chip.append(quantity); }
      return chip;
    }));
  }

  const seenAt = latest?.lastPersonSeenAt;
  const validSeen = finite(seenAt) && seenAt > 1735689600000 && seenAt <= now() + 5000;
  if (validSeen) {
    const age = Math.max(0, now() - seenAt);
    const value = age < 5000 ? 'Just now' : age < 60000 ? `${Math.floor(age / 1000)} seconds ago` : age < 3600000 ? `${Math.floor(age / 60000)} min ago` : age < 86400000 ? `${Math.floor(age / 3600000)} hr ago` : new Date(seenAt).toLocaleDateString();
    setText('last-seen-value', value);
    setText('last-seen-detail', `${time(seenAt)} · Last recorded in the monitored area${fresh ? '' : ' (device data is stale)'}.`);
  } else {
    setText('last-seen-value', 'Not recorded');
    setText('last-seen-detail', fresh ? 'No person recorded in this area since the device started.' : 'Waiting for a fresh device reading.');
  }

  const cameraState = !fresh ? latest ? 'Device offline' : 'Waiting for device' : latest.cameraOnline !== true ? 'Camera unavailable' : latest.labelsValid !== true ? 'Labels unavailable' : 'Camera online';
  setText('camera-status-value', cameraState);
  setText('camera-status-detail', cameraState === 'Camera online' ? `Object Recognition active · Last reading ${time(latest.sampledAt)}.` : cameraState === 'Labels unavailable' ? 'Warning paused until object labels are valid.' : cameraState === 'Camera unavailable' ? 'Warning paused while HuskyLens reconnects.' : 'Vision decisions are paused until fresh data arrives.');
  $('camera-status-icon').className = `insight-icon ${cameraState === 'Camera online' ? '' : cameraState === 'Waiting for device' || cameraState === 'Device offline' ? 'neutral' : 'bad'}`;
}

function render() {
  const state = classify(latest, { now: now(), connected, error });
  const fresh = latest && connected && !error && measurementAge(latest, now()) <= STALE_MS && measurementAge(latest, now()) >= -5000 && latest.schemaVersion === 1;
  const cameraOK = fresh && latest.cameraOnline === true && latest.labelsValid === true && Number.isInteger(latest.people) && latest.people >= 0;
  const powerOK = fresh && latest.pzemOnline === true;
  $('status-banner').className = `status-banner ${state.tone}`;
  setText('status-title', state.title); setText('status-detail', state.detail); setText('status-code', state.code);
  $('status-symbol').setAttribute('href', `./icons.svg#${state.tone === 'good' ? 'check' : state.tone === 'warn' || state.tone === 'bad' ? 'alert' : 'clock'}`);
  const linkText = error ? 'Connection error' : !connected ? 'Connecting' : !latest ? 'Waiting for device' : fresh ? 'Live' : 'Device offline';
  $('connection').className = `badge ${error ? 'bad' : fresh ? 'good' : 'neutral'}`;
  $('connection').replaceChildren(Object.assign(document.createElement('i')), document.createTextNode(linkText));
  setText('power-value', powerOK ? number(latest.power) : '—');
  setText('quick-power', powerOK ? number(latest.power, 0) : '—');
  for (const [key, digits] of [['voltage', 1], ['current', 3], ['energy', 3], ['frequency', 1], ['pf', 2]]) setText(`${key}-value`, powerOK ? number(latest[key], digits) : '—');
  setText('threshold-label', finite(latest?.thresholdW) ? `${latest.thresholdW} W` : '—');
  const people = cameraOK ? latest.people : null;
  setText('quick-people', people ?? '—');
  setText('quick-people-unit', people === 1 ? 'person' : 'people');
  const temperature = previewTemperature(people);
  setText('ac-temperature', temperature ?? '—');
  setText('ac-unit', typeof temperature === 'number' ? '°C' : '');
  setText('ac-caption', people === null ? 'Waiting for valid AI count' : people === 0 ? 'No people · standby preview' : `${people} ${people === 1 ? 'person' : 'people'} · suggested setting`);
  setText('person-count', people ?? '—');
  $('presence-visual').className = `presence-visual ${people === null ? 'unknown' : people > 0 ? 'present' : 'absent'}`;
  setText('presence-title', people === null ? 'Presence unknown' : people > 0 ? `${people} ${people === 1 ? 'person' : 'people'} in view` : 'No person in view');
  setText('presence-detail', people === null ? 'Waiting for a valid camera reading.' : people > 0 ? 'Person detected. Warning cleared.' : 'Other objects are ignored.');
  const timerActive = state.key === 'countdown' || state.key === 'warning';
  const delayMs = finite(latest?.warningDelayMs) && latest.warningDelayMs > 0 ? latest.warningDelayMs : 5000;
  const elapsed = timerActive ? Math.min(latest.unattendedMs, delayMs) : 0;
  setText('timer-label', `${fresh && ['countdown','warning','present','idle'].includes(state.key) ? number(elapsed / 1000, 0) : '—'} / ${delayMs / 1000}s`);
  $('timer-fill').style.width = `${elapsed / delayMs * 100}%`;
  $('timer-track').setAttribute('aria-valuemax', delayMs / 1000); $('timer-track').setAttribute('aria-valuenow', elapsed / 1000);
  setText('timer-note', state.key === 'warning' ? 'Check the space and any appliances left on.' : state.key === 'countdown' ? 'Timer resets as soon as a person is detected.' : state.key === 'present' ? 'Person present. No unattended warning.' : state.key === 'idle' ? 'Power is below the warning threshold.' : 'Warning paused until readings are valid.');
  health('esp-health', fresh ? 'Online' : latest ? 'Offline' : 'Waiting', fresh ? 'good' : 'neutral');
  health('pzem-health', !fresh ? 'Unknown' : latest.pzemOnline === true ? 'Connected' : 'Read error', !fresh ? 'neutral' : latest.pzemOnline === true ? 'good' : 'bad');
  health('camera-health', !fresh ? 'Unknown' : cameraOK ? 'Connected' : latest.cameraOnline === true ? 'Label error' : 'Unavailable', !fresh ? 'neutral' : cameraOK ? 'good' : 'bad');
  setText('last-update', finite(latest?.sampledAt) ? time(latest.sampledAt) : 'Not received');
  renderVision(fresh, cameraOK);
  recordActivity(state);
  drawChart();
}

function receive(data) {
  latest = data;
  if (data && data.sampledAt !== lastSample) {
    lastSample = data.sampledAt;
    const age = measurementAge(data, now());
    if (finite(data.sampledAt) && age >= -5000 && age <= STALE_MS) points.push({ t: data.sampledAt, p: data.pzemOnline === true && finite(data.power) && data.power >= 0 ? data.power : null });
    points = points.filter(p => p.t >= now() - 300000).slice(-300);
  }
  render();
}

function drawChart() {
  const canvas = $('power-chart'), rect = canvas.getBoundingClientRect();
  if (!rect.width || !rect.height) return;
  const ratio = window.devicePixelRatio || 1;
  canvas.width = Math.round(rect.width * ratio); canvas.height = Math.round(rect.height * ratio);
  const ctx = canvas.getContext('2d'); ctx.scale(ratio, ratio);
  const width = rect.width, height = rect.height, left = 4, right = 37, top = 12, bottom = 9;
  const end = now(), start = end - 300000;
  const visible = points.filter(p => p.t >= start && p.t <= end + 5000);
  const valid = visible.filter(p => finite(p.p));
  const max = Math.ceil(Math.max(20, ...valid.map(p => p.p)) / 20) * 20;
  const x = t => left + Math.max(0, Math.min(1, (t - start) / 300000)) * (width - left - right);
  const y = p => height - bottom - p / max * (height - top - bottom);
  ctx.font = '10px "DM Sans", sans-serif'; ctx.textAlign = 'right'; ctx.fillStyle = '#93a2ae'; ctx.lineWidth = 1;
  for (let n = 0; n <= 4; n++) { const value = max * n / 4, yp = y(value); ctx.strokeStyle = '#26333b'; ctx.setLineDash([3, 5]); ctx.beginPath(); ctx.moveTo(left, yp); ctx.lineTo(width - right, yp); ctx.stroke(); ctx.fillText(`${Math.round(value)}`, width - 1, yp + 3); }
  ctx.setLineDash([]);
  const gradient = ctx.createLinearGradient(0, top, 0, height); gradient.addColorStop(0, '#d9ee5b36'); gradient.addColorStop(1, '#d9ee5b00');
  // Draw separate runs so a sensor failure or a long telemetry gap is never bridged.
  const runs = []; let run = [];
  for (const point of visible) {
    if (!finite(point.p) || (run.length && point.t - run.at(-1).t > STALE_MS)) { if (run.length) runs.push(run); run = []; }
    if (finite(point.p)) run.push(point);
  }
  if (run.length) runs.push(run);
  for (const segment of runs) {
    ctx.beginPath(); ctx.moveTo(x(segment[0].t), height - bottom); for (const p of segment) ctx.lineTo(x(p.t), y(p.p)); ctx.lineTo(x(segment.at(-1).t), height - bottom); ctx.closePath(); ctx.fillStyle = gradient; ctx.fill();
    ctx.beginPath(); segment.forEach((p, i) => i ? ctx.lineTo(x(p.t), y(p.p)) : ctx.moveTo(x(p.t), y(p.p))); ctx.strokeStyle = '#d9ee5b'; ctx.lineWidth = 2; ctx.lineJoin = 'round'; ctx.stroke();
  }
  if (valid.length) { const p = valid.at(-1); ctx.beginPath(); ctx.arc(x(p.t), y(p.p), 4, 0, Math.PI * 2); ctx.fillStyle = '#e9ff96'; ctx.fill(); }
  $('chart-empty').hidden = valid.length > 0;
  canvas.setAttribute('aria-label', valid.length ? `Power chart, ${valid.length} readings in the last five minutes; latest ${number(valid.at(-1).p)} watts.` : 'No power readings received in the last five minutes.');
  setText('chart-start', new Date(start).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }));
}

async function connectFirebase() {
  try {
    const [{ initializeApp }, { getDatabase, ref, onValue }] = await Promise.all([
      import('https://www.gstatic.com/firebasejs/12.19.0/firebase-app.js'),
      import('https://www.gstatic.com/firebasejs/12.19.0/firebase-database.js')
    ]);
    const database = getDatabase(initializeApp(firebaseConfig));
    onValue(ref(database, '.info/serverTimeOffset'), snapshot => { serverOffset = snapshot.val() || 0; });
    onValue(ref(database, '.info/connected'), snapshot => { connected = snapshot.val() === true; render(); });
    onValue(ref(database, TELEMETRY_PATH), snapshot => { error = ''; receive(snapshot.val()); }, failure => {
      error = String(failure.code).toLowerCase().includes('permission') ? 'Read access denied. Check your Firebase rules and their expiry date.' : 'Could not read the device data. Check your internet connection and Firebase settings.';
      render();
    });
  } catch {
    error = 'Could not load Firebase. Check your internet connection, then reload this page.'; render();
  }
}

new ResizeObserver(drawChart).observe($('power-chart'));
document.fonts?.ready.then(drawChart);
render(); connectFirebase();
setInterval(render, 1000);
