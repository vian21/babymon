const socket = io();
const chartsContainer = document.getElementById("charts-container");
const chartsSection = document.querySelector(".charts-section");
let charts = [];

const viewSelect = document.getElementById("view-select");
const notificationsDiv = document.getElementById("notifications");

const notificationTypes = ["TELEMETRY_WARNING", "TELEMETRY_ALERT"];

function groupByType(data) {
  return data.reduce((grouped, item) => {
    if (!notificationTypes.includes(item.type)) {
      if (!grouped[item.type]) grouped[item.type] = [];
      grouped[item.type].push(item);
    }
    return grouped;
  }, {});
}

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
  const chartsScrollTop = chartsSection ? chartsSection.scrollTop : 0;
  const notificationsScrollTop = notificationsDiv.scrollTop;
  const pageScrollTop = window.scrollY;

  clearCharts();
  if (view === "today" || view === "date") {
    const grouped = groupByType(data);
    Object.keys(grouped).forEach((type) => {
      createChart(type, grouped[type], view);
    });
  } else if (view === "summary") {
    const grouped = groupByType(data);
    Object.keys(grouped).forEach((type) => {
      ["avg", "min", "max"].forEach((metric) => {
        const metricData = grouped[type]
          .filter((item) => item[metric] !== undefined && item[metric] !== null)
          .map((item) => ({ date: item.date, [metric]: item[metric] }));
        if (metricData.length) {
          createChart(`${type} ${metric}`, metricData, "summary");
        }
      });
    });
    // Add notifications chart
    createChart("notifications", notificationsData, view);
  }

  requestAnimationFrame(() => {
    if (chartsSection) {
      chartsSection.scrollTop = chartsScrollTop;
    }
    notificationsDiv.scrollTop = notificationsScrollTop;
    window.scrollTo(0, pageScrollTop);
  });
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
