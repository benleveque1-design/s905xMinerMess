import { BitcoinHeader } from '../types';

export const SAMPLE_BLOCKS: BitcoinHeader[] = [
  {
    id: 'block-125552',
    name: 'Block 125552 (Hal Finney / Classic Test Vector)',
    height: 125552,
    hex: '0100000081cd02ab7e569e8bcd9317e2fe99f2de44d49ab2b8851ba4a308000000000000e320b6c2fffc8d750423db8b1eb942ae710e951ed797f7affc8892b0f1fc122bc7f5d74df2b9441a42a14695',
    version: 1,
    prevBlockHash: '00000000000008a3a41b85b8b29ad444def299fee21793cdeb9e567eab02cd81',
    merkleRoot: '2b12fcf1b09288fcaff797d71e950e71ae42b91e8bdb2304758dfcffc2b620e3',
    timestamp: 1305966023,
    timestampFormatted: '2011-05-21 14:07:03 UTC',
    nbits: '1a44b9f2',
    nonce: 2504434002, // 0x9546a142 in hex
    expectedHash: '00000000000000001e8d6829a8a21adc5d38d0a473b144b6765798e61f98bd1d',
  },
  {
    id: 'block-0',
    name: 'Block 0 (Bitcoin Genesis Block - Satoshi Nakamoto)',
    height: 0,
    hex: '0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a29ab5f49ffff001d1dac2b7c',
    version: 1,
    prevBlockHash: '0000000000000000000000000000000000000000000000000000000000000000',
    merkleRoot: '4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b',
    timestamp: 1231006505,
    timestampFormatted: '2009-01-03 18:15:05 UTC',
    nbits: '1d00ffff',
    nonce: 2083236893, // 0x7c2bac1d in hex
    expectedHash: '000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f',
  },
  {
    id: 'block-700000',
    name: 'Block 700000 (Historic Taproot Era)',
    height: 700000,
    hex: '00e00020d2d38e2d431dfcbb382dd75a507fa71c360982da91a90500000000000000000078ce0fbdbf8fb0869db00d6061df43cf00fb5c597e7f603c4f74d0840b2a7596f26fa16147490f17b3d36b85',
    version: 0x2000e000,
    prevBlockHash: '00000000000000000005a991da8209361ca77f505ad72d38bbfc1d432d8ed3d2',
    merkleRoot: '96752a0b84d0744f3c607f7e595cfb00cf43df61600db09d86b08fbffb0fce78',
    timestamp: 1631743986,
    timestampFormatted: '2021-09-15 22:13:06 UTC',
    nbits: '170f4947',
    nonce: 2238436275, // 0x856bd3b3 in hex
    expectedHash: '0000000000000000000590fc0f3cbc193a29b5377d5ff6105566dd4b40b60b32',
  },
];
