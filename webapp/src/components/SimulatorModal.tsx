import React, { useState } from 'react';
import { 
  X, 
  Plus, 
  Trash2, 
  Cpu, 
  Server, 
  Play, 
  Layers, 
  Flame,
  CheckCircle2
} from 'lucide-react';
import { WorkerTelemetry } from '../types';

interface SimulatorModalProps {
  workers: WorkerTelemetry[];
  onClose: () => void;
  onSpawnWorker: (name: string) => void;
  onKillWorker: (workerId: string) => void;
}

export const SimulatorModal: React.FC<SimulatorModalProps> = ({
  workers,
  onClose,
  onSpawnWorker,
  onKillWorker,
}) => {
  const [workerName, setWorkerName] = useState('');
  const simulatedWorkers = workers.filter((w) => w.isSimulated);

  const handleSpawn = (e: React.FormEvent) => {
    e.preventDefault();
    const name = workerName.trim() || `S905X Node ${simulatedWorkers.length + 1}`;
    onSpawnWorker(name);
    setWorkerName('');
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/80 backdrop-blur-sm">
      <div className="bg-zinc-900 border border-zinc-700/80 rounded-2xl w-full max-w-2xl max-h-[90vh] flex flex-col shadow-2xl overflow-hidden">
        {/* Header */}
        <div className="p-5 border-b border-zinc-800 bg-zinc-950 flex items-center justify-between">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-xl bg-blue-500/10 border border-blue-500/30 flex items-center justify-center text-blue-400">
              <Cpu className="w-5 h-5" />
            </div>
            <div>
              <h2 className="text-lg font-bold text-white tracking-tight">
                Simulated S905X Miner Nodes
              </h2>
              <p className="text-xs text-zinc-400">
                Test multi-box commanding, thermal scaling, and share submission without physical hardware
              </p>
            </div>
          </div>
          <button
            onClick={onClose}
            className="p-2 rounded-lg bg-zinc-800 hover:bg-zinc-700 text-zinc-400 hover:text-white transition"
          >
            <X className="w-5 h-5" />
          </button>
        </div>

        {/* Content Body */}
        <div className="flex-1 p-5 overflow-y-auto space-y-5 text-xs text-zinc-300">
          {/* Add simulated worker form */}
          <form onSubmit={handleSpawn} className="bg-zinc-950/80 border border-zinc-800 rounded-xl p-4 space-y-3">
            <h3 className="font-semibold text-white text-xs flex items-center gap-2">
              <Plus className="w-4 h-4 text-blue-400" />
              Spawn New Simulated S905X Node
            </h3>
            <div className="flex gap-2">
              <input
                type="text"
                value={workerName}
                onChange={(e) => setWorkerName(e.target.value)}
                placeholder="Node Name (e.g. S905X Living Room)"
                className="flex-1 bg-zinc-900 border border-zinc-700 rounded-lg px-3 py-2 text-xs text-white focus:outline-none focus:border-blue-500"
              />
              <button
                type="submit"
                className="flex items-center gap-1.5 px-4 py-2 rounded-lg bg-blue-600 hover:bg-blue-500 text-white font-semibold text-xs transition"
              >
                <Plus className="w-4 h-4" />
                Spawn Node
              </button>
            </div>
            <p className="text-[11px] text-zinc-400">
              Simulated nodes run realistic 4-thread Cortex-A53 thermal models, frequency throttling, and share generation.
            </p>
          </form>

          {/* List of active simulated workers */}
          <div className="space-y-3">
            <h3 className="font-semibold text-white text-xs flex items-center justify-between">
              <span>Active Simulated Workers ({simulatedWorkers.length})</span>
            </h3>

            {simulatedWorkers.length > 0 ? (
              <div className="space-y-2 max-h-60 overflow-y-auto">
                {simulatedWorkers.map((w) => (
                  <div
                    key={w.workerId}
                    className="bg-zinc-950 border border-zinc-800 rounded-xl p-3 flex items-center justify-between gap-3"
                  >
                    <div>
                      <div className="flex items-center gap-2">
                        <span className="font-semibold text-white">{w.name}</span>
                        <span className="text-[10px] font-mono text-zinc-400">({w.workerId})</span>
                      </div>
                      <div className="text-[11px] text-zinc-400 mt-0.5 flex items-center gap-3">
                        <span>{w.threads} Threads</span>
                        <span>•</span>
                        <span className="text-orange-400 font-mono font-semibold">{w.hashrateMhs.toFixed(2)} MH/s</span>
                        <span>•</span>
                        <span className="text-amber-400 font-mono">{w.tempC != null ? `${w.tempC.toFixed(1)} °C` : 'N/A'}</span>
                      </div>
                    </div>

                    <button
                      onClick={() => onKillWorker(w.workerId)}
                      className="p-2 rounded-lg bg-rose-950/40 hover:bg-rose-900/60 text-rose-300 border border-rose-800/50 transition text-xs flex items-center gap-1"
                      title="Destroy simulated worker"
                    >
                      <Trash2 className="w-3.5 h-3.5" />
                    </button>
                  </div>
                ))}
              </div>
            ) : (
              <div className="text-zinc-500 italic py-6 text-center bg-zinc-950/40 border border-zinc-800 rounded-xl">
                No simulated nodes running. Click "Spawn Node" above to add one.
              </div>
            )}
          </div>
        </div>

        {/* Footer */}
        <div className="p-4 border-t border-zinc-800 bg-zinc-950 flex justify-end">
          <button
            onClick={onClose}
            className="px-4 py-2 rounded-lg bg-zinc-800 hover:bg-zinc-700 text-zinc-300 text-xs font-medium transition"
          >
            Done
          </button>
        </div>
      </div>
    </div>
  );
};
