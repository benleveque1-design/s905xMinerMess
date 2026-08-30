import "dotenv/config";
import express from "express";
import http from "http";
import path from "path";
import fs from "fs";
import crypto from "crypto";
import { WebSocketServer, WebSocket } from "ws";
import { createServer as createViteServer } from "vite";

const PORT = Number(process.env.PORT) || 3010;
const AUTH_TOKEN = process.env.WORKER_AUTH_TOKEN;

if (!AUTH_TOKEN) {
  console.error("[S905X Controller] FATAL: WORKER_AUTH_TOKEN is not set.");
  console.error("[S905X Controller] Set it via .env (WORKER_AUTH_TOKEN=<secret>) or export it, then restart.");
  process.exit(1);
}

// Constant-time token comparison (digests sidestep length leakage)
function tokenMatches(candidate: unknown): boolean {
  if (typeof candidate !== "string" || candidate.length === 0) return false;
  const a = crypto.createHash("sha256").update(candidate).digest();
  const b = crypto.createHash("sha256").update(AUTH_TOKEN!).digest();
  return crypto.timingSafeEqual(a, b);
}

// ---------------------------------------------------------------------------
// Persistent configuration — pool settings and BTC address survive restarts
// ---------------------------------------------------------------------------
const CONFIG_PATH = path.join(process.cwd(), "controller-config.json");

interface PersistentConfig {
  pool: {
    url: string;
    user: string;
    pass: string;
    name: string;
    isSolo: boolean;
  };
  btcAddress: string;
}

const DEFAULT_BTC_ADDRESS = "YOUR_BTC_ADDRESS";

const DEFAULT_CONFIG: PersistentConfig = {
  pool: {
    url: "stratum+tcp://pool.basedmining.xyz:3335",
    user: `${DEFAULT_BTC_ADDRESS}.s905x`,
    pass: "x",
    name: "Based Mining",
    isSolo: false,
  },
  btcAddress: DEFAULT_BTC_ADDRESS,
};

function loadConfig(): PersistentConfig {
  try {
    if (fs.existsSync(CONFIG_PATH)) {
      const raw = fs.readFileSync(CONFIG_PATH, "utf-8");
      const parsed = JSON.parse(raw);
      // Merge with defaults so new fields are always present
      return { ...DEFAULT_CONFIG, ...parsed, pool: { ...DEFAULT_CONFIG.pool, ...parsed.pool } };
    }
  } catch (err) {
    console.warn("[S905X Controller] Failed to load persistent config, using defaults:", err);
  }
  return { ...DEFAULT_CONFIG };
}

function saveConfig(config: PersistentConfig): void {
  try {
    fs.writeFileSync(CONFIG_PATH, JSON.stringify(config, null, 2) + "\n", "utf-8");
    console.log(`[S905X Controller] Config saved to ${CONFIG_PATH}`);
  } catch (err) {
    console.error("[S905X Controller] Failed to save config:", err);
  }
}

let persistentConfig = loadConfig();

// Override BTC address from env if set
if (process.env.POOL_USER) {
  persistentConfig.pool.user = process.env.POOL_USER;
  const addr = process.env.POOL_USER.split(".")[0];
  if (addr) persistentConfig.btcAddress = addr;
}
if (process.env.POOL_URL) {
  persistentConfig.pool.url = process.env.POOL_URL;
  persistentConfig.pool.isSolo = false;
  persistentConfig.pool.name = "Custom Pool";
}

interface WorkerRecord {
  workerId: string;
  name: string;
  ip: string;
  state: 'RUNNING' | 'STOPPED' | 'ERROR' | 'RESTARTING' | 'OFFLINE';
  threads: number;
  maxCores: number;
  hashrateMhs: number;
  tempC: number | null;
  cpuFreqMhz: number;
  sharesFound: number;
  sharesAccepted: number;
  sharesRejected: number;
  uptime: number;
  lastSeen: number;
  arch?: string;
  hwCrypto?: boolean;
  pool: {
    url: string;
    user: string;
    pass: string;
  };
  hashrateHistory: { time: number; hashrate: number }[];
  tempHistory: { time: number; temp: number }[];
  recentLogs: { id: string; workerId: string; timestamp: number; level: string; message: string }[];
}

// In-memory state store
const workers = new Map<string, WorkerRecord>();
const workerSockets = new Map<string, WebSocket>();
const clientSockets = new Set<WebSocket>();

let globalPool = { ...persistentConfig.pool };

// Broadcast message to all connected Web Dashboard clients
function broadcastToClients(msg: any) {
  const payload = JSON.stringify(msg);
  for (const client of clientSockets) {
    if (client.readyState === WebSocket.OPEN) {
      client.send(payload);
    }
  }
}

// Send command to a specific worker
function sendWorkerCommand(workerId: string, command: any): boolean {
  if (workerId === "all") {
    let sentCount = 0;
    for (const [id, ws] of workerSockets.entries()) {
      if (ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ ...command, workerId: id }));
        sentCount++;
      }
    }
    return sentCount > 0;
  }

  const ws = workerSockets.get(workerId);
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(command));
    return true;
  }

  return false;
}

async function startServer() {
  const app = express();
  app.use(express.json());

  // Auth gate for mutating (POST) REST endpoints. Read-only GETs stay open.
  const requireApiAuth: express.RequestHandler = (req, res, next) => {
    if (!tokenMatches(req.headers["x-auth-token"])) {
      res.status(401).json({ error: "Unauthorized: missing or invalid x-auth-token header" });
      return;
    }
    next();
  };

  const server = http.createServer(app);
  const wss = new WebSocketServer({ server });

  // Watchdog timer: check every 2.5 seconds for dead real workers (> 8 seconds silent)
  setInterval(() => {
    const now = Date.now();
    for (const [id, worker] of workers.entries()) {
      if (worker.state !== 'OFFLINE' && now - worker.lastSeen > 8000) {
        worker.state = 'OFFLINE';
        worker.hashrateMhs = 0.0;
        broadcastToClients({ type: "worker_offline", workerId: id });
        broadcastToClients({ type: "worker_update", worker });
      }
    }
  }, 2500);

  // WebSocket Connection Handler
  wss.on("connection", (ws: WebSocket, req) => {
    const urlPath = req.url || "/";
    let isWorker = urlPath.includes("/ws/worker");
    let authenticatedWorkerId: string | null = null;

    if (!isWorker) {
      // Dashboard UI Client
      clientSockets.add(ws);
      
      // Send initial full fleet snapshot to newly connected dashboard
      ws.send(JSON.stringify({
        type: "fleet_sync",
        workers: Array.from(workers.values()),
        poolConfig: globalPool,
      }));

      ws.on("message", (raw) => {
        try {
          const data = JSON.parse(raw.toString());
          if (data.type === "command") {
            // Keep controller-side pool state in sync when pools are set via the
            // dashboard WebSocket (same semantics as POST /api/pool).
            if (data.action === "set_pool" && data.params?.pool) {
              globalPool = { ...globalPool, ...data.params.pool };
              persistentConfig.pool = { ...globalPool };
              saveConfig(persistentConfig);
              broadcastToClients({ type: "pool_update", pool: globalPool });
            }
            const success = sendWorkerCommand(data.workerId, data);
            if (!success) {
              ws.send(JSON.stringify({
                type: "command_ack",
                ack: {
                  cmdId: data.cmdId,
                  workerId: data.workerId,
                  status: "error",
                  message: `Worker ${data.workerId} is offline or unreachable`,
                  timestamp: Date.now(),
                }
              }));
            }
          }
        } catch (err) {
          console.error("Error processing client message:", err);
        }
      });

      ws.on("close", () => {
        clientSockets.delete(ws);
      });

    } else {
      // S905X Worker Daemon Connection
      ws.on("message", (raw) => {
        try {
          const data = JSON.parse(raw.toString());
          const msgType = data.type;

          if (msgType === "auth") {
            if (!tokenMatches(data.token)) {
              console.warn("[S905X Controller] Rejected worker auth: invalid token");
              ws.close(4001, "Invalid Authentication Token");
              return;
            }

            authenticatedWorkerId = data.workerId;
            workerSockets.set(authenticatedWorkerId, ws);

            const now = Date.now();
            const existing = workers.get(authenticatedWorkerId);

            const workerRecord: WorkerRecord = {
              workerId: authenticatedWorkerId,
              name: data.name || authenticatedWorkerId,
              ip: req.socket.remoteAddress?.replace(/^.*:/, '') || '127.0.0.1',
              state: 'RUNNING',
              threads: data.threads || 4,
              maxCores: data.cores || 4,
              hashrateMhs: 0.0,
              tempC: null,
              cpuFreqMhz: 1512,
              sharesFound: 0,
              sharesAccepted: 0,
              sharesRejected: 0,
              uptime: 0,
              lastSeen: now,
              arch: data.arch || "aarch64 Cortex-A53",
              hwCrypto: data.hwCrypto ?? true,
              pool: globalPool,
              hashrateHistory: existing?.hashrateHistory || [],
              tempHistory: existing?.tempHistory || [],
              recentLogs: existing?.recentLogs || [],
            };

            workers.set(authenticatedWorkerId, workerRecord);

            // Ack auth back to worker with current pool & threads
            ws.send(JSON.stringify({
              type: "auth_ack",
              status: "ok",
              pool: globalPool,
              threads: workerRecord.threads,
            }));

            broadcastToClients({ type: "worker_update", worker: workerRecord });

          } else if (msgType === "telemetry" && authenticatedWorkerId) {
            const w = workers.get(authenticatedWorkerId);
            if (w) {
              const now = Date.now();
              w.state = data.state || 'RUNNING';
              w.name = data.name || w.name;
              w.threads = data.threads || w.threads;
              w.pool = data.pool || w.pool;
              w.hashrateMhs = data.hashrateMhs ?? 0.0;
              if (data.tempC !== undefined) w.tempC = data.tempC;
              w.cpuFreqMhz = data.cpuFreqMhz ?? w.cpuFreqMhz;
              w.sharesFound = data.sharesFound ?? w.sharesFound;
              w.sharesAccepted = data.sharesAccepted ?? w.sharesAccepted;
              w.sharesRejected = data.sharesRejected ?? w.sharesRejected;
              w.uptime = data.uptime ?? w.uptime;
              w.lastSeen = now;

              // Append histories
              w.hashrateHistory.push({ time: now, hashrate: w.hashrateMhs });
              if (w.hashrateHistory.length > 30) w.hashrateHistory.shift();

              if (typeof w.tempC === 'number') {
                w.tempHistory.push({ time: now, temp: w.tempC });
                if (w.tempHistory.length > 30) w.tempHistory.shift();
              }

              broadcastToClients({ type: "worker_update", worker: w });
            }

          } else if (msgType === "command_ack") {
            broadcastToClients({ type: "command_ack", ack: data });

          } else if (msgType === "log" && authenticatedWorkerId) {
            const entry = {
              id: Math.random().toString(36).substring(2, 9),
              workerId: authenticatedWorkerId,
              timestamp: Date.now(),
              level: data.level || "INFO",
              message: data.message || "",
            };
            const w = workers.get(authenticatedWorkerId);
            if (w) {
              w.recentLogs.unshift(entry);
              if (w.recentLogs.length > 50) w.recentLogs.pop();
            }
            broadcastToClients({ type: "log", log: entry });
          }

        } catch (err) {
          console.error("Worker WS parse error:", err);
        }
      });

      ws.on("close", () => {
        if (authenticatedWorkerId) {
          workerSockets.delete(authenticatedWorkerId);
          const w = workers.get(authenticatedWorkerId);
          if (w) {
            w.state = "OFFLINE";
            w.hashrateMhs = 0.0;
            broadcastToClients({ type: "worker_offline", workerId: authenticatedWorkerId });
            broadcastToClients({ type: "worker_update", worker: w });
          }
        }
      });
    }
  });

  // REST API Endpoints
  app.get("/api/health", (req, res) => {
    res.json({
      status: "ok",
      server: "S905X Mining Controller",
      connectedWorkers: workerSockets.size,
      connectedDashboards: clientSockets.size,
      timestamp: Date.now(),
    });
  });

  app.get("/api/workers", (req, res) => {
    res.json(Array.from(workers.values()));
  });

  app.post("/api/workers/:id/command", requireApiAuth, (req, res) => {
    const workerId = req.params.id;
    const command = {
      type: "command",
      cmdId: req.body.cmdId || "cmd-" + Date.now(),
      workerId,
      action: req.body.action,
      params: req.body.params || {},
    };

    const sent = sendWorkerCommand(workerId, command);
    res.json({ success: sent, command });
  });

  app.get("/api/pool", (req, res) => {
    res.json(globalPool);
  });

  app.post("/api/pool", requireApiAuth, (req, res) => {
    globalPool = { ...globalPool, ...req.body };
    // Persist pool config
    persistentConfig.pool = { ...globalPool };
    saveConfig(persistentConfig);
    // Broadcast to all workers
    sendWorkerCommand("all", {
      type: "command",
      cmdId: "pool-update-" + Date.now(),
      action: "set_pool",
      params: { pool: globalPool },
    });
    broadcastToClients({ type: "pool_update", pool: globalPool });
    res.json({ success: true, pool: globalPool });
  });

  // Persistent config endpoints (BTC address, etc.)
  app.get("/api/config", (req, res) => {
    res.json({ btcAddress: persistentConfig.btcAddress });
  });

  app.post("/api/config", requireApiAuth, (req, res) => {
    if (req.body.btcAddress) {
      persistentConfig.btcAddress = req.body.btcAddress;
      saveConfig(persistentConfig);
    }
    res.json({ success: true, btcAddress: persistentConfig.btcAddress });
  });

  // Safe controller restart — clean exit triggers systemd Restart=always
  app.post("/api/restart", requireApiAuth, (req, res) => {
    res.json({ success: true, message: "Controller restarting..." });
    console.log("[S905X Controller] Restart requested via API — exiting cleanly");
    // Give the response a moment to reach the client before exiting
    setTimeout(() => process.exit(0), 500);
  });

  // Vite middleware for development & SPA static hosting for production
  if (process.env.NODE_ENV !== "production") {
    const vite = await createViteServer({
      server: { middlewareMode: true },
      appType: "spa",
    });
    app.use(vite.middlewares);
  } else {
    const distPath = path.join(process.cwd(), "dist");
    app.use(express.static(distPath));
    app.get("*", (req, res) => {
      res.sendFile(path.join(distPath, "index.html"));
    });
  }

  server.listen(PORT, "0.0.0.0", () => {
    console.log(`[S905X Controller] Server listening on http://0.0.0.0:${PORT}`);
    console.log(`[S905X Controller] Worker WS Endpoint: ws://<HOST>:${PORT}/ws/worker`);
    console.log(`[S905X Controller] Dashboard WS Endpoint: ws://<HOST>:${PORT}/ws/client`);
    console.log(`[S905X Controller] Pool: ${globalPool.url} | BTC: ${persistentConfig.btcAddress}`);
  });
}

startServer();
