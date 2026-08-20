import React, { useState } from 'react';
import { 
  X, 
  Download, 
  Copy, 
  Check, 
  Terminal, 
  FileText, 
  Layers, 
  ShieldCheck,
  Server
} from 'lucide-react';
import { S905X_PYTHON_AGENT, S905X_SYSTEMD_SERVICE } from '../data/agentScript';

interface AgentInstallerModalProps {
  onClose: () => void;
}

export const AgentInstallerModal: React.FC<AgentInstallerModalProps> = ({ onClose }) => {
  const [copiedSection, setCopiedSection] = useState<string | null>(null);

  const handleCopy = (text: string, id: string) => {
    navigator.clipboard.writeText(text);
    setCopiedSection(id);
    setTimeout(() => setCopiedSection(null), 2000);
  };

  const handleDownloadAgent = () => {
    const blob = new Blob([S905X_PYTHON_AGENT], { type: 'text/x-python' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 's905x_agent.py';
    a.click();
    URL.revokeObjectURL(url);
  };

  const handleDownloadService = () => {
    const blob = new Blob([S905X_SYSTEMD_SERVICE], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 's905x-agent.service';
    a.click();
    URL.revokeObjectURL(url);
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/80 backdrop-blur-sm">
      <div className="bg-zinc-900 border border-zinc-700/80 rounded-2xl w-full max-w-3xl max-h-[90vh] flex flex-col shadow-2xl overflow-hidden">
        {/* Header */}
        <div className="p-5 border-b border-zinc-800 bg-zinc-950 flex items-center justify-between">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-xl bg-emerald-500/10 border border-emerald-500/30 flex items-center justify-center text-emerald-400">
              <Terminal className="w-5 h-5" />
            </div>
            <div>
              <h2 className="text-lg font-bold text-white tracking-tight">
                S905X Worker Agent Setup Guide
              </h2>
              <p className="text-xs text-zinc-400">
                Lightweight outbound daemon for Amlogic S905X (Armbian / Linux / AArch64)
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
        <div className="flex-1 p-5 overflow-y-auto space-y-6 text-xs text-zinc-300">
          {/* Quick 3-Step Setup */}
          <div className="space-y-4">
            <h3 className="text-sm font-bold text-white flex items-center gap-2">
              <Server className="w-4 h-4 text-emerald-400" />
              Quick Setup on Armbian / S905X Box
            </h3>

            {/* Step 1 */}
            <div className="bg-zinc-950 border border-zinc-800 rounded-xl p-4 space-y-2">
              <div className="flex items-center justify-between">
                <span className="font-semibold text-white">1. Compile the Hardware Crypto Miner</span>
                <button
                  onClick={() => handleCopy('gcc -O3 -march=armv8-a+crypto -pthread -o bitcoin_sha256d_s905x bitcoin_sha256d_s905x.c', 'step1')}
                  className="flex items-center gap-1 text-[11px] text-zinc-400 hover:text-white"
                >
                  {copiedSection === 'step1' ? <Check className="w-3.5 h-3.5 text-emerald-400" /> : <Copy className="w-3.5 h-3.5" />}
                  Copy
                </button>
              </div>
              <pre className="bg-zinc-900 p-2.5 rounded text-emerald-300 font-mono text-[11px] overflow-x-auto">
gcc -O3 -march=armv8-a+crypto -pthread -o bitcoin_sha256d_s905x bitcoin_sha256d_s905x.c
              </pre>
            </div>

            {/* Step 2 */}
            <div className="bg-zinc-950 border border-zinc-800 rounded-xl p-4 space-y-2">
              <div className="flex items-center justify-between">
                <span className="font-semibold text-white">2. Launch Worker Agent Daemon</span>
                <button
                  onClick={() => handleCopy('python3 s905x_agent.py --server ws://<CONTROLLER_IP>:3010/ws/worker --id s905x-01 --token s905x_secret_token', 'step2')}
                  className="flex items-center gap-1 text-[11px] text-zinc-400 hover:text-white"
                >
                  {copiedSection === 'step2' ? <Check className="w-3.5 h-3.5 text-emerald-400" /> : <Copy className="w-3.5 h-3.5" />}
                  Copy
                </button>
              </div>
              <pre className="bg-zinc-900 p-2.5 rounded text-emerald-300 font-mono text-[11px] overflow-x-auto">
python3 s905x_agent.py --server ws://&lt;CONTROLLER_IP&gt;:3010/ws/worker --id s905x-01 --token s905x_secret_token
              </pre>
              <p className="text-[11px] text-zinc-400">
                Replace <code className="text-orange-300">&lt;CONTROLLER_IP&gt;</code> with your controller/ESP32 IP address.
              </p>
            </div>

            {/* Step 3: Systemd background service */}
            <div className="bg-zinc-950 border border-zinc-800 rounded-xl p-4 space-y-2">
              <div className="flex items-center justify-between">
                <span className="font-semibold text-white">3. (Optional) Run Automatically on Boot via Systemd</span>
                <button
                  onClick={() => handleCopy('sudo cp s905x-agent.service /etc/systemd/system/ && sudo systemctl enable --now s905x-agent', 'step3')}
                  className="flex items-center gap-1 text-[11px] text-zinc-400 hover:text-white"
                >
                  {copiedSection === 'step3' ? <Check className="w-3.5 h-3.5 text-emerald-400" /> : <Copy className="w-3.5 h-3.5" />}
                  Copy
                </button>
              </div>
              <pre className="bg-zinc-900 p-2.5 rounded text-emerald-300 font-mono text-[11px] overflow-x-auto">
sudo cp s905x-agent.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now s905x-agent
              </pre>
            </div>
          </div>

          {/* Download buttons */}
          <div className="pt-2 flex flex-wrap gap-3">
            <button
              onClick={handleDownloadAgent}
              className="flex items-center gap-2 px-4 py-2 rounded-lg bg-emerald-700 hover:bg-emerald-600 text-white font-semibold transition"
            >
              <Download className="w-4 h-4" />
              Download s905x_agent.py
            </button>
            <button
              onClick={handleDownloadService}
              className="flex items-center gap-2 px-4 py-2 rounded-lg bg-zinc-800 hover:bg-zinc-700 text-zinc-200 font-semibold border border-zinc-700 transition"
            >
              <FileText className="w-4 h-4 text-orange-400" />
              Download s905x-agent.service
            </button>
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
