// ---------- Connection target ----------
// Instead of a hardcoded IP, default to the host that served this page.
// If the ESP32 serves this file itself (recommended), this just works,
// including over mDNS (e.g. http://esp32.local/).
// A manual override is kept in localStorage for local testing / fallback.

const DEFAULT_HOST = "10.136.1.210" || "esp32.local";
const STORAGE_KEY = "smart-storage-host";

function getHost() {
  return localStorage.getItem(STORAGE_KEY) || DEFAULT_HOST;
}

function wsUrl() {
  return `ws://${getHost()}/ws`;
}

// ---------- Elements ----------
const statusEl = document.getElementById("status");
const connLed = document.getElementById("connLed");
const ackEl = document.getElementById("ack");
const schematic = document.getElementById("schematic");
const modeSwitch = document.getElementById("modeSwitch");
const controlSection = document.getElementById("control");
const logStrip = document.querySelector(".log-strip");
const autoStatus = document.getElementById("autoStatus");
const autoSummaryEl = document.getElementById("autoSummary");

const settingsToggle = document.getElementById("settingsToggle");
const settingsPanel = document.getElementById("settingsPanel");
const hostInput = document.getElementById("hostInput");
const hostSave = document.getElementById("hostSave");
const hostReset = document.getElementById("hostReset");

const actuators = {
    vent:{
        button: document.getElementById("ventButton"),
        led: document.getElementById("ledVent"),
        state: document.getElementById("stateVent"),      
        onLabel: "OPEN",  offLabel: "CLOSED",
        cmdOn: "vent_open",       
        cmdOff: "vent_close"
    },
    fan:{
        button: document.getElementById("fanButton"), 
        led: document.getElementById("ledFan"), 
        state: document.getElementById("stateFan"),       
        onLabel: "ON",    offLabel: "OFF",    
        cmdOn: "fan_on",          
        cmdOff: "fan_off"
    },
    peltier:{
        button: document.getElementById("peltierButton"),     
        led: document.getElementById("ledPeltier"),    
        state: document.getElementById("statePeltier"),    
        onLabel: "ON",   
        offLabel: "OFF",    
        cmdOn: "peltier_on",
        cmdOff: "peltier_off"
    },
    humidifier:{
        button: document.getElementById("humidifierButton"),  
        led: document.getElementById("ledHumidifier"), 
        state: document.getElementById("stateHumidifier"), 
        onLabel: "ON",   
        offLabel: "OFF",    
        cmdOn: "humidifier_on",   
        cmdOff: "humidifier_off"
    },
};

// Per-actuator elements in the Auto Status panel (identifier + reason for what triggered it)
const autoItems = {
    vent:{
        led: document.getElementById("autoLedVent"),
        reason: document.getElementById("autoStateVent")
    },
    fan:{
        led: document.getElementById("autoLedFan"),
        reason: document.getElementById("autoStateFan")
    },
    peltier:{
        led: document.getElementById("autoLedPeltier"),
        reason: document.getElementById("autoStatePeltier")
    },
    humidifier:{
        led: document.getElementById("autoLedHumidifier"),
        reason: document.getElementById("autoStateHumidifier")
    }
};

// ---------- WebSocket with auto-reconnect ----------
let ws;
let reconnectDelay = 1000;
const MAX_RECONNECT_DELAY = 15000;

function connect() {
  statusEl.textContent = `Connecting to ${getHost()}…`;
  ws = new WebSocket(wsUrl());

  ws.onopen = () => {
    statusEl.textContent = `Connected (${getHost()})`;
    connLed.classList.add("on");
    schematic.classList.add("live");
    reconnectDelay = 1000;
  };

  ws.onclose = () => {
    statusEl.textContent = "Disconnected — retrying…";
    connLed.classList.remove("on");
    schematic.classList.remove("live");
    scheduleReconnect();
  };

  ws.onerror = () => {
    ws.close();
  };

  ws.onmessage = handleMessage;
}

function scheduleReconnect() {
  setTimeout(connect, reconnectDelay);
  reconnectDelay = Math.min(reconnectDelay * 1.6, MAX_RECONNECT_DELAY);
}

function send(cmd) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(cmd);
  }
}

// ---------- Incoming messages ----------
function handleMessage(event) {
  if (event.data.startsWith("ACK:")) {
    logAck(event.data);
    return;
  }

  let data;
  try {
    data = JSON.parse(event.data);
  } catch (e) {
    logAck(`Unrecognized message: ${event.data}`);
    return;
  }

  document.getElementById("date").textContent = data.date ?? "--";
  document.getElementById("time").textContent = data.time ?? "--:--:--";
  document.getElementById("temp").textContent = data.temperature ?? "--";
  document.getElementById("hum").textContent = data.humidity ?? "--";

  setGauge("gaugeTemp", data.temperature, 0, 50);
  setGauge("gaugeHum", data.humidity, 0, 100);

  // Keep the segmented control / visible panels in sync with the device's
  // authoritative mode (useful if more than one browser tab is connected).
  if (data.mode) {
    applyMode(data.mode);
  }

  setActuatorState("vent", data.servoState == "1");
  setActuatorState("fan", data.fanState == "1");
  setActuatorState("peltier", data.peltierState == "1");
  setActuatorState("humidifier", data.humidifierState == "1");

  // Auto-mode identifier + reason for each actuator (e.g. "humidity>95%")
  setAutoItem("vent", data.servoState == "1");
  setAutoItem("fan", data.fanState == "1");
  setAutoItem("peltier", data.peltierState == "1");
  setAutoItem("humidifier", data.humidifierState == "1");

  autoSummaryEl.textContent = data.autoSummary && data.autoSummary.length ? data.autoSummary : "all readings within range";
}

function setGauge(id, value, min, max) {
  const el = document.getElementById(id);
  if (!el || value === undefined || value === null || isNaN(value)) return;
  const pct = Math.max(0, Math.min(100, ((value - min) / (max - min)) * 100));
  el.style.setProperty("--pct", pct.toFixed(1));
}

function setActuatorState(key, isOn) {
  const a = actuators[key];
  if (!a) return;
  a.button.setAttribute("aria-checked", String(isOn));
  a.state.textContent = isOn ? a.onLabel : a.offLabel;
  a.led.classList.toggle("on", isOn);
  a.button.closest(".module").classList.toggle("active", isOn);
}

// Updates the Auto Status panel: which actuator is active (LED) and why (reason text)
function setAutoItem(key, isOn) {
  const item = autoItems[key];
  if (!item) return;
  item.led.classList.toggle("on", isOn);
  item.led.closest(".auto-item").classList.toggle("active", isOn);
  item.reason.textContent = isOn ? (key == "vent" ? "open" : "active") : (key == "vent" ? "closed" : "idle");
}

function logAck(message) {
  ackEl.textContent = message;
}

// ---------- Actuator buttons ----------
Object.entries(actuators).forEach(([key, a]) => {
  a.button.addEventListener("click", () => {
    const turningOn = a.button.getAttribute("aria-checked") !== "true";
    send(turningOn ? a.cmdOn : a.cmdOff);
    // Optimistic UI update; server ACK / next state push will confirm.
    setActuatorState(key, turningOn);
  });
});

// ---------- Mode switch ----------
// Toggles which panel is visible: manual mode shows the actuator switches,
// auto mode shows the compact status panel (active actuator + reason).
function applyMode(mode) {
  document.querySelectorAll(".segment").forEach((s) => {
    const active = s.dataset.mode === mode;
    s.classList.toggle("active", active);
    s.setAttribute("aria-selected", String(active));
  });

  const manual = mode === "manual";
  controlSection.classList.toggle("hidden", !manual);
  logStrip.classList.toggle("hidden", !manual);
  autoStatus.classList.toggle("hidden", manual);
}

modeSwitch.addEventListener("click", (e) => {
  const btn = e.target.closest(".segment");
  if (!btn) return;

  const mode = btn.dataset.mode;
  applyMode(mode);
  send(mode);
});

// ---------- Connection settings ----------
settingsToggle.addEventListener("click", () => {
  settingsPanel.classList.toggle("hidden");
  hostInput.value = getHost();
  hostInput.placeholder = DEFAULT_HOST;
});

hostSave.addEventListener("click", () => {
  const value = hostInput.value.trim();
  if (value) {
    localStorage.setItem(STORAGE_KEY, value);
    ws && ws.close();
    connect();
  }
});

hostReset.addEventListener("click", () => {
  localStorage.removeItem(STORAGE_KEY);
  hostInput.value = DEFAULT_HOST;
  ws && ws.close();
  connect();
});

// ---------- Start ----------
connect();