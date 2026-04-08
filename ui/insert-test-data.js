const Database = require("libsql");

// Connect to the database
const db = new Database("telemetry.sqlite");

// Current timestamp (approx)
const now = Math.floor(Date.now() / 1000);
const oneDay = 86400;
const oneHour = 3600;

// Test data for today (last 24 hours)
const todayData = [
  { type: "BODY_TEMPERATURE", value: "36.5", timestamp: now - 2 * oneHour },
  { type: "AMBIENT_TEMPERATURE", value: "22.3", timestamp: now - 4 * oneHour },
  { type: "HEART_RATE", value: "120", timestamp: now - 6 * oneHour },
  { type: "OXYGEN_SATURATION", value: "98", timestamp: now - 8 * oneHour },
  { type: "MOVEMENT", value: "1", timestamp: now - 10 * oneHour },
  { type: "HUMIDITY", value: "45", timestamp: now - 12 * oneHour },
  {
    type: "TELEMETRY_WARNING",
    value: "Low movement detected",
    timestamp: now - 1 * oneHour,
  },
];

// Test data for yesterday
const yesterdayData = [
  {
    type: "BODY_TEMPERATURE",
    value: "36.8",
    timestamp: now - oneDay - 2 * oneHour,
  },
  {
    type: "AMBIENT_TEMPERATURE",
    value: "23.1",
    timestamp: now - oneDay - 4 * oneHour,
  },
  { type: "HEART_RATE", value: "115", timestamp: now - oneDay - 6 * oneHour },
  { type: "MOVEMENT", value: "0", timestamp: now - oneDay - 8 * oneHour },
  {
    type: "TELEMETRY_ALERT",
    value: "No movement for 60 minutes",
    timestamp: now - oneDay - 1 * oneHour,
  },
];

// Test data for day before yesterday
const twoDaysAgoData = [
  {
    type: "BODY_TEMPERATURE",
    value: "37.0",
    timestamp: now - 2 * oneDay - 2 * oneHour,
  },
  {
    type: "AMBIENT_TEMPERATURE",
    value: "21.8",
    timestamp: now - 2 * oneDay - 4 * oneHour,
  },
  {
    type: "HEART_RATE",
    value: "125",
    timestamp: now - 2 * oneDay - 6 * oneHour,
  },
];

console.log("Inserting test data...");

const stmt = db.prepare(
  "INSERT INTO telemetry (type, value, timestamp) VALUES (?, ?, ?)",
);

[...todayData, ...yesterdayData, ...twoDaysAgoData].forEach((data) => {
  stmt.run(data.type, data.value, data.timestamp);
  console.log(
    `Inserted: ${data.type} = ${data.value} at ${new Date(data.timestamp * 1000).toISOString()}`,
  );
});

console.log("Test data insertion complete!");
console.log("You can now view the charts at http://localhost:3000");
console.log('- Select "Today" to see recent data');
console.log('- Select "Summary" to see daily statistics');
