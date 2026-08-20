import React, { useState } from 'react';
import { SAMPLE_BLOCKS } from '../data/sampleBlocks';
import { hexToBytes, precomputeMidstate, bytesToHex, formatBitcoinHash, sha256dMidstate } from '../crypto/sha256d';
import { Layers, Search, Hash, Cpu, ArrowRight } from 'lucide-react';

export const BlockInspector: React.FC = () => {
  const [selectedBlockId, setSelectedBlockId] = useState<string>('block-125552');
  const [customNonce, setCustomNonce] = useState<number | ''>('');

  const currentBlock = SAMPLE_BLOCKS.find((b) => b.id === selectedBlockId) || SAMPLE_BLOCKS[0];
  const headerBytes = hexToBytes(currentBlock.hex);
  const midstateCtx = precomputeMidstate(headerBytes);

  const activeNonce = customNonce !== '' ? Number(customNonce) : currentBlock.nonce;
  const calculatedHashBytes = sha256dMidstate(midstateCtx, activeNonce);
  const calculatedDisplayHash = formatBitcoinHash(calculatedHashBytes);

  const isMatchingExpected = activeNonce === currentBlock.nonce && calculatedDisplayHash === currentBlock.expectedHash;

  return (
    <div className="space-y-6">
      {/* Header Selector */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-6 shadow-sm">
        <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4">
          <div>
            <h3 className="text-lg font-bold text-white tracking-tight flex items-center space-x-2">
              <Layers className="w-5 h-5 text-amber-400" />
              <span>Interactive Block &amp; Midstate Inspector</span>
            </h3>
            <p className="text-xs text-slate-400 mt-1">
              Select a historic block to inspect the 64-byte Midstate precomputation, 32-bit state registers, and single-nonce compression stages.
            </p>
          </div>
          <div className="flex items-center space-x-3">
            <select
              value={selectedBlockId}
              onChange={(e) => {
                setSelectedBlockId(e.target.value);
                setCustomNonce('');
              }}
              className="bg-slate-800 border border-slate-700 text-slate-200 text-xs rounded-lg px-3 py-1.5 focus:outline-none focus:border-amber-500"
            >
              {SAMPLE_BLOCKS.map((b) => (
                <option key={b.id} value={b.id}>
                  {b.name}
                </option>
              ))}
            </select>
          </div>
        </div>
      </div>

      {/* Field Breakdown */}
      <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
        {/* Left: 80-Byte Raw Header Breakdown */}
        <div className="lg:col-span-2 bg-slate-900 border border-slate-800 rounded-xl p-5 space-y-4">
          <div className="flex items-center justify-between pb-3 border-b border-slate-800">
            <h4 className="font-bold text-white text-sm">80-Byte Header Field Breakdown</h4>
            <span className="text-xs text-slate-400 font-mono">Height #{currentBlock.height}</span>
          </div>

          <div className="space-y-3 text-xs">
            {/* Version */}
            <div className="bg-slate-800/80 p-3 rounded-lg border border-slate-700/60">
              <div className="flex justify-between items-center mb-1">
                <span className="font-semibold text-blue-400">Version (4 Bytes)</span>
                <span className="font-mono text-slate-400">Offset 0..3 (Chunk 1 - Midstate)</span>
              </div>
              <p className="font-mono text-slate-200 text-[11px] break-all">
                0x{currentBlock.version.toString(16).padStart(8, '0')} (decimal {currentBlock.version})
              </p>
            </div>

            {/* PrevBlockHash */}
            <div className="bg-slate-800/80 p-3 rounded-lg border border-slate-700/60">
              <div className="flex justify-between items-center mb-1">
                <span className="font-semibold text-purple-400">Previous Block Hash (32 Bytes)</span>
                <span className="font-mono text-slate-400">Offset 4..35 (Chunk 1 - Midstate)</span>
              </div>
              <p className="font-mono text-slate-200 text-[11px] break-all select-all">
                {currentBlock.prevBlockHash}
              </p>
            </div>

            {/* Merkle Root */}
            <div className="bg-slate-800/80 p-3 rounded-lg border border-slate-700/60">
              <div className="flex justify-between items-center mb-1">
                <span className="font-semibold text-teal-400">Merkle Root (32 Bytes)</span>
                <span className="font-mono text-slate-400">Offset 36..67 (Split across Chunks 1 &amp; 2!)</span>
              </div>
              <p className="font-mono text-slate-200 text-[11px] break-all select-all">
                {currentBlock.merkleRoot}
              </p>
              <div className="mt-2 text-[10px] text-slate-400 flex items-center space-x-2">
                <span className="bg-teal-950/80 px-1.5 py-0.5 rounded text-teal-300 border border-teal-800/50">
                  Bytes 0..27: In Midstate
                </span>
                <span className="bg-amber-950/80 px-1.5 py-0.5 rounded text-amber-300 border border-amber-800/50">
                  Bytes 28..31: In Block 2
                </span>
              </div>
            </div>

            {/* Timestamp, nBits, Nonce */}
            <div className="grid grid-cols-1 sm:grid-cols-3 gap-3">
              <div className="bg-slate-800/80 p-3 rounded-lg border border-slate-700/60">
                <span className="font-semibold text-indigo-400 block mb-0.5">Timestamp (4B)</span>
                <span className="font-mono text-slate-200 text-[11px] block">{currentBlock.timestamp}</span>
                <span className="text-[10px] text-slate-400 block mt-0.5">{currentBlock.timestampFormatted}</span>
              </div>
              <div className="bg-slate-800/80 p-3 rounded-lg border border-slate-700/60">
                <span className="font-semibold text-amber-400 block mb-0.5">nBits (4B)</span>
                <span className="font-mono text-slate-200 text-[11px] block">0x{currentBlock.nbits}</span>
                <span className="text-[10px] text-slate-400 block mt-0.5">Difficulty target</span>
              </div>
              <div className="bg-orange-950/30 p-3 rounded-lg border border-orange-500/30">
                <span className="font-semibold text-orange-400 block mb-0.5">Nonce (4B)</span>
                <div className="flex items-center space-x-1.5">
                  <input
                    type="number"
                    value={activeNonce}
                    onChange={(e) => setCustomNonce(e.target.value === '' ? '' : Number(e.target.value))}
                    className="bg-slate-900 border border-slate-700 text-orange-300 font-mono text-xs px-2 py-0.5 rounded w-full focus:outline-none focus:border-orange-400"
                  />
                </div>
                <span className="text-[10px] text-orange-400/80 block mt-0.5">Dynamic Search Word</span>
              </div>
            </div>
          </div>
        </div>

        {/* Right: Precomputed Midstate 8-Word Internal State */}
        <div className="bg-slate-900 border border-emerald-500/30 rounded-xl p-5 space-y-4">
          <div className="flex items-center justify-between pb-3 border-b border-slate-800">
            <h4 className="font-bold text-emerald-400 text-sm">Precomputed Midstate Registers</h4>
            <span className="text-[11px] text-emerald-400/80 font-mono">uint32_t state[8]</span>
          </div>

          <p className="text-xs text-slate-400">
            These 8 &times; 32-bit words represent the state of the SHA-256 compression function after the first 64 invariant bytes:
          </p>

          <div className="grid grid-cols-2 gap-2 text-xs font-mono">
            {midstateCtx.midstate.map((word, idx) => (
              <div key={idx} className="bg-slate-950 p-2 rounded border border-slate-800 flex justify-between">
                <span className="text-slate-500 font-semibold">state[{idx}]</span>
                <span className="text-emerald-300 font-bold">
                  0x{(word >>> 0).toString(16).padStart(8, '0')}
                </span>
              </div>
            ))}
          </div>

          {/* Evaluated Hash Output */}
          <div className="pt-2 border-t border-slate-800 space-y-2">
            <div className="flex items-center justify-between">
              <span className="text-xs font-semibold text-slate-300">Target Nonce Hash:</span>
              <span
                className={`text-[10px] px-2 py-0.5 rounded font-mono font-semibold ${
                  isMatchingExpected
                    ? 'bg-emerald-500/20 text-emerald-400 border border-emerald-500/30'
                    : 'bg-slate-800 text-slate-400'
                }`}
              >
                {isMatchingExpected ? 'TARGET MATCH' : 'NONCE ACTIVE'}
              </span>
            </div>
            <div className="bg-slate-950 p-2.5 rounded border border-slate-800 font-mono text-[10px] text-amber-300 break-all select-all">
              {calculatedDisplayHash}
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};
