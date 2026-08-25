import React, { useState } from 'react';
import { 
  Play, 
  Square, 
  RotateCw, 
  Terminal, 
  Thermometer, 
  Gauge, 
  Layers, 
  CheckCircle2, 
  XCircle, 
  Clock, 
  Edit2, 
  Check, 
  X,
  Server,
  Zap
} from 'lucide-react';
import { WorkerTelemetry } from '../types';

interface WorkerCardProps {
  worker: WorkerTelemetry;
  onStart: (id: string) => void;
  onStop: (id: string) => void;
  onRestart: (id: string) => void;
  onSetThreads: (id: string, threads: number) => void;
  onRename: (id: string, newName: string) => void;
  onOpenDetails: (worker: WorkerTelemetry) => void;
}

export const WorkerCard: React.FC<WorkerCardProps> = ({
  worker,
  onStart,
  onStop,
  onRestart,
  onSetThreads,
  onRename,
  onOpenDetails,
}) => {
  const [isEditingName, setIsEditingName] = useState(false);
  const [nameInput, setNameInput] = useState(worker.name);

  const handleSaveName = () => {
    if (nameInput.trim()) {
      onRename(worker.workerId, nameInput.trim());
    }
    setIsEditingName(false);
  };

  const formatUptime = (seconds: number) => {
    const d = Math.floor(seconds / (3600 * 24));
    const h = Math.floor((seconds % (3600 * 24)) / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = seconds % 60;
    if (d > 0) return `${d}d ${h}h ${m}m`;
    if (h > 0) return `${h}h ${m}m ${s}s`;
    return `${m}m ${s}s`;
  };

  const getStatusBadge = () => {
    switch (worker.state) {
      case 'RUNNING':
        return (
          <span className="inline-flex items-center gap-1.5 px-2.5 py-1 rounded-full text-xs font-semibold bg-emerald-950/60 text-emerald-300 border border-emerald-800/80">
            <span className="w-2 h-2 rounded-full bg-emerald-400 animate-pulse" />
            MINING
          </span>
        );
      case 'STOPPED':
        return (
          <span className="inline-flex items-center gap-1.5 px-2.5 py-1 rounded-full text-xs font-semibold bg-zinc-800 text-zinc-300 border border-zinc-700">
            <span className="w-2 h-2 rounded-full bg-zinc-500" />
            IDLE
          </span>
        );
      case 'RESTARTING':
        return (
          <span className="inline-flex items-center gap-1.5 px-2.5 py-1 rounded-full text-xs font-semibold bg-amber-950/60 text-amber-300 border border-amber-800/80">
            <span className="w-2 h-2 rounded-full bg-amber-400 animate-spin" />
            RESTARTING
          </span>
        );
      case 'OFFLINE':
      default:
        return (
          <span className="inline-flex items-center gap-1.5 px-2.5 py-1 rounded-full text-xs font-semibold bg-rose-950/60 text-rose-300 border border-rose-800/80">
            <span className="w-2 h-2 rounded-full bg-rose-500" />
            OFFLINE
          </span>
        );
    }
  };

  const getTempColor = (temp: number) => {
    if (temp < 65) return 'text-emerald-400';
    if (temp < 75) return 'text-amber-400';
    return 'text-rose-400 font-bold';
  };

  const isOnline = worker.state !== 'OFFLINE';

  return (
    <div 
      id={`worker-card-${worker.workerId}`}
      className={`bg-zinc-900 border rounded-xl p-4 transition-all shadow-sm hover:shadow-md flex flex-col justify-between ${
        worker.state === 'RUNNING'
          ? 'border-zinc-700/80 bg-gradient-to-b from-zinc-900 to-zinc-900/90'
          : worker.state === 'OFFLINE'
          ? 'border-zinc-800/60 opacity-70 bg-zinc-950/60'
          : 'border-zinc-800 bg-zinc-900/80'
      }`}
    >
      <div>
        {/* Card Header */}
        <div className="flex items-start justify-between gap-3 pb-3 border-b border-zinc-800/80">
          <div className="flex-1 min-w-0">
            {isEditingName ? (
              <div className="flex items-center gap-1.5">
                <input
                  type="text"
                  value={nameInput}
                  onChange={(e) => setNameInput(e.target.value)}
                  onKeyDown={(e) => e.key === 'Enter' && handleSaveName()}
                  className="bg-zinc-800 text-white text-sm font-semibold rounded px-2 py-0.5 border border-zinc-600 focus:outline-none focus:border-orange-500 w-full"
                  autoFocus
                />
                <button 
                  onClick={handleSaveName}
                  className="p-1 rounded bg-emerald-700 hover:bg-emerald-600 text-white"
                  title="Save name"
                >
                  <Check className="w-3.5 h-3.5" />
                </button>
                <button 
                  onClick={() => { setIsEditingName(false); setNameInput(worker.name); }}
                  className="p-1 rounded bg-zinc-700 hover:bg-zinc-600 text-zinc-300"
                  title="Cancel"
                >
                  <X className="w-3.5 h-3.5" />
                </button>
              </div>
            ) : (
              <div className="flex items-center gap-1.5 group">
                <h3 className="font-semibold text-white text-base truncate" title={worker.name}>
                  {worker.name}
                </h3>
                <button 
                  onClick={() => setIsEditingName(true)}
                  className="opacity-0 group-hover:opacity-100 text-zinc-400 hover:text-zinc-200 transition p-0.5"
                  title="Edit worker name"
                >
                  <Edit2 className="w-3.5 h-3.5" />
                </button>
              </div>
            )}
            
            <div className="flex items-center gap-2 mt-1 text-xs text-zinc-400 font-mono">
              <span className="truncate">{worker.workerId}</span>
              {worker.ip && <span>• {worker.ip}</span>}
              {worker.isSimulated && (
                <span className="px-1.5 py-0.2 rounded text-[10px] bg-blue-950/80 text-blue-300 border border-blue-800/60 font-sans">
                  SIM
                </span>
              )}
            </div>
          </div>

          <div className="flex flex-col items-end gap-1.5">
            {getStatusBadge()}
          </div>
        </div>

        {/* Primary Hashrate Metric */}
        <div className="py-3.5 flex items-center justify-between border-b border-zinc-800/60">
          <div>
            <span className="text-xs text-zinc-400 font-medium">Hashrate</span>
            <div className="flex items-baseline gap-1.5 mt-0.5">
              <span className="text-2xl font-bold font-mono text-white tracking-tight">
                {worker.hashrateMhs.toFixed(2)}
              </span>
              <span className="text-sm font-semibold text-orange-400">MH/s</span>
            </div>
          </div>

          {/* Mini Sparkline Bar representation */}
          <div className="text-right">
            <span className="text-xs text-zinc-400 font-medium">Threads</span>
            <div className="mt-1 flex items-center gap-1">
              {[1, 2, 3, 4].map((t) => (
                <button
                  key={t}
                  disabled={!isOnline}
                  onClick={() => onSetThreads(worker.workerId, t)}
                  className={`w-6 h-6 rounded text-xs font-mono font-semibold transition ${
                    worker.threads === t
                      ? 'bg-orange-500 text-zinc-950 shadow-sm'
                      : 'bg-zinc-800 text-zinc-400 hover:bg-zinc-700 hover:text-white'
                  } ${!isOnline ? 'cursor-not-allowed opacity-50' : ''}`}
                  title={`Set to ${t} CPU thread${t > 1 ? 's' : ''}`}
                >
                  {t}
                </button>
              ))}
            </div>
          </div>
        </div>

        {/* Telemetry Metrics Grid */}
        <div className="grid grid-cols-2 gap-2.5 py-3 text-xs">
          {/* Temperature */}
          <div className="bg-zinc-950/60 border border-zinc-800/80 rounded-lg p-2 flex items-center gap-2">
            <Thermometer className="w-4 h-4 text-amber-400 shrink-0" />
            <div>
              <div className="text-[10px] text-zinc-400">Temp</div>
              <div className={`font-mono font-semibold ${worker.tempC != null ? getTempColor(worker.tempC) : ''}`}>
                {worker.tempC != null && worker.tempC > 0 ? `${worker.tempC.toFixed(1)} °C` : 'N/A'}
              </div>
            </div>
          </div>

          {/* CPU Frequency */}
          <div className="bg-zinc-950/60 border border-zinc-800/80 rounded-lg p-2 flex items-center gap-2">
            <Gauge className="w-4 h-4 text-cyan-400 shrink-0" />
            <div>
              <div className="text-[10px] text-zinc-400">CPU Freq</div>
              <div className="font-mono font-semibold text-zinc-200">
                {worker.cpuFreqMhz} MHz
              </div>
            </div>
          </div>

          {/* Shares Accepted / Rejected */}
          <div className="bg-zinc-950/60 border border-zinc-800/80 rounded-lg p-2 flex items-center gap-2">
            <CheckCircle2 className="w-4 h-4 text-emerald-400 shrink-0" />
            <div>
              <div className="text-[10px] text-zinc-400">Shares (Acc / Rej)</div>
              <div className="font-mono font-semibold text-zinc-200">
                <span className="text-emerald-400">{worker.sharesAccepted}</span>
                <span className="text-zinc-500"> / </span>
                <span className={worker.sharesRejected > 0 ? 'text-rose-400' : 'text-zinc-400'}>
                  {worker.sharesRejected}
                </span>
              </div>
            </div>
          </div>

          {/* Uptime */}
          <div className="bg-zinc-950/60 border border-zinc-800/80 rounded-lg p-2 flex items-center gap-2">
            <Clock className="w-4 h-4 text-purple-400 shrink-0" />
            <div>
              <div className="text-[10px] text-zinc-400">Uptime</div>
              <div className="font-mono font-semibold text-zinc-200 truncate">
                {isOnline ? formatUptime(worker.uptime) : 'Offline'}
              </div>
            </div>
          </div>
        </div>

        {/* Assigned Pool */}
        <div className="px-2.5 py-1.5 rounded bg-zinc-950/40 border border-zinc-800/60 text-[11px] text-zinc-400 flex items-center justify-between mb-3 truncate">
          <span className="flex items-center gap-1.5 truncate">
            <Zap className="w-3 h-3 text-yellow-400 shrink-0" />
            <span className="font-mono truncate">{worker.pool?.url?.replace('stratum+tcp://', '') || 'No Pool'}</span>
          </span>
          <span className="text-zinc-400 font-mono shrink-0 ml-1">
            {worker.pool?.user?.split('.')[1] || 'default'}
          </span>
        </div>
      </div>

      {/* Card Action Footer */}
      <div className="pt-2 border-t border-zinc-800/80 flex items-center justify-between gap-2">
        <div className="flex items-center gap-1.5">
          {worker.state === 'RUNNING' ? (
            <button
              id={`btn-stop-${worker.workerId}`}
              disabled={!isOnline}
              onClick={() => onStop(worker.workerId)}
              className="flex items-center gap-1 px-3 py-1.5 rounded text-xs font-semibold bg-rose-950/50 hover:bg-rose-900/60 text-rose-300 border border-rose-800/60 transition"
              title="Stop Miner"
            >
              <Square className="w-3.5 h-3.5 fill-current" />
              Stop
            </button>
          ) : (
            <button
              id={`btn-start-${worker.workerId}`}
              disabled={!isOnline}
              onClick={() => onStart(worker.workerId)}
              className="flex items-center gap-1 px-3 py-1.5 rounded text-xs font-semibold bg-emerald-950/50 hover:bg-emerald-900/60 text-emerald-300 border border-emerald-800/60 transition"
              title="Start Miner"
            >
              <Play className="w-3.5 h-3.5 fill-current" />
              Start
            </button>
          )}

          <button
            id={`btn-restart-${worker.workerId}`}
            disabled={!isOnline}
            onClick={() => onRestart(worker.workerId)}
            className="p-1.5 rounded text-xs text-zinc-300 bg-zinc-800 hover:bg-zinc-700 border border-zinc-700 transition"
            title="Restart Miner Process"
          >
            <RotateCw className="w-3.5 h-3.5" />
          </button>
        </div>

        <button
          id={`btn-logs-${worker.workerId}`}
          onClick={() => onOpenDetails(worker)}
          className="flex items-center gap-1.5 px-3 py-1.5 rounded text-xs font-medium bg-zinc-800 hover:bg-zinc-700 text-zinc-200 border border-zinc-700 transition"
        >
          <Terminal className="w-3.5 h-3.5 text-orange-400" />
          Logs & Stats
        </button>
      </div>
    </div>
  );
};
