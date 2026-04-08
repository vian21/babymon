const socket = io();
const chartsContainer = document.getElementById("charts-container");
let charts = [];

const viewSelect = document.getElementById("view-select");
const notificationsDiv = document.getElementById("notifications");

const numericTypes = [
  "BODY_TEMPERATURE",
  "AMBIENT_TEMPERATURE",
  "HEART_RATE",
  "OXYGEN_SATURATION",
  "CO2_LEVEL",
  "HUMIDITY",
  "SOUND_LEVEL",
];
const notificationTypes = ["TELEMETRY_WARNING", "TELEMETRY_ALERT"];

function getRandomColor() {
  const letters = "0123456789ABCDEF";
  let color = "#";
  for (let i = 0; i < 6; i++) {
    color += letters[Math.floor(Math.random() * 16)];
  }
  return color;
}

function clearCharts() {
  charts.forEach((chart) => chart.destroy());
  charts = [];
  chartsContainer.innerHTML = "";
}

function createChart(type, data, view) {
  const canvas = document.createElement("canvas");
  chartsContainer.appendChild(canvas);
  const ctx = canvas.getContext("2d");

  let datasets = [];
  let chartTitle = type;

  if (view === "today" || view === "date") {
    datasets = [
      {
        label: type.replace(/_/g, " "),
        data: data.map((item) => ({
          x: new Date(item.timestamp * 1000),
          y: parseFloat(item.value),
        })),
        borderColor: getRandomColor(),
        fill: false,
      },
    ];
    chartTitle = type.replace(/_/g, " ");
  } else if (view === "summary") {
    // Handle notifications
    if (type === "notifications") {
      datasets = [
        {
          label: "Notifications",
          data: data.map((item) => ({ x: new Date(item.date), y: item.count })),
          backgroundColor: "rgba(255, 99, 132, 0.2)",
          borderColor: "rgba(255, 99, 132, 1)",
          type: "bar",
        },
      ];
    }
    // Handle avg/min/max charts (data will have one point with date and value)
    else {
      const metric = type.split(" ")[1]; // avg, min, or max
      const baseType = type.split(" ")[0]; // e.g., BODY_TEMPERATURE

      datasets = [
        {
          label: `${baseType} ${metric}`,
          data: data.map((item) => ({
            x: new Date(item.date),
            y: item[metric] || item.value,
          })),
          borderColor: getRandomColor(),
          fill: false,
        },
      ];
      chartTitle = `${baseType} ${metric}`;
    }
  }

  const chart = new Chart(ctx, {
    type: type.includes("notifications") ? "bar" : "line",
    data: { datasets },
    options: {
      responsive: true,
      scales: {
        x: {
          type: "time",
          time: {
            unit: view === "today" || view === "date" ? "hour" : "day",
            tooltipFormat: "ll",
          },
        },
        y: {
          beginAtZero: false,
        },
      },
      plugins: {
        title: {
          display: true,
          text: chartTitle,
          font: {
            size: 14,
          },
        },
        tooltip: {
          mode: "index",
          intersect: false,
        },
      },
    },
  });
  charts.push(chart);
}

function updateChart(data, notificationsData, view) {
  clearCharts();
  if (view === "today" || view === "date") {
    const grouped = {};
    data.forEach((item) => {
      if (numericTypes.includes(item.type)) {
        if (!grouped[item.type]) grouped[item.type] = [];
        grouped[item.type].push(item);
      }
    });
    Object.keys(grouped).forEach((type) => {
      createChart(type, grouped[type], view);
    });
  } else if (view === "summary") {
    const grouped = {};
    data.forEach((item) => {
      if (numericTypes.includes(item.type)) {
        if (!grouped[item.type]) grouped[item.type] = [];
        grouped[item.type].push(item);
      }
    });
    Object.keys(grouped).forEach((type) => {
      // Create separate charts for avg, min, max
      grouped[type].forEach((item) => {
        // Avg chart
        createChart(
          `${type} avg`,
          [{ date: item.date, avg: item.avg }],
          "summary",
        );
        // Min chart
        createChart(
          `${type} min`,
          [{ date: item.date, min: item.min }],
          "summary",
        );
        // Max chart
        createChart(
          `${type} max`,
          [{ date: item.date, max: item.max }],
          "summary",
        );
      });
    });
    // Add notifications chart
    createChart("notifications", notificationsData, view);
  }
}

function loadData(view, date = null) {
  let url = `/api/telemetry?view=${view}`;
  if (date) url += `&date=${date}`;
  fetch(url)
    .then((res) => res.json())
    .then((data) => {
      if (view === "summary") {
        fetch("/api/notifications_summary")
          .then((res) => res.json())
          .then((notificationsData) =>
            updateChart(data, notificationsData, view),
          )
          .catch((err) => console.error("Error fetching notifications:", err));
      } else {
        updateChart(data, [], view);
      }
    })
    .catch((err) => console.error("Error fetching data:", err));
}

function loadDates() {
  fetch("/api/dates")
    .then((res) => res.json())
    .then((dates) => {
      // Clear existing options except first two
      while (viewSelect.options.length > 2) {
        viewSelect.remove(2);
      }
      dates.forEach((date) => {
        const option = document.createElement("option");
        option.value = `date:${date}`;
        option.textContent = date;
        viewSelect.appendChild(option);
      });
    })
    .catch((err) => console.error("Error fetching dates:", err));
}

function addNotification(type, value, timestamp) {
  const div = document.createElement("div");
  div.className = "notification";
  div.innerHTML = `<strong>${type}:</strong> ${value} <small>${new Date(timestamp * 1000).toLocaleString()}</small>`;
  notificationsDiv.appendChild(div);
  notificationsDiv.scrollTop = notificationsDiv.scrollHeight;
}

viewSelect.addEventListener("change", () => {
  const value = viewSelect.value;
  if (value.startsWith("date:")) {
    const date = value.split(":")[1];
    loadData("date", date);
  } else {
    loadData(value);
  }
});

socket.on("telemetry", (data) => {
  if (notificationTypes.includes(data.type)) {
    addNotification(data.type, data.value, data.timestamp);
  }
  // Reload chart data for live update
  const value = viewSelect.value;
  if (value.startsWith("date:")) {
    const date = value.split(":")[1];
    loadData("date", date);
  } else {
    loadData(value);
  }
});

loadDates();
loadData("today");
