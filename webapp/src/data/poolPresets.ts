import { PoolConfig } from '../types';

export interface PoolPreset {
  id: string;
  name: string;
  url: string;
  defaultPort: number;
  userFormat: string;
  userPlaceholder: string;
  defaultPass: string;
  isSolo: boolean;
  description: string;
  documentationUrl?: string;
}

export const POOL_PRESETS: PoolPreset[] = [
  {
    id: 'solo-ckpool',
    name: 'Solo CKPool (Solo Mining)',
    url: 'stratum+tcp://solo.ckpool.org:3333',
    defaultPort: 3333,
    userFormat: '<YOUR_BITCOIN_ADDRESS>[.workername]',
    userPlaceholder: 'bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh.s905x_1',
    defaultPass: 'x',
    isSolo: true,
    description:
      'Direct solo mining with 98% block reward payout directly to your Bitcoin address when you solve a block. Does not require account registration.',
  },
  {
    id: 'viabtc',
    name: 'ViaBTC (Pooled)',
    url: 'stratum+tcp://btc.viabtc.top:3333',
    defaultPort: 3333,
    userFormat: '<account_username>.<worker_name>',
    userPlaceholder: 'myviabtcafter.s905x_1',
    defaultPass: '123',
    isSolo: false,
    description:
      'Multi-currency pool with PPS+/PPLNS/SOLO payout models. Requires account registration on viabtc.com.',
  },
  {
    id: 'braiins',
    name: 'Braiins Pool / SlushPool',
    url: 'stratum+tcp://stratum.braiins.com:3333',
    defaultPort: 3333,
    userFormat: '<account_username>.<worker_name>',
    userPlaceholder: 'braiinsuser.worker1',
    defaultPass: 'd=auto',
    isSolo: false,
    description:
      'The original Bitcoin mining pool (formerly Slush Pool). Requires account registration; payouts sent to configured address in pool settings.',
  },
  {
    id: 'f2pool',
    name: 'F2Pool',
    url: 'stratum+tcp://btc.f2pool.com:3333',
    defaultPort: 3333,
    userFormat: '<account_username>.<worker_name>',
    userPlaceholder: 'f2pooluser.box1',
    defaultPass: 'x',
    isSolo: false,
    description:
      'Large global Bitcoin pool. Requires user account created on f2pool.com with wallet address linked in account.',
  },
  {
    id: 'custom',
    name: 'Custom Stratum Pool / Private Node',
    url: 'stratum+tcp://192.168.1.50:3333',
    defaultPort: 3333,
    userFormat: 'Custom according to your pool/proxy',
    userPlaceholder: 'user.worker or bitcoin_address',
    defaultPass: 'x',
    isSolo: false,
    description:
      'Connect to a local Stratum proxy (e.g. ckpool, stratum-mining, or bitcoind local proxy) or private pool on your LAN.',
  },
];

export const DEFAULT_POOL: PoolConfig = {
  url: 'stratum+tcp://solo.ckpool.org:3333',
  user: 'bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh.s905x',
  pass: 'x',
  name: 'Solo CKPool',
  isSolo: true,
};
