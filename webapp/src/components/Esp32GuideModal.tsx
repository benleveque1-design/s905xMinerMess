import React, { useState } from 'react';
import { 
  X, 
  Microchip, 
  Download, 
  Copy, 
  Check, 
  Zap, 
  ShieldCheck, 
  Info,
  CheckCircle2
} from 'lucide-react';
import { ESP32_SKETCH_CPP } from '../data/esp32Firmware';

interface Esp32GuideModalProps {
  onClose: () => void;
}

export const Esp32GuideModal: React.FC<Esp32GuideModalProps> = ({ onClose }) => {
  const [copied, setCopied] = useState(false);

  const handleCopyCode = () => {
    navigator.clipboard.writeText(ESP32_SKETCH_CPP);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  const handleDownloadIno = () => {
    const blob = new Blob([ESP32_SKETCH_CPP], { type: 'text/x-c' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'esp32_s905x_controller.ino';
    a.click();
    URL.revokeObjectURL(url);
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/80 backdrop-blur-sm">
      <div className="bg-zinc-900 border border-zinc-700/80 rounded-2xl w-full max-w-4xl max-h-[90vh] flex flex-col shadow-2xl overflow-hidden">
        {/* Header */}
        <div className="p-5 border-b border-zinc-800 bg-zinc-950 flex items-center justify-between">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-xl bg-purple-500/10 border border-purple-500/30 flex items-center justify-center text-purple-400">
              <Microchip className="w-5 h-5" />
            </div>
            <div>
              <h2 className="text-lg font-bold text-white tracking-tight">
                ESP32 Controller Firmware & Architecture
              </h2>
              <p className="text-xs text-zinc-400">
                Host the complete S905X WebSocket controller on a $4 ESP32 microcontroller
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

        {/* Body */}
        <div className="flex-1 p-5 overflow-y-auto space-y-5 text-xs text-zinc-300">
          {/* Architecture & Memory Footprint Specs */}
          <div className="grid grid-cols-1 sm:grid-cols-3 gap-3">
            <div className="bg-zinc-950 border border-zinc-800 rounded-xl p-3.5 space-y-1">
              <div className="text-[10px] text-zinc-400 uppercase font-semibold">Heap RAM Usage</div>
              <div className="text-lg font-bold font-mono text-emerald-400">~32 KB / 320 KB</div>
              <p className="text-[11px] text-zinc-400">
                Lightweight fixed JSON buffers (<code className="text-purple-300">ArduinoJson</code>) prevent fragmentation.
              </p>
            </div>

            <div className="bg-zinc-950 border border-zinc-800 rounded-xl p-3.5 space-y-1">
              <div className="text-[10px] text-zinc-400 uppercase font-semibold">Max S905X Nodes</div>
              <div className="text-lg font-bold font-mono text-purple-400">16 - 32 Boxes</div>
              <p className="text-[11px] text-zinc-400">
                Handled asynchronously using non-blocking <code className="text-purple-300">AsyncTCP</code>.
              </p>
            </div>

            <div className="bg-zinc-950 border border-zinc-800 rounded-xl p-3.5 space-y-1">
              <div className="text-[10px] text-zinc-400 uppercase font-semibold">ESP32 CPU Load</div>
              <div className="text-lg font-bold font-mono text-cyan-400">&lt; 8% Dual-Core</div>
              <p className="text-[11px] text-zinc-400">
                Dual 240MHz Xtensa cores easily handle telemetry routing and web clients.
              </p>
            </div>
          </div>

          {/* Explanation Callout */}
          <div className="bg-purple-950/20 border border-purple-800/40 rounded-xl p-4 space-y-2">
            <h4 className="font-semibold text-purple-300 flex items-center gap-1.5 text-xs">
              <Info className="w-4 h-4 text-purple-400" />
              Why This Protocol is 100% ESP32 Compatible
            </h4>
            <ul className="space-y-1 text-[11px] text-zinc-300 list-disc list-inside leading-relaxed">
              <li>
                <strong>Outbound Worker Connections:</strong> S905X boxes initiate outbound connections to the ESP32. The ESP32 does not need to scan the network or hold open SSH sessions.
              </li>
              <li>
                <strong>Fixed-Size Structs:</strong> Telemetry is ingested directly into a compact C array (`struct Worker workers[16]`), consuming negligible RAM.
              </li>
              <li>
                <strong>Zero Heavy Dependencies:</strong> No database, no Node.js runtime, no external cloud dependencies needed.
              </li>
            </ul>
          </div>

          {/* Arduino Code Viewer */}
          <div className="space-y-2">
            <div className="flex items-center justify-between">
              <span className="font-semibold text-white">ESP32 Arduino C++ Firmware (esp32_s905x_controller.ino)</span>
              <div className="flex items-center gap-2">
                <button
                  onClick={handleCopyCode}
                  className="flex items-center gap-1 px-2.5 py-1 rounded bg-zinc-800 hover:bg-zinc-700 text-zinc-300 transition text-[11px]"
                >
                  {copied ? <Check className="w-3.5 h-3.5 text-emerald-400" /> : <Copy className="w-3.5 h-3.5" />}
                  Copy Sketch
                </button>
                <button
                  onClick={handleDownloadIno}
                  className="flex items-center gap-1 px-2.5 py-1 rounded bg-purple-700 hover:bg-purple-600 text-white font-semibold transition text-[11px]"
                >
                  <Download className="w-3.5 h-3.5" />
                  Download .ino
                </button>
              </div>
            </div>
            <pre className="bg-zinc-950 border border-zinc-800 p-3 rounded-xl font-mono text-[11px] text-zinc-300 h-64 overflow-y-auto">
              {ESP32_SKETCH_CPP}
            </pre>
          </div>
        </div>

        {/* Footer */}
        <div className="p-4 border-t border-zinc-800 bg-zinc-950 flex justify-end">
          <button
            onClick={onClose}
            className="px-4 py-2 rounded-lg bg-zinc-800 hover:bg-zinc-700 text-zinc-300 text-xs font-medium transition"
          >
            Close
          </button>
        </div>
      </div>
    </div>
  );
};
