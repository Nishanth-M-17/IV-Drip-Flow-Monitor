const CONFIG = {
  WS_URL:          'ws://localhost:8080/ws',
  API_BASE:        'http://localhost:8080/api',
  TOTAL_VOLUME:    500,
  CRITICAL_PCT:    0.12,
  WARNING_PCT:     0.15,
  SIMULATE:        true,
  SIM_DRAIN_RATE:  0.06,
  SIM_DPM_BASE:    64,
  SIM_DPM_NOISE:   3,
};

let state = {
  volume:          72,
  dpm:             64,
  clamped:         false,
  criticalFired:   false,
  dpmHistory:      [],
  ws:              null,
};

for (let i = 0; i < 60; i++) {
  state.dpmHistory.push(CONFIG.SIM_DPM_BASE + (Math.random() * CONFIG.SIM_DPM_NOISE * 2 - CONFIG.SIM_DPM_NOISE));
}

const $ = id => document.getElementById(id);

function updateClock() {
  $('clockDisplay').textContent = new Date().toLocaleTimeString('en-IN', { hour12: false });
}
setInterval(updateClock, 1000);
updateClock();

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
      setWs
