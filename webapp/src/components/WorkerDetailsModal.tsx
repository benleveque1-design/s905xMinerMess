import React, { useState } from 'react';
import { 
  X, 
  Terminal, 
  Activity, 
  Thermometer, 
  Cpu, 
  Zap, 
  Server, 
  Play, 
  Square, 
  RotateCw, 
  Layers,
  Copy,
  Check
} from 'lucide-react';
import { WorkerTelemetry } from '../types';

interface WorkerDetailsModalProps {
  worker: WorkerTelemetry | null;
  onClose: () => void;
  onStart: (id: string) => void;
  onStop: (id: string) => void;
  onRestart: (id: string) => void;
  onSetThreads: (id: string, threads: number) => void;
}

export const WorkerDetailsModal: React.FC<WorkerDetailsModalProps> = ({
  worker,
  onClose,
  onStart,
  onStop,
  onRestart,
  onSetThreads,
}) => {
  const [copied, setCopied] = useState(false);
  const [activeTab, setActiveTab] = useState<'logs' | 'hardware' | 'charts'>('logs');

  if (!worker) return null;

  const handleCopyId = () => {
    navigator.clipboard.writeText(worker.workerId);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/80 backdrop-blur-sm">
      <div className="bg-zinc-900 border border-zinc-700/80 rounded-2xl w-full max-w-4xl max-h-[90vh] flex flex-col shadow-2xl overflow-hidden">
        {/* Modal Header */}
        <div className="p-4 sm:p-5 border-b border-zinc-800 bg-zinc-950/80 flex items-center justify-between">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-xl bg-orange-500/10 border border-orange-500/30 flex items-center justify-center text-orange-400">
              <Cpu className="w-5 h-5" />
            </div>
            <div>
              <div className="flex items-center gap-2">
                <h2 className="text-lg font-bold text-white tracking-tight">{worker.name}</h2>
                <span className="text-xs font-mono px-2 py-0.5 rounded bg-zinc-800 text-zinc-300 border border-zinc-700">
                  {worker.workerId}
                </span>
                {worker.isSimulated && (
                  <span className="text-[10px] font-sans px-1.5 py-0.5 rounded bg-blue-950 text-blue-300 border border-blue-800">
                    SIMULATED
                  </span>
                )}
              </div>
              <p className="text-xs text-zinc-400 mt-0.5">
                IP: {worker.ip || '127.0.0.1'} • Status: <span className="text-emerald-400 font-semibold">{worker.state}</span> • {worker.threads} Threads @ {worker.cpuFreqMhz} MHz
              </p>
            </div>
          </div>

          <div className="flex items-center gap-2">
            <button
              onClick={handleCopyId}
              className="p-2 rounded-lg bg-zinc-800 hover:bg-zinc-700 text-zinc-300 transition text-xs flex items-center gap-1.5"
              title="Copy Worker ID"
            >
              {copied ? <Check className="w-4 h-4 text-emerald-400" /> : <Copy className="w-4 h-4" />}
            </button>
            <button
              onClick={onClose}
              className="p-2 rounded-lg bg-zinc-800 hover:bg-zinc-700 text-zinc-400 hover:text-white transition"
              title="Close modal"
            >
              <X className="w-5 h-5" />
            </button>
          </div>
        </div>

        {/* Action Controls & Navigation Tabs */}
        <div className="px-5 py-3 border-b border-zinc-800 bg-zinc-900/60 flex flex-wrap items-center justify-between gap-3">
          {/* Tabs */}
          <div className="flex items-center gap-2">
            <button
              onClick={() => setActiveTab('logs')}
              className={`flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-semibold transition ${
                activeTab === 'logs'
                  ? 'bg-orange-500 text-zinc-950'
                  : 'bg-zinc-800 text-zinc-400 hover:text-white'
              }`}
            >
              <Terminal className="w-3.5 h-3.5" />
              Live Console Logs ({worker.recentLogs?.length || 0})
            </button>
            <button
              onClick={() => setActiveTab('hardware')}
              className={`flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-semibold transition ${
                activeTab === 'hardware'
                  ? 'bg-orange-500 text-zinc-950'
                  : 'bg-zinc-800 text-zinc-400 hover:text-white'
              }`}
            >
              <Cpu className="w-3.5 h-3.5" />
              Hardware & SoC Telemetry
            </button>
            <button
              onClick={() => setActiveTab('charts')}
              className={`flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-semibold transition ${
                activeTab === 'charts'
                  ? 'bg-orange-500 text-zinc-950'
                  : 'bg-zinc-800 text-zinc-400 hover:text-white'
              }`}
            >
              <Activity className="w-3.5 h-3.5" />
              Performance Curves
            </button>
          </div>

          {/* Quick Actions */}
          <div className="flex items-center gap-2">
            {worker.state === 'RUNNING' ? (
              <button
                onClick={() => onStop(worker.workerId)}
                className="flex items-center gap-1 px-3 py-1.5 rounded-lg text-xs font-semibold bg-rose-950/60 hover:bg-rose-900 text-rose-300 border border-rose-800/80 transition"
              >
                <Square className="w-3.5 h-3.5 fill-current" />
                Stop
              </button>
            ) : (
              <button
                onClick={() => onStart(worker.workerId)}
                className="flex items-center gap-1 px-3 py-1.5 rounded-lg text-xs font-semibold bg-emerald-950/60 hover:bg-emerald-900 text-emerald-300 border border-emerald-800/80 transition"
              >
                <Play className="w-3.5 h-3.5 fill-current" />
                Start
              </button>
            )}

            <button
              onClick={() => onRestart(worker.workerId)}
              className="flex items-center gap-1 px-3 py-1.5 rounded-lg text-xs font-semibold bg-zinc-800 hover:bg-zinc-700 text-zinc-200 border border-zinc-700 transition"
            >
              <RotateCw className="w-3.5 h-3.5" />
              Restart
            </button>

            {/* Thread selector */}
            <div className="flex items-center bg-zinc-950 p-1 rounded-lg border border-zinc-800">
              <span className="text-[10px] text-zinc-400 px-1.5 font-medium">Threads:</span>
              {[1, 2, 3, 4].map((t) => (
                <button
                  key={t}
                  onClick={() => onSetThreads(worker.workerId, t)}
                  className={`w-5 h-5 rounded text-xs font-mono font-bold transition ${
                    worker.threads === t
                      ? 'bg-orange-500 text-zinc-950'
                      : 'text-zinc-400 hover:text-white'
                  }`}
                >
                  {t}
                </button>
              ))}
            </div>
          </div>
        </div>

        {/* Modal Body */}
        <div className="flex-1 p-5 overflow-y-auto min-h-[360px]">
          {activeTab === 'logs' && (
            <div className="bg-zinc-950 rounded-xl border border-zinc-800 p-4 font-mono text-xs text-zinc-300 h-[380px] overflow-y-auto space-y-1.5">
              <div className="text-zinc-500 pb-2 border-b border-zinc-800/80">
                [AGENT] Connected to controller ws://server:3010/ws/worker | Worker: {worker.workerId}
              </div>
              {worker.recentLogs && worker.recentLogs.length > 0 ? (
                worker.recentLogs.map((log) => (
                  <div key={log.id} className="flex items-start gap-2">
                    <span className="text-zinc-500 shrink-0">
                      {new Date(log.timestamp).toLocaleTimeString()}
                    </span>
                    <span
                      className={`font-semibold shrink-0 ${
                        log.level === 'SUCCESS'
                          ? 'text-emerald-400'
                          : log.level === 'WARN'
                          ? 'text-amber-400'
                          : log.level === 'ERROR'
                          ? 'text-rose-400'
                          : 'text-blue-400'
                      }`}
                    >
                      [{log.level}]
                    </span>
                    <span className="text-zinc-200 break-all">{log.message}</span>
                  </div>
                ))
              ) : (
                <div className="text-zinc-500 italic py-8 text-center">
                  Waiting for miner log events or share discoveries...
                </div>
              )}
            </div>
          )}

          {activeTab === 'hardware' && (
            <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
              <div className="bg-zinc-950/80 border border-zinc-800 rounded-xl p-4 space-y-3">
                <h4 className="text-sm font-semibold text-white flex items-center gap-2">
                  <Cpu className="w-4 h-4 text-orange-400" />
                  SoC & Architecture
                </h4>
                <div className="space-y-2 text-xs">
                  <div className="flex justify-between py-1 border-b border-zinc-800/60">
                    <span className="text-zinc-400">Processor</span>
                    <span className="font-mono text-zinc-200">{worker.arch || 'Amlogic S905X (4x Cortex-A53)'}</span>
                  </div>
                  <div className="flex justify-between py-1 border-b border-zinc-800/60">
                    <span className="text-zinc-400">ARMv8 Crypto Extension</span>
                    {worker.hwCrypto ? (
                      <span className="font-mono text-emerald-400 font-semibold">Enabled (+crypto / sha256_hw)</span>
                    ) : (
                      <span className="font-mono text-zinc-400 font-semibold">Not available (software fallback)</span>
                    )}
                  </div>
                  <div className="flex justify-between py-1 border-b border-zinc-800/60">
                    <span className="text-zinc-400">Active CPU Frequency</span>
                    <span className="font-mono text-zinc-200">{worker.cpuFreqMhz} MHz</span>
                  </div>
                  <div className="flex justify-between py-1 border-b border-zinc-800/60">
                    <span className="text-zinc-400">Thermal Sensor Zone 0</span>
                    <span className="font-mono text-amber-400 font-semibold">{worker.tempC != null ? `${worker.tempC.toFixed(1)} °C` : 'N/A'}</span>
                  </div>
                </div>
              </div>

              <div className="bg-zinc-950/80 border border-zinc-800 rounded-xl p-4 space-y-3">
                <h4 className="text-sm font-semibold text-white flex items-center gap-2">
                  <Zap className="w-4 h-4 text-yellow-400" />
                  Mining Job & Stratum Status
                </h4>
                <div className="space-y-2 text-xs">
                  <div className="flex justify-between py-1 border-b border-zinc-800/60">
                    <span className="text-zinc-400">Stratum Server</span>
                    <span className="font-mono text-zinc-200 truncate max-w-[200px]" title={worker.pool.url}>
                      {worker.pool.url}
                    </span>
                  </div>
                  <div className="flex justify-between py-1 border-b border-zinc-800/60">
                    <span className="text-zinc-400">Worker User / Payout</span>
                    <span className="font-mono text-zinc-200 truncate max-w-[200px]" title={worker.pool.user}>
                      {worker.pool.user}
                    </span>
                  </div>
                  <div className="flex justify-between py-1 border-b border-zinc-800/60">
                    <span className="text-zinc-400">Accepted Shares</span>
                    <span className="font-mono text-emerald-400 font-bold">{worker.sharesAccepted}</span>
                  </div>
                  <div className="flex justify-between py-1 border-b border-zinc-800/60">
                    <span className="text-zinc-400">Rejected / Stale</span>
                    <span className="font-mono text-zinc-400">{worker.sharesRejected}</span>
                  </div>
                </div>
              </div>
            </div>
          )}

          {activeTab === 'charts' && (
            <div className="space-y-4">
              <div className="bg-zinc-950/80 border border-zinc-800 rounded-xl p-4">
                <h4 className="text-xs font-semibold text-zinc-400 uppercase tracking-wider mb-2">
                  Hashrate Telemetry History (Last 60s)
                </h4>
                <div className="h-32 flex items-end gap-1 pt-4 border-b border-zinc-800">
                  {worker.hashrateHistory && worker.hashrateHistory.length > 0 ? (
                    worker.hashrateHistory.map((pt, i) => {
                      const heightPercent = Math.min(100, Math.max(5, (pt.hashrate / 5.0) * 100));
                      return (
                        <div
                          key={i}
                          className="flex-1 bg-orange-500/80 hover:bg-orange-400 rounded-t transition-all group relative"
                          style={{ height: `${heightPercent}%` }}
                        >
                          <div className="opacity-0 group-hover:opacity-100 absolute bottom-full mb-1 left-1/2 -translate-x-1/2 bg-zinc-800 text-[10px] text-white px-1.5 py-0.5 rounded font-mono pointer-events-none whitespace-nowrap z-10">
                            {pt.hashrate} MH/s
                          </div>
                        </div>
                      );
                    })
                  ) : (
                    <div className="text-zinc-500 text-xs italic m-auto">Collecting telemetry samples...</div>
                  )}
                </div>
              </div>

              <div className="bg-zinc-950/80 border border-zinc-800 rounded-xl p-4">
                <h4 className="text-xs font-semibold text-zinc-400 uppercase tracking-wider mb-2">
                  Die Temperature (°C) History
                </h4>
                <div className="h-32 flex items-end gap-1 pt-4 border-b border-zinc-800">
                  {worker.tempHistory && worker.tempHistory.length > 0 ? (
                    worker.tempHistory.map((pt, i) => {
                      const heightPercent = Math.min(100, Math.max(10, ((pt.temp - 30) / 60) * 100));
                      return (
                        <div
                          key={i}
                          className="flex-1 bg-amber-500/70 hover:bg-amber-400 rounded-t transition-all group relative"
                          style={{ height: `${heightPercent}%` }}
                        >
                          <div className="opacity-0 group-hover:opacity-100 absolute bottom-full mb-1 left-1/2 -translate-x-1/2 bg-zinc-800 text-[10px] text-white px-1.5 py-0.5 rounded font-mono pointer-events-none whitespace-nowrap z-10">
                            {pt.temp} °C
                          </div>
                        </div>
                      );
                    })
                  ) : (
                    <div className="text-zinc-500 text-xs italic m-auto">Collecting temperature samples...</div>
                  )}
                </div>
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
