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
  Esp32GuideModal 
} from './components/Esp32GuideModal';
import { 
  WorkerTelemetry, 
  PoolConfig, 
  FleetStats, 
  WsServerMessage 
} from './types';
import { DEFAULT_POOL } from './data/poolPresets';

export default function App() {
  const [workers, setWorkers] = useState<WorkerTelemetry[]>([]);
  const [poolConfig, setPoolConfig] = useState<PoolConfig>(DEFAULT_POOL);
  const [wsConnected, setWsConnected] = useState<boolean>(false);
  const [selectedWorker, setSelectedWorker] = useState<WorkerTelemetry | null>(null);

  // Modals state
  const [isPoolModalOpen, setIsPoolModalOpen] = useState(false);
  const [isSimulatorModalOpen, setIsSimulatorModalOpen] = useState(false);
  const [isAgentModalOpen, setIsAgentModalOpen] = useState(false);
  const [isEsp32ModalOpen, setIsEsp32ModalOpen] = useState(false);

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

  // Send Command to Worker(s)
  const sendCommand = (workerId: string, action: string, params: any = {}) => {
    const cmdPayload = {
      type: 'command',
      cmdId: 'cmd-' + Date.now(),
      workerId,
      action,
      params,
    };

    if (wsRef.current && wsRef.current.readyState === WebSocket.OPEN) {
      wsRef.current.send(JSON.stringify(cmdPayload));
    } else {
      // Fallback REST call
      fetch(`/api/workers/${workerId}/command`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(cmdPayload),
      }).catch((e) => console.error('REST command error:', e));
    }
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
    fetch('/api/pool', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(newPool),
    })
      .then((res) => res.json())
      .then(() => {
        showToast(`Stratum pool broadcasted to all workers: ${newPool.url}`, 'success');
      })
      .catch((err) => {
        showToast(`Failed to broadcast pool: ${err}`, 'error');
      });
  };

  // Simulator actions
  const handleSpawnSimulator = (name: string) => {
    fetch('/api/simulator/spawn', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
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
      headers: { 'Content-Type': 'application/json' },
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
  const onlineWorkersWithTemp = workers.filter((w) => w.state !== 'OFFLINE' && w.tempC > 0);
  const avgTemperatureC =
    onlineWorkersWithTemp.length > 0
      ? onlineWorkersWithTemp.reduce((sum, w) => sum + w.tempC, 0) / onlineWorkersWithTemp.length
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
        onOpenEsp32Guide={() => setIsEsp32ModalOpen(true)}
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

      {isEsp32ModalOpen && (
        <Esp32GuideModal onClose={() => setIsEsp32ModalOpen(false)} />
      )}
    </div>
  );
}
