// SHA-256 constants
const K: number[] = [
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
];

const SHA256_H0: number[] = [
  0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
  0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
];

function rotr(x: number, n: number): number {
  return (x >>> n) | (x << (32 - n));
}

function ch(x: number, y: number, z: number): number {
  return (x & y) ^ (~x & z);
}

function maj(x: number, y: number, z: number): number {
  return (x & y) ^ (x & z) ^ (y & z);
}

function sigma0(x: number): number {
  return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

function sigma1(x: number): number {
  return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

function gamma0(x: number): number {
  return rotr(x, 7) ^ rotr(x, 18) ^ (x >>> 3);
}

function gamma1(x: number): number {
  return rotr(x, 17) ^ rotr(x, 19) ^ (x >>> 10);
}

export function sha256Compress(state: number[], block: Uint8Array): void {
  const W = new Int32Array(64);

  for (let i = 0; i < 16; i++) {
    W[i] = (block[i * 4] << 24) | (block[i * 4 + 1] << 16) | (block[i * 4 + 2] << 8) | block[i * 4 + 3];
  }

  for (let i = 16; i < 64; i++) {
    W[i] = (gamma1(W[i - 2]) + W[i - 7] + gamma0(W[i - 15]) + W[i - 16]) | 0;
  }

  let a = state[0], b = state[1], c = state[2], d = state[3];
  let e = state[4], f = state[5], g = state[6], h = state[7];

  for (let i = 0; i < 64; i++) {
    const t1 = (h + sigma1(e) + ch(e, f, g) + K[i] + W[i]) | 0;
    const t2 = (sigma0(a) + maj(a, b, c)) | 0;
    h = g;
    g = f;
    f = e;
    e = (d + t1) | 0;
    d = c;
    c = b;
    b = a;
    a = (t1 + t2) | 0;
  }

  state[0] = (state[0] + a) | 0;
  state[1] = (state[1] + b) | 0;
  state[2] = (state[2] + c) | 0;
  state[3] = (state[3] + d) | 0;
  state[4] = (state[4] + e) | 0;
  state[5] = (state[5] + f) | 0;
  state[6] = (state[6] + g) | 0;
  state[7] = (state[7] + h) | 0;
}

/**
 * Standard single SHA-256 on arbitrary Uint8Array
 */
export function sha256(data: Uint8Array): Uint8Array {
  const state = [...SHA256_H0];
  const totalBits = data.length * 8;
  let offset = 0;

  // Process full 64-byte blocks
  while (offset + 64 <= data.length) {
    sha256Compress(state, data.subarray(offset, offset + 64));
    offset += 64;
  }

  // Padding
  const remaining = data.length - offset;
  const buffer = new Uint8Array(64);
  buffer.set(data.subarray(offset));
  buffer[remaining] = 0x80;

  if (remaining >= 56) {
    sha256Compress(state, buffer);
    buffer.fill(0);
  }

  // Write 64-bit big-endian length
  const hi = Math.floor(totalBits / 0x100000000);
  const lo = totalBits & 0xffffffff;
  buffer[56] = (hi >>> 24) & 0xff;
  buffer[57] = (hi >>> 16) & 0xff;
  buffer[58] = (hi >>> 8) & 0xff;
  buffer[59] = hi & 0xff;
  buffer[60] = (lo >>> 24) & 0xff;
  buffer[61] = (lo >>> 16) & 0xff;
  buffer[62] = (lo >>> 8) & 0xff;
  buffer[63] = lo & 0xff;

  sha256Compress(state, buffer);

  const digest = new Uint8Array(32);
  for (let i = 0; i < 8; i++) {
    digest[i * 4] = (state[i] >>> 24) & 0xff;
    digest[i * 4 + 1] = (state[i] >>> 16) & 0xff;
    digest[i * 4 + 2] = (state[i] >>> 8) & 0xff;
    digest[i * 4 + 3] = state[i] & 0xff;
  }
  return digest;
}

/**
 * Naive double-SHA256 (3 compression operations for 80-byte header)
 */
export function sha256dNaive(header: Uint8Array): Uint8Array {
  const first = sha256(header); // 2 compressions (Block 1 + Block 2)
  return sha256(first);        // 1 compression
}

export interface MidstateContext {
  midstate: number[];
  block2Template: Uint8Array;
}

/**
 * Precompute Midstate for an 80-byte Bitcoin block header
 */
export function precomputeMidstate(header: Uint8Array): MidstateContext {
  const state = [...SHA256_H0];
  // Compress block 1 (bytes 0..63)
  sha256Compress(state, header.subarray(0, 64));

  // Prepare block 2 template (bytes 64..79 + static SHA-256 padding for 80-byte msg)
  const block2 = new Uint8Array(64);
  block2.set(header.subarray(64, 80), 0);
  block2[16] = 0x80;
  // 640 bits total message length in big-endian at byte 62..63 (0x0280)
  block2[62] = 0x02;
  block2[63] = 0x80;

  return {
    midstate: state,
    block2Template: block2,
  };
}

/**
 * Optimized double-SHA256 using precomputed midstate (2 compressions per hash)
 */
export function sha256dMidstate(ms: MidstateContext, nonce: number): Uint8Array {
  // Pass 1, Block 2: Start from midstate
  const state1 = [...ms.midstate];
  const b2 = new Uint8Array(ms.block2Template);
  // Nonce is little-endian at bytes 12..15 of block 2 (offsets 76..79 of header)
  b2[12] = nonce & 0xff;
  b2[13] = (nonce >>> 8) & 0xff;
  b2[14] = (nonce >>> 16) & 0xff;
  b2[15] = (nonce >>> 24) & 0xff;

  sha256Compress(state1, b2); // 1st compression

  // Pass 2, Single Block: Hash the 32-byte intermediate digest
  const bPass2 = new Uint8Array(64);
  for (let i = 0; i < 8; i++) {
    bPass2[i * 4] = (state1[i] >>> 24) & 0xff;
    bPass2[i * 4 + 1] = (state1[i] >>> 16) & 0xff;
    bPass2[i * 4 + 2] = (state1[i] >>> 8) & 0xff;
    bPass2[i * 4 + 3] = state1[i] & 0xff;
  }
  bPass2[32] = 0x80;
  // 256 bits length at byte 62..63 (0x0100)
  bPass2[62] = 0x01;
  bPass2[63] = 0x00;

  const state2 = [...SHA256_H0];
  sha256Compress(state2, bPass2); // 2nd compression

  const finalDigest = new Uint8Array(32);
  for (let i = 0; i < 8; i++) {
    finalDigest[i * 4] = (state2[i] >>> 24) & 0xff;
    finalDigest[i * 4 + 1] = (state2[i] >>> 16) & 0xff;
    finalDigest[i * 4 + 2] = (state2[i] >>> 8) & 0xff;
    finalDigest[i * 4 + 3] = state2[i] & 0xff;
  }

  return finalDigest;
}

export function hexToBytes(hex: string): Uint8Array {
  const cleanHex = hex.trim().replace(/\s+/g, '');
  const bytes = new Uint8Array(cleanHex.length / 2);
  for (let i = 0; i < cleanHex.length; i += 2) {
    bytes[i / 2] = parseInt(cleanHex.substring(i, i + 2), 16);
  }
  return bytes;
}

export function bytesToHex(bytes: Uint8Array): string {
  return Array.from(bytes)
    .map(b => b.toString(16).padStart(2, '0'))
    .join('');
}

export function reverseHex(hex: string): string {
  const bytes = hexToBytes(hex);
  const reversed = new Uint8Array(bytes.length);
  for (let i = 0; i < bytes.length; i++) {
    reversed[i] = bytes[bytes.length - 1 - i];
  }
  return bytesToHex(reversed);
}

export function formatBitcoinHash(rawBytes: Uint8Array): string {
  const reversed = new Uint8Array(32);
  for (let i = 0; i < 32; i++) {
    reversed[i] = rawBytes[31 - i];
  }
  return bytesToHex(reversed);
}
