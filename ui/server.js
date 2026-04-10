const express = require("express");
const http = require("http");
const socketIo = require("socket.io");
const Database = require("libsql");
const cors = require("cors");
const path = require("path");

const app = express();
const server = http.createServer(app);
const io = socketIo(server, {
  cors: {
    origin: "*",
    methods: ["GET", "POST"],
  },
});

const PORT = process.env.PORT || 3000;

// Middleware
app.use(cors());
app.use(express.json());
app.use(express.static(__dirname));
app.use(
  "/vendor/chartjs",
  express.static(path.join(__dirname, "node_modules/chart.js/dist")),
);
app.use(
  "/vendor/chartjs-adapter",
  express.static(
    path.join(__dirname, "node_modules/chartjs-adapter-date-fns/dist"),
  ),
);
app.use(
  "/vendor/socket.io",
  express.static(path.join(__dirname, "node_modules/socket.io/client-dist")),
);

// LibSQL database
const db = new Database("telemetry.sqlite");

// Initialize database
async function initDB() {
  try {
    db.exec(`CREATE TABLE IF NOT EXISTS telemetry (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      type TEXT NOT NULL,
      value TEXT NOT NULL,
      timestamp INTEGER NOT NULL
    )`);
    console.log("Database initialized.");
  } catch (err) {
    console.error("Error initializing database:", err);
  }
}

initDB();

// API endpoint
app.post("/api/telemetry", (req, res) => {
  const { type, value, timestamp } = req.body;
  if (!type || !value || !timestamp) {
    return res
      .status(400)
      .json({ error: "Missing required fields: type, value, timestamp" });
  }

  try {
    const stmt = db.prepare(
      "INSERT INTO telemetry (type, value, timestamp) VALUES (?, ?, ?)",
    );
    stmt.run(type, value, timestamp);
    console.log(`Telemetry stored: ${type} - ${value} at ${timestamp}`);
    // Emit to all connected clients
    io.emit("telemetry", { type, value, timestamp });
    res.status(200).json({ success: true });
  } catch (err) {
    console.error("Error inserting telemetry:", err.message);
    return res.status(500).json({ error: "Failed to store telemetry" });
  }
});

// Fetch telemetry data
app.get("/api/telemetry", (req, res) => {
  const view = req.query.view;
  const date = req.query.date;
  try {
    let rows;
    if (view === "today") {
      const now = Math.floor(Date.now() / 1000);
      const yesterday = now - 86400;
      const stmt = db.prepare(
        "SELECT * FROM telemetry WHERE timestamp > ? ORDER BY timestamp ASC",
      );
      rows = stmt.all(yesterday);
    } else if (view === "summary") {
      const stmt = db.prepare(`SELECT 
        DATE(timestamp, 'unixepoch') as date,
        type,
        COUNT(*) as count,
        AVG(CAST(value AS REAL)) as avg,
        MIN(CAST(value AS REAL)) as min,
        MAX(CAST(value AS REAL)) as max
        FROM telemetry 
        WHERE type NOT IN ('TELEMETRY_WARNING', 'TELEMETRY_ALERT')
        GROUP BY date, type
        ORDER BY date ASC`);
      rows = stmt.all();
    } else if (view === "date" && date) {
      const stmt = db.prepare(
        "SELECT * FROM telemetry WHERE DATE(timestamp, 'unixepoch') = ? ORDER BY timestamp ASC",
      );
      rows = stmt.all(date);
    } else {
      return res.status(400).json({ error: "Invalid view or missing date" });
    }
    res.json(rows);
  } catch (err) {
    console.error("Error fetching telemetry:", err);
    res.status(500).json({ error: err.message });
  }
});

// Fetch notification summary
app.get("/api/notifications_summary", (req, res) => {
  try {
    const stmt = db.prepare(
      "SELECT DATE(timestamp, 'unixepoch') as date, COUNT(*) as count FROM telemetry WHERE type IN ('TELEMETRY_WARNING', 'TELEMETRY_ALERT') GROUP BY date ORDER BY date ASC",
    );
    const rows = stmt.all();
    res.json(rows);
  } catch (err) {
    console.error("Error fetching notification summary:", err);
    res.status(500).json({ error: err.message });
  }
});

// Fetch available dates
app.get("/api/dates", (req, res) => {
  try {
    const stmt = db.prepare(
      "SELECT DISTINCT DATE(timestamp, 'unixepoch') as date FROM telemetry ORDER BY date DESC",
    );
    const rows = stmt.all();
    res.json(rows.map((r) => r.date));
  } catch (err) {
    console.error("Error fetching dates:", err);
    res.status(500).json({ error: err.message });
  }
});

// Fetch available dates
app.get("/api/dates", (req, res) => {
  try {
    const result = db.execute(
      "SELECT DISTINCT DATE(timestamp, 'unixepoch') as date FROM telemetry ORDER BY date DESC",
    );
    res.json(result.rows.map((r) => r.date));
  } catch (err) {
    console.error("Error fetching dates:", err);
    res.status(500).json({ error: err.message });
  }
});

// Serve index.html
app.get("/", (req, res) => {
  res.sendFile(__dirname + "/index.html");
});

// WebSocket connection
io.on("connection", (socket) => {
  console.log("Client connected");
  socket.on("disconnect", () => {
    console.log("Client disconnected");
  });
});

server.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
});
