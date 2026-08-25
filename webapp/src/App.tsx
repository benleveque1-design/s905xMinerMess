import React, { useState, useEffect, useRef } from 'react';
import { 
  Header 
} from './components/Header';
import { 
  WorkerGrid 
} from './components/WorkerGrid';
import { 
  WorkerDetailsModal 
} from './components/WorkerDetailsModal';
import { 
  PoolConfigModal 
} from './components/PoolConfigModal';
import { 
  SimulatorModal 
} from './components/SimulatorModal';
import { 
  AgentInstallerModal 
} from './components/AgentInstallerModal';
import { 
  WorkerTelemetry, 
  PoolConfig, 
  FleetStats, 
  WsServerMessage 
} from './types';
import { DEFAULT_POOL } from './data/poolPresets';

// Worker auth token is intentionally NOT baked into static builds. All worker
// control traffic (commands, pool updates) flows over the authenticated
// dashboard WebSocket; mutating REST endpoints remain controller-side only.
const env = (import.meta as any).env || {};
const API_TOKEN: string | undefined = env.VITE_WORKER_AUTH_TOKEN;
const authHeaders = (): Record<string, string> =>
  API_TOKEN ? { 'x-auth-token': API_TOKEN } : {};

export default function App() {
  const [workers, setWorkers] = useState<WorkerTelemetry[]>([]);
  const [poolConfig, setPoolConfig] = useState<PoolConfig>(DEFAULT_POOL);
  const [wsConnected, setWsConnected] = useState<boolean>(false);
  const [selectedWorker, setSelectedWorker] = useState<WorkerTelemetry | null>(null);

  // Modals state
  const [isPoolModalOpen, setIsPoolModalOpen] = useState(false);
  const [isSimulatorModalOpen, setIsSimulatorModalOpen] = useState(false);
  const [isAgentModalOpen, setIsAgentModalOpen] = useState(false);

  // Toast feedback
  const [toastMessage, setToastMessage] = useState<{ text: string; type: 'success' | 'info' | 'error' } | null>(null);

  const wsRef = useRef<WebSocket | null>(null);

  const showToast = (text: string, type: 'success' | 'info' | 'error' = 'info') => {
    setToastMessage({ text, type });
    setTimeout(() => {
      setToastMessage((cur) => (cur?.text === text ? null : cur));
    }, 3500);
  };

  // Connect to Central Controller WebSocket
  useEffect(() => {
    let reconnectTimer: NodeJS.Timeout;
    let isUnmounted = false;

    function connectWs() {
      const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
      const host = window.location.host;
      const wsUrl = `${protocol}//${host}/ws/client`;

      const ws = new WebSocket(wsUrl);
      wsRef.current = ws;

      ws.onopen = () => {
        if (isUnmounted) return;
        setWsConnected(true);
        console.log('[Dashboard] Connected to controller WebSocket');
      };

      ws.onmessage = (event) => {
        try {
          const msg: WsServerMessage = JSON.parse(event.data);
          
          if (msg.type === 'fleet_sync' && msg.workers) {
            setWorkers(msg.workers);
            if (msg.poolConfig) setPoolConfig(msg.poolConfig);
          } else if (msg.type === 'worker_update' && msg.worker) {
            const updated = msg.worker;
            setWorkers((prev) => {
              const idx = prev.findIndex((w) => w.workerId === updated.workerId);
              if (idx >= 0) {
                const copy = [...prev];
                copy[idx] = updated;
                return copy;
              }
              return [...prev, updated];
            });
            // Update selected modal worker if viewing
            setSelectedWorker((curr) => (curr?.workerId === updated.workerId ? updated : curr));
          } else if (msg.type === 'worker_offline' && msg.workerId) {
            setWorkers((prev) =>
              prev.map((w) =>
                w.workerId === msg.workerId ? { ...w, state: 'OFFLINE', hashrateMhs: 0 } : w
              )
            );
          } else if (msg.type === 'command_ack' && msg.ack) {
            showToast(`[${msg.ack.workerId}] ${msg.ack.message}`, msg.ack.status === 'ok' ? 'success' : 'error');
          }
        } catch (err) {
          console.error('[Dashboard] Error parsing WS message:', err);
        }
      };

      ws.onclose = () => {
        if (isUnmounted) return;
        setWsConnected(false);
        reconnectTimer = setTimeout(connectWs, 2000);
      };

      ws.onerror = (err) => {
        console.warn('[Dashboard] WS Error:', err);
        ws.close();
      };
    }

    connectWs();

    return () => {
      isUnmounted = true;
      clearTimeout(reconnectTimer);
      if (wsRef.current) wsRef.current.close();
    };
  }, []);

  // Send Command to Worker(s).
  // Commands only travel over the dashboard WebSocket: mutating REST endpoints
  // require x-auth-token, which is deliberately NOT baked into static builds.
  const sendCommand = (workerId: string, action: string, params: any = {}) => {
    const cmdPayload = {
      type: 'command',
      cmdId: 'cmd-' + Date.now(),
      workerId,
      action,
      params,
    };

    const ws = wsRef.current;
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify(cmdPayload));
      return;
    }

    // Socket not open yet (initial connect or reconnect window): poll briefly
    // for it to open instead of failing over to an unauthenticated REST call.
    const startedAt = Date.now();
    const waiter = setInterval(() => {
      const cur = wsRef.current;
      if (cur && cur.readyState === WebSocket.OPEN) {
        clearInterval(waiter);
        cur.send(JSON.stringify(cmdPayload));
      } else if (Date.now() - startedAt > 5000) {
        clearInterval(waiter);
        showToast(`Failed to send "${action}" command: dashboard not connected`, 'error');
      }
    }, 250);
  };

  const handleStartWorker = (id: string) => sendCommand(id, 'start');
  const handleStopWorker = (id: string) => sendCommand(id, 'stop');
  const handleRestartWorker = (id: string) => sendCommand(id, 'restart');
  const handleSetThreads = (id: string, threads: number) => sendCommand(id, 'set_threads', { threads });
  const handleRenameWorker = (id: string, name: string) => sendCommand(id, 'rename', { name });

  const handleStartAll = () => {
    sendCommand('all', 'start');
    showToast('Sent START command to all S905X workers', 'info');
  };

  const handleStopAll = () => {
    sendCommand('all', 'stop');
    showToast('Sent STOP command to all S905X workers', 'info');
  };

  const handleRestartAll = () => {
    sendCommand('all', 'restart');
    showToast('Sent RESTART command to all S905X workers', 'info');
  };

  const handleSaveAndBroadcastPool = (newPool: PoolConfig) => {
    setPoolConfig(newPool);
    // Routed through the dashboard WebSocket (no baked auth token needed);
    // per-worker acks arrive via command_ack and confirm the broadcast.
    sendCommand('all', 'set_pool', { pool: newPool });
    showToast(`Stratum pool broadcast queued: ${newPool.url}`, 'info');
  };

  // Simulator actions
  const handleSpawnSimulator = (name: string) => {
    fetch('/api/simulator/spawn', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', ...authHeaders() },
      body: JSON.stringify({ name }),
    })
      .then((res) => res.json())
      .then((data) => {
        showToast(`Spawned simulated node: ${data.name}`, 'success');
      });
  };

  const handleKillSimulator = (workerId: string) => {
    fetch('/api/simulator/kill', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', ...authHeaders() },
      body: JSON.stringify({ workerId }),
    })
      .then((res) => res.json())
      .then(() => {
        showToast(`Removed simulated node ${workerId}`, 'info');
      });
  };

  // Calculate Fleet Statistics
  const activeWorkers = workers.filter((w) => w.state === 'RUNNING');
  const totalHashrateMhs = activeWorkers.reduce((sum, w) => sum + (w.hashrateMhs || 0), 0);
  const totalSharesAccepted = workers.reduce((sum, w) => sum + (w.sharesAccepted || 0), 0);
  const totalSharesRejected = workers.reduce((sum, w) => sum + (w.sharesRejected || 0), 0);
  const onlineWorkersWithTemp = workers.filter((w) => w.state !== 'OFFLINE' && typeof w.tempC === 'number' && w.tempC > 0);
  const avgTemperatureC =
    onlineWorkersWithTemp.length > 0
      ? onlineWorkersWithTemp.reduce((sum, w) => sum + (w.tempC as number), 0) / onlineWorkersWithTemp.length
      : 0;

  const fleetStats: FleetStats = {
    totalHashrateMhs,
    activeWorkers: activeWorkers.length,
    totalWorkers: workers.length,
    totalSharesAccepted,
    totalSharesRejected,
    avgTemperatureC,
    onlineRatio: workers.length > 0 ? activeWorkers.length / workers.length : 0,
  };

  return (
    <div className="min-h-screen bg-zinc-950 text-zinc-100 flex flex-col font-sans selection:bg-orange-500/30 selection:text-orange-200">
      {/* Toast Notification */}
      {toastMessage && (
        <div
          className={`fixed bottom-5 right-5 z-50 px-4 py-2.5 rounded-xl border shadow-xl text-xs font-semibold flex items-center gap-2 transition-all animate-bounce ${
            toastMessage.type === 'success'
              ? 'bg-emerald-950 text-emerald-300 border-emerald-800'
              : toastMessage.type === 'error'
              ? 'bg-rose-950 text-rose-300 border-rose-800'
              : 'bg-zinc-900 text-zinc-200 border-zinc-700'
          }`}
        >
          <span>{toastMessage.text}</span>
        </div>
      )}

      {/* Main Top Header & Fleet Status Bar */}
      <Header
        stats={fleetStats}
        pool={poolConfig}
        wsConnected={wsConnected}
        onStartAll={handleStartAll}
        onStopAll={handleStopAll}
        onRestartAll={handleRestartAll}
        onOpenPoolConfig={() => setIsPoolModalOpen(true)}
        onOpenSimulator={() => setIsSimulatorModalOpen(true)}
        onOpenAgentSetup={() => setIsAgentModalOpen(true)}
      />

      {/* Main Content Area */}
      <main className="flex-1 max-w-7xl w-full mx-auto px-4 sm:px-6 lg:px-8 py-6">
        <WorkerGrid
          workers={workers}
          onStart={handleStartWorker}
          onStop={handleStopWorker}
          onRestart={handleRestartWorker}
          onSetThreads={handleSetThreads}
          onRename={handleRenameWorker}
          onOpenDetails={(w) => setSelectedWorker(w)}
          onOpenSimulator={() => setIsSimulatorModalOpen(true)}
        />
      </main>

      {/* Footer */}
      <footer className="border-t border-zinc-900 bg-zinc-950 py-5 text-center text-xs text-zinc-500">
        <div className="max-w-7xl mx-auto px-4 flex flex-col sm:flex-row items-center justify-between gap-2">
          <span>
            S905X Bitcoin Mining Controller • Persistent Outbound Telemetry & Command Protocol
          </span>
          <span className="font-mono text-zinc-600">
            Ubuntu Controller • S905X AArch64 Fleet • Hardware Crypto SHA-256d
          </span>
        </div>
      </footer>

      {/* Modals */}
      {selectedWorker && (
        <WorkerDetailsModal
          worker={selectedWorker}
          onClose={() => setSelectedWorker(null)}
          onStart={handleStartWorker}
          onStop={handleStopWorker}
          onRestart={handleRestartWorker}
          onSetThreads={handleSetThreads}
        />
      )}

      {isPoolModalOpen && (
        <PoolConfigModal
          currentPool={poolConfig}
          onClose={() => setIsPoolModalOpen(false)}
          onSaveAndBroadcast={handleSaveAndBroadcastPool}
        />
      )}

      {isSimulatorModalOpen && (
        <SimulatorModal
          workers={workers}
          onClose={() => setIsSimulatorModalOpen(false)}
          onSpawnWorker={handleSpawnSimulator}
          onKillWorker={handleKillSimulator}
        />
      )}

      {isAgentModalOpen && (
        <AgentInstallerModal onClose={() => setIsAgentModalOpen(false)} />
      )}
    </div>
  );
}
