import React, { useState } from 'react';
import { Copy, Check, Download, Terminal, FileCode, Sparkles } from 'lucide-react';
import { OPTIMIZED_C_SOURCE } from '../data/cSourceCode';

interface CodeViewerProps {
  onDownloadCFile: () => void;
}

export const CodeViewer: React.FC<CodeViewerProps> = ({ onDownloadCFile }) => {
  const [copied, setCopied] = useState<boolean>(false);

  const handleCopy = () => {
    navigator.clipboard.writeText(OPTIMIZED_C_SOURCE);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  return (
    <div className="space-y-6">
      {/* Compilation & Optimization Quick Guide */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
        <div className="bg-slate-900 border border-slate-800 rounded-xl p-5 shadow-sm space-y-3">
          <div className="flex items-center space-x-2 text-amber-400">
            <Terminal className="w-4 h-4" />
            <h4 className="text-sm font-bold text-white">Native Build (Amlogic S905X / ARM Cortex-A53)</h4>
          </div>
          <p className="text-xs text-slate-400">
            Compile natively with ARMv8 Cryptography Extensions hardware instructions (SHA256H, SHA256H2, SHA256SU0, SHA256SU1) and POSIX threads:
          </p>
          <pre className="bg-slate-950 p-2.5 rounded border border-slate-800 text-emerald-400 font-mono text-[11px] overflow-x-auto select-all">
            gcc -O3 -march=armv8-a+crypto -pthread -o bitcoin_sha256d_s905x bitcoin_sha256d_s905x.c
          </pre>
        </div>

        <div className="bg-slate-900 border border-slate-800 rounded-xl p-5 shadow-sm space-y-3">
          <div className="flex items-center space-x-2 text-blue-400">
            <Terminal className="w-4 h-4" />
            <h4 className="text-sm font-bold text-white">Execution Commands</h4>
          </div>
          <p className="text-xs text-slate-400">
            Run correctness tests and multi-threaded mining benchmarks:
          </p>
          <div className="space-y-1.5 font-mono text-[11px]">
            <div className="bg-slate-950 px-2 py-1 rounded border border-slate-800 text-slate-300">
              ./bitcoin_sha256d_s905x -t <span className="text-slate-500"># Correctness test suite</span>
            </div>
            <div className="bg-slate-950 px-2 py-1 rounded border border-slate-800 text-slate-300">
              ./bitcoin_sha256d_s905x -b -n 20000000 <span className="text-slate-500"># 4-core benchmark</span>
            </div>
          </div>
        </div>
      </div>

      {/* Code Display */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl overflow-hidden shadow-sm">
        <div className="flex items-center justify-between px-5 py-3 bg-slate-950 border-b border-slate-800">
          <div className="flex items-center space-x-2">
            <FileCode className="w-4 h-4 text-orange-400" />
            <span className="font-mono text-xs font-semibold text-slate-200">
              bitcoin_sha256d_s905x.c
            </span>
          </div>
          <div className="flex items-center space-x-2">
            <button
              onClick={handleCopy}
              className="inline-flex items-center space-x-1 px-2.5 py-1 rounded bg-slate-800 hover:bg-slate-700 text-slate-300 text-xs transition cursor-pointer"
            >
              {copied ? <Check className="w-3.5 h-3.5 text-emerald-400" /> : <Copy className="w-3.5 h-3.5" />}
              <span>{copied ? 'Copied' : 'Copy Code'}</span>
            </button>
            <button
              onClick={onDownloadCFile}
              className="inline-flex items-center space-x-1 px-2.5 py-1 rounded bg-orange-600 hover:bg-orange-500 text-white text-xs font-semibold transition cursor-pointer"
            >
              <Download className="w-3.5 h-3.5" />
              <span>Download</span>
            </button>
          </div>
        </div>

        <pre className="p-5 font-mono text-xs text-slate-300 bg-slate-950 overflow-x-auto max-h-[600px] leading-relaxed select-all">
          <code>{OPTIMIZED_C_SOURCE}</code>
        </pre>
      </div>
    </div>
  );
};
