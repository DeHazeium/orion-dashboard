import { firebaseConfig, TELEMETRY_PATH } from './firebase-config.js';
import { classify, finite, measurementAge, STALE_MS } from './monitor.js';

const $ = id => document.getElementById(id);
const demo = new URLSearchParams(location.search).get('demo') === '1';
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

function render() {
  const state = classify(latest, { now: now(), connected, error });
  const fresh = latest && connected && !error && measurementAge(latest, now()) <= STALE_MS && measurementAge(latest, now()) >= -5000 && latest.schemaVersion === 1;
  const cameraOK = fresh && latest.cameraOnline === true && latest.labelsValid === true && Number.isInteger(latest.people) && latest.people >= 0;
  const powerOK = fresh && latest.pzemOnline === true;
  $('status-banner').className = `status-banner ${state.tone}`;
  setText('status-title', state.title); setText('status-detail', state.detail); setText('status-code', state.code);
  setText('status-icon', state.tone === 'good' ? '✓' : state.tone === 'warn' || state.tone === 'bad' ? '!' : '—');
  const linkText = demo ? 'Demo' : error ? 'Connection error' : !connected ? 'Connecting' : !latest ? 'Waiting for device' : fresh ? 'Live' : 'Device offline';
  $('connection').className = `badge ${demo ? 'warn' : error ? 'bad' : fresh ? 'good' : 'neutral'}`;
  $('connection').replaceChildren(Object.assign(document.createElement('i')), document.createTextNode(linkText));
  setText('power-value', powerOK ? number(latest.power) : '—');
  for (const [key, digits] of [['voltage', 1], ['current', 3], ['energy', 3], ['frequency', 1], ['pf', 2]]) setText(`${key}-value`, powerOK ? number(latest[key], digits) : '—');
  setText('threshold-label', finite(latest?.thresholdW) ? `${latest.thresholdW} W` : '—');
  const people = cameraOK ? latest.people : null;
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
  const gradient = ctx.createLinearGradient(0, top, 0, height); gradient.addColorStop(0, '#97edc536'); gradient.addColorStop(1, '#97edc500');
  // Draw separate runs so a sensor failure or a long telemetry gap is never bridged.
  const runs = []; let run = [];
  for (const point of visible) {
    if (!finite(point.p) || (run.length && point.t - run.at(-1).t > STALE_MS)) { if (run.length) runs.push(run); run = []; }
    if (finite(point.p)) run.push(point);
  }
  if (run.length) runs.push(run);
  for (const segment of runs) {
    ctx.beginPath(); ctx.moveTo(x(segment[0].t), height - bottom); for (const p of segment) ctx.lineTo(x(p.t), y(p.p)); ctx.lineTo(x(segment.at(-1).t), height - bottom); ctx.closePath(); ctx.fillStyle = gradient; ctx.fill();
    ctx.beginPath(); segment.forEach((p, i) => i ? ctx.lineTo(x(p.t), y(p.p)) : ctx.moveTo(x(p.t), y(p.p))); ctx.strokeStyle = '#97edc5'; ctx.lineWidth = 2; ctx.lineJoin = 'round'; ctx.stroke();
  }
  if (valid.length) { const p = valid.at(-1); ctx.beginPath(); ctx.arc(x(p.t), y(p.p), 4, 0, Math.PI * 2); ctx.fillStyle = '#bdffdb'; ctx.fill(); }
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

function startDemo() {
  $('demo-banner').hidden = false; $('mode-link').href = location.pathname; $('mode-link').textContent = 'Return to live ↗';
  connected = true;
  let awaySince = Date.now();
  const sample = () => {
    const scenario = $('demo-scenario').value;
    const elapsed = scenario === 'warning' ? 5000 : scenario === 'away' ? Math.min(Date.now() - awaySince, 5000) : 0;
    receive({ schemaVersion: 1, sampledAt: Date.now() - (scenario === 'offline' ? 30000 : 0), power: 77.4 + 4 * Math.sin(Date.now() / 4100), voltage: 233.1, current: 0.348, energy: 1.284, frequency: 50.0, pf: 0.96, people: scenario === 'present' ? 1 : 0, cameraOnline: scenario !== 'fault', labelsValid: scenario !== 'fault', pzemOnline: true, thresholdW: 10, warningDelayMs: 5000, unattendedMs: elapsed });
  };
  for (let i = 150; i > 0; i--) points.push({ t: Date.now() - i * 2000, p: 77 + 4 * Math.sin(i / 8) + 2 * Math.cos(i / 2) });
  $('demo-scenario').addEventListener('change', () => { awaySince = Date.now(); sample(); });
  sample(); setInterval(sample, 1000);
}

new ResizeObserver(drawChart).observe($('power-chart'));
document.fonts?.ready.then(drawChart);
if (demo) startDemo(); else { render(); connectFirebase(); }
setInterval(render, 1000);
