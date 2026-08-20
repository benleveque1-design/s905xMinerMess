import React, { useState } from 'react';
import { 
  Search, 
  Filter, 
  Server, 
  Grid, 
  List, 
  PlusCircle,
  Activity,
  Layers
} from 'lucide-react';
import { WorkerTelemetry } from '../types';
import { WorkerCard } from './WorkerCard';

interface WorkerGridProps {
  workers: WorkerTelemetry[];
  onStart: (id: string) => void;
  onStop: (id: string) => void;
  onRestart: (id: string) => void;
  onSetThreads: (id: string, threads: number) => void;
  onRename: (id: string, newName: string) => void;
  onOpenDetails: (worker: WorkerTelemetry) => void;
  onOpenSimulator: () => void;
}

export const WorkerGrid: React.FC<WorkerGridProps> = ({
  workers,
  onStart,
  onStop,
  onRestart,
  onSetThreads,
  onRename,
  onOpenDetails,
  onOpenSimulator,
}) => {
  const [searchQuery, setSearchQuery] = useState('');
  const [statusFilter, setStatusFilter] = useState<'ALL' | 'RUNNING' | 'STOPPED' | 'OFFLINE'>('ALL');

  const filteredWorkers = workers.filter((w) => {
    const matchesSearch =
      w.name.toLowerCase().includes(searchQuery.toLowerCase()) ||
      w.workerId.toLowerCase().includes(searchQuery.toLowerCase()) ||
      (w.ip && w.ip.includes(searchQuery));

    if (!matchesSearch) return false;
    if (statusFilter === 'ALL') return true;
    return w.state === statusFilter;
  });

  const activeCount = workers.filter((w) => w.state === 'RUNNING').length;
  const stoppedCount = workers.filter((w) => w.state === 'STOPPED' || w.state === 'RESTARTING').length;
  const offlineCount = workers.filter((w) => w.state === 'OFFLINE').length;

  return (
    <div className="space-y-5">
      {/* Control Bar: Search & Filter Tabs */}
      <div className="flex flex-col sm:flex-row items-stretch sm:items-center justify-between gap-3 bg-zinc-900/90 border border-zinc-800 p-3 rounded-xl">
        {/* Search Input */}
        <div className="relative flex-1 max-w-md">
          <Search className="w-4 h-4 text-zinc-400 absolute left-3 top-1/2 -translate-y-1/2" />
          <input
            type="text"
            value={searchQuery}
            onChange={(e) => setSearchQuery(e.target.value)}
            placeholder="Search S905X by name, worker ID, or IP..."
            className="w-full bg-zinc-950 border border-zinc-800 rounded-lg pl-9 pr-3 py-1.5 text-xs text-white placeholder:text-zinc-500 focus:outline-none focus:border-orange-500"
          />
        </div>

        {/* Status Filter Pills */}
        <div className="flex items-center gap-1 bg-zinc-950 p-1 rounded-lg border border-zinc-800 text-xs">
          <button
            onClick={() => setStatusFilter('ALL')}
            className={`px-3 py-1 rounded-md font-semibold transition ${
              statusFilter === 'ALL'
                ? 'bg-zinc-800 text-white'
                : 'text-zinc-400 hover:text-zinc-200'
            }`}
          >
            All ({workers.length})
          </button>
          <button
            onClick={() => setStatusFilter('RUNNING')}
            className={`px-3 py-1 rounded-md font-semibold transition ${
              statusFilter === 'RUNNING'
                ? 'bg-emerald-950 text-emerald-300 border border-emerald-800'
                : 'text-zinc-400 hover:text-emerald-300'
            }`}
          >
            Mining ({activeCount})
          </button>
          <button
            onClick={() => setStatusFilter('STOPPED')}
            className={`px-3 py-1 rounded-md font-semibold transition ${
              statusFilter === 'STOPPED'
                ? 'bg-zinc-800 text-zinc-200 border border-zinc-700'
                : 'text-zinc-400 hover:text-zinc-200'
            }`}
          >
            Idle ({stoppedCount})
          </button>
          <button
            onClick={() => setStatusFilter('OFFLINE')}
            className={`px-3 py-1 rounded-md font-semibold transition ${
              statusFilter === 'OFFLINE'
                ? 'bg-rose-950 text-rose-300 border border-rose-800'
                : 'text-zinc-400 hover:text-rose-300'
            }`}
          >
            Offline ({offlineCount})
          </button>
        </div>
      </div>

      {/* Workers Grid */}
      {filteredWorkers.length > 0 ? (
        <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
          {filteredWorkers.map((worker) => (
            <WorkerCard
              key={worker.workerId}
              worker={worker}
              onStart={onStart}
              onStop={onStop}
              onRestart={onRestart}
              onSetThreads={onSetThreads}
              onRename={onRename}
              onOpenDetails={onOpenDetails}
            />
          ))}
        </div>
      ) : (
        <div className="bg-zinc-900 border border-zinc-800 rounded-2xl p-12 text-center space-y-4">
          <div className="w-12 h-12 rounded-2xl bg-zinc-800 flex items-center justify-center text-zinc-500 mx-auto">
            <Server className="w-6 h-6" />
          </div>
          <div>
            <h3 className="text-base font-bold text-white">No Connected S905X Workers Found</h3>
            <p className="text-xs text-zinc-400 mt-1 max-w-md mx-auto">
              Start your worker daemons on your Amlogic S905X boxes or spin up simulated workers to test remote commanding.
            </p>
          </div>
          <button
            onClick={onOpenSimulator}
            className="inline-flex items-center gap-2 px-4 py-2 rounded-lg bg-orange-500 hover:bg-orange-600 text-zinc-950 text-xs font-bold transition shadow"
          >
            <PlusCircle className="w-4 h-4" />
            Spawn Simulated S905X Node
          </button>
        </div>
      )}
    </div>
  );
};
