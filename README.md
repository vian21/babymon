# Baby Monitor

### Configure the project

This project uses the ESP Component Manager for external dependencies (like `esp_websocket_client`). These will be downloaded automatically during the first build.

```
idf.py menuconfig
```

### Build and Flash

Build the project and flash it to the board, then run monitor tool to view serial output:

```
idf.py build flash monitor
```

(Replace PORT with the name of the serial port to use.)

(To exit the serial monitor, type `Ctrl-]`.)

### Telemetry UI Server

The project includes a telemetry visualization UI that runs as a separate Node.js server.

#### Prerequisites
- Node.js (version 14 or higher)
- npm

#### Installation
1. Navigate to the `ui` directory:
   ```
   cd ui
   ```

2. Install dependencies:
   ```
   npm install
   ```

#### Running the Server
Start the telemetry server on port 3000:
```
npm start
```

The server will:
- Serve the telemetry dashboard at `http://localhost:3000`
- Accept telemetry data via POST to `/api/telemetry`
- Provide real-time updates via WebSocket
- Store data in a local SQLite database (`telemetry.sqlite`)

#### Configuration
Update the ESP32 environment variables in `.env` to point to your server:
```
TELEMETRY_SERVER_IP=192.168.1.100  # IP of the machine running the UI server
TELEMETRY_SERVER_PORT=3000
```

#### Testing
You can test the server by sending a POST request:
```bash
curl -X POST http://localhost:3000/api/telemetry \
  -H "Content-Type: application/json" \
  -d '{"type":"MOVEMENT","value":"1","timestamp":1700000000}'
```

Then open `http://localhost:3000` in your browser to see the data visualized.
