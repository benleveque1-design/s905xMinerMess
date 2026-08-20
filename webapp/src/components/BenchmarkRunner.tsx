import React, { useState, useTransition } from 'react';
import { Play, RotateCcw, Zap, CheckCircle2, TrendingUp, Cpu, Gauge } from 'lucide-react';
import { SAMPLE_BLOCKS } from '../data/sampleBlocks';
import {
  hexToBytes,
  sha256dNaive,
  precomputeMidstate,
  sha256dMidstate,
  formatBitcoinHash,
} from '../crypto/sha256d';
import { BenchmarkRunResult } from '../types';

export const BenchmarkRunner: React.FC = () => {
  const [selectedBlockId, setSelectedBlockId] = useState<string>('block-125552');
  const [iterations, setIterations] = useState<number>(50000);
  const [isRunning, setIsRunning] = useState<boolean>(false);
  const [naiveResult, setNaiveResult] = useState<BenchmarkRunResult | null>(null);
  const [midstateResult, setMidstateResult] = useState<BenchmarkRunResult | null>(null);
  const [progress, setProgress] = useState<number>(0);
  const [, startTransition] = useTransition();

  const selectedBlock = SAMPLE_BLOCKS.find((b) => b.id === selectedBlockId) || SAMPLE_BLOCKS[0];

  const runBenchmark = () => {
    setIsRunning(true);
    setProgress(10);
    setNaiveResult(null);
    setMidstateResult(null);

    setTimeout(() => {
      const headerBytes = hexToBytes(selectedBlock.hex);
      const startNonce = selectedBlock.nonce;

      // 1. Run Naive Full Hashing
      const t0 = performance.now();
      let lastNaiveHash = new Uint8Array(32);
      for (let i = 0; i < iterations; i++) {
        // update nonce in header
        const nonce = (startNonce + i) >>> 0;
        headerBytes[76] = nonce & 0xff;
        headerBytes[77] = (nonce >>> 8) & 0xff;
        headerBytes[78] = (nonce >>> 16) & 0xff;
        headerBytes[79] = (nonce >>> 24) & 0xff;
        lastNaiveHash = sha256dNaive(headerBytes);
      }
      const t1 = performance.now();
      const elapsedNaive = Math.max(t1 - t0, 0.001);
      const naiveRate = (iterations / (elapsedNaive / 1000));

      const naiveRes: BenchmarkRunResult = {
        mode: 'naive',
        hashes: iterations,
        elapsedMs: elapsedNaive,
        hashrate: naiveRate,
        compressionsTotal: iterations * 3,
        compressionsPerHash: 3,
        sampleHash: formatBitcoinHash(lastNaiveHash),
      };

      setNaiveResult(naiveRes);
      setProgress(60);

      // 2. Run Optimized Midstate Hashing
      setTimeout(() => {
        const msContext = precomputeMidstate(headerBytes);
        const t2 = performance.now();
        let lastMidstateHash = new Uint8Array(32);
        for (let i = 0; i < iterations; i++) {
          const nonce = (startNonce + i) >>> 0;
          lastMidstateHash = sha256dMidstate(msContext, nonce);
        }
        const t3 = performance.now();
        const elapsedMidstate = Math.max(t3 - t2, 0.001);
        const midstateRate = (iterations / (elapsedMidstate / 1000));

        const midstateRes: BenchmarkRunResult = {
          mode: 'midstate',
          hashes: iterations,
          elapsedMs: elapsedMidstate,
          hashrate: midstateRate,
          compressionsTotal: 1 + iterations * 2, // 1 precompute + 2 per hash
          compressionsPerHash: 2,
          sampleHash: formatBitcoinHash(lastMidstateHash),
        };

        startTransition(() => {
          setMidstateResult(midstateRes);
          setIsRunning(false);
          setProgress(100);
        });
      }, 50);
    }, 50);
  };

  const speedup =
    naiveResult && midstateResult && naiveResult.elapsedMs > 0
      ? (naiveResult.elapsedMs / midstateResult.elapsedMs).toFixed(2)
      : null;

  const hashrateGain =
    naiveResult && midstateResult && naiveResult.hashrate > 0
      ? (((midstateResult.hashrate - naiveResult.hashrate) / naiveResult.hashrate) * 100).toFixed(1)
      : null;

  const isHashMatching =
    naiveResult && midstateResult ? naiveResult.sampleHash === midstateResult.sampleHash : false;

  return (
    <div className="space-y-6">
      {/* Controls Card */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-6 shadow-sm">
        <div className="flex flex-col md:flex-row md:items-center md:justify-between gap-4">
          <div>
            <h3 className="text-lg font-bold text-white tracking-tight flex items-center space-x-2">
              <Gauge className="w-5 h-5 text-amber-400" />
              <span>Live In-Browser SHA-256d Benchmark</span>
            </h3>
            <p className="text-xs text-slate-400 mt-1">
              Executes real double SHA-256 hashing across multiple nonces to directly compare 3-compression vs 2-compression midstate throughput.
            </p>
          </div>

          <div className="flex flex-wrap items-center gap-3">
            <div>
              <label className="block text-[11px] text-slate-400 font-medium mb-1">
                Target Block Header
              </label>
              <select
                value={selectedBlockId}
                onChange={(e) => setSelectedBlockId(e.target.value)}
                disabled={isRunning}
                className="bg-slate-800 border border-slate-700 text-slate-200 text-xs rounded-lg px-3 py-1.5 focus:outline-none focus:border-amber-500"
              >
                {SAMPLE_BLOCKS.map((b) => (
                  <option key={b.id} value={b.id}>
                    {b.name}
                  </option>
                ))}
              </select>
            </div>

            <div>
              <label className="block text-[11px] text-slate-400 font-medium mb-1">
                Iterations (Nonces)
              </label>
              <select
                value={iterations}
                onChange={(e) => setIterations(Number(e.target.value))}
                disabled={isRunning}
                className="bg-slate-800 border border-slate-700 text-slate-200 text-xs rounded-lg px-3 py-1.5 focus:outline-none focus:border-amber-500 font-mono"
              >
                <option value={10000}>10,000 hashes</option>
                <option value={50000}>50,000 hashes (recommended)</option>
                <option value={100000}>100,000 hashes</option>
                <option value={250000}>250,000 hashes</option>
              </select>
            </div>

            <div className="self-end">
              <button
                onClick={runBenchmark}
                disabled={isRunning}
                className={`inline-flex items-center space-x-2 px-4 py-2 rounded-lg font-semibold text-xs transition shadow-lg cursor-pointer ${
                  isRunning
                    ? 'bg-slate-700 text-slate-400 cursor-not-allowed'
                    : 'bg-gradient-to-r from-amber-500 to-orange-500 hover:from-amber-400 hover:to-orange-400 text-white shadow-orange-500/20'
                }`}
              >
                <Play className={`w-3.5 h-3.5 ${isRunning ? 'animate-spin' : ''}`} />
                <span>{isRunning ? 'Running Benchmark...' : 'Run Benchmark'}</span>
              </button>
            </div>
          </div>
        </div>

        {isRunning && (
          <div className="mt-4">
            <div className="flex justify-between text-xs text-slate-400 mb-1">
              <span>Hashing in progress...</span>
              <span>{progress}%</span>
            </div>
            <div className="w-full bg-slate-800 h-1.5 rounded-full overflow-hidden">
              <div
                className="bg-gradient-to-r from-amber-500 to-orange-500 h-full transition-all duration-300"
                style={{ width: `${progress}%` }}
              />
            </div>
          </div>
        )}
      </div>

      {/* Results Comparison Grid */}
      {naiveResult && midstateResult && (
        <div className="space-y-6">
          {/* Summary Metric Cards */}
          <div className="grid grid-cols-1 sm:grid-cols-3 gap-4">
            <div className="bg-slate-900 border border-slate-800 rounded-xl p-5 shadow-sm text-center">
              <span className="text-xs text-slate-400 font-medium block">Measured Speedup</span>
              <div className="flex items-center justify-center space-x-1.5 mt-1">
                <TrendingUp className="w-5 h-5 text-emerald-400" />
                <span className="text-2xl font-bold text-emerald-400 font-mono">
                  {speedup}x
                </span>
              </div>
              <span className="text-[11px] text-emerald-400/90 font-mono font-medium block mt-1">
                +{hashrateGain}% Hashrate Increase
              </span>
            </div>

            <div className="bg-slate-900 border border-slate-800 rounded-xl p-5 shadow-sm text-center">
              <span className="text-xs text-slate-400 font-medium block">Compressions Saved</span>
              <span className="text-2xl font-bold text-amber-400 font-mono mt-1 block">
                {(naiveResult.compressionsTotal - midstateResult.compressionsTotal).toLocaleString()}
              </span>
              <span className="text-[11px] text-slate-400 font-mono block mt-1">
                33.3% fewer compression cycles
              </span>
            </div>

            <div className="bg-slate-900 border border-slate-800 rounded-xl p-5 shadow-sm text-center">
              <span className="text-xs text-slate-400 font-medium block">Bitwise Parity</span>
              <div className="flex items-center justify-center space-x-1.5 mt-1">
                <CheckCircle2 className="w-5 h-5 text-blue-400" />
                <span className="text-2xl font-bold text-blue-400 font-mono">
                  {isHashMatching ? 'VERIFIED' : 'MISMATCH'}
                </span>
              </div>
              <span className="text-[11px] text-blue-300/80 font-mono block mt-1">
                Identical 32-byte final digests
              </span>
            </div>
          </div>

          {/* Detailed Side-by-Side Table */}
          <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
            {/* Naive Card */}
            <div className="bg-slate-900 border border-slate-800 rounded-xl p-5 space-y-3">
              <div className="flex items-center justify-between pb-3 border-b border-slate-800">
                <h4 className="font-bold text-slate-300 text-sm">Naive Full 80-Byte Hashing</h4>
                <span className="text-xs font-mono text-red-400 bg-red-950/40 px-2 py-0.5 rounded border border-red-900/40">
                  3 Compressions/Hash
                </span>
              </div>

              <div className="space-y-2 text-xs">
                <div className="flex justify-between py-1 border-b border-slate-800/60">
                  <span className="text-slate-400">Total Hashes:</span>
                  <span className="text-slate-200 font-mono font-medium">
                    {naiveResult.hashes.toLocaleString()}
                  </span>
                </div>
                <div className="flex justify-between py-1 border-b border-slate-800/60">
                  <span className="text-slate-400">Elapsed Time:</span>
                  <span className="text-slate-200 font-mono font-medium">
                    {naiveResult.elapsedMs.toFixed(2)} ms
                  </span>
                </div>
                <div className="flex justify-between py-1 border-b border-slate-800/60">
                  <span className="text-slate-400">Throughput:</span>
                  <span className="text-amber-400 font-mono font-bold">
                    {(naiveResult.hashrate / 1000).toFixed(2)} kH/s
                  </span>
                </div>
                <div className="flex justify-between py-1 border-b border-slate-800/60">
                  <span className="text-slate-400">Total Compressions:</span>
                  <span className="text-slate-200 font-mono">
                    {naiveResult.compressionsTotal.toLocaleString()}
                  </span>
                </div>
                <div className="pt-2">
                  <span className="text-[11px] text-slate-400 block mb-1">Last Nonce Hash:</span>
                  <div className="bg-slate-950 p-2 rounded border border-slate-800 font-mono text-[10px] text-slate-300 break-all select-all">
                    {naiveResult.sampleHash}
                  </div>
                </div>
              </div>
            </div>

            {/* Midstate Card */}
            <div className="bg-slate-900 border border-emerald-500/30 rounded-xl p-5 space-y-3">
              <div className="flex items-center justify-between pb-3 border-b border-slate-800">
                <h4 className="font-bold text-emerald-400 text-sm">Optimized Midstate Hashing</h4>
                <span className="text-xs font-mono text-emerald-400 bg-emerald-950/40 px-2 py-0.5 rounded border border-emerald-500/40">
                  2 Compressions/Hash
                </span>
              </div>

              <div className="space-y-2 text-xs">
                <div className="flex justify-between py-1 border-b border-slate-800/60">
                  <span className="text-slate-400">Total Hashes:</span>
                  <span className="text-slate-200 font-mono font-medium">
                    {midstateResult.hashes.toLocaleString()}
                  </span>
                </div>
                <div className="flex justify-between py-1 border-b border-slate-800/60">
                  <span className="text-slate-400">Elapsed Time:</span>
                  <span className="text-emerald-400 font-mono font-medium">
                    {midstateResult.elapsedMs.toFixed(2)} ms
                  </span>
                </div>
                <div className="flex justify-between py-1 border-b border-slate-800/60">
                  <span className="text-slate-400">Throughput:</span>
                  <span className="text-emerald-400 font-mono font-bold">
                    {(midstateResult.hashrate / 1000).toFixed(2)} kH/s
                  </span>
                </div>
                <div className="flex justify-between py-1 border-b border-slate-800/60">
                  <span className="text-slate-400">Total Compressions:</span>
                  <span className="text-slate-200 font-mono">
                    {midstateResult.compressionsTotal.toLocaleString()}
                  </span>
                </div>
                <div className="pt-2">
                  <span className="text-[11px] text-slate-400 block mb-1">Last Nonce Hash:</span>
                  <div className="bg-slate-950 p-2 rounded border border-slate-800 font-mono text-[10px] text-emerald-300 break-all select-all">
                    {midstateResult.sampleHash}
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>
      )}
    </div>
  );
};
