/* ════════════════════════════════════════════════════════
   IV DRIP MONITORING SYSTEM — script.js
   Handles: cursor-follow, GSAP stagger reveal, tab navigation,
   patient card rendering, Chart.js graphs, Socket.IO real-time
   ════════════════════════════════════════════════════════ */

'use strict';

/* ── 1. INITIAL DATA (replace with Flask API later) ───── */
const PATIENTS = [
  {
    id: 1, name: "Arjun Mehta", bed: "3B-07",
    solution: "0.9% NaCl", volume: 12, flow: 42,
    timeLeft: "18 min", status: "critical", motorOn: true,
    deviceId: "ESP32-001"
  },
  {
    id: 2, name: "Priya Sharma", bed: "3B-02",
    solution: "Glucose 5%", volume: 68, flow: 60,
    timeLeft: "3h 12m", status: "ok", motorOn: true,
    deviceId: "ESP32-002"
  },
  {
    id: 3, name: "Ramesh Kumar", bed: "3B-09",
    solution: "Ringer's Lactate", volume: 45, flow: 35,
    timeLeft: "2h 08m", status: "warning", motorOn: true,
    deviceId: "ESP32-003"
  },
  {
    id: 4, name: "Deepa Nair", bed: "3B-11",
    solution: "0.9% NaCl", volume: 88, flow: 50,
    timeLeft: "5h 45m", status: "ok", motorOn: true,
    deviceId: "ESP32-004"
  },
  {
    id: 5, name: "Suresh Pillai", bed: "3B-14",
    solution: "Dextrose + NaCl", volume: 33, flow: 30,
    timeLeft: "1h 50m", status: "warning", motorOn: false,
    deviceId: "ESP32-005"
  }
];

const ALERTS_DATA = [
  { type: "critical", badge: "!", title: "CRITICAL: Low Volume", detail: "Arjun Mehta (Bed 3B-07) — 12% remaining (~18 min)", time: "just now" },
  { type: "warning",  badge: "▲", title: "WARNING: Low Volume", detail: "Ramesh Kumar (Bed 3B-09) — 45% remaining (~2h 08m)", time: "4 min ago" },
  { type: "warning",  badge: "▲", title: "WARNING: Low Volume", detail: "Suresh Pillai (Bed 3B-14) — 33% remaining (~1h 50m)", time: "9 min ago" },
  { type: "info",     badge: "✓", title: "Completed", detail: "Kavitha Reddy (Bed 3B-05) — IV administration complete", time: "32 min ago" },
  { type: "info",     badge: "i", title: "New Patient Added", detail: "Deepa Nair admitted to Bed 3B-11", time: "1h ago" }
];


/* ── 2. CUSTOM CURSOR ───────────────────────────────────── */
const cursorDot = document.getElementById('cursorDot');

document.addEventListener('mousemove', (e) => {
  cursorDot.style.left = e.clientX + 'px';
  cursorDot.style.top  = e.clientY + 'px';
});


/* ── 3. HERO GRAPHIC — CURSOR-FOLLOW PARALLAX ─────────── */
function lerp(a, b, t) { return a + (b - a) * t; }

const heroGraphic = document.getElementById('heroGraphic');

if (heroGraphic) {
  let gfxX = 0, gfxY = 0;
  let targetGfxX = 0, targetGfxY = 0;

  document.addEventListener('mousemove', (e) => {
    const cx = window.innerWidth  / 2;
    const cy = window.innerHeight / 2;
    // Offset relative to center, max ±20px movement
    targetGfxX = (e.clientX - cx) / cx * 20;
    targetGfxY = (e.clientY - cy) / cy * 12;
  });

  function animateGraphic() {
    gfxX = lerp(gfxX, targetGfxX, 0.06);  // slow, dreamy parallax
    gfxY = lerp(gfxY, targetGfxY, 0.06);
    heroGraphic.style.transform = `translate(${gfxX}px, ${gfxY}px)`;
    requestAnimationFrame(animateGraphic);
  }
  animateGraphic();
}


/* ── 4. NAVBAR SCROLL EFFECT ───────────────────────────── */
const navbar = document.getElementById('navbar');
window.addEventListener('scroll', () => {
  navbar.classList.toggle('scrolled', window.scrollY > 20);
});


/* ── 5. TAB NAVIGATION ─────────────────────────────────── */
const navLinks = document.querySelectorAll('.nav-link[data-tab]');
const heroBtns = document.querySelectorAll('[data-tab]');

function switchTab(tabId) {
  // Deactivate all views
  document.querySelectorAll('.tab-view').forEach(v => v.classList.remove('active'));
  document.querySelectorAll('.nav-link').forEach(l => l.classList.remove('active'));

  // Activate target view
  const targetView = document.getElementById('view-' + tabId);
  if (targetView) {
    targetView.classList.add('active');
    // Scroll to app main
    document.getElementById('appMain').scrollIntoView({ behavior: 'smooth', block: 'start' });
  }

  // Highlight nav link
  document.querySelectorAll(`.nav-link[data-tab="${tabId}"]`).forEach(l => l.classList.add('active'));

  // Trigger stagger reveals in newly active section
  setTimeout(() => triggerReveal(targetView), 100);
}

heroBtns.forEach(btn => {
  btn.addEventListener('click', () => {
    const tab = btn.getAttribute('data-tab');
    if (tab) switchTab(tab);
  });
});


/* ── 6. SCROLL-TRIGGERED STAGGER REVEAL (GSAP) ─────────── */
gsap.registerPlugin(ScrollTrigger);

function triggerReveal(container = document) {
  const items = container.querySelectorAll('.reveal-item');
  items.forEach((el, i) => {
    gsap.fromTo(el,
      { y: 36, opacity: 0 },
      {
        y: 0, opacity: 1,
        duration: 0.7,
        delay: i * 0.10,
        ease: 'back.out(1.4)',
        scrollTrigger: {
          trigger: el,
          start: 'top 88%',
          toggleActions: 'play none none none'
        }
      }
    );
  });
}

window.addEventListener('load', () => {
  triggerReveal(document);
  // Slight delay for hero content
  gsap.from('.hero-sub, .hero-actions', {
    y: 20, opacity: 0, duration: 0.8, delay: 0.6,
    ease: 'power3.out', stagger: 0.15
  });
});


/* ── 7. PATIENT CARDS RENDERER ──────────────────────────── */
function getVolumeClass(vol) {
  if (vol <= 20) return 'critical';
  if (vol <= 40) return 'warning';
  return '';
}

function renderPatientCards(data) {
  const grid = document.getElementById('patientCardsGrid');
  if (!grid) return;
  grid.innerHTML = '';

  data.forEach((p, idx) => {
    const volClass = getVolumeClass(p.volume);
    const card = document.createElement('div');
    card.className = `patient-card ${p.status}`;
    card.style.animationDelay = `${idx * 0.08}s`;

    card.innerHTML = `
      <div class="card-top">
        <div>
          <div class="card-name">${p.name}</div>
        </div>
        <div class="card-bed">BED ${p.bed}</div>
      </div>
      <div class="card-solution">${p.solution}</div>

      <div class="volume-bar-label">
        <span class="volume-label-text">Volume Remaining</span>
        <span class="volume-pct" id="vol-pct-${p.id}">${p.volume}%</span>
      </div>
      <div class="volume-track">
        <div class="volume-fill ${volClass}" id="vol-bar-${p.id}" style="width: ${p.volume}%"></div>
      </div>

      <div class="card-meta-row">
        <div class="card-meta-item">
          <span class="meta-val" id="flow-val-${p.id}">${p.flow} mL/h</span>
          <span class="meta-key">Flow Rate</span>
        </div>
        <div class="card-meta-item">
          <span class="meta-val" id="time-val-${p.id}">${p.timeLeft}</span>
          <span class="meta-key">Time Left</span>
        </div>
        <div class="card-meta-item">
          <span class="meta-val" style="font-size:11px;color:var(--ink-muted)">${p.deviceId}</span>
          <span class="meta-key">Device</span>
        </div>
      </div>

      <div class="card-actions">
        <button
          class="card-btn card-btn-flow ${p.motorOn ? 'active' : ''}"
          id="btn-flow-${p.id}"
          data-patient="${p.id}"
          data-action="flow"
        >▶ FLOW</button>
        <button
          class="card-btn card-btn-stop ${!p.motorOn ? 'active' : ''}"
          id="btn-stop-${p.id}"
          data-patient="${p.id}"
          data-action="stop"
        >⏹ STOP</button>
      </div>
    `;

    grid.appendChild(card);
  });

  // Attach card button events
  document.querySelectorAll('.card-btn').forEach(btn => {
    btn.addEventListener('click', handleCardAction);
  });
}

/**
 * Handles FLOW / STOP button on patient card.
 * Sends command to Flask backend (or logs if disconnected).
 * Backend → ESP32 via servo state API.
 */
function handleCardAction(e) {
  const btn = e.currentTarget;
  const patientId = parseInt(btn.dataset.patient);
  const action = btn.dataset.action;
  const patient = PATIENTS.find(p => p.id === patientId);
  if (!patient) return;

  const isFlow = (action === 'flow');
  patient.motorOn = isFlow;

  // Update button states
  const flowBtn = document.getElementById(`btn-flow-${patientId}`);
  const stopBtn = document.getElementById(`btn-stop-${patientId}`);
  flowBtn.classList.toggle('active', isFlow);
  stopBtn.classList.toggle('active', !isFlow);

  // ── Flask backend call (uncomment when connected) ──
  // fetch(`/api/servo-command`, {
  //   method: 'POST',
  //   headers: { 'Content-Type': 'application/json' },
  //   body: JSON.stringify({ patient_id: patientId, state: isFlow ? 'FLOW' : 'STOP' })
  // });

  console.log(`[IV Monitor] Patient ${patient.name} (${patient.deviceId}) → ${action.toUpperCase()}`);
}


/* ── 8. ALERTS RENDERER ─────────────────────────────────── */
function renderAlerts() {
  const list = document.getElementById('alertsList');
  if (!list) return;

  ALERTS_DATA.forEach((alert, i) => {
    const item = document.createElement('div');
    item.className = `alert-item ${alert.type}`;
    item.style.animationDelay = `${i * 0.07}s`;
    item.innerHTML = `
      <span class="alert-badge">${alert.badge}</span>
      <div class="alert-info">
        <div class="alert-title">${alert.title}</div>
        <div class="alert-detail">${alert.detail}</div>
      </div>
      <span class="alert-time">${alert.time}</span>
    `;
    list.appendChild(item);
  });
}


/* ── 9. PATIENT SEARCH ──────────────────────────────────── */
document.getElementById('patientSearch')?.addEventListener('input', (e) => {
  const q = e.target.value.toLowerCase();
  const filtered = PATIENTS.filter(p =>
    p.name.toLowerCase().includes(q) ||
    p.bed.toLowerCase().includes(q) ||
    p.solution.toLowerCase().includes(q)
  );
  renderPatientCards(filtered);
});


/* ── 10. MODAL — ADD PATIENT ──────────────────────────────── */
const modalOverlay  = document.getElementById('modalOverlay');
const modalClose    = document.getElementById('modalClose');
const modalCancel   = document.getElementById('modalCancelBtn');
const modalSubmit   = document.getElementById('modalSubmitBtn');
const addPatientBtn = document.getElementById('addPatientBtn');

function openModal() { modalOverlay.classList.add('open'); }
function closeModal() { modalOverlay.classList.remove('open'); }

addPatientBtn?.addEventListener('click', openModal);
modalClose?.addEventListener('click', closeModal);
modalCancel?.addEventListener('click', closeModal);
modalOverlay?.addEventListener('click', (e) => { if (e.target === modalOverlay) closeModal(); });

modalSubmit?.addEventListener('click', () => {
  const name     = document.getElementById('inp-name')?.value.trim();
  const bed      = document.getElementById('inp-bed')?.value.trim();
  const bagVol   = parseInt(document.getElementById('inp-volume')?.value) || 500;
  const solution = document.getElementById('inp-solution')?.value;

  if (!name || !bed) {
    alert('Please fill in name and bed number.');
    return;
  }

  const newPatient = {
    id: Date.now(), name, bed, solution,
    volume: 100, flow: 40,
    timeLeft: 'Calculating…',
    status: 'ok', motorOn: false,
    deviceId: 'ESP32-NEW',
    bagVolume: bagVol
  };

  PATIENTS.push(newPatient);
  renderPatientCards(PATIENTS);
  closeModal();

  // ── Flask backend call ──
  // fetch('/api/patients', {
  //   method: 'POST',
  //   headers: { 'Content-Type': 'application/json' },
  //   body: JSON.stringify({ name, bed, flow_rate: flow, solution })
  // });

  console.log('[IV Monitor] New patient added:', newPatient);

  // Auto-switch to patients tab
  switchTab('patients');
});


/* ── 11. CHART.JS — DASHBOARD FLOW CHART ───────────────── */
let flowChartInstance = null;

function initFlowChart() {
  const canvas = document.getElementById('flowChart');
  if (!canvas) return;

  const labels = ['00:00','00:05','00:10','00:15','00:20','00:25','00:30'];
  const datasets = PATIENTS.slice(0, 4).map((p, i) => ({
    label: p.name.split(' ')[0],
    data: Array.from({ length: 7 }, () => p.flow + (Math.random() - 0.5) * 10),
    borderColor: ['#0E0E0E', '#9DBF72', '#FFB800', '#00E5FF'][i],
    backgroundColor: 'transparent',
    tension: 0.4, borderWidth: 2, pointRadius: 0
  }));

  if (flowChartInstance) flowChartInstance.destroy();
  flowChartInstance = new Chart(canvas, {
    type: 'line',
    data: { labels, datasets },
    options: {
      responsive: true, animation: false,
      plugins: { legend: { labels: { font: { family: 'Space Mono', size: 10 }, boxWidth: 12 } } },
      scales: {
        x: { grid: { color: 'rgba(0,0,0,0.04)' }, ticks: { font: { family: 'Space Mono', size: 9 } } },
        y: {
          grid: { color: 'rgba(0,0,0,0.04)' },
          ticks: { font: { family: 'Space Mono', size: 9 } },
          title: { display: true, text: 'mL/h', font: { family: 'Space Mono', size: 9 } }
        }
      }
    }
  });
}

/* Simulate live chart updates */
function simulateLiveData() {
  if (!flowChartInstance) return;
  const now = new Date().toLocaleTimeString('en-GB', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
  flowChartInstance.data.labels.push(now);
  if (flowChartInstance.data.labels.length > 12) flowChartInstance.data.labels.shift();

  flowChartInstance.data.datasets.forEach((ds, i) => {
    const base = PATIENTS[i]?.flow || 40;
    ds.data.push(Math.max(10, base + (Math.random() - 0.5) * 12));
    if (ds.data.length > 12) ds.data.shift();
  });
  flowChartInstance.update('none');
}

setInterval(simulateLiveData, 5000);


/* ── 12. CHART.JS — GRAPHS TAB CHARTS ──────────────────── */
let chartFlow = null, chartVolume = null;

function initGraphsCharts() {
  const labels = ['10m', '20m', '30m', '40m', '50m', '1h', '1h10m', '1h20m'];

  const cFlow = document.getElementById('chart-flow');
  const cVol  = document.getElementById('chart-volume');
  if (!cFlow || !cVol) return;

  if (chartFlow) chartFlow.destroy();
  if (chartVolume) chartVolume.destroy();

  const chartDefaults = {
    responsive: true,
    plugins: {
      legend: { labels: { font: { family: 'Space Mono', size: 10 }, boxWidth: 12 } }
    },
    scales: {
      x: { grid: { color: 'rgba(0,0,0,0.04)' }, ticks: { font: { family: 'Space Mono', size: 9 } } },
      y: { grid: { color: 'rgba(0,0,0,0.04)' }, ticks: { font: { family: 'Space Mono', size: 9 } } }
    }
  };

  chartFlow = new Chart(cFlow, {
    type: 'line',
    data: {
      labels,
      datasets: PATIENTS.map((p, i) => ({
        label: p.name.split(' ')[0],
        data: labels.map(() => p.flow + (Math.random()-0.5)*8),
        borderColor: ['#0E0E0E','#9DBF72','#FFB800','#00E5FF','#FF4444'][i],
        backgroundColor: 'transparent',
        tension: 0.4, borderWidth: 2, pointRadius: 2
      }))
    },
    options: chartDefaults
  });

  chartVolume = new Chart(cVol, {
    type: 'line',
    data: {
      labels,
      datasets: PATIENTS.map((p, i) => ({
        label: p.name.split(' ')[0],
        data: labels.map((_, j) => Math.max(0, p.volume - j * (p.flow / 60 / 5 / 500 * 100))),
        borderColor: ['#0E0E0E','#9DBF72','#FFB800','#00E5FF','#FF4444'][i],
        backgroundColor: 'transparent',
        tension: 0.4, borderWidth: 2, pointRadius: 2
      }))
    },
    options: chartDefaults
  });

  // Populate graph patient select
  const sel = document.getElementById('graphPatientSelect');
  if (sel) {
    PATIENTS.forEach(p => {
      const opt = document.createElement('option');
      opt.value = p.id; opt.textContent = p.name;
      sel.appendChild(opt);
    });
  }
}


/* ── 13. SOCKET.IO — REAL-TIME BACKEND ─────────────────── */
/**
 * Uncomment the block below to connect to Flask-SocketIO backend.
 * The server emits 'sensor_update' events with live weight/flow data.
 *
 * Expected payload from backend:
 * { patient_id: 1, weight_g: 320, volume_pct: 64, flow_rate: 42, time_left: "2h 30m" }
 */

/*
const socket = io('http://localhost:5000');

socket.on('connect', () => {
  console.log('[Socket.IO] Connected to IV Monitor backend');
});

socket.on('sensor_update', (data) => {
  console.log('[Socket.IO] Live update:', data);
  updatePatientCard(data);
});

socket.on('disconnect', () => {
  console.warn('[Socket.IO] Disconnected from backend');
});
*/

/**
 * Call this when real socket data arrives.
 * @param {Object} data - { patient_id, volume_pct, flow_rate, time_left }
 */
function updatePatientCard(data) {
  const { patient_id, volume_pct, flow_rate, time_left } = data;
  const patient = PATIENTS.find(p => p.id === patient_id);
  if (!patient) return;

  // Update local state
  patient.volume   = volume_pct;
  patient.flow     = flow_rate;
  patient.timeLeft = time_left;

  // Update DOM live (no full re-render)
  const pctEl  = document.getElementById(`vol-pct-${patient_id}`);
  const barEl  = document.getElementById(`vol-bar-${patient_id}`);
  const flowEl = document.getElementById(`flow-val-${patient_id}`);
  const timeEl = document.getElementById(`time-val-${patient_id}`);

  if (pctEl)  pctEl.textContent  = `${volume_pct}%`;
  if (barEl)  { barEl.style.width = `${volume_pct}%`; barEl.className = `volume-fill ${getVolumeClass(volume_pct)}`; }
  if (flowEl) flowEl.textContent  = `${flow_rate} mL/h`;
  if (timeEl) timeEl.textContent  = time_left;

  // Show alert banner if critical
  if (volume_pct <= 20) {
    const banner = document.getElementById('alertBanner');
    if (banner) {
      banner.style.display = 'flex';
      banner.querySelector('.alert-msg').innerHTML =
        `Patient <strong>${patient.name}</strong> (Bed ${patient.bed}) — IV volume critical: <strong>${volume_pct}%</strong> remaining.`;
    }
  }

  // Update stats
  updateDashboardStats();
}


/* ── 14. DASHBOARD STATS ─────────────────────────────────── */
function updateDashboardStats() {
  const active    = PATIENTS.filter(p => p.motorOn).length;
  const alertsN   = PATIENTS.filter(p => p.volume <= 40).length;

  const sa = document.getElementById('stat-active');
  const al = document.getElementById('stat-alerts');
  const sp = document.getElementById('stat-patients');
  if (sa) sa.textContent = active;
  if (al) al.textContent = alertsN;
  if (sp) sp.textContent = PATIENTS.length;
}

/* ── 15. SIMULATE LIVE VOLUME DROPS (for demo) ─────────── */
function simulateVolumeDrop() {
  PATIENTS.forEach(p => {
    if (p.motorOn && p.volume > 0) {
      p.volume = Math.max(0, p.volume - (p.flow / 60 / 500 * 100 * 5));
      p.volume = Math.round(p.volume * 10) / 10;
      // Feed into card updater
      updatePatientCard({
        patient_id: p.id,
        volume_pct: Math.round(p.volume),
        flow_rate: p.flow,
        time_left: p.timeLeft
      });
    }
  });
}
setInterval(simulateVolumeDrop, 5000); // every 5s


/* ── 16. INIT EVERYTHING ────────────────────────────────── */
document.addEventListener('DOMContentLoaded', () => {
  renderPatientCards(PATIENTS);
  renderAlerts();
  initFlowChart();
  initGraphsCharts();
  updateDashboardStats();

  // Pre-render reveal for initial dashboard
  setTimeout(() => triggerReveal(document.getElementById('view-dashboard')), 300);
});
