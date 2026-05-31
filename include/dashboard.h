#ifndef DASHBOARD_H
#define DASHBOARD_H

// Dashboard HTML/CSS/JS stored in flash (PROGMEM)
// Uses Tailwind CDN + vanilla JS WebSocket — no build step needed

static const char DASHBOARD_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="th">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32 Dashboard</title>
<script src="https://cdn.tailwindcss.com"></script>
<style>
  body {
    background: linear-gradient(135deg, #e0f2fe 0%, #f0fdf4 50%, #fef9c3 100%);
    font-family: 'Segoe UI', sans-serif;
    min-height: 100vh;
  }
  .card {
    background: #ffffff;
    border: 1px solid #e2e8f0;
    border-radius: 16px;
    box-shadow: 0 2px 12px rgba(0,0,0,0.07);
  }
  .card-header {
    font-size: 0.85rem;
    font-weight: 700;
    color: #64748b;
    letter-spacing: 0.08em;
    text-transform: uppercase;
    margin-bottom: 1rem;
    display: flex;
    align-items: center;
    gap: 0.5rem;
  }
  .relay-on  { background: #f0fdf4; border: 1.5px solid #86efac; }
  .relay-off { background: #f8fafc; border: 1.5px solid #e2e8f0; }
  .btn-on  { background: #16a34a; color: #fff; }
  .btn-on:hover  { background: #15803d; }
  .btn-off { background: #f1f5f9; color: #475569; border: 1px solid #cbd5e1; }
  .btn-off:hover { background: #e2e8f0; }
  .val-box {
    background: #f8fafc;
    border: 1px solid #e2e8f0;
    border-radius: 12px;
    padding: 0.75rem;
    text-align: center;
  }
  .info-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 0.5rem 0;
    border-bottom: 1px solid #f1f5f9;
  }
  .info-row:last-child { border-bottom: none; }
  .info-label { color: #94a3b8; font-size: 0.82rem; }
  .info-value { color: #1e293b; font-family: monospace; font-size: 0.85rem; font-weight: 600; }
  .badge-live { animation: pulse 2s infinite; }
  @keyframes pulse { 0%,100%{opacity:1} 50%{opacity:.35} }
  .aqi-1{color:#16a34a} .aqi-2{color:#65a30d} .aqi-3{color:#ca8a04}
  .aqi-4{color:#ea580c} .aqi-5{color:#dc2626}
  .gauge-track { background:#e2e8f0; border-radius:999px; height:6px; overflow:hidden; }
  .gauge-bar   { height:6px; border-radius:999px; transition:width 0.6s ease; }
  .mqtt-topic-row { display:flex; align-items:center; gap:0.5rem; flex-wrap:wrap; }
  .mqtt-dir { font-size:0.65rem; font-weight:700; padding:1px 6px; border-radius:4px; flex-shrink:0; }
  .mqtt-dir.pub { background:#dbeafe; color:#1d4ed8; }
  .mqtt-dir.sub { background:#dcfce7; color:#15803d; }
  .mqtt-topic { font-size:0.75rem; background:#f1f5f9; color:#334155;
                padding:2px 8px; border-radius:6px; border:1px solid #e2e8f0; word-break:break-all; }
  .mqtt-desc { font-size:0.72rem; color:#94a3b8; }
  /* Schedule UI */
  .sched-card { border: 1.5px solid #e2e8f0; border-radius:12px; padding:1rem; transition:border-color 0.2s; }
  .sched-card.sched-active { border-color: #86efac; background:#f0fdf4; }
  .time-input { border:1px solid #cbd5e1; border-radius:8px; padding:4px 8px;
                font-family:monospace; font-size:1.1rem; width:72px; text-align:center;
                background:#f8fafc; color:#1e293b; }
  .time-input:focus { outline:none; border-color:#6366f1; box-shadow:0 0 0 2px #e0e7ff; }
  .day-btn { width:32px; height:32px; border-radius:8px; border:1.5px solid #cbd5e1;
             font-size:0.72rem; font-weight:700; cursor:pointer; transition:all 0.15s;
             background:#f8fafc; color:#64748b; }
  .day-btn.selected { background:#6366f1; color:#fff; border-color:#6366f1; }
  .save-btn { background:#6366f1; color:#fff; border-radius:8px; padding:6px 16px;
              font-size:0.82rem; font-weight:700; cursor:pointer; transition:background 0.15s; }
  .save-btn:hover { background:#4f46e5; }
  .toggle-sched { position:relative; display:inline-block; width:44px; height:24px; }
  .toggle-sched input { opacity:0; width:0; height:0; }
  .toggle-sched .slider {
    position:absolute; cursor:pointer; inset:0;
    background:#cbd5e1; border-radius:24px; transition:.2s;
  }
  .toggle-sched .slider:before {
    content:""; position:absolute; height:18px; width:18px;
    left:3px; bottom:3px; background:#fff; border-radius:50%; transition:.2s;
  }
  .toggle-sched input:checked + .slider { background:#16a34a; }
  .toggle-sched input:checked + .slider:before { transform:translateX(20px); }
  /* NTP clock */
  .ntp-clock { font-size:2.8rem; font-weight:800; font-family:monospace;
               color:#1e293b; letter-spacing:2px; }
  .ntp-date  { font-size:0.9rem; color:#64748b; margin-top:2px; }
</style>
</head>
<body class="p-4 md:p-6">

<!-- ═══ HEADER ═══ -->
<div class="flex flex-wrap items-center justify-between mb-6 gap-3">
  <div>
    <h1 class="text-2xl font-bold text-slate-800 tracking-tight">
      ⚙️ ESP32 Dashboard
    </h1>
    <p class="text-sky-600 text-sm mt-0.5 font-mono" id="ip-label">กำลังเชื่อมต่อ...</p>
  </div>
  <div class="flex items-center gap-2 bg-white rounded-full px-4 py-2 shadow-sm border border-slate-200">
    <span class="badge-live w-2.5 h-2.5 rounded-full bg-emerald-400" id="live-dot"></span>
    <span class="text-sm font-semibold text-slate-600" id="live-label">Connecting</span>
    <span class="text-xs text-slate-400 ml-1" id="last-update"></span>
  </div>
</div>

<!-- ═══ GRID ═══ -->
<div class="grid grid-cols-1 md:grid-cols-2 xl:grid-cols-3 gap-5">

  <!-- ── NTP CLOCK ── -->
  <div class="card p-5">
    <div class="card-header">🕐 เวลาจริง (NTP)
      <span class="ml-auto" id="ntp-status-badge"></span>
    </div>
    <div class="flex flex-col items-center py-3 gap-1">
      <div class="ntp-clock" id="ntp-time">--:--:--</div>
      <div class="ntp-date" id="ntp-date">----/--/--</div>
      <div class="text-xs text-slate-400 mt-1">Asia/Bangkok (ICT UTC+7) · th.pool.ntp.org</div>
    </div>
  </div>

  <!-- ── RELAY CONTROL ── -->
  <div class="card p-5">
    <div class="card-header">⚡ Relay Control</div>
    <div class="space-y-3" id="relay-container">
      <!-- injected by JS -->
    </div>
  </div>

  <!-- ── DS18B20 ── -->
  <div class="card p-5">
    <div class="card-header">
      🌡 อุณหภูมิ DS18B20
      <span class="ml-auto" id="ds18-sim-badge"></span>
    </div>
    <div class="flex flex-col items-center justify-center py-4 gap-2">
      <div class="text-7xl font-bold text-rose-500 tracking-tight" id="ds18-temp">--</div>
      <div class="text-slate-400 text-sm">°C · GPIO 14</div>
      <div class="w-full mt-3">
        <div class="flex justify-between text-xs text-slate-400 mb-1.5">
          <span>ช่วงที่วัดได้</span><span>-55°C → 125°C</span>
        </div>
        <div class="gauge-track">
          <div class="gauge-bar bg-rose-400" id="ds18-bar" style="width:0%"></div>
        </div>
      </div>
    </div>
  </div>

  <!-- ── XYMD SENSOR ── -->
  <div class="card p-5">
    <div class="card-header">
      🌡 XY-MD03 (ID:2)
      <span class="ml-auto" id="xymd-sim-badge"></span>
    </div>
    <div class="grid grid-cols-2 gap-3">
      <div class="val-box">
        <div class="text-4xl font-bold text-teal-500" id="xymd-temp">--</div>
        <div class="text-xs text-slate-400 mt-1">อุณหภูมิ °C</div>
        <div class="gauge-track mt-2">
          <div class="gauge-bar bg-teal-400" id="xymd-temp-bar" style="width:0%"></div>
        </div>
      </div>
      <div class="val-box">
        <div class="text-4xl font-bold text-cyan-500" id="xymd-hum">--</div>
        <div class="text-xs text-slate-400 mt-1">ความชื้น %</div>
        <div class="gauge-track mt-2">
          <div class="gauge-bar bg-cyan-400" id="xymd-hum-bar" style="width:0%"></div>
        </div>
      </div>
      <div class="col-span-2 text-xs text-slate-400 text-center">
        Modbus RTU · Serial0 · 9600 8N1
      </div>
    </div>
  </div>

  <!-- ── OPEN WEATHER ── -->
  <div class="card p-5">
    <div class="card-header">
      🌤 สภาพอากาศ
      <span class="ml-auto normal-case font-normal text-slate-400 text-xs">Nakhon Si Thammarat</span>
    </div>
    <div class="grid grid-cols-2 gap-3">
      <div class="val-box">
        <div class="text-3xl font-bold text-orange-500" id="w-temp">--</div>
        <div class="text-xs text-slate-400 mt-1">อุณหภูมิ °C</div>
      </div>
      <div class="val-box">
        <div class="text-3xl font-bold text-sky-500" id="w-hum">--</div>
        <div class="text-xs text-slate-400 mt-1">ความชื้น %</div>
      </div>
      <div class="col-span-2 val-box">
        <div class="flex justify-between text-sm mb-2">
          <span class="text-slate-500 font-medium">🌧 โอกาสฝนตก</span>
          <span class="text-sky-600 font-bold" id="w-rain-pct">--%</span>
        </div>
        <div class="gauge-track">
          <div class="gauge-bar bg-sky-400" id="w-rain-bar" style="width:0%"></div>
        </div>
      </div>
      <div class="val-box">
        <div class="text-2xl font-bold" id="w-aqi-label">--</div>
        <div class="text-xs text-slate-400 mt-1">AQI · <span id="w-aqi-num">-</span></div>
      </div>
      <div class="val-box">
        <div class="text-2xl font-bold text-amber-500" id="w-pm25">--</div>
        <div class="text-xs text-slate-400 mt-1">PM2.5 µg/m³</div>
      </div>
    </div>
  </div>

  <!-- ── WIFI & SYSTEM ── -->
  <div class="card p-5">
    <div class="card-header">📶 WiFi &amp; Network</div>
    <div class="space-y-0.5">
      <div class="info-row">
        <span class="info-label">SSID</span>
        <span class="info-value text-slate-700" id="wifi-ssid">--</span>
      </div>
      <div class="info-row">
        <span class="info-label">IP Address</span>
        <span class="info-value text-emerald-600" id="wifi-ip">--</span>
      </div>
      <div class="info-row">
        <span class="info-label">RSSI</span>
        <span class="info-value" id="wifi-rssi-text">--</span>
      </div>
      <div class="info-row">
        <span class="info-label">MAC Address</span>
        <span class="info-value text-slate-500 text-xs" id="wifi-mac">--</span>
      </div>
      <div class="info-row">
        <span class="info-label">Free Heap</span>
        <span class="info-value text-violet-600" id="sys-heap">-- KB</span>
      </div>
      <div class="info-row">
        <span class="info-label">Uptime</span>
        <span class="info-value text-slate-600" id="sys-uptime">--</span>
      </div>
    </div>
    <div class="mt-4">
      <div class="flex justify-between text-xs text-slate-400 mb-1.5">
        <span>Signal Strength</span>
        <span id="wifi-rssi-val">-- dBm</span>
      </div>
      <div class="gauge-track">
        <div class="gauge-bar" id="wifi-signal-bar" style="width:0%"></div>
      </div>
    </div>
  </div>

</div><!-- end main grid -->

<!-- ═══ RELAY SCHEDULE (full width) ═══ -->
<div class="card p-5 mt-5">
  <div class="card-header">⏰ ตั้งเวลา Relay (Schedule)
    <span class="ml-auto normal-case font-normal text-slate-400 text-xs">
      บันทึกลง Flash — ทำงานอัตโนมัติตามเวลา NTP
    </span>
  </div>
  <div class="grid grid-cols-1 md:grid-cols-3 gap-4" id="schedule-container">
    <!-- injected by JS -->
  </div>
</div>

<!-- ═══ TELEGRAM SETTINGS (full width) ═══ -->
<div class="card p-5 mt-5" id="tg-panel">
  <div class="card-header">
    ✈️ Telegram Alerts
    <span class="ml-2" id="tg-status-badge"></span>
    <span class="ml-auto flex gap-2">
      <button onclick="tgSendTest()"
        class="normal-case font-semibold text-xs bg-sky-100 text-sky-700 hover:bg-sky-200
               px-3 py-1 rounded-full transition-colors">
        🔔 ทดสอบส่ง
      </button>
      <button onclick="tgSendStatus()"
        class="normal-case font-semibold text-xs bg-violet-100 text-violet-700 hover:bg-violet-200
               px-3 py-1 rounded-full transition-colors">
        📊 รายงานสถานะ
      </button>
    </span>
  </div>

  <div class="grid grid-cols-1 md:grid-cols-2 xl:grid-cols-3 gap-5">

    <!-- ── Alert Toggles ── -->
    <div>
      <p class="text-xs font-bold text-slate-500 uppercase tracking-wide mb-3">
        🔔 ประเภทการแจ้งเตือน
      </p>
      <div class="space-y-2" id="tg-alert-toggles">
        <!-- injected by JS -->
      </div>
    </div>

    <!-- ── Threshold Settings ── -->
    <div>
      <p class="text-xs font-bold text-slate-500 uppercase tracking-wide mb-3">
        ⚙️ ค่า Threshold
      </p>
      <div class="space-y-3">
        <div class="flex items-center justify-between gap-3">
          <label class="text-sm text-slate-600 flex-1">🔥 อุณหภูมิสูง (°C)</label>
          <input id="tg-temp-high" type="number" step="0.5" min="20" max="80"
            class="time-input w-20" oninput="tgMarkDirty()">
        </div>
        <div class="flex items-center justify-between gap-3">
          <label class="text-sm text-slate-600 flex-1">🧊 อุณหภูมิต่ำ (°C)</label>
          <input id="tg-temp-low" type="number" step="0.5" min="-20" max="30"
            class="time-input w-20" oninput="tgMarkDirty()">
        </div>
        <div class="flex items-center justify-between gap-3">
          <label class="text-sm text-slate-600 flex-1">🌧 โอกาสฝน (%)</label>
          <input id="tg-rain-limit" type="number" step="5" min="10" max="100"
            class="time-input w-20" oninput="tgMarkDirty()">
        </div>
        <div class="flex items-center justify-between gap-3">
          <label class="text-sm text-slate-600 flex-1">🌫 AQI Level (1-5)</label>
          <input id="tg-aqi-level" type="number" step="1" min="1" max="5"
            class="time-input w-20" oninput="tgMarkDirty()">
        </div>
      </div>
    </div>

    <!-- ── Status / Save ── -->
    <div class="flex flex-col justify-between gap-4">
      <div>
        <p class="text-xs font-bold text-slate-500 uppercase tracking-wide mb-3">
          📡 สถานะ
        </p>
        <div class="space-y-0.5">
          <div class="info-row">
            <span class="info-label">Queue รอส่ง</span>
            <span class="info-value text-violet-600" id="tg-queue">0 ข้อ</span>
          </div>
          <div class="info-row">
            <span class="info-label">Alert Mask</span>
            <span class="info-value font-mono text-xs text-slate-500" id="tg-mask">--</span>
          </div>
          <div class="info-row">
            <span class="info-label">Cooldown</span>
            <span class="info-value text-slate-500">5 นาที / alert type</span>
          </div>
        </div>
      </div>
      <div class="space-y-2">
        <button id="tg-save-btn" onclick="tgSaveConfig()"
          class="save-btn w-full">
          💾 บันทึกการตั้งค่า
        </button>
        <div class="text-xs text-center h-4" id="tg-save-msg"></div>
      </div>
    </div>

  </div>
</div>

<!-- ═══ MQTT PANEL (full width) ═══ -->
<div class="card p-5 mt-5">
  <div class="card-header">
    🔗 MQTT Broker
    <span class="ml-2" id="mqtt-status-badge"></span>
    <span class="ml-auto normal-case font-normal text-slate-400 text-xs" id="mqtt-host-label">--</span>
  </div>
  <div class="grid grid-cols-1 md:grid-cols-2 gap-4">
    <div>
      <p class="text-xs font-bold text-slate-500 uppercase tracking-wide mb-2">📤 Publish Topics</p>
      <div class="space-y-1.5" id="mqtt-pub-topics">
        <div class="mqtt-topic-row">
          <span class="mqtt-dir pub">PUB</span>
          <code class="mqtt-topic" id="t-telemetry">--</code>
          <span class="mqtt-desc">ข้อมูลทั้งหมด (ทุก 5s)</span>
        </div>
        <div class="mqtt-topic-row">
          <span class="mqtt-dir pub">PUB</span>
          <code class="mqtt-topic" id="t-status">--</code>
          <span class="mqtt-desc">online / offline (LWT)</span>
        </div>
        <div class="mqtt-topic-row">
          <span class="mqtt-dir pub">PUB</span>
          <code class="mqtt-topic" id="t-r1-state">--</code>
          <span class="mqtt-desc">Relay 1 state</span>
        </div>
        <div class="mqtt-topic-row">
          <span class="mqtt-dir pub">PUB</span>
          <code class="mqtt-topic" id="t-r2-state">--</code>
          <span class="mqtt-desc">Relay 2 state</span>
        </div>
        <div class="mqtt-topic-row">
          <span class="mqtt-dir pub">PUB</span>
          <code class="mqtt-topic" id="t-r3-state">--</code>
          <span class="mqtt-desc">Relay 3 state</span>
        </div>
      </div>
    </div>
    <div>
      <p class="text-xs font-bold text-slate-500 uppercase tracking-wide mb-2">📥 Subscribe Topics (Commands)</p>
      <div class="space-y-1.5">
        <div class="mqtt-topic-row">
          <span class="mqtt-dir sub">SUB</span>
          <code class="mqtt-topic" id="t-r1-set">--</code>
          <span class="mqtt-desc">ON / OFF / TOGGLE</span>
        </div>
        <div class="mqtt-topic-row">
          <span class="mqtt-dir sub">SUB</span>
          <code class="mqtt-topic" id="t-r2-set">--</code>
          <span class="mqtt-desc">ON / OFF / TOGGLE</span>
        </div>
        <div class="mqtt-topic-row">
          <span class="mqtt-dir sub">SUB</span>
          <code class="mqtt-topic" id="t-r3-set">--</code>
          <span class="mqtt-desc">ON / OFF / TOGGLE</span>
        </div>
        <div class="mt-3 p-3 bg-slate-50 rounded-lg border border-slate-200 text-xs text-slate-500 font-mono">
          <span class="font-bold text-slate-600">ตัวอย่าง:</span><br>
          mosquitto_pub -h broker.hivemq.com \<br>
          &nbsp;&nbsp;-t <span id="t-r1-set-ex" class="text-violet-600">--</span> -m ON
        </div>
      </div>
    </div>
  </div>
</div>

<p class="text-center text-slate-400 text-xs mt-6">
  ESP32 Local Dashboard · Real-time via WebSocket · NTP Schedule
</p>

<script>
// ─── WebSocket ───────────────────────────────────────────────────
const wsUrl = `ws://${location.hostname}/ws`;
let ws, reconnectTimer;

// ─── Schedule state — แยกออกจาก WebSocket data ──────────────────
// localSched คือ state ที่ user กำลังแก้ไขอยู่บน form
// serverSched คือค่าล่าสุดที่ได้จาก server (ใช้เปรียบเทียบ)
let localSched   = null;   // null = ยังไม่เคย init
let serverSched  = null;
let schedDirty   = [false, false, false]; // user แก้ค่าแล้วยังไม่ save
let schedPending = [false, false, false]; // รอ server confirm หลัง save

function connect() {
  ws = new WebSocket(wsUrl);
  ws.onopen    = () => { setLive(true);  clearTimeout(reconnectTimer); };
  ws.onclose   = () => { setLive(false); reconnectTimer = setTimeout(connect, 3000); };
  ws.onerror   = () => ws.close();
  ws.onmessage = (e) => { try { render(JSON.parse(e.data)); } catch(_){} };
}

function setLive(ok) {
  document.getElementById('live-dot').className =
    `badge-live w-2.5 h-2.5 rounded-full ${ok ? 'bg-emerald-400' : 'bg-red-400'}`;
  document.getElementById('live-label').textContent = ok ? 'Live' : 'Disconnected';
}

// ─── Render ──────────────────────────────────────────────────────
function render(d) {
  document.getElementById('last-update').textContent =
    new Date().toLocaleTimeString('th-TH');

  // ── NTP ──
  if (d.ntp) {
    const n = d.ntp;
    document.getElementById('ntp-time').textContent = n.synced ? n.time : '--:--:--';
    document.getElementById('ntp-date').textContent = n.synced ? n.date : '----/--/--';
    const badge = document.getElementById('ntp-status-badge');
    badge.innerHTML = n.synced
      ? '<span class="text-xs font-semibold bg-emerald-100 text-emerald-700 px-2 py-0.5 rounded-full normal-case">Synced</span>'
      : '<span class="text-xs font-semibold bg-amber-100 text-amber-700 px-2 py-0.5 rounded-full normal-case">Syncing...</span>';
  }

  // ── Relay ──
  if (d.relay) {
    const rc = document.getElementById('relay-container');
    rc.innerHTML = '';
    d.relay.forEach((on, i) => {
      const n = i + 1;
      const row = document.createElement('div');
      row.className = `flex items-center justify-between rounded-xl px-4 py-3 transition-all ${on ? 'relay-on' : 'relay-off'}`;
      row.innerHTML = `
        <div class="flex items-center gap-3">
          <div class="w-9 h-9 rounded-full flex items-center justify-center text-lg
            ${on ? 'bg-emerald-100' : 'bg-slate-100'}">${on ? '🟢' : '⚫'}</div>
          <div>
            <div class="font-semibold text-slate-800">Relay ${n}</div>
            <div class="text-xs ${on ? 'text-emerald-600' : 'text-slate-400'} font-medium">
              ${on ? 'เปิดอยู่' : 'ปิดอยู่'}
            </div>
          </div>
        </div>
        <button onclick="toggleRelay(${n})"
          class="px-5 py-1.5 rounded-lg text-sm font-bold transition-all ${on ? 'btn-on' : 'btn-off'}">
          ${on ? 'ปิด' : 'เปิด'}
        </button>`;
      rc.appendChild(row);
    });
  }

  // ── Telegram ──
  if (d.tg) updateTgPanel(d.tg);

  // ── Schedule ──────────────────────────────────────────────────
  // กฎ: rebuild DOM เฉพาะครั้งแรก หรือเมื่อ server confirm การ save กลับมา
  // broadcast ปกติ (ทุก 2s) → อัปเดตเฉพาะ relay-state badge ใน card เท่านั้น
  if (d.schedules) {
    if (localSched === null) {
      // ครั้งแรก — init local state แล้ว build DOM
      localSched  = d.schedules.map(s => Object.assign({}, s));
      serverSched = d.schedules.map(s => Object.assign({}, s));
      buildScheduleDOM();
    } else {
      // broadcast ปกติ — ตรวจว่า server ส่งค่าที่ต่างจากเดิมมา
      // (หมายความว่า save สำเร็จแล้ว) → sync เฉพาะ relay ที่ pending
      d.schedules.forEach((s, i) => {
        if (schedPending[i] && scheduleChanged(s, serverSched[i])) {
          // server confirm แล้ว → sync local + redraw card นั้น
          localSched[i]    = Object.assign({}, s);
          serverSched[i]   = Object.assign({}, s);
          schedPending[i]  = false;
          schedDirty[i]    = false;
          updateScheduleCard(i);
          showSchedMsg(i, '✅ บันทึกสำเร็จ', 3000);
        } else {
          // อัปเดต serverSched เงียบๆ (เช่น reboot restore)
          serverSched[i] = Object.assign({}, s);
        }
      });
    }
  }

  // ── XYMD ──
  if (d.xymd !== undefined) {
    const xt = parseFloat(d.xymd.temp);
    const xh = parseFloat(d.xymd.hum);
    document.getElementById('xymd-temp').textContent = xt.toFixed(1);
    document.getElementById('xymd-hum').textContent  = xh.toFixed(1) + '%';
    document.getElementById('xymd-temp-bar').style.width =
      Math.max(0, Math.min(100, (xt / 50) * 100)) + '%';
    document.getElementById('xymd-hum-bar').style.width  =
      Math.max(0, Math.min(100, xh)) + '%';
    const xbadge = document.getElementById('xymd-sim-badge');
    xbadge.innerHTML = d.xymd.sim
      ? '<span class="text-xs font-semibold bg-amber-100 text-amber-700 px-2 py-0.5 rounded-full normal-case">SIM</span>'
      : '<span class="text-xs font-semibold bg-emerald-100 text-emerald-700 px-2 py-0.5 rounded-full normal-case">LIVE</span>';
  }

  // ── DS18B20 ──
  if (d.ds18 !== undefined) {
    const t = parseFloat(d.ds18.temp);
    document.getElementById('ds18-temp').textContent = t.toFixed(2);
    const pct = Math.max(0, Math.min(100, (t + 10) / 60 * 100));
    document.getElementById('ds18-bar').style.width = pct + '%';
    const badge = document.getElementById('ds18-sim-badge');
    badge.innerHTML = d.ds18.sim
      ? '<span class="text-xs font-semibold bg-amber-100 text-amber-700 px-2 py-0.5 rounded-full normal-case">SIM</span>'
      : '<span class="text-xs font-semibold bg-emerald-100 text-emerald-700 px-2 py-0.5 rounded-full normal-case">LIVE</span>';
  }

  // ── Weather ──
  if (d.weather && d.weather.valid) {
    const w = d.weather;
    document.getElementById('w-temp').textContent     = parseFloat(w.temp).toFixed(1);
    document.getElementById('w-hum').textContent      = w.hum + '%';
    document.getElementById('w-rain-pct').textContent = w.rain + '%';
    document.getElementById('w-rain-bar').style.width = w.rain + '%';
    document.getElementById('w-pm25').textContent     = parseFloat(w.pm25).toFixed(1);
    document.getElementById('w-aqi-num').textContent  = w.aqi;
    const aqiEl = document.getElementById('w-aqi-label');
    aqiEl.textContent = w.aqiLabel;
    aqiEl.className   = `text-2xl font-bold aqi-${w.aqi}`;
  }

  // ── WiFi ──
  if (d.wifi) {
    const wf = d.wifi;
    document.getElementById('wifi-ssid').textContent = wf.ssid || '--';
    document.getElementById('wifi-ip').textContent   = wf.ip   || '--';
    document.getElementById('wifi-mac').textContent  = wf.mac  || '--';
    document.getElementById('ip-label').textContent  = 'http://' + (wf.ip || '...');
    const rssi  = wf.rssi || -100;
    const pct   = Math.max(0, Math.min(100, (rssi + 100) * 2));
    const tColor = pct > 60 ? 'text-emerald-600' : pct > 30 ? 'text-amber-500' : 'text-red-500';
    const bColor = pct > 60 ? 'bg-emerald-400'  : pct > 30 ? 'bg-amber-400'   : 'bg-red-400';
    document.getElementById('wifi-rssi-val').textContent  = rssi + ' dBm';
    document.getElementById('wifi-rssi-text').textContent = rssi + ' dBm';
    document.getElementById('wifi-rssi-text').className   = `info-value font-mono ${tColor}`;
    const bar = document.getElementById('wifi-signal-bar');
    bar.style.width = pct + '%';
    bar.className   = `gauge-bar ${bColor}`;
  }

  // ── MQTT ──
  if (d.mqtt) {
    const m = d.mqtt;
    document.getElementById('mqtt-host-label').textContent = m.host + ':' + m.port;
    const badge = document.getElementById('mqtt-status-badge');
    badge.innerHTML = m.connected
      ? '<span class="text-xs font-semibold bg-emerald-100 text-emerald-700 px-2 py-0.5 rounded-full">Connected</span>'
      : '<span class="text-xs font-semibold bg-red-100 text-red-600 px-2 py-0.5 rounded-full">Disconnected</span>';
    const set = (id, val) => { const el = document.getElementById(id); if (el) el.textContent = val; };
    set('t-telemetry', m.t_telemetry); set('t-status',   m.t_status);
    set('t-r1-state',  m.t_r1_state);  set('t-r2-state', m.t_r2_state);
    set('t-r3-state',  m.t_r3_state);
    set('t-r1-set',    m.t_r1_set);    set('t-r2-set',   m.t_r2_set);
    set('t-r3-set',    m.t_r3_set);    set('t-r1-set-ex',m.t_r1_set);
  }

  // ── System ──
  if (d.sys) {
    document.getElementById('sys-heap').textContent   = (d.sys.heap / 1024).toFixed(1) + ' KB';
    document.getElementById('sys-uptime').textContent = fmtUptime(d.sys.uptime);
  }
}

// ─── Schedule UI ─────────────────────────────────────────────────
const DAY_LABELS = ['อา','จ','อ','พ','พฤ','ศ','ส'];

// สร้าง DOM ครั้งเดียวตอน init — ไม่ถูกแตะระหว่าง broadcast
function buildScheduleDOM() {
  const cont = document.getElementById('schedule-container');
  cont.innerHTML = '';
  localSched.forEach((s, i) => {
    const div = document.createElement('div');
    div.className = `sched-card ${s.enabled ? 'sched-active' : ''}`;
    div.id = `sched-card-${i}`;

    let dayBtns = '';
    for (let d = 0; d < 7; d++) {
      const sel = (s.dayMask >> d) & 1 ? 'selected' : '';
      dayBtns += `<button class="day-btn ${sel}" id="day-${i}-${d}"
        onclick="toggleDay(${i},${d})">${DAY_LABELS[d]}</button>`;
    }

    div.innerHTML = `
      <div class="flex items-center justify-between mb-3">
        <span class="font-bold text-slate-700">⚡ Relay ${i+1}</span>
        <label class="toggle-sched">
          <input type="checkbox" id="sched-en-${i}" ${s.enabled ? 'checked' : ''}
                 onchange="onEnableChange(${i})">
          <span class="slider"></span>
        </label>
      </div>
      <div class="grid grid-cols-2 gap-3 mb-3">
        <div class="text-center">
          <div class="text-xs text-slate-400 mb-1">🟢 เปิด</div>
          <div class="flex items-center gap-1 justify-center">
            <input class="time-input" id="on-h-${i}" type="number" min="0" max="23"
                   value="${pad2(s.onHour)}"
                   oninput="clampInput(this,0,23); markDirty(${i})">
            <span class="text-slate-400 font-bold">:</span>
            <input class="time-input" id="on-m-${i}" type="number" min="0" max="59"
                   value="${pad2(s.onMinute)}"
                   oninput="clampInput(this,0,59); markDirty(${i})">
          </div>
        </div>
        <div class="text-center">
          <div class="text-xs text-slate-400 mb-1">🔴 ปิด</div>
          <div class="flex items-center gap-1 justify-center">
            <input class="time-input" id="off-h-${i}" type="number" min="0" max="23"
                   value="${pad2(s.offHour)}"
                   oninput="clampInput(this,0,23); markDirty(${i})">
            <span class="text-slate-400 font-bold">:</span>
            <input class="time-input" id="off-m-${i}" type="number" min="0" max="59"
                   value="${pad2(s.offMinute)}"
                   oninput="clampInput(this,0,59); markDirty(${i})">
          </div>
        </div>
      </div>
      <div class="flex gap-1 justify-center mb-3">${dayBtns}</div>
      <button class="save-btn w-full" id="save-btn-${i}" onclick="saveSchedule(${i})">
        💾 บันทึก
      </button>
      <div class="text-xs text-center mt-2 h-4" id="sched-msg-${i}"></div>
    `;
    cont.appendChild(div);
  });
}

// อัปเดตเฉพาะ card เดียว (หลัง server confirm)
function updateScheduleCard(i) {
  const s = localSched[i];
  // enable toggle
  document.getElementById(`sched-en-${i}`).checked = s.enabled;
  document.getElementById(`sched-card-${i}`).className =
    `sched-card ${s.enabled ? 'sched-active' : ''}`;
  // time inputs — อัปเดตเฉพาะถ้าไม่ได้ focus อยู่
  const fields = [
    [`on-h-${i}`,  s.onHour],  [`on-m-${i}`,  s.onMinute],
    [`off-h-${i}`, s.offHour], [`off-m-${i}`, s.offMinute],
  ];
  fields.forEach(([id, val]) => {
    const el = document.getElementById(id);
    if (el && document.activeElement !== el) el.value = pad2(val);
  });
  // day buttons
  for (let d = 0; d < 7; d++) {
    const btn = document.getElementById(`day-${i}-${d}`);
    if (btn) btn.className = `day-btn ${(s.dayMask >> d) & 1 ? 'selected' : ''}`;
  }
  updateSaveBtn(i);
}

// ตรวจว่า server object เปลี่ยนจาก prev หรือไม่
function scheduleChanged(a, b) {
  if (!b) return true;
  return a.enabled !== b.enabled || a.onHour !== b.onHour ||
    a.onMinute !== b.onMinute || a.offHour !== b.offHour ||
    a.offMinute !== b.offMinute || a.dayMask !== b.dayMask;
}

function pad2(n) { return String(n).padStart(2,'0'); }

function clampInput(el, mn, mx) {
  let v = parseInt(el.value);
  if (!isNaN(v)) el.value = Math.max(mn, Math.min(mx, v));
}

function markDirty(i) {
  schedDirty[i] = true;
  updateSaveBtn(i);
}

function updateSaveBtn(i) {
  const btn = document.getElementById(`save-btn-${i}`);
  if (!btn) return;
  if (schedPending[i]) {
    btn.textContent = '⏳ กำลังบันทึก...';
    btn.disabled = true;
    btn.style.opacity = '0.6';
  } else if (schedDirty[i]) {
    btn.textContent = '💾 บันทึก *';
    btn.disabled = false;
    btn.style.opacity = '1';
  } else {
    btn.textContent = '💾 บันทึก';
    btn.disabled = false;
    btn.style.opacity = '1';
  }
}

let schedMsgTimers = [null, null, null];
function showSchedMsg(i, text, ms) {
  const el = document.getElementById(`sched-msg-${i}`);
  if (!el) return;
  el.textContent = text;
  el.style.color = text.startsWith('✅') ? '#16a34a' : '#64748b';
  clearTimeout(schedMsgTimers[i]);
  schedMsgTimers[i] = setTimeout(() => { el.textContent = ''; }, ms);
}

function onEnableChange(i) {
  localSched[i].enabled = document.getElementById(`sched-en-${i}`).checked;
  document.getElementById(`sched-card-${i}`).className =
    `sched-card ${localSched[i].enabled ? 'sched-active' : ''}`;
  markDirty(i);
}

function toggleDay(schedIdx, dayIdx) {
  localSched[schedIdx].dayMask ^= (1 << dayIdx);
  const btn = document.getElementById(`day-${schedIdx}-${dayIdx}`);
  if (btn) btn.className =
    `day-btn ${(localSched[schedIdx].dayMask >> dayIdx) & 1 ? 'selected' : ''}`;
  markDirty(schedIdx);
}

function saveSchedule(i) {
  if (!ws || ws.readyState !== WebSocket.OPEN) return;
  // อ่านค่าจาก DOM → sync เข้า localSched
  const onh = parseInt(document.getElementById(`on-h-${i}`).value)  || 0;
  const onm = parseInt(document.getElementById(`on-m-${i}`).value)  || 0;
  const ofh = parseInt(document.getElementById(`off-h-${i}`).value) || 0;
  const ofm = parseInt(document.getElementById(`off-m-${i}`).value) || 0;
  localSched[i].onHour    = Math.max(0, Math.min(23, onh));
  localSched[i].onMinute  = Math.max(0, Math.min(59, onm));
  localSched[i].offHour   = Math.max(0, Math.min(23, ofh));
  localSched[i].offMinute = Math.max(0, Math.min(59, ofm));

  schedPending[i] = true;
  schedDirty[i]   = false;
  updateSaveBtn(i);
  showSchedMsg(i, '📡 กำลังส่ง...', 8000);

  ws.send(JSON.stringify({
    cmd: 'set_schedule', n: i + 1,
    enabled:   localSched[i].enabled,
    onHour:    localSched[i].onHour,
    onMinute:  localSched[i].onMinute,
    offHour:   localSched[i].offHour,
    offMinute: localSched[i].offMinute,
    dayMask:   localSched[i].dayMask,
  }));

  // timeout fallback — ถ้า 5s ไม่ได้ confirm ก็คืนสถานะ
  setTimeout(() => {
    if (schedPending[i]) {
      schedPending[i] = false;
      schedDirty[i]   = true;
      updateSaveBtn(i);
      showSchedMsg(i, '⚠️ ไม่ได้รับการยืนยัน', 4000);
    }
  }, 5000);
}

// ─── Telegram Settings ───────────────────────────────────────────
const TG_ALERT_DEFS = [
  { key: 'alRelayOn',  label: '⚡ Relay เปิด',         desc: 'แจ้งทุกครั้งที่ relay เปิด' },
  { key: 'alRelayOff', label: '🔌 Relay ปิด',          desc: 'แจ้งทุกครั้งที่ relay ปิด' },
  { key: 'alTempHigh', label: '🔥 อุณหภูมิสูง',        desc: 'DS18B20 เกิน Threshold' },
  { key: 'alTempLow',  label: '🧊 อุณหภูมิต่ำ',        desc: 'DS18B20 ต่ำกว่า Threshold' },
  { key: 'alAqi',      label: '🌫 AQI แย่',            desc: 'AQI ≥ ระดับที่กำหนด' },
  { key: 'alRain',     label: '🌧 โอกาสฝนสูง',         desc: 'โอกาสฝน ≥ Threshold' },
  { key: 'alBoot',     label: '🚀 เปิดเครื่อง',         desc: 'แจ้งทุกครั้งที่ ESP32 restart' },
  { key: 'alSchedOn',  label: '⏰ Schedule เปิด Relay', desc: 'Relay เปิดตามตาราง' },
  { key: 'alSchedOff', label: '⏰ Schedule ปิด Relay', desc: 'Relay ปิดตามตาราง' },
];

let tgConfig = null;    // cache จาก server
let tgDirty  = false;
let tgSaveTimer = null;

// init toggles DOM ครั้งเดียว
function buildTgToggles() {
  const cont = document.getElementById('tg-alert-toggles');
  if (!cont || cont.children.length > 0) return;
  TG_ALERT_DEFS.forEach(def => {
    const row = document.createElement('div');
    row.className = 'flex items-center justify-between gap-2 py-1.5 border-b border-slate-100 last:border-0';
    row.innerHTML = `
      <div class="flex-1 min-w-0">
        <div class="text-sm font-medium text-slate-700 leading-tight">${def.label}</div>
        <div class="text-xs text-slate-400">${def.desc}</div>
      </div>
      <label class="toggle-sched flex-shrink-0">
        <input type="checkbox" id="tg-al-${def.key}" checked onchange="tgMarkDirty()">
        <span class="slider"></span>
      </label>`;
    cont.appendChild(row);
  });
}

function updateTgPanel(tg) {
  if (!tg || !tg.enabled) return;
  tgConfig = tg;

  buildTgToggles();  // no-op ถ้า build แล้ว

  // status badge
  const badge = document.getElementById('tg-status-badge');
  if (badge) badge.innerHTML =
    '<span class="text-xs font-semibold bg-emerald-100 text-emerald-700 px-2 py-0.5 rounded-full normal-case">Active</span>';

  // queue / mask
  const q = document.getElementById('tg-queue');
  if (q) q.textContent = tg.queueCount + ' ข้อ';
  const m = document.getElementById('tg-mask');
  if (m) m.textContent = '0x' + tg.alertMask.toString(16).toUpperCase().padStart(4,'0');

  // threshold inputs — อัปเดตเฉพาะถ้าไม่ dirty และไม่ focus
  if (!tgDirty) {
    const setVal = (id, v) => {
      const el = document.getElementById(id);
      if (el && document.activeElement !== el) el.value = v;
    };
    setVal('tg-temp-high',  tg.tempHigh);
    setVal('tg-temp-low',   tg.tempLow);
    setVal('tg-rain-limit', tg.rainLimit);
    setVal('tg-aqi-level',  tg.aqiLevel);

    // alert toggles
    TG_ALERT_DEFS.forEach(def => {
      const el = document.getElementById('tg-al-' + def.key);
      if (el) el.checked = tg[def.key] !== false;
    });
  }
}

function tgMarkDirty() {
  tgDirty = true;
  const btn = document.getElementById('tg-save-btn');
  if (btn) { btn.textContent = '💾 บันทึก *'; btn.style.opacity = '1'; }
}

function tgSaveConfig() {
  if (!ws || ws.readyState !== WebSocket.OPEN) return;

  const payload = { cmd: 'tg_config' };
  const num = (id, fb) => { const v = parseFloat(document.getElementById(id)?.value); return isNaN(v) ? fb : v; };
  payload.tempHigh  = num('tg-temp-high', 40);
  payload.tempLow   = num('tg-temp-low', 10);
  payload.rainLimit = parseInt(document.getElementById('tg-rain-limit')?.value) || 70;
  payload.aqiLevel  = parseInt(document.getElementById('tg-aqi-level')?.value)  || 4;

  TG_ALERT_DEFS.forEach(def => {
    const el = document.getElementById('tg-al-' + def.key);
    payload[def.key] = el ? el.checked : true;
  });

  ws.send(JSON.stringify(payload));

  tgDirty = false;
  const btn = document.getElementById('tg-save-btn');
  if (btn) { btn.textContent = '⏳ กำลังบันทึก...'; btn.style.opacity = '0.6'; }

  clearTimeout(tgSaveTimer);
  tgSaveTimer = setTimeout(() => {
    const btn2 = document.getElementById('tg-save-btn');
    if (btn2) { btn2.textContent = '💾 บันทึกการตั้งค่า'; btn2.style.opacity = '1'; }
    const msg = document.getElementById('tg-save-msg');
    if (msg) { msg.textContent = '✅ บันทึกแล้ว'; msg.style.color='#16a34a';
      setTimeout(()=>{ msg.textContent=''; }, 3000); }
  }, 1500);
}

function tgSendTest() {
  if (ws && ws.readyState === WebSocket.OPEN)
    ws.send(JSON.stringify({ cmd: 'tg_test' }));
}

function tgSendStatus() {
  if (ws && ws.readyState === WebSocket.OPEN)
    ws.send(JSON.stringify({ cmd: 'tg_status' }));
}

// ─── Actions ─────────────────────────────────────────────────────
function toggleRelay(n) {
  if (ws && ws.readyState === WebSocket.OPEN)
    ws.send(JSON.stringify({ cmd: 'relay', n }));
}

// ─── Helpers ─────────────────────────────────────────────────────
function fmtUptime(sec) {
  const h = Math.floor(sec / 3600);
  const m = Math.floor((sec % 3600) / 60);
  const s = sec % 60;
  return `${h}h ${m}m ${s}s`;
}

connect();
</script>
</body>
</html>
)rawhtml";

#endif // DASHBOARD_H
