const socket = io();
const chartsContainer = document.getElementById("charts-container");
const notificationsDiv = document.getElementById("notifications");
const themeToggle = document.getElementById("theme-toggle");

const STATUS_COLORS = {
  good: "#2ca25f",
  ok: "#f2b705",
  bad: "#d7263d",
  unknown: "#9aa4b2",
};

const notificationTypes = ["TELEMETRY_WARNING", "TELEMETRY_ALERT"];
const BINARY_TEXT_TYPES = ["MOVEMENT", "SMOKE_DETECTED"];
const latestTelemetryByType = {};

const SENSOR_CONFIG = {
  BODY_TEMPERATURE: {
    label: "Body Temperature",
    unit: "C",
    min: 34,
    max: 41,
    bands: [
      { min: 34, max: 36, level: "bad" },
      { min: 36, max: 36.5, level: "ok" },
      { min: 36.5, max: 37.5, level: "good" },
      { min: 37.5, max: 37.9, level: "ok" },
      { min: 37.9, max: 41, level: "bad" },
    ],
  },
  AMBIENT_TEMPERATURE: {
    label: "Ambient Temperature",
    unit: "C",
    min: 10,
    max: 40,
    bands: [
      { min: 10, max: 18, level: "bad" },
      { min: 19, max: 20, level: "ok" },
      { min: 20, max: 23, level: "good" },
      { min: 23, max: 25, level: "ok" },
      { min: 25, max: 40, level: "bad" },
    ],
  },
  HEART_RATE: {
    label: "Heart Rate",
    unit: "bpm",
    min: 60,
    max: 210,
    bands: [
      { min: 60, max: 70, level: "bad" },
      { min: 70, max: 80, level: "ok" },
      { min: 80, max: 180, level: "good" },
      { min: 180, max: 190, level: "ok" },
      { min: 190, max: 210, level: "bad" },
    ],
  },
  OXYGEN_SATURATION: {
    label: "Oxygen Saturation",
    unit: "%",
    min: 80,
    max: 100,
    bands: [
      { min: 80, max: 92, level: "bad" },
      { min: 92, max: 95, level: "ok" },
      { min: 95, max: 100, level: "good" },
    ],
  },
  CO2_LEVEL: {
    label: "CO2 Level",
    unit: "ppm",
    min: 300,
    max: 3000,
    bands: [
      { min: 300, max: 1000, level: "good" },
      { min: 1000, max: 1500, level: "ok" },
      { min: 1500, max: 3000, level: "bad" },
    ],
  },
  HUMIDITY: {
    label: "Humidity",
    unit: "%",
    min: 0,
    max: 100,
    bands: [
      { min: 0, max: 35, level: "bad" },
      { min: 35, max: 45, level: "ok" },
      { min: 45, max: 55, level: "good" },
      { min: 55, max: 60, level: "ok" },
      { min: 60, max: 100, level: "bad" },
    ],
  },
  MOVEMENT: {
    label: "Movement",
    unit: "",
    min: 0,
    max: 1,
  },
  SOUND_LEVEL: {
    label: "Sound Level",
    unit: "level",
    min: 0,
    max: 40000,
    bands: [
      { min: 0, max: 12000, level: "good" },
      { min: 12000, max: 25000, level: "ok" },
      { min: 25000, max: 40000, level: "bad" },
    ],
  },
  SMOKE_DETECTED: {
    label: "Smoke Detection",
    unit: "",
    min: 0,
    max: 1,
  },
};

const gaugeRegistry = {};

function deriveSmokeFallbackEntry() {
  // Prefer explicit smoke telemetry if available from firmware.
  if (latestTelemetryByType.SMOKE_DETECTED) {
    return null;
  }

  const co2 = Number.parseFloat(latestTelemetryByType.CO2_LEVEL?.value);
  const ambient = Number.parseFloat(latestTelemetryByType.AMBIENT_TEMPERATURE?.value);

  const hasCo2 = Number.isFinite(co2);
  const hasAmbient = Number.isFinite(ambient);
  if (!hasCo2 && !hasAmbient) {
    return null;
  }

  const smokeDetected = (hasCo2 && co2 >= 1600) || (hasAmbient && ambient >= 27);

  const co2Ts = Number(latestTelemetryByType.CO2_LEVEL?.timestamp) || 0;
  const ambientTs = Number(latestTelemetryByType.AMBIENT_TEMPERATURE?.timestamp) || 0;

  return {
    type: "SMOKE_DETECTED",
    value: smokeDetected ? "1" : "0",
    timestamp: Math.max(co2Ts, ambientTs),
  };
}

function refreshDerivedSmokeStatus() {
  const fallback = deriveSmokeFallbackEntry();
  if (fallback) {
    updateGauge(fallback.type, fallback.value, fallback.timestamp);
  }
}

const gaugeNeedlePlugin = {
  id: "gaugeNeedle",
  afterDatasetDraw(chart) {
    const value = chart.$value;
    const min = chart.$min;
    const max = chart.$max;
    if (value === null || value === undefined || Number.isNaN(value)) return;

    const meta = chart.getDatasetMeta(0);
    if (!meta || !meta.data || !meta.data.length) return;

    const arc = meta.data[0];
    const { ctx } = chart;
    const clamped = Math.max(min, Math.min(max, value));
    const ratio = (clamped - min) / (max - min || 1);
    const angle = -Math.PI + ratio * Math.PI;
    const radius = arc.outerRadius * 0.9;

    ctx.save();
    ctx.translate(arc.x, arc.y);
    ctx.rotate(angle);
    ctx.beginPath();
    ctx.moveTo(0, 0);
    ctx.lineTo(radius, 0);
    ctx.lineWidth = 3;
    const needleColor = getComputedStyle(document.body)
      .getPropertyValue("--needle-color")
      .trim();
    ctx.strokeStyle = needleColor || "#1f2937";
    ctx.stroke();
    ctx.beginPath();
    ctx.arc(0, 0, 5, 0, Math.PI * 2);
    ctx.fillStyle = needleColor || "#1f2937";
    ctx.fill();
    ctx.restore();
  },
};

Chart.register(gaugeNeedlePlugin);

function formatSensorValue(type, value, config) {
  if (value === null || value === undefined || Number.isNaN(value)) {
    return "--";
  }
  const rounded = Math.abs(value) >= 1000 ? Math.round(value) : value.toFixed(1);
  return `${rounded}${config.unit ? ` ${config.unit}` : ""}`;
}

function formatBinaryStatusText(type, value) {
  if (value === null || value === undefined || Number.isNaN(value)) {
    return "--";
  }

  const detected = value >= 0.5;
  if (type === "SMOKE_DETECTED") {
    return detected ? "SMOKE DETECTED" : "NORMAL";
  }
  if (type === "MOVEMENT") {
    return detected ? "MOVEMENT DETECTED" : "NO MOVEMENT";
  }

  return detected ? "DETECTED" : "NORMAL";
}

function statusForValue(config, value) {
  if (value === null || value === undefined || Number.isNaN(value)) {
    return "unknown";
  }
  const hit = config.bands.find((band) => value >= band.min && value < band.max);
  return hit ? hit.level : "bad";
}

function createGauge(type, config) {
  const card = document.createElement("article");
  card.className = "gauge-card";

  const title = document.createElement("h3");
  title.textContent = config.label;

  const gaugeWrap = document.createElement("div");
  gaugeWrap.className = "gauge-wrap";

  const canvas = document.createElement("canvas");
  gaugeWrap.appendChild(canvas);

  const valueEl = document.createElement("p");
  valueEl.className = "gauge-value";
  valueEl.textContent = "--";

  const statusEl = document.createElement("span");
  statusEl.className = "status-pill status-unknown";
  statusEl.textContent = "UNKNOWN";

  const updatedAtEl = document.createElement("p");
  updatedAtEl.className = "updated-at";
  updatedAtEl.textContent = "No data yet";

  card.appendChild(title);
  card.appendChild(gaugeWrap);
  card.appendChild(valueEl);
  card.appendChild(statusEl);
  card.appendChild(updatedAtEl);
  chartsContainer.appendChild(card);

  const bandData = config.bands.map((band) => Math.max(0, band.max - band.min));
  const bandColors = config.bands.map((band) => STATUS_COLORS[band.level]);

  const chart = new Chart(canvas.getContext("2d"), {
    type: "doughnut",
    data: {
      labels: config.bands.map((band) => band.level),
      datasets: [
        {
          data: bandData,
          backgroundColor: bandColors,
          borderWidth: 0,
        },
      ],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      rotation: 270,
      circumference: 180,
      cutout: "68%",
      plugins: {
        legend: { display: false },
        tooltip: { enabled: false },
      },
    },
  });

  chart.$min = config.min;
  chart.$max = config.max;
  chart.$value = null;

  gaugeRegistry[type] = {
    chart,
    config,
    valueEl,
    statusEl,
    updatedAtEl,
  };
}

function createBinaryStatusCard(type, config) {
  const card = document.createElement("article");
  card.className = "gauge-card";

  const title = document.createElement("h3");
  title.textContent = config.label;

  const valueEl = document.createElement("p");
  valueEl.className = "gauge-value";
  valueEl.textContent = "--";

  const statusEl = document.createElement("span");
  statusEl.className = "status-pill status-unknown";
  statusEl.textContent = "UNKNOWN";

  const updatedAtEl = document.createElement("p");
  updatedAtEl.className = "updated-at";
  updatedAtEl.textContent = "No data yet";

  card.appendChild(title);
  card.appendChild(valueEl);
  card.appendChild(statusEl);
  card.appendChild(updatedAtEl);
  chartsContainer.appendChild(card);

  gaugeRegistry[type] = {
    chart: null,
    config,
    valueEl,
    statusEl,
    updatedAtEl,
  };
}

function updateGauge(type, value, timestamp) {
  const gauge = gaugeRegistry[type];
  if (!gauge) return;

  const numericValue = Number.parseFloat(value);
  const status = statusForValue(gauge.config, numericValue);

  if (gauge.chart) {
    gauge.chart.$value = Number.isFinite(numericValue) ? numericValue : null;
    gauge.chart.update("none");
  }

  if (BINARY_TEXT_TYPES.includes(type)) {
    gauge.valueEl.textContent = formatBinaryStatusText(type, numericValue);
  } else {
    gauge.valueEl.textContent = formatSensorValue(type, numericValue, gauge.config);
  }
  gauge.statusEl.className = `status-pill status-${status}`;
  gauge.statusEl.textContent = status.toUpperCase();

  if (timestamp && Number(timestamp) > 0) {
    gauge.updatedAtEl.textContent = `Updated ${new Date(
      Number(timestamp) * 1000,
    ).toLocaleTimeString()}`;
  }
}

function renderAllGauges() {
  chartsContainer.innerHTML = "";
  Object.entries(SENSOR_CONFIG).forEach(([type, config]) => {
    if (BINARY_TEXT_TYPES.includes(type)) {
      createBinaryStatusCard(type, config);
    } else {
      createGauge(type, config);
    }
  });
}

function addNotification(type, value, timestamp) {
  const div = document.createElement("div");
  div.className = "notification";
  div.innerHTML = `<strong>${type}:</strong> ${value} <small>${new Date(timestamp * 1000).toLocaleString()}</small>`;
  notificationsDiv.appendChild(div);
}

function applyLatestTelemetry(data) {
  data.forEach((entry) => {
    if (!entry || !entry.type || notificationTypes.includes(entry.type)) return;
    if (!SENSOR_CONFIG[entry.type]) return;

    const current = latestTelemetryByType[entry.type];
    if (!current || Number(entry.timestamp) > Number(current.timestamp)) {
      latestTelemetryByType[entry.type] = entry;
    }
  });

  Object.entries(latestTelemetryByType).forEach(([type, item]) => {
    updateGauge(type, item.value, item.timestamp);
  });

  refreshDerivedSmokeStatus();
}

function loadLatestTelemetry() {
  fetch("/api/telemetry?view=today")
    .then((res) => res.json())
    .then((data) => applyLatestTelemetry(data))
    .catch((err) => console.error("Error fetching telemetry:", err));
}

function applyTheme(theme) {
  document.body.setAttribute("data-theme", theme);
  localStorage.setItem("ui-theme", theme);
  Object.values(gaugeRegistry).forEach((g) => g.chart?.update("none"));
}

function initTheme() {
  const saved = localStorage.getItem("ui-theme");
  if (saved === "dark" || saved === "light") {
    applyTheme(saved);
    return;
  }
  const prefersDark = window.matchMedia("(prefers-color-scheme: dark)").matches;
  applyTheme(prefersDark ? "dark" : "light");
}

themeToggle?.addEventListener("click", () => {
  const current = document.body.getAttribute("data-theme") || "light";
  applyTheme(current === "dark" ? "light" : "dark");
});

socket.on("telemetry", (data) => {
  if (notificationTypes.includes(data.type)) {
    addNotification(data.type, data.value, data.timestamp);
    return;
  }
  if (SENSOR_CONFIG[data.type]) {
    const current = latestTelemetryByType[data.type];
    if (!current || Number(data.timestamp) >= Number(current.timestamp)) {
      latestTelemetryByType[data.type] = data;
    }
    updateGauge(data.type, data.value, data.timestamp);
    if (
      data.type === "CO2_LEVEL" ||
      data.type === "AMBIENT_TEMPERATURE" ||
      data.type === "SMOKE_DETECTED"
    ) {
      refreshDerivedSmokeStatus();
    }
  }
});

renderAllGauges();
initTheme();
loadLatestTelemetry();
setInterval(loadLatestTelemetry, 10000);
