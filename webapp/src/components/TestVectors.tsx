import React, { useState, useEffect } from 'react';
import { SAMPLE_BLOCKS } from '../data/sampleBlocks';
import { hexToBytes, sha256dNaive, precomputeMidstate, sha256dMidstate, formatBitcoinHash } from '../crypto/sha256d';
import { CheckCircle2, XCircle, Play, ShieldCheck, Cpu } from 'lucide-react';
import { TestResult } from '../types';

export const TestVectors: React.FC = () => {
  const [results, setResults] = useState<TestResult[]>([]);
  const [isRunning, setIsRunning] = useState<boolean>(false);

  const runAllTests = () => {
    setIsRunning(true);

    setTimeout(() => {
      const testList: TestResult[] = [];

      // Test 1: Block 125552
      const b1 = SAMPLE_BLOCKS[0];
      const h1 = hexToBytes(b1.hex);
      const naiveHash1 = formatBitcoinHash(sha256dNaive(h1));
      const ms1 = precomputeMidstate(h1);
      const midstateHash1 = formatBitcoinHash(sha256dMidstate(ms1, b1.nonce));

      testList.push({
        id: 'test-1',
        name: 'Test 1: Block 125552 Header Hashing',
        description: 'Classic Bitcoin test vector (Hal Finney era). Validates full 80-byte SHA-256d vs optimized midstate path.',
        fullHash: naiveHash1,
        midstateHash: midstateHash1,
        expectedHash: b1.expectedHash,
        passed: naiveHash1 === b1.expectedHash && midstateHash1 === b1.expectedHash,
      });

      // Test 2: Genesis Block (Block 0)
      const b0 = SAMPLE_BLOCKS[1];
      const h0 = hexToBytes(b0.hex);
      const naiveHash0 = formatBitcoinHash(sha256dNaive(h0));
      const ms0 = precomputeMidstate(h0);
      const midstateHash0 = formatBitcoinHash(sha256dMidstate(ms0, b0.nonce));

      testList.push({
        id: 'test-2',
        name: 'Test 2: Bitcoin Genesis Block (Block 0)',
        description: 'Satoshi Nakamoto Genesis block header. Tests all-zero prevHash and initial coin reward merkle root.',
        fullHash: naiveHash0,
        midstateHash: midstateHash0,
        expectedHash: b0.expectedHash,
        passed: naiveHash0 === b0.expectedHash && midstateHash0 === b0.expectedHash,
      });

      // Test 3: Taproot Block 700000
      const b7 = SAMPLE_BLOCKS[2];
      const h7 = hexToBytes(b7.hex);
      const naiveHash7 = formatBitcoinHash(sha256dNaive(h7));
      const ms7 = precomputeMidstate(h7);
      const midstateHash7 = formatBitcoinHash(sha256dMidstate(ms7, b7.nonce));

      testList.push({
        id: 'test-3',
        name: 'Test 3: Block 700000 (Modern Taproot Era)',
        description: 'Modern block header with BIP9 version bits (0x2000e000) and high difficulty.',
        fullHash: naiveHash7,
        midstateHash: midstateHash7,
        expectedHash: b7.expectedHash,
        passed: naiveHash7 === b7.expectedHash && midstateHash7 === b7.expectedHash,
      });

      setResults(testList);
      setIsRunning(false);
    }, 50);
  };

  useEffect(() => {
    runAllTests();
  }, []);

  const allPassed = results.length > 0 && results.every((r) => r.passed);

  return (
    <div className="space-y-6">
      {/* Test Banner */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-6 shadow-sm">
        <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4">
          <div>
            <div className="flex items-center space-x-2">
              <ShieldCheck className="w-5 h-5 text-emerald-400" />
              <h3 className="text-lg font-bold text-white tracking-tight">
                Correctness &amp; Compatibility Test Suite
              </h3>
            </div>
            <p className="text-xs text-slate-400 mt-1">
              Validates that the optimized midstate precomputation algorithm produces 100% bit-exact results across all standard Bitcoin test vectors.
            </p>
          </div>
          <button
            onClick={runAllTests}
            disabled={isRunning}
            className="inline-flex items-center space-x-1.5 px-3.5 py-1.5 rounded-lg bg-slate-800 hover:bg-slate-700 text-slate-200 text-xs font-semibold border border-slate-700 transition cursor-pointer"
          >
            <Play className={`w-3.5 h-3.5 ${isRunning ? 'animate-spin' : ''}`} />
            <span>{isRunning ? 'Running Tests...' : 'Re-run All Tests'}</span>
          </button>
        </div>
      </div>

      {/* Tests List */}
      <div className="space-y-4">
        {results.map((test) => (
          <div
            key={test.id}
            className={`bg-slate-900 border rounded-xl p-5 shadow-sm transition ${
              test.passed ? 'border-emerald-500/30' : 'border-red-500/30'
            }`}
          >
            <div className="flex items-center justify-between pb-3 border-b border-slate-800">
              <div className="flex items-center space-x-2.5">
                {test.passed ? (
                  <CheckCircle2 className="w-5 h-5 text-emerald-400" />
                ) : (
                  <XCircle className="w-5 h-5 text-red-400" />
                )}
                <div>
                  <h4 className="text-sm font-bold text-white">{test.name}</h4>
                  <p className="text-xs text-slate-400">{test.description}</p>
                </div>
              </div>
              <span
                className={`text-xs px-2.5 py-1 rounded font-mono font-bold uppercase tracking-wider ${
                  test.passed
                    ? 'bg-emerald-500/10 text-emerald-400 border border-emerald-500/20'
                    : 'bg-red-500/10 text-red-400 border border-red-500/20'
                }`}
              >
                {test.passed ? 'PASS' : 'FAIL'}
              </span>
            </div>

            <div className="mt-3 space-y-2 text-xs font-mono">
              <div>
                <span className="text-slate-500 block text-[10px]">Expected Hash:</span>
                <div className="text-slate-300 bg-slate-950 p-2 rounded border border-slate-800 text-[11px] break-all select-all">
                  {test.expectedHash}
                </div>
              </div>
              <div className="grid grid-cols-1 sm:grid-cols-2 gap-2">
                <div>
                  <span className="text-slate-500 block text-[10px]">Full SHA-256d Output:</span>
                  <div className="text-slate-300 bg-slate-950 p-2 rounded border border-slate-800 text-[11px] break-all select-all">
                    {test.fullHash}
                  </div>
                </div>
                <div>
                  <span className="text-slate-500 block text-[10px]">Optimized Midstate Output:</span>
                  <div className="text-emerald-400 bg-slate-950 p-2 rounded border border-slate-800 text-[11px] break-all select-all">
                    {test.midstateHash}
                  </div>
                </div>
              </div>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
};
