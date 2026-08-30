export type WorkerState = 'RUNNING' | 'STOPPED' | 'ERROR' | 'RESTARTING' | 'OFFLINE';

export interface PoolConfig {
  url: string;
  user: string;
  pass: string;
  name?: string;
  isSolo?: boolean;
}

export interface WorkerTelemetry {
  workerId: string;
  name: string;
  ip?: string;
  state: WorkerState;
  threads: number;
  maxCores: number;
  hashrateMhs: number;
  tempC: number | null;
  cpuFreqMhz: number;
  sharesFound: number;
  sharesAccepted: number;
  sharesRejected: number;
  uptime: number; // in seconds
  lastSeen: number; // timestamp ms
  pool: PoolConfig;
  arch?: string;
  hwCrypto?: boolean;
  recentLogs?: LogEntry[];
  hashrateHistory?: { time: number; hashrate: number }[];
  tempHistory?: { time: number; temp: number }[];
}

export interface LogEntry {
  id: string;
  workerId: string;
  timestamp: number;
  level: 'INFO' | 'WARN' | 'ERROR' | 'SUCCESS';
  message: string;
}

export type CommandAction =
  | 'start'
  | 'stop'
  | 'restart'
  | 'set_threads'
  | 'set_pool'
  | 'rename'
  | 'ping';

export interface WorkerCommand {
  cmdId: string;
  workerId: string;
  action: CommandAction;
  params?: {
    threads?: number;
    name?: string;
    pool?: PoolConfig;
  };
}

export interface CommandAck {
  cmdId: string;
  workerId: string;
  status: 'ok' | 'error';
  message: string;
  timestamp: number;
}

export interface BitcoinHeader {
  id: string;
  name: string;
  height: number;
  hex: string;
  version: number;
  prevBlockHash: string;
  merkleRoot: string;
  timestamp: number;
  timestampFormatted: string;
  nbits: string;
  nonce: number;
  expectedHash: string;
}

export interface BenchmarkRunResult {
  mode?: string;
  durationMs?: number;
  elapsedMs?: number;
  hashesPerSecond?: number;
  hashrate?: number;
  totalHashes?: number;
  hashes?: number;
  compressionsTotal?: number;
  compressionsPerHash?: number;
  sampleHash?: string;
  instructionsSaved?: number;
  timeReductionPercent?: number;
  architecture?: string;
  threads?: number;
}

export interface TestResult {
  id?: string;
  name?: string;
  description?: string;
  testId?: string;
  blockName?: string;
  calculatedHash?: string;
  expectedHash?: string;
  fullHash?: string;
  midstateHash?: string;
  passed: boolean;
  durationMs?: number;
  standardTimeMs?: number;
  optimizedTimeMs?: number;
  speedupMultiplier?: number;
}


export interface FleetStats {
  totalHashrateMhs: number;
  activeWorkers: number;
  totalWorkers: number;
  totalSharesAccepted: number;
  totalSharesRejected: number;
  avgTemperatureC: number;
  onlineRatio: number;
}

export interface WsServerMessage {
  type: 'fleet_sync' | 'worker_update' | 'worker_offline' | 'command_ack' | 'log' | 'pong';
  workers?: WorkerTelemetry[];
  worker?: WorkerTelemetry;
  workerId?: string;
  ack?: CommandAck;
  log?: LogEntry;
  poolConfig?: PoolConfig;
}
