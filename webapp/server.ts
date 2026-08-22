import "dotenv/config";
import express from "express";
import http from "http";
import path from "path";
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

interface WorkerRecord {
  workerId: string;
  name: string;
  ip: string;
  state: 'RUNNING' | 'STOPPED' | 'ERROR' | 'RESTARTING' | 'OFFLINE';
  threads: number;
  maxCores: number;
  hashrateMhs: number;
  tempC: number;
  cpuFreqMhz: number;
  sharesFound: number;
  sharesAccepted: number;
  sharesRejected: number;
  uptime: number;
  lastSeen: number;
  isSimulated?: boolean;
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

let globalPool = {
  url: "stratum+tcp://solo.ckpool.org:3333",
  user: "bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh.s905x",
  pass: "x",
  name: "Solo CKPool",
  isSolo: true,
};

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
    // Also apply to simulated workers if active
    for (const sim of simulatedWorkers.values()) {
      sim.handleCommand({ ...command, workerId: sim.id });
    }
    return sentCount > 0 || simulatedWorkers.size > 0;
  }

  const ws = workerSockets.get(workerId);
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(command));
    return true;
  }

  const sim = simulatedWorkers.get(workerId);
  if (sim) {
    sim.handleCommand(command);
    return true;
  }

  return false;
}

// Simulated Worker Class for zero-setup realistic testing
class SimulatedWorker {
  id: string;
  name: string;
  threads: number = 4;
  state: 'RUNNING' | 'STOPPED' | 'ERROR' | 'RESTARTING' = 'RUNNING';
  temp: number = 54.0;
  targetTemp: number = 62.0;
  freq: number = 1512;
  sharesFound: number = 0;
  sharesAccepted: number = 0;
  sharesRejected: number = 0;
  uptime: number = 0;
  timer: NodeJS.Timeout | null = null;
  baseHashratePerThread: number = 1.05; // 4.2 MH/s on 4 cores Cortex-A53 HW crypto

  constructor(id: string, name: string) {
    this.id = id;
    this.name = name;
    this.startLoop();
  }

  startLoop() {
    this.timer = setInterval(() => {
      this.uptime += 2;
      
      // Calculate realistic thermal and frequency dynamics
      if (this.state === 'RUNNING') {
        this.targetTemp = 52.0 + this.threads * 3.2 + (Math.random() * 1.5 - 0.75);
        this.temp += (this.targetTemp - this.temp) * 0.15;
        this.freq = this.temp > 78 ? 1200 : 1512; // Thermal throttle simulation
        
        // Hashrate calculation with slight jitter
        const jitter = (Math.random() * 0.08 - 0.04);
        const hashrate = Number((this.threads * this.baseHashratePerThread * (this.freq / 1512) + jitter).toFixed(2));
        
        // Share generation (~1 share every ~20-30 seconds per worker)
        if (Math.random() < 0.12) {
          this.sharesFound++;
          if (Math.random() < 0.98) {
            this.sharesAccepted++;
            this.log('SUCCESS', `Found share target matching difficulty! [Accepted]`);
          } else {
            this.sharesRejected++;
            this.log('WARN', `Share rejected by pool (stale job)`);
          }
        }

        this.updateTelemetry(hashrate);
      } else {
        // Cooling down
        this.temp += (42.0 - this.temp) * 0.1;
        this.updateTelemetry(0.0);
      }
    }, 2000);
  }

  updateTelemetry(hashrate: number) {
    const existing = workers.get(this.id);
    const now = Date.now();
    
    const hHist = existing ? [...existing.hashrateHistory] : [];
    hHist.push({ time: now, hashrate });
    if (hHist.length > 30) hHist.shift();

    const tHist = existing ? [...existing.tempHistory] : [];
    tHist.push({ time: now, temp: Number(this.temp.toFixed(1)) });
    if (tHist.length > 30) tHist.shift();

    const telemetry: WorkerRecord = {
      workerId: this.id,
      name: this.name,
      ip: "192.168.1.1" + (Math.abs(hashCode(this.id)) % 80 + 10),
      state: this.state,
      threads: this.threads,
      maxCores: 4,
      hashrateMhs: hashrate,
      tempC: Number(this.temp.toFixed(1)),
      cpuFreqMhz: this.freq,
      sharesFound: this.sharesFound,
      sharesAccepted: this.sharesAccepted,
      sharesRejected: this.sharesRejected,
      uptime: this.uptime,
      lastSeen: now,
      isSimulated: true,
      arch: "aarch64 Cortex-A53 (Simulated)",
      hwCrypto: true,
      pool: { ...globalPool },
      hashrateHistory: hHist,
      tempHistory: tHist,
      recentLogs: existing?.recentLogs || [],
    };

    workers.set(this.id, telemetry);
    broadcastToClients({ type: "worker_update", worker: telemetry });
  }

  log(level: string, message: string) {
    const entry = {
      id: Math.random().toString(36).substring(2, 9),
      workerId: this.id,
      timestamp: Date.now(),
      level,
      message,
    };
    const existing = workers.get(this.id);
    if (existing) {
      existing.recentLogs.unshift(entry);
      if (existing.recentLogs.length > 50) existing.recentLogs.pop();
    }
    broadcastToClients({ type: "log", log: entry });
  }

  handleCommand(cmd: any) {
    const { action, params = {}, cmdId } = cmd;
    let message = "";
    
    if (action === "start") {
      this.state = "RUNNING";
      message = `Simulated worker ${this.name} started (${this.threads} threads)`;
      this.log('INFO', message);
    } else if (action === "stop") {
      this.state = "STOPPED";
      message = `Simulated worker ${this.name} stopped`;
      this.log('INFO', message);
    } else if (action === "restart") {
      this.state = "RESTARTING";
      this.log('INFO', `Restarting miner process...`);
      setTimeout(() => {
        this.state = "RUNNING";
        this.log('INFO', `Miner restarted successfully with ${this.threads} threads`);
      }, 800);
      message = `Miner restarted`;
    } else if (action === "set_threads") {
      this.threads = Math.max(1, Math.min(4, Number(params.threads || 4)));
      message = `Configured ${this.threads} threads on ${this.name}`;
      this.log('INFO', message);
    } else if (action === "set_pool") {
      if (params.pool) {
        globalPool = { ...globalPool, ...params.pool };
      }
      message = `Pool updated to ${params.pool?.url || globalPool.url}`;
      this.log('INFO', message);
    } else if (action === "rename") {
      this.name = params.name || this.name;
      message = `Worker renamed to ${this.name}`;
    }

    broadcastToClients({
      type: "command_ack",
      ack: {
        cmdId: cmdId || "cmd-" + Date.now(),
        workerId: this.id,
        status: "ok",
        message,
        timestamp: Date.now(),
      },
    });
  }

  destroy() {
    if (this.timer) clearInterval(this.timer);
    workers.delete(this.id);
    broadcastToClients({ type: "worker_offline", workerId: this.id });
  }
}

function hashCode(str: string) {
  let hash = 0;
  for (let i = 0; i < str.length; i++) {
    hash = (hash << 5) - hash + str.charCodeAt(i);
    hash |= 0;
  }
  return hash;
}

const simulatedWorkers = new Map<string, SimulatedWorker>();

// Initialize with 3 simulated S905X nodes out of the box for immediate previewing
function initDefaultSimulators() {
  simulatedWorkers.set("s905x-node-01", new SimulatedWorker("s905x-node-01", "S905X Rack Unit 1 (Living Room)"));
  simulatedWorkers.set("s905x-node-02", new SimulatedWorker("s905x-node-02", "S905X Rack Unit 2 (Lab Bench)"));
  simulatedWorkers.set("s905x-node-03", new SimulatedWorker("s905x-node-03", "S905X Rack Unit 3 (Cluster Node)"));
}

// When the first real S905X agent authenticates, retire the demo fleet so the
// dashboard reflects physical hardware instead of fabricated telemetry.
let realHardwareSeen = false;
function destroySimulatedFleet(reason: string) {
  for (const [id, sim] of simulatedWorkers.entries()) {
    sim.destroy();
    simulatedWorkers.delete(id);
  }
  if (simulatedWorkers.size === 0) {
    console.log(`[S905X Controller] Simulated fleet removed: ${reason}`);
  }
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

  // Initialize initial simulated workers
  initDefaultSimulators();

  // Watchdog timer: check every 2.5 seconds for dead real workers (> 8 seconds silent)
  setInterval(() => {
    const now = Date.now();
    for (const [id, worker] of workers.entries()) {
      if (!worker.isSimulated && worker.state !== 'OFFLINE' && now - worker.lastSeen > 8000) {
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

            if (!realHardwareSeen) {
              realHardwareSeen = true;
              destroySimulatedFleet(`real worker '${authenticatedWorkerId}' connected`);
              broadcastToClients({ type: "fleet_sync", workers: Array.from(workers.values()), poolConfig: globalPool });
            }

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
              tempC: 45.0,
              cpuFreqMhz: 1512,
              sharesFound: 0,
              sharesAccepted: 0,
              sharesRejected: 0,
              uptime: 0,
              lastSeen: now,
              isSimulated: false,
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
              w.tempC = data.tempC ?? w.tempC;
              w.cpuFreqMhz = data.cpuFreqMhz ?? w.cpuFreqMhz;
              w.sharesFound = data.sharesFound ?? w.sharesFound;
              w.sharesAccepted = data.sharesAccepted ?? w.sharesAccepted;
              w.sharesRejected = data.sharesRejected ?? w.sharesRejected;
              w.uptime = data.uptime ?? w.uptime;
              w.lastSeen = now;

              // Append histories
              w.hashrateHistory.push({ time: now, hashrate: w.hashrateMhs });
              if (w.hashrateHistory.length > 30) w.hashrateHistory.shift();

              w.tempHistory.push({ time: now, temp: w.tempC });
              if (w.tempHistory.length > 30) w.tempHistory.shift();

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
      connectedWorkers: workerSockets.size + simulatedWorkers.size,
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

  // Simulator Endpoints
  app.post("/api/simulator/spawn", requireApiAuth, (req, res) => {
    const count = simulatedWorkers.size + 1;
    const id = `s905x-sim-${count}`;
    const name = req.body.name || `S905X Node 0${count}`;
    const sim = new SimulatedWorker(id, name);
    simulatedWorkers.set(id, sim);
    res.json({ success: true, workerId: id, name });
  });

  app.post("/api/simulator/kill", requireApiAuth, (req, res) => {
    const id = req.body.workerId;
    const sim = simulatedWorkers.get(id);
    if (sim) {
      sim.destroy();
      simulatedWorkers.delete(id);
      res.json({ success: true, workerId: id });
    } else {
      res.status(404).json({ error: "Simulated worker not found" });
    }
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
  });
}

startServer();
