/*
 * bitcoin_sha256d_s905x.c
 *
 * Bitcoin SHA-256d (double SHA-256) block-header hashing, optimized with:
 * 1. Midstate Precomputation: The first 64 bytes (Version, PrevHash, MerkleRoot[0..27])
 *    are invariant across nonce iterations. The first SHA-256 compression is computed ONCE
 *    per job/work item, reducing compression cycles from 3 to 2 per hash (33.3% theoretical
 *    instruction reduction, ~1.5x speedup).
 * 2. ARMv8 Cryptography Extension hardware SHA-256 acceleration (SHA256H, SHA256H2,
 *    SHA256SU0, SHA256SU1) for Cortex-A53 (Amlogic S905X / aarch64) with software fallback.
 * 3. Pre-formatted block templates with invariant SHA-256 length/padding constants prefilled,
 *    eliminating per-hash buffer allocations, generic stream updates, and length calculations.
 *
 * Build (native on S905X / aarch64):
 *   gcc -O3 -march=armv8-a+crypto -pthread -o bitcoin_sha256d_s905x bitcoin_sha256d_s905x.c
 *
 * Generic build:
 *   gcc -O3 -pthread -o bitcoin_sha256d_s905x bitcoin_sha256d_s905x.c
 *
 * Run:
 *   ./bitcoin_sha256d_s905x -t                    # correctness tests
 *   ./bitcoin_sha256d_s905x -b -n 20000000         # benchmark, all 4 cores
 *   ./bitcoin_sha256d_s905x -b -n 20000000 -j 1    # benchmark, single core
 */

#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>
#include <errno.h>

#if defined(__aarch64__)
#include <arm_neon.h>
#include <sys/auxv.h>
#ifndef HWCAP_SHA2
#define HWCAP_SHA2 (1 << 6)
#endif
#define HAVE_ARM_SHA2_TARGET 1
#endif

/* ============================================================================
 * SECTION 1: SHA-256 Constants
 * ============================================================================ */

/* Initial SHA-256 state constants (H0) */
static const uint32_t SHA256_H0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

/* SHA-256 round constants */
static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/* ============================================================================
 * SECTION 2: Software (portable) compression function - fallback path
 * ============================================================================ */

static inline uint32_t rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SIGMA0(x)    (rotr32((x), 2)  ^ rotr32((x), 13) ^ rotr32((x), 22))
#define SIGMA1(x)    (rotr32((x), 6)  ^ rotr32((x), 11) ^ rotr32((x), 25))
#define sigma0(x)    (rotr32((x), 7)  ^ rotr32((x), 18) ^ ((x) >> 3))
#define sigma1(x)    (rotr32((x), 17) ^ rotr32((x), 19) ^ ((x) >> 10))

static void sha256_compress_sw(uint32_t state[8], const uint8_t block[64]) {
    uint32_t W[64];

    for (int i = 0; i < 16; i++) {
        W[i] = ((uint32_t)block[i * 4]     << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8)  |
               ((uint32_t)block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        W[i] = sigma1(W[i - 2]) + W[i - 7] + sigma0(W[i - 15]) + W[i - 16];
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t T1 = h + SIGMA1(e) + CH(e, f, g) + K[i] + W[i];
        uint32_t T2 = SIGMA0(a) + MAJ(a, b, c);
        h = g; g = f; f = e; e = d + T1;
        d = c; c = b; b = a; a = T1 + T2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

/* ============================================================================
 * SECTION 3: ARMv8 Crypto Extension compression function - S905X hw path
 * ============================================================================ */

#if defined(HAVE_ARM_SHA2_TARGET)

__attribute__((target("+crypto")))
static void sha256_compress_hw(uint32_t state[8], const uint8_t block[64]) {
    uint32x4_t MSG0, MSG1, MSG2, MSG3;
    uint32x4_t TMP0, TMP1, TMP2;

    uint32x4_t abcd = vld1q_u32(&state[0]);
    uint32x4_t efgh = vld1q_u32(&state[4]);

    /* ARMv8 SHA256 hardware instructions require:
     * STATE0 = { a, b, e, f }
     * STATE1 = { c, d, g, h }
     */
    uint32x4_t STATE0 = vcombine_u32(vget_low_u32(abcd), vget_low_u32(efgh));
    uint32x4_t STATE1 = vcombine_u32(vget_high_u32(abcd), vget_high_u32(efgh));

    uint32x4_t ABEF_SAVE = STATE0;
    uint32x4_t CDGH_SAVE = STATE1;

    /* Load 64-byte block and byte-swap each 32-bit word (block is big-endian) */
    MSG0 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(block +  0)));
    MSG1 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(block + 16)));
    MSG2 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(block + 32)));
    MSG3 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(block + 48)));

    /* Rounds 0-3 */
    TMP0 = vaddq_u32(MSG0, vld1q_u32(&K[0]));
    TMP1 = STATE0;
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP1, TMP0);

    /* Rounds 4-7 */
    TMP0 = vaddq_u32(MSG1, vld1q_u32(&K[4]));
    TMP1 = STATE0;
    MSG0 = vsha256su0q_u32(MSG0, MSG1);
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP1, TMP0);
    MSG0 = vsha256su1q_u32(MSG0, MSG2, MSG3);

    /* Rounds 8-11 */
    TMP0 = vaddq_u32(MSG2, vld1q_u32(&K[8]));
    TMP1 = STATE0;
    MSG1 = vsha256su0q_u32(MSG1, MSG2);
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP1, TMP0);
    MSG1 = vsha256su1q_u32(MSG1, MSG3, MSG0);

    /* Rounds 12-15 */
    TMP0 = vaddq_u32(MSG3, vld1q_u32(&K[12]));
    TMP1 = STATE0;
    MSG2 = vsha256su0q_u32(MSG2, MSG3);
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP1, TMP0);
    MSG2 = vsha256su1q_u32(MSG2, MSG0, MSG1);

    /* Rounds 16-19 */
    TMP0 = vaddq_u32(MSG0, vld1q_u32(&K[16]));
    TMP1 = STATE0;
    MSG3 = vsha256su0q_u32(MSG3, MSG0);
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP1, TMP0);
    MSG3 = vsha256su1q_u32(MSG3, MSG1, MSG2);

    /* Rounds 20-23 */
    TMP0 = vaddq_u32(MSG1, vld1q_u32(&K[20]));
    TMP1 = STATE0;
    MSG0 = vsha256su0q_u32(MSG0, MSG1);
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP1, TMP0);
    MSG0 = vsha256su1q_u32(MSG0, MSG2, MSG3);

    /* Rounds 24-27 */
    TMP0 = vaddq_u32(MSG2, vld1q_u32(&K[24]));
    TMP1 = STATE0;
    MSG1 = vsha256su0q_u32(MSG1, MSG2);
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP1, TMP0);
    MSG1 = vsha256su1q_u32(MSG1, MSG3, MSG0);

    /* Rounds 28-31 */
    TMP0 = vaddq_u32(MSG3, vld1q_u32(&K[28]));
    TMP1 = STATE0;
    MSG2 = vsha256su0q_u32(MSG2, MSG3);
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP1, TMP0);
    MSG2 = vsha256su1q_u32(MSG2, MSG0, MSG1);

    /* Rounds 32-35 */
    TMP0 = vaddq_u32(MSG0, vld1q_u32(&K[32]));
    TMP1 = STATE0;
    MSG3 = vsha256su0q_u32(MSG3, MSG0);
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP1, TMP0);
    MSG3 = vsha256su1q_u32(MSG3, MSG1, MSG2);

    /* Rounds 36-39 */
    TMP0 = vaddq_u32(MSG1, vld1q_u32(&K[36]));
    TMP1 = STATE0;
    MSG0 = vsha256su0q_u32(MSG0, MSG1);
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP1, TMP0);
    MSG0 = vsha256su1q_u32(MSG0, MSG2, MSG3);

    /* Rounds 40-43 */
    TMP0 = vaddq_u32(MSG2, vld1q_u32(&K[40]));
    TMP1 = STATE0;
    MSG1 = vsha256su0q_u32(MSG1, MSG2);
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP1, TMP0);
    MSG1 = vsha256su1q_u32(MSG1, MSG3, MSG0);

    /* Rounds 44-47 */
    TMP0 = vaddq_u32(MSG3, vld1q_u32(&K[44]));
    TMP1 = STATE0;
    MSG2 = vsha256su0q_u32(MSG2, MSG3);
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP1, TMP0);
    MSG2 = vsha256su1q_u32(MSG2, MSG0, MSG1);

    /* Rounds 48-51 */
    TMP0 = vaddq_u32(MSG0, vld1q_u32(&K[48]));
    TMP1 = STATE0;
    MSG3 = vsha256su0q_u32(MSG3, MSG0);
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP1, TMP0);
    MSG3 = vsha256su1q_u32(MSG3, MSG1, MSG2);

    /* Rounds 52-55 (no further message schedule needed) */
    TMP0 = vaddq_u32(MSG1, vld1q_u32(&K[52]));
    TMP1 = STATE0;
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP1, TMP0);

    /* Rounds 56-59 */
    TMP0 = vaddq_u32(MSG2, vld1q_u32(&K[56]));
    TMP1 = STATE0;
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP1, TMP0);

    /* Rounds 60-63 */
    TMP0 = vaddq_u32(MSG3, vld1q_u32(&K[60]));
    TMP1 = STATE0;
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP1, TMP0);

    STATE0 = vaddq_u32(STATE0, ABEF_SAVE);
    STATE1 = vaddq_u32(STATE1, CDGH_SAVE);

    /* Convert back from {a, b, e, f} and {c, d, g, h} to {a, b, c, d} and {e, f, g, h} */
    uint32x4_t abcd_out = vcombine_u32(vget_low_u32(STATE0), vget_low_u32(STATE1));
    uint32x4_t efgh_out = vcombine_u32(vget_high_u32(STATE0), vget_high_u32(STATE1));

    vst1q_u32(&state[0], abcd_out);
    vst1q_u32(&state[4], efgh_out);
}

static int cpu_has_sha256_hw(void) {
    unsigned long hwcap = getauxval(AT_HWCAP);
    return (hwcap & HWCAP_SHA2) != 0;
}

#else /* !HAVE_ARM_SHA2_TARGET : non-aarch64 build, hw path unavailable */

static int cpu_has_sha256_hw(void) { return 0; }

#endif

/* ============================================================================
 * SECTION 4: Backend dispatch
 * ============================================================================ */

typedef void (*sha256_compress_fn_t)(uint32_t state[8], const uint8_t block[64]);

static sha256_compress_fn_t sha256_compress_fn = sha256_compress_sw;
static const char *backend_name = "software (portable C, no HW crypto)";

static void select_sha256_backend(void) {
    if (cpu_has_sha256_hw()) {
#if defined(HAVE_ARM_SHA2_TARGET)
        sha256_compress_fn = sha256_compress_hw;
        backend_name = "ARMv8 Crypto Extension (SHA256H/H2/SU0/SU1, S905X Cortex-A53)";
        return;
#endif
    }
    sha256_compress_fn = sha256_compress_sw;
    backend_name = "software (portable C fallback - HWCAP_SHA2 not present)";
}

/* ============================================================================
 * SECTION 5: SHA-256 High-Level Wrapper
 * ============================================================================ */

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buffer[64];
    size_t   buffer_len;
} sha256_ctx;

static void sha256_init(sha256_ctx *ctx) {
    memcpy(ctx->state, SHA256_H0, sizeof(SHA256_H0));
    ctx->count = 0;
    ctx->buffer_len = 0;
}

static void sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t len) {
    ctx->count += len;
    size_t offset = 0;

    if (ctx->buffer_len > 0) {
        size_t fill = 64 - ctx->buffer_len;
        if (len >= fill) {
            memcpy(ctx->buffer + ctx->buffer_len, data, fill);
            sha256_compress_fn(ctx->state, ctx->buffer);
            ctx->buffer_len = 0;
            offset += fill;
        } else {
            memcpy(ctx->buffer + ctx->buffer_len, data, len);
            ctx->buffer_len += len;
            return;
        }
    }

    while (offset + 64 <= len) {
        sha256_compress_fn(ctx->state, data + offset);
        offset += 64;
    }

    if (offset < len) {
        memcpy(ctx->buffer, data + offset, len - offset);
        ctx->buffer_len = len - offset;
    }
}

static void sha256_final(sha256_ctx *ctx, uint8_t digest[32]) {
    uint64_t total_bits = ctx->count * 8;

    ctx->buffer[ctx->buffer_len++] = 0x80;

    if (ctx->buffer_len > 56) {
        memset(ctx->buffer + ctx->buffer_len, 0, 64 - ctx->buffer_len);
        sha256_compress_fn(ctx->state, ctx->buffer);
        ctx->buffer_len = 0;
    }

    memset(ctx->buffer + ctx->buffer_len, 0, 56 - ctx->buffer_len);

    for (int i = 0; i < 8; i++) {
        ctx->buffer[56 + i] = (uint8_t)((total_bits >> (56 - i * 8)) & 0xFF);
    }
    sha256_compress_fn(ctx->state, ctx->buffer);

    for (int i = 0; i < 8; i++) {
        digest[i * 4]     = (uint8_t)((ctx->state[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = (uint8_t)((ctx->state[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = (uint8_t)((ctx->state[i] >> 8)  & 0xFF);
        digest[i * 4 + 3] = (uint8_t)( ctx->state[i]        & 0xFF);
    }
}

static void sha256(const uint8_t *data, size_t len, uint8_t digest[32]) {
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

/* ============================================================================
 * SECTION 6: SHA-256d (Double SHA-256) Wrapper & Optimized Midstate Engine
 * ============================================================================ */

static void sha256d(const uint8_t *data, size_t len, uint8_t digest[32]) {
    uint8_t intermediate[32];
    sha256(data, len, intermediate);
    sha256(intermediate, 32, digest);
}

/*
 * Bitcoin Header Midstate Precomputation Struct:
 *
 * An 80-byte Bitcoin block header consists of:
 * - Chunk 1 (64 bytes): Version (4), PrevHash (32), MerkleRoot[0..27] (28) -> INVARIANT across nonces.
 * - Chunk 2 (16 bytes): MerkleRoot[28..31] (4), Timestamp (4), nBits (4), Nonce (4) -> Only Nonce changes!
 *
 * By precomputing the SHA-256 state after the first 64 bytes (`midstate`), we eliminate 1 of the 3
 * compression calls on EVERY hash iteration (from 3 down to 2 compressions per hash).
 */
typedef struct {
    uint32_t midstate[8];  /* SHA-256 state after processing bytes 0..63 */
    uint8_t  block2[64];   /* Pre-padded template for bytes 64..79 + SHA-256 padding */
} bitcoin_midstate_t;

/* Precomputes the midstate from an 80-byte block header template */
static void bitcoin_midstate_init(bitcoin_midstate_t *ms, const uint8_t header[80]) {
    /* Initialize with standard SHA-256 constants H0 */
    memcpy(ms->midstate, SHA256_H0, sizeof(SHA256_H0));

    /* Compress chunk 1 (first 64 bytes of header) */
    sha256_compress_fn(ms->midstate, header);

    /* Prepare block 2 template:
     * - offset 0..15  : header bytes 64..79 (Merkle[28..31], Time, Bits, Nonce)
     * - offset 16     : 0x80 (padding start)
     * - offset 17..55 : 0x00
     * - offset 56..63 : 0x0000000000000280 (640 bits total message length)
     */
    memset(ms->block2, 0, 64);
    memcpy(ms->block2, header + 64, 16);
    ms->block2[16] = 0x80;
    ms->block2[62] = 0x02;
    ms->block2[63] = 0x80;
}

/*
 * Optimized Double-SHA256 evaluation for a specific nonce using precomputed midstate.
 * Runs exactly 2 compression rounds (instead of 3) with zero heap or stream overhead.
 */
static inline void bitcoin_hash_nonce_opt(const bitcoin_midstate_t *ms, uint32_t nonce, uint8_t hash[32]) {
    /* --- Pass 1, Block 2: Continue from precomputed midstate --- */
    uint32_t state1[8];
    memcpy(state1, ms->midstate, sizeof(state1));

    uint8_t b2[64];
    memcpy(b2, ms->block2, 64);
    /* Update nonce in little-endian at bytes 12..15 of block 2 */
    b2[12] = (uint8_t)( nonce        & 0xFF);
    b2[13] = (uint8_t)((nonce >> 8)  & 0xFF);
    b2[14] = (uint8_t)((nonce >> 16) & 0xFF);
    b2[15] = (uint8_t)((nonce >> 24) & 0xFF);

    sha256_compress_fn(state1, b2);

    /* --- Pass 2, Single Block: SHA-256 of 32-byte intermediate hash --- */
    uint8_t b_pass2[64];
    /* Write 32-byte intermediate digest in big-endian */
    for (int i = 0; i < 8; i++) {
        b_pass2[i * 4 + 0] = (uint8_t)((state1[i] >> 24) & 0xFF);
        b_pass2[i * 4 + 1] = (uint8_t)((state1[i] >> 16) & 0xFF);
        b_pass2[i * 4 + 2] = (uint8_t)((state1[i] >> 8)  & 0xFF);
        b_pass2[i * 4 + 3] = (uint8_t)( state1[i]        & 0xFF);
    }
    b_pass2[32] = 0x80;
    memset(b_pass2 + 33, 0, 23);
    /* 32 bytes = 256 bits = 0x0000000000000100 */
    b_pass2[56] = 0; b_pass2[57] = 0; b_pass2[58] = 0; b_pass2[59] = 0;
    b_pass2[60] = 0; b_pass2[61] = 0; b_pass2[62] = 0x01; b_pass2[63] = 0x00;

    uint32_t state2[8];
    memcpy(state2, SHA256_H0, sizeof(SHA256_H0));

    sha256_compress_fn(state2, b_pass2);

    /* Serialize final hash */
    for (int i = 0; i < 8; i++) {
        hash[i * 4 + 0] = (uint8_t)((state2[i] >> 24) & 0xFF);
        hash[i * 4 + 1] = (uint8_t)((state2[i] >> 16) & 0xFF);
        hash[i * 4 + 2] = (uint8_t)((state2[i] >> 8)  & 0xFF);
        hash[i * 4 + 3] = (uint8_t)( state2[i]        & 0xFF);
    }
}

/* ============================================================================
 * SECTION 7: Bitcoin Block Header Construction & Byte Ordering
 * ============================================================================ */

typedef struct {
    uint32_t version;
    uint8_t  prev_block_hash[32];
    uint8_t  merkle_root[32];
    uint32_t timestamp;
    uint32_t nbits;
    uint32_t nonce;
} bitcoin_header_t;

static inline void write_uint32_le(uint8_t *buf, uint32_t val) {
    buf[0] = (uint8_t)( val        & 0xFF);
    buf[1] = (uint8_t)((val >> 8)  & 0xFF);
    buf[2] = (uint8_t)((val >> 16) & 0xFF);
    buf[3] = (uint8_t)((val >> 24) & 0xFF);
}

static void serialize_bitcoin_header(const bitcoin_header_t *hdr, uint8_t buf[80]) {
    write_uint32_le(buf + 0,  hdr->version);
    memcpy(buf + 4,  hdr->prev_block_hash, 32);
    memcpy(buf + 36, hdr->merkle_root, 32);
    write_uint32_le(buf + 68, hdr->timestamp);
    write_uint32_le(buf + 72, hdr->nbits);
    write_uint32_le(buf + 76, hdr->nonce);
}

static inline void update_header_nonce_le(uint8_t buf[80], uint32_t nonce) {
    write_uint32_le(buf + 76, nonce);
}

static uint8_t parse_hex_nibble(char c) {
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    return 0;
}

static void hex_to_bytes(const char *hex, uint8_t *out, size_t len) {
    for (size_t i = 0; i < len; i++) {
        out[i] = (uint8_t)((parse_hex_nibble(hex[i * 2]) << 4) | parse_hex_nibble(hex[i * 2 + 1]));
    }
}

static void bytes_to_hex(const uint8_t *in, size_t len, char *out) {
    static const char hex_chars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = hex_chars[(in[i] >> 4) & 0x0F];
        out[i * 2 + 1] = hex_chars[ in[i]       & 0x0F];
    }
    out[len * 2] = '\0';
}

static void reverse_bytes(uint8_t *dst, const uint8_t *src, size_t len) {
    for (size_t i = 0; i < len; i++) {
        dst[i] = src[len - 1 - i];
    }
}

/* ============================================================================
 * SECTION 8: Correctness Tests
 * ============================================================================ */

static int run_correctness_tests(void) {
    printf("======================================================================\n");
    printf("Running Bitcoin SHA-256d Correctness Tests  [backend: %s]\n", backend_name);
    printf("======================================================================\n");

    int failures = 0;

    {
        const char *b125552_hex = "0100000081cd02ab7e569e8bcd9317e2fe99f2de44d49ab2b8851ba4a308000000000000e320b6c2fffc8d750423db8b1eb942ae710e951ed797f7affc8892b0f1fc122bc7f5d74df2b9441a42a14695";
        const char *expected_b125552 = "00000000000000001e8d6829a8a21adc5d38d0a473b144b6765798e61f98bd1d";

        uint8_t raw_header[80];
        hex_to_bytes(b125552_hex, raw_header, 80);

        uint8_t raw_hash[32];
        sha256d(raw_header, 80, raw_hash);

        uint8_t display_hash[32];
        reverse_bytes(display_hash, raw_hash, 32);

        char calc_hex[65];
        bytes_to_hex(display_hash, 32, calc_hex);

        printf("Test 1: Block 125552 Header Hashing (Full SHA-256d)\n");
        printf("  Calculated: %s\n", calc_hex);
        printf("  Expected  : %s\n", expected_b125552);

        if (strcmp(calc_hex, expected_b125552) == 0) {
            printf("  Status    : [PASS]\n\n");
        } else {
            printf("  Status    : [FAIL]\n\n");
            failures++;
        }

        /* Midstate Optimization Verification on Block 125552 */
        bitcoin_midstate_t ms;
        bitcoin_midstate_init(&ms, raw_header);
        uint32_t nonce = ((uint32_t)raw_header[76]) |
                         ((uint32_t)raw_header[77] << 8) |
                         ((uint32_t)raw_header[78] << 16) |
                         ((uint32_t)raw_header[79] << 24);

        uint8_t opt_hash[32];
        bitcoin_hash_nonce_opt(&ms, nonce, opt_hash);

        uint8_t opt_display[32];
        reverse_bytes(opt_display, opt_hash, 32);
        char opt_hex[65];
        bytes_to_hex(opt_display, 32, opt_hex);

        printf("Test 1b: Block 125552 Optimized Midstate Path Verification\n");
        printf("  Calculated: %s\n", opt_hex);
        printf("  Expected  : %s\n", expected_b125552);

        if (strcmp(opt_hex, expected_b125552) == 0) {
            printf("  Status    : [PASS]\n\n");
        } else {
            printf("  Status    : [FAIL]\n\n");
            failures++;
        }
    }

    {
        bitcoin_header_t gen_hdr;
        gen_hdr.version = 1;
        memset(gen_hdr.prev_block_hash, 0, 32);

        uint8_t mr_be[32];
        hex_to_bytes("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b", mr_be, 32);
        reverse_bytes(gen_hdr.merkle_root, mr_be, 32);

        gen_hdr.timestamp = 1231006505;
        gen_hdr.nbits     = 0x1d00ffff;
        gen_hdr.nonce     = 2083236893;

        uint8_t serialized_hdr[80];
        serialize_bitcoin_header(&gen_hdr, serialized_hdr);

        uint8_t raw_hash[32];
        sha256d(serialized_hdr, 80, raw_hash);

        uint8_t display_hash[32];
        reverse_bytes(display_hash, raw_hash, 32);

        char calc_hex[65];
        bytes_to_hex(display_hash, 32, calc_hex);

        const char *expected_genesis = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f";

        printf("Test 2: Bitcoin Genesis Block (Block 0) Header Hashing\n");
        printf("  Calculated: %s\n", calc_hex);
        printf("  Expected  : %s\n", expected_genesis);

        if (strcmp(calc_hex, expected_genesis) == 0) {
            printf("  Status    : [PASS]\n\n");
        } else {
            printf("  Status    : [FAIL]\n\n");
            failures++;
        }

        /* Midstate test on Genesis block */
        bitcoin_midstate_t gen_ms;
        bitcoin_midstate_init(&gen_ms, serialized_hdr);
        uint8_t gen_opt_hash[32];
        bitcoin_hash_nonce_opt(&gen_ms, gen_hdr.nonce, gen_opt_hash);

        uint8_t gen_opt_display[32];
        reverse_bytes(gen_opt_display, gen_opt_hash, 32);
        char gen_opt_hex[65];
        bytes_to_hex(gen_opt_display, 32, gen_opt_hex);

        printf("Test 2b: Genesis Block Optimized Midstate Path Verification\n");
        printf("  Calculated: %s\n", gen_opt_hex);
        printf("  Expected  : %s\n", expected_genesis);

        if (strcmp(gen_opt_hex, expected_genesis) == 0) {
            printf("  Status    : [PASS]\n\n");
        } else {
            printf("  Status    : [FAIL]\n\n");
            failures++;
        }
    }

    /* Cross-check: if the hw backend is active, also run the same vectors
     * through the software path and confirm both backends agree. This
     * catches an intrinsic-ordering mistake even when the final test
     * vectors above happen to still pass. */
#if defined(HAVE_ARM_SHA2_TARGET)
    if (sha256_compress_fn == sha256_compress_hw) {
        printf("Test 3: HW backend vs SW backend agreement (random-ish block)\n");
        uint8_t sample[80];
        for (int i = 0; i < 80; i++) sample[i] = (uint8_t)(i * 37 + 11);

        sha256_compress_fn = sha256_compress_sw;
        uint8_t sw_hash[32];
        sha256d(sample, 80, sw_hash);

        sha256_compress_fn = sha256_compress_hw;
        uint8_t hw_hash[32];
        sha256d(sample, 80, hw_hash);

        char sw_hex[65], hw_hex[65];
        bytes_to_hex(sw_hash, 32, sw_hex);
        bytes_to_hex(hw_hash, 32, hw_hex);

        printf("  SW: %s\n", sw_hex);
        printf("  HW: %s\n", hw_hex);

        if (memcmp(sw_hash, hw_hash, 32) == 0) {
            printf("  Status    : [PASS]\n\n");
        } else {
            printf("  Status    : [FAIL]\n\n");
            failures++;
        }
    }
#endif

    if (failures == 0) {
        printf("All correctness tests PASSED successfully!\n");
        return 0;
    } else {
        printf("ERROR: %d correctness test(s) FAILED!\n", failures);
        return 1;
    }
}

/* ============================================================================
 * SECTION 8b: Mining Control-Plane Simulation (Cores 0-2 hash / Core 3 control)
 * ============================================================================ */

static inline long timespec_diff_ns(const struct timespec *a, const struct timespec *b) {
    return (a->tv_sec - b->tv_sec) * 1000000000L + (a->tv_nsec - b->tv_nsec);
}

#define JOB_HEADER_LEN 80
#define SHARE_QUEUE_CAP 64

typedef struct {
    pthread_mutex_t lock;
    atomic_uint version;
    uint8_t header[JOB_HEADER_LEN];
} shared_job_t;

static void shared_job_init(shared_job_t *job, const uint8_t *initial_header) {
    pthread_mutex_init(&job->lock, NULL);
    atomic_init(&job->version, 1);
    memcpy(job->header, initial_header, JOB_HEADER_LEN);
}

static void shared_job_destroy(shared_job_t *job) {
    pthread_mutex_destroy(&job->lock);
}

/* Called only by the control thread (core 3), roughly a few times a second. */
static void shared_job_publish(shared_job_t *job, const uint8_t *new_header) {
    pthread_mutex_lock(&job->lock);
    memcpy(job->header, new_header, JOB_HEADER_LEN);
    pthread_mutex_unlock(&job->lock);
    atomic_fetch_add(&job->version, 1);
}

/* Called by hash workers only when their cached version is stale. */
static unsigned shared_job_load(shared_job_t *job, uint8_t *out_header) {
    unsigned v = atomic_load(&job->version);
    pthread_mutex_lock(&job->lock);
    memcpy(out_header, job->header, JOB_HEADER_LEN);
    pthread_mutex_unlock(&job->lock);
    return v;
}

typedef struct {
    uint32_t nonce;
    unsigned job_version;
} share_t;

typedef struct {
    pthread_mutex_t lock;
    share_t items[SHARE_QUEUE_CAP];
    int head, tail, count;
    atomic_ulong found_total;
    atomic_ulong dropped_total;
} share_queue_t;

static void share_queue_init(share_queue_t *q) {
    pthread_mutex_init(&q->lock, NULL);
    q->head = q->tail = q->count = 0;
    atomic_init(&q->found_total, 0);
    atomic_init(&q->dropped_total, 0);
}

static void share_queue_destroy(share_queue_t *q) {
    pthread_mutex_destroy(&q->lock);
}

static void share_queue_push(share_queue_t *q, uint32_t nonce, unsigned jv) {
    atomic_fetch_add(&q->found_total, 1);
    pthread_mutex_lock(&q->lock);
    if (q->count < SHARE_QUEUE_CAP) {
        q->items[q->tail].nonce = nonce;
        q->items[q->tail].job_version = jv;
        q->tail = (q->tail + 1) % SHARE_QUEUE_CAP;
        q->count++;
        pthread_mutex_unlock(&q->lock);
    } else {
        pthread_mutex_unlock(&q->lock);
        atomic_fetch_add(&q->dropped_total, 1);
    }
}

static int share_queue_pop(share_queue_t *q, share_t *out) {
    int got = 0;
    pthread_mutex_lock(&q->lock);
    if (q->count > 0) {
        *out = q->items[q->head];
        q->head = (q->head + 1) % SHARE_QUEUE_CAP;
        q->count--;
        got = 1;
    }
    pthread_mutex_unlock(&q->lock);
    return got;
}

/* ---- Stub pool: stands in for a remote Stratum server over loopback TCP ---- */

typedef struct {
    atomic_int *stop_flag;
    atomic_int ready;
    int port;
} stub_pool_arg_t;

static void *stub_pool_thread(void *arg) {
    stub_pool_arg_t *a = (stub_pool_arg_t *)arg;

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) return NULL;
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(listen_fd);
        return NULL;
    }
    socklen_t alen = sizeof(addr);
    getsockname(listen_fd, (struct sockaddr *)&addr, &alen);
    a->port = ntohs(addr.sin_port);
    listen(listen_fd, 1);
    atomic_store(&a->ready, 1);

    while (!atomic_load(a->stop_flag)) {
        int conn_fd = -1;
        while (!atomic_load(a->stop_flag) && conn_fd < 0) {
            struct pollfd pfd = { listen_fd, POLLIN, 0 };
            int pr = poll(&pfd, 1, 100);
            if (pr > 0 && (pfd.revents & POLLIN)) {
                conn_fd = accept(listen_fd, NULL, NULL);
            }
        }
        if (conn_fd < 0) break;

        unsigned job_counter = 0;
        char buf[256];
        while (!atomic_load(a->stop_flag)) {
            struct pollfd pfd = { conn_fd, POLLIN, 0 };
            int pr = poll(&pfd, 1, 100);
            if (pr <= 0) continue;
            ssize_t n = recv(conn_fd, buf, sizeof(buf) - 1, 0);
            if (n <= 0) break;
            buf[n] = '\0';
            if (strncmp(buf, "GETJOB", 6) == 0) {
                job_counter++;
                char resp[64];
                int len = snprintf(resp, sizeof(resp), "JOB %u\n", job_counter);
                send(conn_fd, resp, (size_t)len, 0);
            } else if (strncmp(buf, "SUBMIT", 6) == 0) {
                const char *ok = "OK\n";
                send(conn_fd, ok, 3, 0);
            }
        }
        close(conn_fd);
    }

    close(listen_fd);
    return NULL;
}

/* ---- Control thread: the actual "mining client" Stratum/job/share thread ---- */

typedef struct {
    int cpu_id;
    atomic_int *stop_flag;
    shared_job_t *job;
    share_queue_t *shares;
    int stub_port;
    int job_interval_ms;
    int reconnect_interval_ms;
    atomic_ulong job_updates;
    atomic_ulong shares_submitted;
    atomic_ulong reconnects;
} control_thread_arg_t;

static int control_connect(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 150000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return fd;
}

static void *control_worker(void *arg) {
    control_thread_arg_t *targ = (control_thread_arg_t *)arg;

    if (targ->cpu_id >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(targ->cpu_id, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }

    const char *b125552_hex = "0100000081cd02ab7e569e8bcd9317e2fe99f2de44d49ab2b8851ba4a308000000000000e320b6c2fffc8d750423db8b1eb942ae710e951ed797f7affc8892b0f1fc122bc7f5d74df2b9441a42a14695";
    uint8_t base_header[80];
    hex_to_bytes(b125552_hex, base_header, 80);

    int fd = -1;
    while (!atomic_load(targ->stop_flag) && fd < 0) {
        fd = control_connect(targ->stub_port);
        if (fd < 0) usleep(50000);
    }

    struct timespec last_job, last_reconnect, now;
    clock_gettime(CLOCK_MONOTONIC, &last_job);
    clock_gettime(CLOCK_MONOTONIC, &last_reconnect);

    while (!atomic_load(targ->stop_flag)) {
        clock_gettime(CLOCK_MONOTONIC, &now);

        /* Poll for a new job */
        if (fd >= 0 && timespec_diff_ns(&now, &last_job) / 1000000 >= targ->job_interval_ms) {
            const char *req = "GETJOB\n";
            if (send(fd, req, strlen(req), 0) >= 0) {
                char buf[64];
                ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
                if (n > 0) {
                    buf[n] = '\0';
                    unsigned jc = 0;
                    sscanf(buf, "JOB %u", &jc);
                    uint8_t new_header[80];
                    memcpy(new_header, base_header, 80);
                    write_uint32_le(new_header + 68, (uint32_t)(1231006505u + jc));
                    shared_job_publish(targ->job, new_header);
                    atomic_fetch_add(&targ->job_updates, 1);
                }
            }
            last_job = now;
        }

        /* Drain and submit shares */
        share_t s;
        while (fd >= 0 && share_queue_pop(targ->shares, &s)) {
            char msg[64];
            int len = snprintf(msg, sizeof(msg), "SUBMIT %u\n", s.nonce);
            if (send(fd, msg, (size_t)len, 0) >= 0) {
                char ack[16];
                recv(fd, ack, sizeof(ack), 0);
                atomic_fetch_add(&targ->shares_submitted, 1);
            }
        }

        /* Periodically simulate a pool reconnect */
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (targ->reconnect_interval_ms > 0 &&
            timespec_diff_ns(&now, &last_reconnect) / 1000000 >= targ->reconnect_interval_ms) {
            if (fd >= 0) close(fd);
            usleep(20000);
            fd = control_connect(targ->stub_port);
            atomic_fetch_add(&targ->reconnects, 1);
            last_reconnect = now;
        }

        usleep(20000);
    }

    if (fd >= 0) close(fd);
    return NULL;
}

/*
 * Benchmark Worker:
 * Optimized using Midstate Precomputation. The initial 64-byte block (invariant)
 * is hashed ONCE per work template, and each subsequent nonce only hashes the
 * second block + the second SHA-256 pass (2 compressions per hash instead of 3).
 */
typedef struct {
    uint64_t start_nonce;
    uint64_t count;
    int      cpu_id;
    uint8_t  final_hash[32];
    shared_job_t  *job;
    share_queue_t *shares;
    int            share_zero_bits;
} bench_thread_arg_t;

#define JOB_CHECK_MASK 0x1FFFu /* re-check job version every 8192 nonces */

static void *bench_worker(void *arg) {
    bench_thread_arg_t *targ = (bench_thread_arg_t *)arg;

    if (targ->cpu_id >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(targ->cpu_id, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }

    uint8_t hash[32];
    memset(hash, 0, 32);

    if (targ->job == NULL) {
        /* ---- Fixed-header mode with Midstate Optimization ---- */
        const char *b125552_hex = "0100000081cd02ab7e569e8bcd9317e2fe99f2de44d49ab2b8851ba4a308000000000000e320b6c2fffc8d750423db8b1eb942ae710e951ed797f7affc8892b0f1fc122bc7f5d74df2b9441a42a14695";
        uint8_t header_buf[80];
        hex_to_bytes(b125552_hex, header_buf, 80);

        /* Precompute midstate ONCE for the entire nonce range */
        bitcoin_midstate_t ms;
        bitcoin_midstate_init(&ms, header_buf);

        for (uint64_t i = 0; i < targ->count; i++) {
            uint32_t nonce = (uint32_t)(targ->start_nonce + i);
            bitcoin_hash_nonce_opt(&ms, nonce, hash);
        }
    } else {
        /* ---- Control-plane-aware mode with Midstate Optimization ---- */
        uint8_t local_header[80];
        unsigned local_version = shared_job_load(targ->job, local_header);

        bitcoin_midstate_t ms;
        bitcoin_midstate_init(&ms, local_header);

        for (uint64_t i = 0; i < targ->count; i++) {
            if ((i & JOB_CHECK_MASK) == 0) {
                unsigned v = atomic_load(&targ->job->version);
                if (v != local_version) {
                    local_version = shared_job_load(targ->job, local_header);
                    /* Recompute midstate only when new job arrives */
                    bitcoin_midstate_init(&ms, local_header);
                }
            }

            uint32_t nonce = (uint32_t)(targ->start_nonce + i);
            bitcoin_hash_nonce_opt(&ms, nonce, hash);

            if (targ->shares && targ->share_zero_bits > 0) {
                uint32_t lead = ((uint32_t)hash[0] << 24) | ((uint32_t)hash[1] << 16) |
                                 ((uint32_t)hash[2] << 8) | (uint32_t)hash[3];
                uint32_t threshold = (targ->share_zero_bits >= 32) ? 0u
                                    : (0xFFFFFFFFu >> targ->share_zero_bits);
                if (lead <= threshold) {
                    share_queue_push(targ->shares, nonce, local_version);
                }
            }
        }
    }

    memcpy(targ->final_hash, hash, 32);
    return NULL;
}

static long detect_core_count(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n >= 1) ? n : 1;
}

static long read_sysfs_long(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    long val;
    int ok = (fscanf(f, "%ld", &val) == 1);
    fclose(f);
    return ok ? val : -1;
}

static long read_cpu0_freq_khz(void) {
    return read_sysfs_long("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
}

static long read_thermal_zone0_millideg(void) {
    return read_sysfs_long("/sys/class/thermal/thermal_zone0/temp");
}

/* Diagnostic busy-neighbor thread definitions */
#define SPIN_CYCLE_NS 20000000L

typedef enum {
    SPIN_MODE_ALU = 0,
    SPIN_MODE_LOADSTORE,
    SPIN_MODE_MEMBW,
    SPIN_MODE_NEON,
    SPIN_MODE_SHA
} spin_mode_t;

static const char *spin_mode_name(spin_mode_t m) {
    switch (m) {
        case SPIN_MODE_ALU:       return "alu (integer arithmetic, no memory)";
        case SPIN_MODE_LOADSTORE: return "loadstore (L1-resident load/store)";
        case SPIN_MODE_MEMBW:     return "membw (streaming R/W across 8MB buffer)";
        case SPIN_MODE_NEON:      return "neon (SIMD arithmetic, no memory)";
        case SPIN_MODE_SHA:       return "sha (SHA256 crypto instructions, no memory)";
        default:                  return "unknown";
    }
}

typedef struct {
    int cpu_id;
    atomic_int *stop_flag;
    int duty_percent;
    spin_mode_t mode;
} spin_thread_arg_t;

static volatile uint64_t g_spin_sink = 0;

static inline void spin_batch_alu(volatile uint64_t *counter) {
    for (int b = 0; b < 2000; b++) (*counter)++;
}

static inline void spin_batch_loadstore(volatile uint64_t *scratch, size_t n) {
    for (int r = 0; r < 100; r++) {
        for (size_t i = 0; i < n; i++) {
            scratch[i] = scratch[i] + 1;
        }
    }
}

static inline void spin_batch_membw(volatile uint64_t *buf, size_t words) {
    for (size_t i = 0; i < words; i++) {
        buf[i] = buf[i] ^ 0x1ULL;
    }
}

#if defined(__aarch64__)
static inline void spin_batch_neon(void) {
    uint32x4_t v = vdupq_n_u32(1);
    for (int r = 0; r < 4000; r++) {
        v = vaddq_u32(v, v);
    }
    g_spin_sink = (uint64_t)vgetq_lane_u32(v, 0);
}
#endif

#if defined(HAVE_ARM_SHA2_TARGET)
__attribute__((target("+crypto")))
static inline void spin_batch_sha(void) {
    uint32x4_t state0 = vdupq_n_u32(0x6a09e667u);
    uint32x4_t state1 = vdupq_n_u32(0xbb67ae85u);
    uint32x4_t wk = vdupq_n_u32(0x428a2f98u);
    for (int r = 0; r < 2000; r++) {
        uint32x4_t tmp = state0;
        state0 = vsha256hq_u32(state0, state1, wk);
        state1 = vsha256h2q_u32(state1, tmp, wk);
    }
    g_spin_sink = (uint64_t)vgetq_lane_u32(state0, 0);
}
#endif

static void *spin_worker(void *arg) {
    spin_thread_arg_t *targ = (spin_thread_arg_t *)arg;

    if (targ->cpu_id >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(targ->cpu_id, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }

    long busy_ns = (SPIN_CYCLE_NS * (long)targ->duty_percent) / 100;
    long idle_ns = SPIN_CYCLE_NS - busy_ns;

    volatile uint64_t counter = 0;
    enum { LS_WORDS = 64 };
    volatile uint64_t ls_scratch[LS_WORDS];
    for (int i = 0; i < LS_WORDS; i++) ls_scratch[i] = 0;

    volatile uint64_t *membw_buf = NULL;
    size_t membw_words = 0;
    if (targ->mode == SPIN_MODE_MEMBW) {
        size_t bytes = 8 * 1024 * 1024;
        membw_buf = (volatile uint64_t *)malloc(bytes);
        if (membw_buf) {
            membw_words = bytes / sizeof(uint64_t);
            for (size_t i = 0; i < membw_words; i++) membw_buf[i] = 0;
        }
    }

    while (!atomic_load(targ->stop_flag)) {
        if (busy_ns > 0) {
            struct timespec cycle_start, now;
            clock_gettime(CLOCK_MONOTONIC, &cycle_start);
            do {
                switch (targ->mode) {
                    case SPIN_MODE_LOADSTORE:
                        spin_batch_loadstore(ls_scratch, LS_WORDS);
                        break;
                    case SPIN_MODE_MEMBW:
                        if (membw_buf) {
                            spin_batch_membw(membw_buf, membw_words);
                        } else {
                            spin_batch_alu(&counter);
                        }
                        break;
#if defined(__aarch64__)
                    case SPIN_MODE_NEON:
                        spin_batch_neon();
                        break;
#endif
#if defined(HAVE_ARM_SHA2_TARGET)
                    case SPIN_MODE_SHA:
                        spin_batch_sha();
                        break;
#endif
                    case SPIN_MODE_ALU:
                    default:
                        spin_batch_alu(&counter);
                        break;
                }
                clock_gettime(CLOCK_MONOTONIC, &now);
            } while (timespec_diff_ns(&now, &cycle_start) < busy_ns && !atomic_load(targ->stop_flag));
        }
        if (idle_ns > 0) {
            struct timespec ts;
            ts.tv_sec = idle_ns / 1000000000L;
            ts.tv_nsec = idle_ns % 1000000000L;
            nanosleep(&ts, NULL);
        }
    }

    if (membw_buf) free((void *)membw_buf);
    (void)counter;
    return NULL;
}

static void run_benchmark(uint64_t iterations, int num_threads, int core_offset, int pin_enabled,
                           int spin_core, int spin_duty, spin_mode_t spin_mode,
                           int control_core, int job_interval_ms, int reconnect_interval_ms, int share_zero_bits) {
    if (num_threads < 1) num_threads = 1;

    long total_cores = detect_core_count();

    printf("======================================================================\n");
    printf("Running Bitcoin SHA-256d Benchmark on Amlogic S905X (Optimized Midstate)\n");
    printf("======================================================================\n");
    printf("Backend           : %s\n", backend_name);
    printf("Optimization      : Midstate Precomputation (2 compressions/hash instead of 3)\n");
    printf("Threads           : %d\n", num_threads);
    if (pin_enabled) {
        printf("Core Pinning      : enabled (offset %d, %ld cores online)\n", core_offset, total_cores);
    } else {
        printf("Core Pinning      : disabled (OS scheduler decides placement)\n");
    }
    if (spin_core >= 0) {
        printf("Busy Neighbor     : enabled (core %d, %d%% duty cycle, mode: %s)\n",
               spin_core, spin_duty, spin_mode_name(spin_mode));
    }
    if (control_core >= 0) {
        printf("Control-Plane Arch: enabled (control thread pinned to core %d, job every %dms, "
               "reconnect every %dms, share target ~1-in-2^%d)\n",
               control_core, job_interval_ms, reconnect_interval_ms, share_zero_bits);
    }
    printf("Target Iterations : %lu hashes\n", (unsigned long)iterations);

    bench_thread_arg_t *args = calloc((size_t)num_threads, sizeof(bench_thread_arg_t));
    pthread_t *threads = calloc((size_t)num_threads, sizeof(pthread_t));
    if (!args || !threads) {
        fprintf(stderr, "Error: allocation failed for %d threads.\n", num_threads);
        free(args);
        free(threads);
        return;
    }

    shared_job_t job;
    share_queue_t shares;
    int control_active = 0;
    atomic_int control_stop = 0;
    stub_pool_arg_t stub_arg;
    control_thread_arg_t control_arg;
    pthread_t stub_thread_handle, control_thread_handle;
    int stub_started = 0, control_started = 0;

    if (control_core >= 0) {
        const char *b125552_hex = "0100000081cd02ab7e569e8bcd9317e2fe99f2de44d49ab2b8851ba4a308000000000000e320b6c2fffc8d750423db8b1eb942ae710e951ed797f7affc8892b0f1fc122bc7f5d74df2b9441a42a14695";
        uint8_t initial_header[80];
        hex_to_bytes(b125552_hex, initial_header, 80);
        shared_job_init(&job, initial_header);
        share_queue_init(&shares);
        control_active = 1;

        stub_arg.stop_flag = &control_stop;
        atomic_init(&stub_arg.ready, 0);
        stub_arg.port = -1;
        if (pthread_create(&stub_thread_handle, NULL, stub_pool_thread, &stub_arg) == 0) {
            stub_started = 1;
            for (int spins = 0; spins < 200 && !atomic_load(&stub_arg.ready); spins++) {
                usleep(5000);
            }
        } else {
            fprintf(stderr, "Warning: failed to start stub-pool thread.\n");
        }

        control_arg.cpu_id = control_core;
        control_arg.stop_flag = &control_stop;
        control_arg.job = &job;
        control_arg.shares = &shares;
        control_arg.stub_port = stub_arg.port;
        control_arg.job_interval_ms = job_interval_ms;
        control_arg.reconnect_interval_ms = reconnect_interval_ms;
        atomic_init(&control_arg.job_updates, 0);
        atomic_init(&control_arg.shares_submitted, 0);
        atomic_init(&control_arg.reconnects, 0);
        if (stub_started && stub_arg.port >= 0) {
            if (pthread_create(&control_thread_handle, NULL, control_worker, &control_arg) == 0) {
                control_started = 1;
            } else {
                fprintf(stderr, "Warning: failed to start control thread on core %d.\n", control_core);
            }
        }
    }

    uint64_t base = iterations / (uint64_t)num_threads;
    uint64_t remainder = iterations % (uint64_t)num_threads;
    uint64_t cursor = 0;
    for (int t = 0; t < num_threads; t++) {
        args[t].start_nonce = cursor;
        args[t].count = base + ((uint64_t)t < remainder ? 1 : 0);
        args[t].cpu_id = pin_enabled ? (int)((core_offset + t) % total_cores) : -1;
        args[t].job = control_active ? &job : NULL;
        args[t].shares = control_active ? &shares : NULL;
        args[t].share_zero_bits = share_zero_bits;
        cursor += args[t].count;
    }

    struct timespec start_time, end_time;

    atomic_int spin_stop = 0;
    pthread_t spin_thread;
    spin_thread_arg_t spin_arg = { spin_core, &spin_stop, spin_duty, spin_mode };
    int spin_started = 0;
    if (spin_core >= 0) {
        if (pthread_create(&spin_thread, NULL, spin_worker, &spin_arg) == 0) {
            spin_started = 1;
        } else {
            fprintf(stderr, "Warning: failed to start busy-neighbor thread on core %d.\n", spin_core);
        }
    }

    long freq_before = read_cpu0_freq_khz();
    long temp_before = read_thermal_zone0_millideg();

    clock_gettime(CLOCK_MONOTONIC, &start_time);

    for (int t = 0; t < num_threads; t++) {
        if (pthread_create(&threads[t], NULL, bench_worker, &args[t]) != 0) {
            fprintf(stderr, "Error: failed to create worker thread %d.\n", t);
            num_threads = t;
            break;
        }
    }
    for (int t = 0; t < num_threads; t++) {
        pthread_join(threads[t], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);

    long freq_after = read_cpu0_freq_khz();
    long temp_after = read_thermal_zone0_millideg();

    if (spin_started) {
        atomic_store(&spin_stop, 1);
        pthread_join(spin_thread, NULL);
    }

    if (control_active) {
        atomic_store(&control_stop, 1);
        if (control_started) pthread_join(control_thread_handle, NULL);
        if (stub_started) pthread_join(stub_thread_handle, NULL);
    }

    double elapsed_sec = (double)(end_time.tv_sec - start_time.tv_sec) +
                         (double)(end_time.tv_nsec - start_time.tv_nsec) / 1e9;

    double hashes_per_sec = (elapsed_sec > 0.0) ? ((double)iterations / elapsed_sec) : 0.0;

    uint8_t display_hash[32];
    reverse_bytes(display_hash, args[num_threads - 1].final_hash, 32);
    char final_hex[65];
    bytes_to_hex(display_hash, 32, final_hex);

    printf("\nBenchmark Results:\n");
    printf("  Total Hashes    : %lu\n", (unsigned long)iterations);
    printf("  Elapsed Time    : %.6f seconds\n", elapsed_sec);
    printf("  Hash Rate       : %.2f H/s (%.2f KH/s, %.3f MH/s)\n",
           hashes_per_sec, hashes_per_sec / 1000.0, hashes_per_sec / 1000000.0);
    printf("  Per-core Rate   : ~%.2f KH/s/core\n", (hashes_per_sec / 1000.0) / num_threads);
    if (temp_before >= 0 && temp_after >= 0) {
        printf("  Temp (C)        : %.1f -> %.1f (delta %+.1f)\n",
               temp_before / 1000.0, temp_after / 1000.0, (temp_after - temp_before) / 1000.0);
    }
    if (freq_before >= 0 && freq_after >= 0) {
        printf("  CPU0 Freq (MHz) : %.0f -> %.0f\n", freq_before / 1000.0, freq_after / 1000.0);
    }
    if (control_active) {
        printf("  Job Updates     : %lu\n", atomic_load(&control_arg.job_updates));
        printf("  Shares Found    : %lu\n", atomic_load(&shares.found_total));
        printf("  Shares Submitted: %lu\n", atomic_load(&control_arg.shares_submitted));
        printf("  Shares Dropped  : %lu\n", atomic_load(&shares.dropped_total));
        printf("  Reconnects      : %lu\n", atomic_load(&control_arg.reconnects));
    }
    printf("  Sample Hash     : %s (from last worker's final nonce)\n", final_hex);
    printf("======================================================================\n");

    if (control_active) {
        shared_job_destroy(&job);
        share_queue_destroy(&shares);
    }

    free(args);
    free(threads);
}

/* ============================================================================
 * SECTION 10: CLI
 * ============================================================================ */

static void print_usage(const char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -t, --test              Run known Bitcoin block header correctness tests.\n");
    printf("  -b, --benchmark         Run proof-of-work nonce hashing benchmark.\n");
    printf("  -n, --iterations <N>    Number of benchmark iterations (default: 5000000).\n");
    printf("  -j, --threads <N>       Worker threads for benchmark (default: all online cores).\n");
    printf("  -o, --offset <N>        First CPU core to pin threads to (default: 0). Implies pinning.\n");
    printf("  -p, --no-pin            Disable CPU pinning; let the OS scheduler place threads.\n");
    printf("  -x, --spin-core <N>     Diagnostic: run a dummy busy-spin thread pinned to core N\n");
    printf("                          alongside the hash workers (no memory/crypto traffic).\n");
    printf("                          Used to isolate generic contention from crypto-specific\n");
    printf("                          contention. Default: disabled.\n");
    printf("  -y, --spin-duty <P>     Duty cycle percent (0-100) for the spin thread, in ~20ms\n");
    printf("                          cycles of busy/sleep. Only meaningful with -x. Default: 100.\n");
    printf("  -m, --spin-mode <M>     Workload type for the spin thread: alu, loadstore, membw,\n");
    printf("                          neon, sha. Only meaningful with -x. Default: alu.\n");
    printf("  -c, --control-core <N>  Real mining-architecture mode: pin a genuine Stratum-style\n");
    printf("                          control thread (job polling, share submission, reconnects,\n");
    printf("                          over a real loopback TCP socket) to core N, instead of the\n");
    printf("                          synthetic -x spin thread. Mutually exclusive with -x.\n");
    printf("  -J, --job-interval <ms> How often the control thread polls for a new job. Only\n");
    printf("                          meaningful with -c. Default: 250.\n");
    printf("  -R, --reconnect <ms>    How often the control thread simulates a pool reconnect.\n");
    printf("                          0 disables reconnect simulation. Only meaningful with -c.\n");
    printf("                          Default: 5000.\n");
    printf("  -z, --share-bits <N>    Synthetic share-target strictness: a hash is a \"share\" if\n");
    printf("                          its leading N bits are zero (~1-in-2^N hashes). Only\n");
    printf("                          meaningful with -c. Default: 24 (~20 shares/min at 5.8MH/s).\n");
    printf("  -s, --sw-only           Force the portable software backend (disable HW crypto).\n");
    printf("  -h, --help              Display this help message.\n");
}

int main(int argc, char *argv[]) {
    int do_test = 0;
    int do_benchmark = 0;
    int force_sw = 0;
    uint64_t iterations = 5000000ULL;
    int num_threads = (int)detect_core_count();
    int core_offset = 0;
    int pin_enabled = 1;
    int spin_core = -1;
    int spin_duty = 100;
    spin_mode_t spin_mode = SPIN_MODE_ALU;
    int control_core = -1;
    int job_interval_ms = 250;
    int reconnect_interval_ms = 5000;
    int share_zero_bits = 24;

    if (argc < 2) {
        print_usage(argv[0]);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--test") == 0) {
            do_test = 1;
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--benchmark") == 0) {
            do_benchmark = 1;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--sw-only") == 0) {
            force_sw = 1;
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--iterations") == 0) {
            if (i + 1 < argc) {
                iterations = strtoull(argv[++i], NULL, 10);
            } else {
                fprintf(stderr, "Error: Option %s requires an integer argument.\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-j") == 0 || strcmp(argv[i], "--threads") == 0) {
            if (i + 1 < argc) {
                num_threads = atoi(argv[++i]);
                if (num_threads < 1) {
                    fprintf(stderr, "Error: --threads must be >= 1.\n");
                    return 1;
                }
            } else {
                fprintf(stderr, "Error: Option %s requires an integer argument.\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--offset") == 0) {
            if (i + 1 < argc) {
                core_offset = atoi(argv[++i]);
                if (core_offset < 0) {
                    fprintf(stderr, "Error: --offset must be >= 0.\n");
                    return 1;
                }
                pin_enabled = 1;
            } else {
                fprintf(stderr, "Error: Option %s requires an integer argument.\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--no-pin") == 0) {
            pin_enabled = 0;
        } else if (strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "--spin-core") == 0) {
            if (i + 1 < argc) {
                spin_core = atoi(argv[++i]);
                if (spin_core < 0) {
                    fprintf(stderr, "Error: --spin-core must be >= 0.\n");
                    return 1;
                }
            } else {
                fprintf(stderr, "Error: Option %s requires an integer argument.\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-y") == 0 || strcmp(argv[i], "--spin-duty") == 0) {
            if (i + 1 < argc) {
                spin_duty = atoi(argv[++i]);
                if (spin_duty < 0 || spin_duty > 100) {
                    fprintf(stderr, "Error: --spin-duty must be between 0 and 100.\n");
                    return 1;
                }
            } else {
                fprintf(stderr, "Error: Option %s requires an integer argument.\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--spin-mode") == 0) {
            if (i + 1 < argc) {
                const char *modearg = argv[++i];
                if (strcmp(modearg, "alu") == 0) {
                    spin_mode = SPIN_MODE_ALU;
                } else if (strcmp(modearg, "loadstore") == 0) {
                    spin_mode = SPIN_MODE_LOADSTORE;
                } else if (strcmp(modearg, "membw") == 0) {
                    spin_mode = SPIN_MODE_MEMBW;
                } else if (strcmp(modearg, "neon") == 0) {
                    spin_mode = SPIN_MODE_NEON;
                } else if (strcmp(modearg, "sha") == 0) {
                    spin_mode = SPIN_MODE_SHA;
                } else {
                    fprintf(stderr, "Error: --spin-mode must be one of: alu, loadstore, membw, neon, sha.\n");
                    return 1;
                }
            } else {
                fprintf(stderr, "Error: Option %s requires an argument.\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--control-core") == 0) {
            if (i + 1 < argc) {
                control_core = atoi(argv[++i]);
                if (control_core < 0) {
                    fprintf(stderr, "Error: --control-core must be >= 0.\n");
                    return 1;
                }
            } else {
                fprintf(stderr, "Error: Option %s requires an integer argument.\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-J") == 0 || strcmp(argv[i], "--job-interval") == 0) {
            if (i + 1 < argc) {
                job_interval_ms = atoi(argv[++i]);
                if (job_interval_ms < 1) {
                    fprintf(stderr, "Error: --job-interval must be >= 1.\n");
                    return 1;
                }
            } else {
                fprintf(stderr, "Error: Option %s requires an integer argument.\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-R") == 0 || strcmp(argv[i], "--reconnect") == 0) {
            if (i + 1 < argc) {
                reconnect_interval_ms = atoi(argv[++i]);
                if (reconnect_interval_ms < 0) {
                    fprintf(stderr, "Error: --reconnect must be >= 0.\n");
                    return 1;
                }
            } else {
                fprintf(stderr, "Error: Option %s requires an integer argument.\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-z") == 0 || strcmp(argv[i], "--share-bits") == 0) {
            if (i + 1 < argc) {
                share_zero_bits = atoi(argv[++i]);
                if (share_zero_bits < 0 || share_zero_bits > 32) {
                    fprintf(stderr, "Error: --share-bits must be between 0 and 32.\n");
                    return 1;
                }
            } else {
                fprintf(stderr, "Error: Option %s requires an integer argument.\n", argv[i]);
                return 1;
            }
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    select_sha256_backend();
    if (force_sw) {
        sha256_compress_fn = sha256_compress_sw;
        backend_name = "software (forced via -s / --sw-only)";
    }
    printf("SHA-256 backend selected: %s\n\n", backend_name);

    if (spin_core >= 0 && control_core >= 0) {
        fprintf(stderr, "Error: -x/--spin-core and -c/--control-core are mutually exclusive.\n");
        return 1;
    }

    if (spin_core >= 0 && spin_mode == SPIN_MODE_NEON) {
#if !defined(__aarch64__)
        fprintf(stderr, "Warning: --spin-mode neon requires aarch64; falling back to alu.\n");
        spin_mode = SPIN_MODE_ALU;
#endif
    }
    if (spin_core >= 0 && spin_mode == SPIN_MODE_SHA) {
#if defined(HAVE_ARM_SHA2_TARGET)
        if (!cpu_has_sha256_hw()) {
            fprintf(stderr, "Warning: --spin-mode sha requires HWCAP_SHA2; falling back to alu.\n");
            spin_mode = SPIN_MODE_ALU;
        }
#else
        fprintf(stderr, "Warning: --spin-mode sha requires an aarch64 crypto-capable build; falling back to alu.\n");
        spin_mode = SPIN_MODE_ALU;
#endif
    }

    int test_result = 0;
    if (do_test) {
        test_result = run_correctness_tests();
        if (test_result != 0) {
            return test_result;
        }
    }

    if (do_benchmark) {
        run_benchmark(iterations, num_threads, core_offset, pin_enabled, spin_core, spin_duty, spin_mode,
                      control_core, job_interval_ms, reconnect_interval_ms, share_zero_bits);
    }

    return test_result;
}
