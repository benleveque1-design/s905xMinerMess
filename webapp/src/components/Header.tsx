import React from 'react';
import { 
  Cpu, 
  Play, 
  Square, 
  RotateCw, 
  Server, 
  Flame, 
  CheckCircle2, 
  Layers, 
  Settings2, 
  PlusCircle, 
  Download, 
  Activity,
  Zap
} from 'lucide-react';
import { FleetStats, PoolConfig } from '../types';

interface HeaderProps {
  stats: FleetStats;
  pool: PoolConfig;
  wsConnected: boolean;
  onStartAll: () => void;
  onStopAll: () => void;
  onRestartAll: () => void;
  onOpenPoolConfig: () => void;
  onOpenSimulator: () => void;
  onOpenAgentSetup: () => void;
}

export const Header: React.FC<HeaderProps> = ({
  stats,
  pool,
  wsConnected,
  onStartAll,
  onStopAll,
  onRestartAll,
  onOpenPoolConfig,
  onOpenSimulator,
  onOpenAgentSetup,
}) => {
  const getTempColor = (t: number) => {
    if (t <= 0) return 'text-zinc-400';
    if (t < 65) return 'text-emerald-400';
    if (t < 75) return 'text-amber-400';
    return 'text-rose-400';
  };

  return (
    <header className="bg-zinc-900 border-b border-zinc-800 sticky top-0 z-30 shadow-md">
      {/* Top Brand Bar */}
      <div className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8 py-3.5 flex flex-wrap items-center justify-between gap-4">
        <div className="flex items-center gap-3">
          <div className="w-10 h-10 rounded-lg bg-orange-500/10 border border-orange-500/30 flex items-center justify-center text-orange-400 shadow-inner">
            <Cpu className="w-6 h-6" />
          </div>
          <div>
            <div className="flex items-center gap-2">
              <h1 className="text-lg font-bold tracking-tight text-white flex items-center gap-2">
                S905X Bitcoin Mining Controller
              </h1>
              <span className="inline-flex items-center px-2 py-0.5 rounded text-xs font-medium bg-zinc-800 text-zinc-300 border border-zinc-700">
                ARMv8 Crypto
              </span>
            </div>
            <p className="text-xs text-zinc-400">
              Centralized Outbound Command & Telemetry for Amlogic A53 Quad-Core Nodes
            </p>
          </div>
        </div>

        {/* Global Action Buttons */}
        <div className="flex items-center flex-wrap gap-2">
          {/* WS Connection Pill */}
          <div 
            id="ws-status-pill"
            className={`flex items-center gap-1.5 px-3 py-1.5 rounded-md text-xs font-mono border ${
              wsConnected 
                ? 'bg-emerald-950/40 text-emerald-300 border-emerald-800/60' 
                : 'bg-rose-950/40 text-rose-300 border-rose-800/60'
            }`}
          >
            <span className={`w-2 h-2 rounded-full ${wsConnected ? 'bg-emerald-400 animate-pulse' : 'bg-rose-400'}`} />
            {wsConnected ? 'WS ONLINE' : 'DISCONNECTED'}
          </div>

          <button
            id="btn-pool-config"
            onClick={onOpenPoolConfig}
            className="flex items-center gap-1.5 px-3 py-1.5 text-xs font-medium rounded-md bg-zinc-800 hover:bg-zinc-700 text-zinc-200 border border-zinc-700 transition"
          >
            <Settings2 className="w-3.5 h-3.5 text-orange-400" />
            Pool Config
          </button>

          <button
            id="btn-add-sim"
            onClick={onOpenSimulator}
            className="flex items-center gap-1.5 px-3 py-1.5 text-xs font-medium rounded-md bg-zinc-800 hover:bg-zinc-700 text-zinc-200 border border-zinc-700 transition"
          >
            <PlusCircle className="w-3.5 h-3.5 text-blue-400" />
            Simulate S905X
          </button>

          <button
            id="btn-agent-setup"
            onClick={onOpenAgentSetup}
            className="flex items-center gap-1.5 px-3 py-1.5 text-xs font-medium rounded-md bg-zinc-800 hover:bg-zinc-700 text-zinc-200 border border-zinc-700 transition"
          >
            <Download className="w-3.5 h-3.5 text-emerald-400" />
            S905X Agent
          </button>
        </div>
      </div>

      {/* Fleet Summary Metrics Strip */}
      <div className="bg-zinc-950 border-t border-zinc-800/80 px-4 sm:px-6 lg:px-8 py-3">
        <div className="max-w-7xl mx-auto grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-6 gap-3">
          {/* 1. Fleet Hashrate */}
          <div className="bg-zinc-900/90 border border-zinc-800 rounded-lg p-2.5">
            <div className="text-[11px] font-medium text-zinc-400 uppercase tracking-wider flex items-center justify-between">
              <span>Fleet Hashrate</span>
              <Activity className="w-3.5 h-3.5 text-orange-400" />
            </div>
            <div className="mt-1 flex items-baseline gap-1">
              <span className="text-xl font-bold font-mono text-white">
                {stats.totalHashrateMhs.toFixed(2)}
              </span>
              <span className="text-xs font-medium text-orange-400">MH/s</span>
            </div>
          </div>

          {/* 2. Active S905X Workers */}
          <div className="bg-zinc-900/90 border border-zinc-800 rounded-lg p-2.5">
            <div className="text-[11px] font-medium text-zinc-400 uppercase tracking-wider flex items-center justify-between">
              <span>Workers Online</span>
              <Server className="w-3.5 h-3.5 text-emerald-400" />
            </div>
            <div className="mt-1 flex items-baseline gap-1.5">
              <span className="text-xl font-bold font-mono text-emerald-400">
                {stats.activeWorkers}
              </span>
              <span className="text-xs text-zinc-400">/ {stats.totalWorkers} Nodes</span>
            </div>
          </div>

          {/* 3. Shares Accepted / Rejected */}
          <div className="bg-zinc-900/90 border border-zinc-800 rounded-lg p-2.5">
            <div className="text-[11px] font-medium text-zinc-400 uppercase tracking-wider flex items-center justify-between">
              <span>Accepted Shares</span>
              <CheckCircle2 className="w-3.5 h-3.5 text-emerald-400" />
            </div>
            <div className="mt-1 flex items-baseline gap-2 font-mono">
              <span className="text-xl font-bold text-white">{stats.totalSharesAccepted}</span>
              {stats.totalSharesRejected > 0 && (
                <span className="text-xs text-rose-400 font-semibold">({stats.totalSharesRejected} rej)</span>
              )}
            </div>
          </div>

          {/* 4. Avg Chip Temp */}
          <div className="bg-zinc-900/90 border border-zinc-800 rounded-lg p-2.5">
            <div className="text-[11px] font-medium text-zinc-400 uppercase tracking-wider flex items-center justify-between">
              <span>Avg S905X Temp</span>
              <Flame className="w-3.5 h-3.5 text-amber-400" />
            </div>
            <div className="mt-1 flex items-baseline gap-1">
              <span className={`text-xl font-bold font-mono ${getTempColor(stats.avgTemperatureC)}`}>
                {stats.avgTemperatureC > 0 ? stats.avgTemperatureC.toFixed(1) : '--'}
              </span>
              <span className="text-xs text-zinc-400">°C</span>
            </div>
          </div>

          {/* 5. Active Mining Pool */}
          <div className="bg-zinc-900/90 border border-zinc-800 rounded-lg p-2.5">
            <div className="text-[11px] font-medium text-zinc-400 uppercase tracking-wider flex items-center justify-between">
              <span>Configured Pool</span>
              <Zap className="w-3.5 h-3.5 text-yellow-400" />
            </div>
            <div className="mt-1 truncate font-mono text-xs font-semibold text-zinc-200" title={pool.url}>
              {pool.url.replace('stratum+tcp://', '')}
            </div>
          </div>

          {/* 6. Batch Control Buttons */}
          <div className="bg-zinc-900/90 border border-zinc-800 rounded-lg p-2 flex items-center justify-around gap-1">
            <button
              id="btn-start-all"
              onClick={onStartAll}
              title="Start mining on all connected workers"
              className="flex-1 flex flex-col items-center justify-center p-1.5 rounded bg-emerald-950/40 hover:bg-emerald-900/60 text-emerald-300 border border-emerald-800/50 transition text-[10px] font-medium"
            >
              <Play className="w-3.5 h-3.5 mb-0.5 fill-current" />
              Start All
            </button>
            <button
              id="btn-stop-all"
              onClick={onStopAll}
              title="Stop mining on all connected workers"
              className="flex-1 flex flex-col items-center justify-center p-1.5 rounded bg-rose-950/40 hover:bg-rose-900/60 text-rose-300 border border-rose-800/50 transition text-[10px] font-medium"
            >
              <Square className="w-3.5 h-3.5 mb-0.5 fill-current" />
              Stop All
            </button>
            <button
              id="btn-restart-all"
              onClick={onRestartAll}
              title="Restart miner processes across all workers"
              className="flex-1 flex flex-col items-center justify-center p-1.5 rounded bg-zinc-800 hover:bg-zinc-700 text-zinc-300 border border-zinc-700 transition text-[10px] font-medium"
            >
              <RotateCw className="w-3.5 h-3.5 mb-0.5" />
              Restart All
            </button>
          </div>
        </div>
      </div>
    </header>
  );
};
