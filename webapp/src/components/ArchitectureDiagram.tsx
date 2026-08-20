import React from 'react';
import { ArrowRight, Check, Zap, AlertTriangle, ShieldCheck, Cpu } from 'lucide-react';

export const ArchitectureDiagram: React.FC = () => {
  return (
    <div className="space-y-6">
      {/* Overview Banner */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-6 shadow-sm">
        <div className="flex flex-col lg:flex-row lg:items-center lg:justify-between gap-4">
          <div>
            <div className="inline-flex items-center space-x-2 px-2.5 py-1 rounded bg-amber-500/10 border border-amber-500/20 text-amber-400 text-xs font-semibold uppercase tracking-wider mb-2">
              <Zap className="w-3.5 h-3.5" />
              <span>Core Optimization Principle</span>
            </div>
            <h2 className="text-xl font-bold text-white tracking-tight">
              Why Re-Hashing the Entire 80-Byte Header Is Wasteful
            </h2>
            <p className="text-slate-300 text-sm mt-1 max-w-3xl leading-relaxed">
              A Bitcoin block header is exactly 80 bytes long. When searching through the <code className="bg-slate-800 px-1 py-0.5 rounded text-amber-300 font-mono">nonce</code> space (or evaluating millions of hashes per second), 
              the first 64 bytes (<span className="text-emerald-400 font-medium">Version</span>, <span className="text-emerald-400 font-medium">PrevBlockHash</span>, and first 28 bytes of <span className="text-emerald-400 font-medium">MerkleRoot</span>) 
              <strong> never change</strong>!
            </p>
          </div>
          <div className="grid grid-cols-2 sm:grid-cols-3 gap-3">
            <div className="bg-slate-800/80 p-3 rounded-lg border border-slate-700/60 text-center">
              <span className="text-xs text-slate-400 block font-medium">Compressions / Hash</span>
              <span className="text-xl font-bold text-amber-400 mt-0.5 block">3 &rarr; 2</span>
              <span className="text-[11px] text-emerald-400 font-mono font-medium">-33.3% Ops</span>
            </div>
            <div className="bg-slate-800/80 p-3 rounded-lg border border-slate-700/60 text-center">
              <span className="text-xs text-slate-400 block font-medium">Speedup Potential</span>
              <span className="text-xl font-bold text-emerald-400 mt-0.5 block">~1.45x &ndash; 1.50x</span>
              <span className="text-[11px] text-slate-400 font-mono">On A53 / x86</span>
            </div>
            <div className="bg-slate-800/80 p-3 rounded-lg border border-slate-700/60 text-center col-span-2 sm:col-span-1">
              <span className="text-xs text-slate-400 block font-medium">Bitwise Parity</span>
              <span className="text-xl font-bold text-blue-400 mt-0.5 block">100%</span>
              <span className="text-[11px] text-blue-300 font-mono">Exact Match</span>
            </div>
          </div>
        </div>
      </div>

      {/* Side-by-Side Execution Pipeline Comparison */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        {/* Naive Pipeline */}
        <div className="bg-slate-900/90 border border-red-500/20 rounded-xl p-5 shadow-sm space-y-4">
          <div className="flex items-center justify-between pb-3 border-b border-slate-800">
            <div className="flex items-center space-x-2">
              <span className="w-2.5 h-2.5 rounded-full bg-red-500 animate-pulse" />
              <h3 className="font-bold text-white text-base">Naive Full Hashing (Original)</h3>
            </div>
            <span className="text-xs px-2 py-0.5 rounded bg-red-500/20 text-red-400 border border-red-500/30 font-mono font-semibold">
              3 Compressions / Hash
            </span>
          </div>

          <div className="space-y-3">
            {/* Step 1 */}
            <div className="bg-slate-800/90 p-3 rounded-lg border border-slate-700/70">
              <div className="flex items-center justify-between text-xs mb-1">
                <span className="font-semibold text-slate-200">1. Compress Block 1 (Bytes 0..63)</span>
                <span className="text-red-400 font-mono text-[11px]">Repeated every nonce!</span>
              </div>
              <p className="text-xs text-slate-400">
                Hashes Version (4B) + PrevBlockHash (32B) + Merkle[0..27] (28B). Identical result every single nonce.
              </p>
            </div>

            {/* Step 2 */}
            <div className="bg-slate-800/90 p-3 rounded-lg border border-slate-700/70">
              <div className="flex items-center justify-between text-xs mb-1">
                <span className="font-semibold text-slate-200">2. Compress Block 2 (Bytes 64..79 + Padding)</span>
                <span className="text-slate-400 font-mono text-[11px]">Dynamic Nonce</span>
              </div>
              <p className="text-xs text-slate-400">
                Hashes Merkle[28..31] + Time + Bits + Nonce + 0x80 padding + 640-bit length trailer.
              </p>
            </div>

            {/* Step 3 */}
            <div className="bg-slate-800/90 p-3 rounded-lg border border-slate-700/70">
              <div className="flex items-center justify-between text-xs mb-1">
                <span className="font-semibold text-slate-200">3. Second SHA-256 Pass (32B Digest + Padding)</span>
                <span className="text-slate-400 font-mono text-[11px]">Final Hash</span>
              </div>
              <p className="text-xs text-slate-400">
                Hashes the intermediate 32-byte hash with 0x80 padding + 256-bit length trailer.
              </p>
            </div>
          </div>

          <div className="p-3 bg-red-950/30 border border-red-900/50 rounded-lg text-xs text-red-300">
            <strong>Bottleneck:</strong> Computes 192 rounds of SHA-256 (64 rounds &times; 3) plus full buffer serialization and length calculations per nonce.
          </div>
        </div>

        {/* Optimized Midstate Pipeline */}
        <div className="bg-slate-900/90 border border-emerald-500/30 rounded-xl p-5 shadow-sm space-y-4">
          <div className="flex items-center justify-between pb-3 border-b border-slate-800">
            <div className="flex items-center space-x-2">
              <span className="w-2.5 h-2.5 rounded-full bg-emerald-500" />
              <h3 className="font-bold text-white text-base">Optimized Midstate Pipeline</h3>
            </div>
            <span className="text-xs px-2 py-0.5 rounded bg-emerald-500/20 text-emerald-400 border border-emerald-500/30 font-mono font-semibold">
              2 Compressions / Hash
            </span>
          </div>

          <div className="space-y-3">
            {/* Setup */}
            <div className="bg-emerald-950/30 p-3 rounded-lg border border-emerald-500/30">
              <div className="flex items-center justify-between text-xs mb-1">
                <span className="font-semibold text-emerald-300">Precomputation: Compute Midstate ONCE</span>
                <span className="text-emerald-400 font-mono text-[11px]">Executed 1x per Job</span>
              </div>
              <p className="text-xs text-emerald-200/80">
                Computes SHA-256 compression on invariant Block 1 once. Stores 8-word <code className="font-mono text-amber-300">midstate[8]</code> and pre-fills static padding.
              </p>
            </div>

            {/* Inner Loop Step 1 */}
            <div className="bg-slate-800/90 p-3 rounded-lg border border-slate-700/70">
              <div className="flex items-center justify-between text-xs mb-1">
                <span className="font-semibold text-slate-200">1. Compress Block 2 from Midstate</span>
                <span className="text-emerald-400 font-mono text-[11px]">1st Hot Loop Compress</span>
              </div>
              <p className="text-xs text-slate-400">
                Initializes internal state from cached <code className="font-mono text-amber-300">midstate</code>, writes 4-byte nonce, and compresses only Block 2!
              </p>
            </div>

            {/* Inner Loop Step 2 */}
            <div className="bg-slate-800/90 p-3 rounded-lg border border-slate-700/70">
              <div className="flex items-center justify-between text-xs mb-1">
                <span className="font-semibold text-slate-200">2. Second SHA-256 Pass (Pre-padded Block)</span>
                <span className="text-emerald-400 font-mono text-[11px]">2nd Hot Loop Compress</span>
              </div>
              <p className="text-xs text-slate-400">
                Compresses intermediate digest into final hash using static padding template with zero stream overhead.
              </p>
            </div>
          </div>

          <div className="p-3 bg-emerald-950/30 border border-emerald-900/50 rounded-lg text-xs text-emerald-300">
            <strong>Result:</strong> Only 128 rounds of SHA-256 (64 rounds &times; 2). 33.3% fewer instructions executed on every single nonce!
          </div>
        </div>
      </div>

      {/* 80-Byte Header Visual Decomposition */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-6 shadow-sm">
        <h3 className="text-base font-bold text-white mb-3">
          Bitcoin 80-Byte Header Byte-Level Memory Layout
        </h3>
        <p className="text-xs text-slate-400 mb-4">
          Visual breakdown of how an 80-byte Bitcoin block header maps into the two 64-byte SHA-256 chunks:
        </p>

        {/* Chunks */}
        <div className="space-y-4">
          {/* Chunk 1 */}
          <div className="bg-slate-950 border border-slate-800 rounded-lg p-4">
            <div className="flex items-center justify-between text-xs mb-2">
              <span className="font-mono font-semibold text-amber-400">
                Chunk 1 (Bytes 0 &ndash; 63) &rarr; PRECOMPUTED MIDSTATE
              </span>
              <span className="text-emerald-400 text-xs font-mono font-medium bg-emerald-500/10 px-2 py-0.5 rounded border border-emerald-500/20">
                100% Invariant across Nonces
              </span>
            </div>
            <div className="grid grid-cols-12 gap-1.5 text-center text-xs font-mono">
              <div className="col-span-1 bg-blue-900/40 border border-blue-500/40 p-2 rounded text-blue-200">
                <span className="block text-[10px] text-blue-400">Ver</span>
                <span className="text-[11px] font-bold">4B</span>
              </div>
              <div className="col-span-6 bg-purple-900/40 border border-purple-500/40 p-2 rounded text-purple-200">
                <span className="block text-[10px] text-purple-400">Previous Block Hash</span>
                <span className="text-[11px] font-bold">32 Bytes</span>
              </div>
              <div className="col-span-5 bg-teal-900/40 border border-teal-500/40 p-2 rounded text-teal-200">
                <span className="block text-[10px] text-teal-400">Merkle Root [0..27]</span>
                <span className="text-[11px] font-bold">28 Bytes</span>
              </div>
            </div>
          </div>

          {/* Chunk 2 */}
          <div className="bg-slate-950 border border-slate-800 rounded-lg p-4">
            <div className="flex items-center justify-between text-xs mb-2">
              <span className="font-mono font-semibold text-amber-400">
                Chunk 2 (Bytes 64 &ndash; 79 + 48-byte SHA-256 Padding)
              </span>
              <span className="text-amber-400 text-xs font-mono font-medium bg-amber-500/10 px-2 py-0.5 rounded border border-amber-500/20">
                Only Nonce (4B) changes!
              </span>
            </div>
            <div className="grid grid-cols-12 gap-1.5 text-center text-xs font-mono">
              <div className="col-span-1 bg-teal-900/40 border border-teal-500/40 p-2 rounded text-teal-200">
                <span className="block text-[10px] text-teal-400">Mrkl[28..31]</span>
                <span className="text-[11px] font-bold">4B</span>
              </div>
              <div className="col-span-1 bg-indigo-900/40 border border-indigo-500/40 p-2 rounded text-indigo-200">
                <span className="block text-[10px] text-indigo-400">Time</span>
                <span className="text-[11px] font-bold">4B</span>
              </div>
              <div className="col-span-1 bg-amber-900/40 border border-amber-500/40 p-2 rounded text-amber-200">
                <span className="block text-[10px] text-amber-400">Bits</span>
                <span className="text-[11px] font-bold">4B</span>
              </div>
              <div className="col-span-2 bg-orange-600 border border-orange-400 p-2 rounded text-white shadow-md">
                <span className="block text-[10px] text-orange-200 font-bold">NONCE</span>
                <span className="text-[11px] font-bold">4B (Hot)</span>
              </div>
              <div className="col-span-1 bg-slate-800 border border-slate-700 p-2 rounded text-slate-300">
                <span className="block text-[10px] text-slate-400">0x80</span>
                <span className="text-[11px] font-bold">1B</span>
              </div>
              <div className="col-span-4 bg-slate-800/60 border border-slate-700/60 p-2 rounded text-slate-400">
                <span className="block text-[10px] text-slate-400">Zeros</span>
                <span className="text-[11px]">39 Bytes</span>
              </div>
              <div className="col-span-2 bg-slate-800 border border-slate-700 p-2 rounded text-slate-300">
                <span className="block text-[10px] text-slate-400">Length (640b)</span>
                <span className="text-[11px] font-bold">8B</span>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};
