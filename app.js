/* ============================================================
   IVGuard — Frontend Application Logic
   Connects to Spring Boot backend via WebSocket (STOMP/SockJS)
   Falls back to simulation mode if backend is unreachable.
   ============================================================ */

const CONFIG = {
  WS_URL:          'ws://localhost:8080/ws',        // Spring Boot WebSocket endpoint
  API_BASE:        'http://localhost:8080/api',     // REST base URL
  TOTAL_VOLUME:    500,                             // ml
  CRITICAL_PCT:    0.12,                            // 12% threshold
  WARNING_PCT:     0.15,                            // 15% warning
  SIMULATE:        true,                            // true = run without backend
  SIM_DRAIN_RATE:  0.06,                            // % per 3 s tick
  SIM_DPM_BASE:    64,
  SIM_DPM_NOISE:   3,
};

/* ── State ── */
let state = {
  volume:          72,      // current % remaining
  dpm:             64,
  clamped:         false,
  criticalFired:   false,
  dpmHistory:      [],
  ws:              null,
};

/* seed sparkline history */
for (let i = 0; i < 60; i++) {
  state.dpmHistory.push(CONFIG.SIM_DPM_BASE + (Math.random() * CONFIG.SIM_DPM_NOISE * 2 - CONFIG.SIM_DPM_NOISE));
}

/* ── DOM refs ── */
const $ = id => document.getElementById(id);

/* ============================================================
   CLOCK
   ============================================================ */
function updateClock() {
  $('clockDisplay').textContent = new Date().toLocaleTimeString('en-IN', { hour12: false });
}
setInterval(updateClock, 1000);
updateClock();

/* ============================================================
   WEBSOCKET  (STOMP over raw WebSocket)
   Falls back silently to simulation if backend is down.
   ============================================================ */
function connectWS() {
  try {
    const ws = new WebSocket(CONFIG.WS_URL);
    state.ws = ws;

    ws.onopen = () => {
      setWsStatus(true);
      console.log('[IVGuard] WebSocket connected');
    };

    ws.onmessage = (evt) => {
      try {
        const msg = JSON.parse(evt.data);
        handleServerMessage(msg);
      } catch (e) {
        console.warn('[IVGuard] WS parse error', e);
      }
    };

    ws.onclose = () => {
      setWsStatus(false);
      console.warn('[IVGuard] WS disconnected — retrying in 5s');
      setTimeout(connectWS, 5000);
    };

    ws.onerror = () => {
      console.warn('[IVGuard] WS error — running in simulation mode');
      setWsStatus(false);
    };
  } catch (e) {
    setWsStatus(false);
  }
}

function setWsStatus(connected) {
  const el = $('wsStatus');
  const chip = $('wsStatusChip');
  if (connected) {
    el.className = 'ws-status connected';
    el.innerHTML = '<span class="pulse-dot"></span>LIVE';
    if (chip) { chip.textContent = 'Connected'; chip.className = 'safety-status s-active'; }
  } else {
    el.className = 'ws-status disconnected';
    el.innerHTML = '<span class="pulse-dot"></span>OFFLINE';
    if (chip) { chip.textContent = 'Offline'; chip.className = 'safety-status s-offline'; }
  }
}

/**
 * Handle messages pushed from Spring Boot backend.
 * Expected message shapes:
 *   { type: "SENSOR_UPDATE", roomId, dpm, volumePct }
 *   { type: "CRITICAL_ALERT", roomId, message }
 *   { type: "CLAMP_ACK",  roomId, status }
 */
function handleServerMessage(msg) {
  switch (msg.type) {
    case 'SENSOR_UPDATE':
      if (msg.roomId === '201') {
        updateDpm(msg.dpm);
        updateVolume(msg.volumePct);
      }
      break;
    case 'CRITICAL_ALERT':
      showToast(msg.message || 'CRITICAL: Volume below threshold!');
      addAlert('err', msg.roomId, msg.message);
      break;
    case 'CLAMP_ACK':
      handleClampAck(msg.status);
      break;
    default:
      console.log('[IVGuard] Unknown WS message type:', msg.type);
  }
}

/* ============================================================
   BOTTLE / VOLUME UI
   ============================================================ */
function updateVolume(pct) {
  state.volume = Math.max(0, pct);
  const totalH   = 104;                            // SVG liquid column height
  const fillH    = Math.max(0, (pct / 100) * totalH);
  const fillY    = 26 + (totalH - fillH);          // SVG top of rect

  const liq      = document.getElementById('liquidFill');
  if (liq) {
    liq.setAttribute('y', fillY.toFixed(1));
    liq.setAttribute('height', fillH.toFixed(1));

    const critical = pct <= CONFIG.CRITICAL_PCT * 100;
    const warning  = pct <= CONFIG.WARNING_PCT * 100;
    liq.setAttribute('fill', critical ? '#E74C3C' : warning ? '#F39C12' : '#85C1E9');
    liq.setAttribute('opacity', critical ? '0.85' : '0.75');
  }

  const volumeCard = $('volumeCard');
  const volPctEl   = $('volPct');
  const ml         = Math.round(pct / 100 * CONFIG.TOTAL_VOLUME);

  volPctEl.textContent = Math.round(pct) + '%';

  if (pct <= CONFIG.CRITICAL_PCT * 100) {
    volPctEl.classList.add('critical');
    volumeCard.classList.add('critical');
    if (!state.criticalFired) {
      state.criticalFired = true;
      showToast('CRITICAL: Volume below 12% — Room 201');
      addAlert('err', '201', 'CRITICAL: Volume <12% — Immediate action required');
    }
  } else {
    volPctEl.classList.remove('critical');
    volumeCard.classList.remove('critical');
    if (pct > 15) state.criticalFired = false;
  }

  $('volMl').textContent = ml + ' ml remaining';

  /* predictive time-to-empty */
  const mlPerMin   = (state.dpm / 20);             // ~60 drops = 3ml
  const minsLeft   = mlPerMin > 0 ? Math.round(ml / mlPerMin) : 999;
  const h          = Math.floor(minsLeft / 60);
  const m          = minsLeft % 60;
  $('predictiveTime').textContent = `Predictive Time to Empty: ${h}:${String(m).padStart(2, '0')} HRS`;
}

/* ============================================================
   DPM + SPARKLINE
   ============================================================ */
let sparkChart;

function initSparkline() {
  const ctx = $('sparkCanvas').getContext('2d');
  sparkChart = new Chart(ctx, {
    type: 'line',
    data: {
      labels: Array.from({ length: 60 }, () => ''),
      datasets: [{
        data: [...state.dpmHistory],
        borderColor: '#3498DB',
        borderWidth: 1.8,
        pointRadius: 0,
        tension: 0.4,
        fill: { target: 'origin', above: 'rgba(52,152,219,0.08)' },
      }],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      plugins: { legend: { display: false }, tooltip: { enabled: false } },
      scales: {
        x: { display: false },
        y: { display: false, min: 50, max: 80 },
      },
      animation: { duration: 400 },
    },
  });
}

function updateDpm(value) {
  state.dpm = value;
  state.dpmHistory.shift();
  state.dpmHistory.push(value);

  $('dripValue').textContent = Math.round(value);

  const arr10  = state.dpmHistory.slice(-10);
  const avg    = arr10.reduce((a, b) => a + b, 0) / arr10.length;
  const peak   = Math.max(...state.dpmHistory);
  const dev    = Math.sqrt(arr10.reduce((a, b) => a + (b - avg) ** 2, 0) / arr10.length);

  $('avgDpm').textContent = avg.toFixed(1) + ' DPM';
  $('peakDpm').textContent = Math.round(peak) + ' DPM';
  $('deviation').textContent = '±' + dev.toFixed(1) + ' DPM';

  const interval = (60 / value).toFixed(2);
  $('lastDrop').textContent = interval + 's';

  /* flow badge */
  const badge = $('flowBadge');
  const stat  = $('flowStatus');
  const diff  = Math.abs(value - CONFIG.SIM_DPM_BASE);
  if (diff < 6) {
    badge.className = 'badge nominal'; stat.textContent = 'Nominal';
  } else if (diff < 12) {
    badge.className = 'badge warning-badge'; stat.textContent = 'Check Flow';
  } else {
    badge.className = 'badge danger-badge'; stat.textContent = 'IRREGULAR';
  }

  sparkChart.data.datasets[0].data = [...state.dpmHistory];
  sparkChart.update('none');
}

/* ============================================================
   HISTORICAL CHART
   ============================================================ */
function buildHistoricalChart() {
  const hours = Array.from({ length: 25 }, (_, i) => i + 'h');

  const genLine = (base) => hours.map(() =>
    Math.max(30, base + Math.random() * 10 - 5 + (Math.random() > 0.92 ? -15 : 0))
  );

  const ctx = $('histChart').getContext('2d');
  new Chart(ctx, {
    type: 'line',
    data: {
      labels: hours,
      datasets: [
        { label: 'Room 201', data: genLine(64), borderColor: '#3498DB', borderWidth: 2, pointRadius: 0, tension: 0.4, borderDash: [] },
        { label: 'Room 202', data: genLine(58), borderColor: '#27AE60', borderWidth: 2, pointRadius: 0, tension: 0.4, borderDash: [5, 3] },
        { label: 'Room 203', data: genLine(70), borderColor: '#F39C12', borderWidth: 2, pointRadius: 0, tension: 0.4, borderDash: [2, 2] },
        { label: 'Room 204', data: genLine(55), borderColor: '#9B59B6', borderWidth: 2, pointRadius: 0, tension: 0.4, borderDash: [] },
      ],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      plugins: {
        legend: { display: false },
        tooltip: { mode: 'index', intersect: false },
      },
      scales: {
        x: {
          ticks: { color: '#7F8C8D', font: { size: 10 }, maxTicksLimit: 9 },
          grid: { color: 'rgba(0,0,0,0.04)' },
        },
        y: {
          min: 30, max: 85,
          ticks: { color: '#7F8C8D', font: { size: 10 }, callback: v => v + ' DPM' },
          grid: { color: 'rgba(0,0,0,0.04)' },
        },
      },
    },
  });
}

/* ============================================================
   EMERGENCY CLAMP
   ============================================================ */
async function emergencyClamp() {
  if (state.clamped) {
    /* Release */
    state.clamped = false;
    $('emergencyBtn').innerHTML = '<i class="ti ti-lock"></i>EMERGENCY CLAMP';
    $('emergencyBtn').classList.remove('clamped');
    $('valveStatus').textContent = 'Ready';
    $('valveStatus').className = 'safety-status s-ready';
    $('emergencyHint').textContent = 'Sends POST /api/clamp + MQTT LOCK signal';
    showToast('Clamp released — monitoring resumed');
    sendClampRequest('UNLOCK');
    return;
  }

  /* Engage */
  state.clamped = true;
  $('emergencyBtn').innerHTML = '<i class="ti ti-lock-open"></i>CLAMP ACTIVE — TAP TO RELEASE';
  $('emergencyBtn').classList.add('clamped');
  $('valveStatus').textContent = 'LOCKED';
  $('valveStatus').className = 'safety-status s-offline';
  $('emergencyHint').textContent = 'Valve locked. Hardware signal sent.';
  showToast('EMERGENCY CLAMP ENGAGED — Signal sent to hardware');
  addAlert('err', '201', 'EMERGENCY CLAMP ACTIVATED — Servo valve locked by operator');
  sendClampRequest('LOCK');
}

async function sendClampRequest(action) {
  const payload = {
    roomId:    '201',
    patientId: 'john-doe',
    action,
    timestamp: new Date().toISOString(),
  };

  try {
    const res = await fetch(`${CONFIG.API_BASE}/clamp`, {
      method:  'POST',
      headers: { 'Content-Type': 'application/json' },
      body:    JSON.stringify(payload),
    });
    if (!res.ok) console.error('[IVGuard] Clamp request failed:', res.status);
    else         console.log('[IVGuard] Clamp request OK:', action);
  } catch (e) {
    console.warn('[IVGuard] Clamp fetch error (backend may be offline):', e.message);
  }
}

function handleClampAck(status) {
  console.log('[IVGuard] Clamp ACK from backend:', status);
  /* Server confirmed — no UI change needed; local state is already updated */
}

/* ============================================================
   ALERTS
   ============================================================ */
function addAlert(type, room, message) {
  const list = $('alertsList');
  const now  = new Date();
  const ts   = now.toLocaleTimeString('en-IN', { hour12: false, hour: '2-digit', minute: '2-digit' });

  const li = document.createElement('li');
  li.className = `alert-item ${type}`;
  li.innerHTML = `
    <span class="alert-time">${ts}</span>
    <span class="alert-room ${type}">[Rm ${room}]</span>
    <span class="alert-msg">${message}</span>`;
  list.insertBefore(li, list.firstChild);

  /* cap at 50 entries */
  while (list.children.length > 50) list.removeChild(list.lastChild);
}

/* ============================================================
   TOAST
   ============================================================ */
let toastTimer;
function showToast(msg) {
  const t = $('critToast');
  $('toastMsg').textContent = msg;
  t.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.remove('show'), 5500);
}

/* ============================================================
   SIMULATION LOOP (runs when backend is unreachable)
   ============================================================ */
function startSimulation() {
  /* DPM updates every second */
  setInterval(() => {
    if (state.clamped) return;
    const newDpm = CONFIG.SIM_DPM_BASE
      + (Math.random() * CONFIG.SIM_DPM_NOISE * 2 - CONFIG.SIM_DPM_NOISE)
      + (Math.random() > 0.97 ? -8 : 0); /* occasional spike */
    updateDpm(Math.max(10, newDpm));
  }, 1000);

  /* Volume drains every 3 s */
  setInterval(() => {
    if (state.clamped) return;
    updateVolume(Math.max(0, state.volume - CONFIG.SIM_DRAIN_RATE));
  }, 3000);
}

/* ============================================================
   SIDEBAR SEARCH
   ============================================================ */
$('ptSearch').addEventListener('input', function () {
  const q = this.value.toLowerCase();
  document.querySelectorAll('#patientList .patient-item').forEach(el => {
    const name = el.querySelector('.pt-name').textContent.toLowerCase();
    const room = el.querySelector('.pt-room').textContent.toLowerCase();
    el.style.display = (name.includes(q) || room.includes(q)) ? '' : 'none';
  });
});

/* ── Nav items ── */
document.querySelectorAll('.nav-item').forEach(el => {
  el.addEventListener('click', () => {
    document.querySelectorAll('.nav-item').forEach(n => n.classList.remove('active'));
    el.classList.add('active');
  });
});

/* ============================================================
   BOOT
   ============================================================ */
document.addEventListener('DOMContentLoaded', () => {
  initSparkline();
  buildHistoricalChart();
  updateVolume(state.volume);
  updateDpm(state.dpm);
  $('emergencyBtn').addEventListener('click', emergencyClamp);

  if (CONFIG.SIMULATE) {
    startSimulation();
  }
  connectWS(); /* always try; will fall back gracefully */
});
