import React, { useState } from 'react';
import { 
  X, 
  Zap, 
  Check, 
  HelpCircle, 
  Server, 
  Wallet, 
  ShieldCheck, 
  ArrowRight,
  Radio
} from 'lucide-react';
import { PoolConfig } from '../types';
import { POOL_PRESETS, PoolPreset } from '../data/poolPresets';

interface PoolConfigModalProps {
  currentPool: PoolConfig;
  onClose: () => void;
  onSaveAndBroadcast: (pool: PoolConfig) => void;
}

export const PoolConfigModal: React.FC<PoolConfigModalProps> = ({
  currentPool,
  onClose,
  onSaveAndBroadcast,
}) => {
  const [selectedPreset, setSelectedPreset] = useState<string>('solo-ckpool');
  const [poolUrl, setPoolUrl] = useState<string>(currentPool.url);
  const [poolUser, setPoolUser] = useState<string>(currentPool.user);
  const [poolPass, setPoolPass] = useState<string>(currentPool.pass || 'x');
  const [walletAddress, setWalletAddress] = useState<string>(
    currentPool.user.split('.')[0] || 'bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh'
  );
  const [workerSuffix, setWorkerSuffix] = useState<string>(
    currentPool.user.split('.')[1] || 's905x'
  );

  const handleSelectPreset = (preset: PoolPreset) => {
    setSelectedPreset(preset.id);
    setPoolUrl(preset.url);
    setPoolPass(preset.defaultPass);
    if (preset.isSolo) {
      setPoolUser(`${walletAddress}.${workerSuffix}`);
    } else {
      setPoolUser(preset.userPlaceholder);
    }
  };

  const handleApplyWallet = (address: string) => {
    setWalletAddress(address);
    if (selectedPreset === 'solo-ckpool' || poolUrl.includes('ckpool')) {
      setPoolUser(`${address}.${workerSuffix}`);
    }
  };

  const handleSave = () => {
    const finalPool: PoolConfig = {
      url: poolUrl.trim(),
      user: poolUser.trim(),
      pass: poolPass.trim(),
      isSolo: selectedPreset === 'solo-ckpool' || poolUrl.includes('solo'),
      name: POOL_PRESETS.find((p) => p.id === selectedPreset)?.name || 'Custom Pool',
    };
    onSaveAndBroadcast(finalPool);
    onClose();
  };

  const isValidBtcAddress = (addr: string) => {
    const clean = addr.trim();
    return (
      (clean.startsWith('1') || clean.startsWith('3') || clean.startsWith('bc1')) &&
      clean.length >= 26 &&
      clean.length <= 62
    );
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/80 backdrop-blur-sm">
      <div className="bg-zinc-900 border border-zinc-700/80 rounded-2xl w-full max-w-2xl max-h-[90vh] flex flex-col shadow-2xl overflow-hidden">
        {/* Header */}
        <div className="p-5 border-b border-zinc-800 bg-zinc-950 flex items-center justify-between">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-xl bg-orange-500/10 border border-orange-500/30 flex items-center justify-center text-orange-400">
              <Zap className="w-5 h-5" />
            </div>
            <div>
              <h2 className="text-lg font-bold text-white tracking-tight">
                Bitcoin Mining Pool Configuration
              </h2>
              <p className="text-xs text-zinc-400">
                Broadcast Stratum server & payout credentials to all connected S905X boxes
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

        {/* Form Body */}
        <div className="flex-1 p-5 overflow-y-auto space-y-5">
          {/* Preset Selector */}
          <div>
            <label className="block text-xs font-semibold text-zinc-300 uppercase tracking-wider mb-2">
              Pool Presets & Templates
            </label>
            <div className="grid grid-cols-1 sm:grid-cols-2 gap-2">
              {POOL_PRESETS.map((preset) => (
                <button
                  key={preset.id}
                  onClick={() => handleSelectPreset(preset)}
                  className={`p-3 rounded-xl border text-left transition flex flex-col justify-between ${
                    selectedPreset === preset.id
                      ? 'bg-orange-500/10 border-orange-500/60 text-white'
                      : 'bg-zinc-950/60 border-zinc-800 text-zinc-300 hover:bg-zinc-800/40'
                  }`}
                >
                  <div className="flex items-center justify-between">
                    <span className="font-bold text-xs">{preset.name}</span>
                    {preset.isSolo && (
                      <span className="text-[10px] px-1.5 py-0.5 rounded bg-emerald-950 text-emerald-300 border border-emerald-800 font-mono">
                        SOLO
                      </span>
                    )}
                  </div>
                  <span className="text-[11px] text-zinc-400 mt-1 font-mono truncate">
                    {preset.url}
                  </span>
                </button>
              ))}
            </div>
          </div>

          {/* Stratum URL & Port */}
          <div className="bg-zinc-950/60 border border-zinc-800 rounded-xl p-4 space-y-3">
            <div>
              <label className="block text-xs font-medium text-zinc-300 mb-1">
                Stratum Endpoint URL
              </label>
              <div className="relative">
                <input
                  type="text"
                  value={poolUrl}
                  onChange={(e) => setPoolUrl(e.target.value)}
                  className="w-full bg-zinc-900 border border-zinc-700 rounded-lg px-3 py-2 text-xs font-mono text-white focus:outline-none focus:border-orange-500"
                  placeholder="stratum+tcp://solo.ckpool.org:3333"
                />
              </div>
            </div>

            {/* Bitcoin Wallet Address Section */}
            <div>
              <div className="flex items-center justify-between mb-1">
                <label className="text-xs font-medium text-zinc-300 flex items-center gap-1.5">
                  <Wallet className="w-3.5 h-3.5 text-orange-400" />
                  Your Bitcoin Payout Address
                </label>
                {isValidBtcAddress(walletAddress) && (
                  <span className="text-[10px] text-emerald-400 flex items-center gap-1">
                    <ShieldCheck className="w-3 h-3" /> Valid BTC Address
                  </span>
                )}
              </div>
              <input
                type="text"
                value={walletAddress}
                onChange={(e) => handleApplyWallet(e.target.value)}
                className="w-full bg-zinc-900 border border-zinc-700 rounded-lg px-3 py-2 text-xs font-mono text-white focus:outline-none focus:border-orange-500"
                placeholder="bc1q..."
              />
              <p className="text-[11px] text-zinc-400 mt-1">
                Supports Native SegWit (<code className="text-orange-300">bc1q...</code>), Taproot (<code className="text-orange-300">bc1p...</code>), or Legacy (<code className="text-orange-300">1...</code>).
              </p>
            </div>

            {/* Stratum Username / Worker Name */}
            <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
              <div>
                <label className="block text-xs font-medium text-zinc-300 mb-1">
                  Stratum Username / User
                </label>
                <input
                  type="text"
                  value={poolUser}
                  onChange={(e) => setPoolUser(e.target.value)}
                  className="w-full bg-zinc-900 border border-zinc-700 rounded-lg px-3 py-2 text-xs font-mono text-white focus:outline-none focus:border-orange-500"
                  placeholder="bc1q...s905x or account.worker"
                />
              </div>

              <div>
                <label className="block text-xs font-medium text-zinc-300 mb-1">
                  Stratum Password
                </label>
                <input
                  type="text"
                  value={poolPass}
                  onChange={(e) => setPoolPass(e.target.value)}
                  className="w-full bg-zinc-900 border border-zinc-700 rounded-lg px-3 py-2 text-xs font-mono text-white focus:outline-none focus:border-orange-500"
                  placeholder="x (or custom)"
                />
              </div>
            </div>
          </div>

          {/* Important Educational Explanation Callout */}
          <div className="bg-blue-950/30 border border-blue-800/50 rounded-xl p-3.5 text-xs text-blue-200/90 space-y-1.5">
            <div className="font-semibold text-blue-300 flex items-center gap-1.5">
              <HelpCircle className="w-4 h-4 text-blue-400" />
              How Bitcoin Pools Handle Wallet Addresses vs Account Names
            </div>
            <p className="text-[11px] leading-relaxed text-zinc-300">
              • <strong className="text-white">Solo CKPool (Recommended for S905X solo lottery):</strong> Payouts go directly to the Bitcoin address provided in the username field. No account or registration is needed.
            </p>
            <p className="text-[11px] leading-relaxed text-zinc-300">
              • <strong className="text-white">Account Pools (ViaBTC, Braiins, F2Pool):</strong> Require an account username registered on their website. The pool sends rewards to the wallet configured in your web account profile.
            </p>
          </div>
        </div>

        {/* Footer */}
        <div className="p-4 border-t border-zinc-800 bg-zinc-950 flex items-center justify-end gap-3">
          <button
            onClick={onClose}
            className="px-4 py-2 rounded-lg bg-zinc-800 hover:bg-zinc-700 text-zinc-300 text-xs font-medium transition"
          >
            Cancel
          </button>
          <button
            id="btn-broadcast-pool"
            onClick={handleSave}
            className="flex items-center gap-2 px-4 py-2 rounded-lg bg-orange-500 hover:bg-orange-600 text-zinc-950 text-xs font-bold transition shadow"
          >
            <Zap className="w-4 h-4 fill-current" />
            Apply & Broadcast to All S905X Workers
          </button>
        </div>
      </div>
    </div>
  );
};
