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
#include <netdb.h>
#include <signal.h>
#include <errno.h>

#ifndef atomic_uint64_t
typedef _Atomic(uint64_t) atomic_uint64_t;
#endif

/* Global signal termination flag */
static volatile sig_atomic_t g_miner_stop = 0;
static void handle_miner_sig(int sig) {
    (void)sig;
    g_miner_stop = 1;
}

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
    uint32x4_t TMP0, TMP1;

    /* ARMv8 SHA-256 state register layout:
     * abcd = { a, b, c, d }
     * efgh = { e, f, g, h }
     */
    uint32x4_t abcd = vld1q_u32(&state[0]);
    uint32x4_t efgh = vld1q_u32(&state[4]);

    uint32x4_t abcd_save = abcd;
    uint32x4_t efgh_save = efgh;

    /* Load 64-byte block and byte-swap each 32-bit word (block is big-endian) */
    MSG0 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(block +  0)));
    MSG1 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(block + 16)));
    MSG2 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(block + 32)));
    MSG3 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(block + 48)));

    /* Rounds 0-3 */
    TMP0 = vaddq_u32(MSG0, vld1q_u32(&K[0]));
    TMP1 = abcd;
    abcd = vsha256hq_u32(abcd, efgh, TMP0);
    efgh = vsha256h2q_u32(efgh, TMP1, TMP0);

    /* Rounds 4-7 */
    TMP0 = vaddq_u32(MSG1, vld1q_u32(&K[4]));
    TMP1 = abcd;
    MSG0 = vsha256su0q_u32(MSG0, MSG1);
    abcd = vsha256hq_u32(abcd, efgh, TMP0);
    efgh = vsha256h2q_u32(efgh, TMP1, TMP0);
    MSG0 = vsha256su1q_u32(MSG0, MSG2, MSG3);

    /* Rounds 8-11 */
    TMP0 = vaddq_u32(MSG2, vld1q_u32(&K[8]));
    TMP1 = abcd;
    MSG1 = vsha256su0q_u32(MSG1, MSG2);
    abcd = vsha256hq_u32(abcd, efgh, TMP0);
    efgh = vsha256h2q_u32(efgh, TMP1, TMP0);
    MSG1 = vsha256su1q_u32(MSG1, MSG3, MSG0);

    /* Rounds 12-15 */
    TMP0 = vaddq_u32(MSG3, vld1q_u32(&K[12]));
    TMP1 = abcd;
    MSG2 = vsha256su0q_u32(MSG2, MSG3);
    abcd = vsha256hq_u32(abcd, efgh, TMP0);
    efgh = vsha256h2q_u32(efgh, TMP1, TMP0);
    MSG2 = vsha256su1q_u32(MSG2, MSG0, MSG1);

    /* Rounds 16-19 */
    TMP0 = vaddq_u32(MSG0, vld1q_u32(&K[16]));
    TMP1 = abcd;
    MSG3 = vsha256su0q_u32(MSG3, MSG0);
    abcd = vsha256hq_u32(abcd, efgh, TMP0);
    efgh = vsha256h2q_u32(efgh, TMP1, TMP0);
    MSG3 = vsha256su1q_u32(MSG3, MSG1, MSG2);

    /* Rounds 20-23 */
    TMP0 = vaddq_u32(MSG1, vld1q_u32(&K[20]));
    TMP1 = abcd;
    MSG0 = vsha256su0q_u32(MSG0, MSG1);
    abcd = vsha256hq_u32(abcd, efgh, TMP0);
    efgh = vsha256h2q_u32(efgh, TMP1, TMP0);
    MSG0 = vsha256su1q_u32(MSG0, MSG2, MSG3);

    /* Rounds 24-27 */
    TMP0 = vaddq_u32(MSG2, vld1q_u32(&K[24]));
    TMP1 = abcd;
    MSG1 = vsha256su0q_u32(MSG1, MSG2);
    abcd = vsha256hq_u32(abcd, efgh, TMP0);
    efgh = vsha256h2q_u32(efgh, TMP1, TMP0);
    MSG1 = vsha256su1q_u32(MSG1, MSG3, MSG0);

    /* Rounds 28-31 */
    TMP0 = vaddq_u32(MSG3, vld1q_u32(&K[28]));
    TMP1 = abcd;
    MSG2 = vsha256su0q_u32(MSG2, MSG3);
    abcd = vsha256hq_u32(abcd, efgh, TMP0);
    efgh = vsha256h2q_u32(efgh, TMP1, TMP0);
    MSG2 = vsha256su1q_u32(MSG2, MSG0, MSG1);

    /* Rounds 32-35 */
    TMP0 = vaddq_u32(MSG0, vld1q_u32(&K[32]));
    TMP1 = abcd;
    MSG3 = vsha256su0q_u32(MSG3, MSG0);
    abcd = vsha256hq_u32(abcd, efgh, TMP0);
    efgh = vsha256h2q_u32(efgh, TMP1, TMP0);
    MSG3 = vsha256su1q_u32(MSG3, MSG1, MSG2);

    /* Rounds 36-39 */
    TMP0 = vaddq_u32(MSG1, vld1q_u32(&K[36]));
    TMP1 = abcd;
    MSG0 = vsha256su0q_u32(MSG0, MSG1);
    abcd = vsha256hq_u32(abcd, efgh, TMP0);
    efgh = vsha256h2q_u32(efgh, TMP1, TMP0);
    MSG0 = vsha256su1q_u32(MSG0, MSG2, MSG3);

    /* Rounds 40-43 */
    TMP0 = vaddq_u32(MSG2, vld1q_u32(&K[40]));
    TMP1 = abcd;
    MSG1 = vsha256su0q_u32(MSG1, MSG2);
    abcd = vsha256hq_u32(abcd, efgh, TMP0);
    efgh = vsha256h2q_u32(efgh, TMP1, TMP0);
    MSG1 = vsha256su1q_u32(MSG1, MSG3, MSG0);

    /* Rounds 44-47 */
    TMP0 = vaddq_u32(MSG3, vld1q_u32(&K[44]));
    TMP1 = abcd;
    MSG2 = vsha256su0q_u32(MSG2, MSG3);
    abcd = vsha256hq_u32(abcd, efgh, TMP0);
    efgh = vsha256h2q_u32(efgh, TMP1, TMP0);
    MSG2 = vsha256su1q_u32(MSG2, MSG0, MSG1);

    /* Rounds 48-51 */
    TMP0 = vaddq_u32(MSG0, vld1q_u32(&K[48]));
    TMP1 = abcd;
    MSG3 = vsha256su0q_u32(MSG3, MSG0);
    abcd = vsha256hq_u32(abcd, efgh, TMP0);
    efgh = vsha256h2q_u32(efgh, TMP1, TMP0);
    MSG3 = vsha256su1q_u32(MSG3, MSG1, MSG2);

    /* Rounds 52-55 (no further message schedule needed) */
    TMP0 = vaddq_u32(MSG1, vld1q_u32(&K[52]));
    TMP1 = abcd;
    abcd = vsha256hq_u32(abcd, efgh, TMP0);
    efgh = vsha256h2q_u32(efgh, TMP1, TMP0);

    /* Rounds 56-59 */
    TMP0 = vaddq_u32(MSG2, vld1q_u32(&K[56]));
    TMP1 = abcd;
    abcd = vsha256hq_u32(abcd, efgh, TMP0);
    efgh = vsha256h2q_u32(efgh, TMP1, TMP0);

    /* Rounds 60-63 */
    TMP0 = vaddq_u32(MSG3, vld1q_u32(&K[60]));
    TMP1 = abcd;
    abcd = vsha256hq_u32(abcd, efgh, TMP0);
    efgh = vsha256h2q_u32(efgh, TMP1, TMP0);

    abcd = vaddq_u32(abcd, abcd_save);
    efgh = vaddq_u32(efgh, efgh_save);

    vst1q_u32(&state[0], abcd);
    vst1q_u32(&state[4], efgh);
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
 * Store 8 x uint32 as 32 bytes in big-endian (network) byte order.
 * NEON path uses vrev32q_u8 (2 instructions vs ~40 compiler-generated).
 */
static inline void be32_store(uint8_t dst[32], const uint32_t src[8]) {
#if defined(HAVE_ARM_SHA2_TARGET)
    uint8x16_t lo = vrev32q_u8(vreinterpretq_u8_u32(vld1q_u32(&src[0])));
    uint8x16_t hi = vrev32q_u8(vreinterpretq_u8_u32(vld1q_u32(&src[4])));
    vst1q_u8(dst, lo);
    vst1q_u8(dst + 16, hi);
#else
    for (int i = 0; i < 8; i++) {
        dst[i * 4 + 0] = (uint8_t)((src[i] >> 24) & 0xFF);
        dst[i * 4 + 1] = (uint8_t)((src[i] >> 16) & 0xFF);
        dst[i * 4 + 2] = (uint8_t)((src[i] >> 8)  & 0xFF);
        dst[i * 4 + 3] = (uint8_t)( src[i]        & 0xFF);
    }
#endif
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
    be32_store(b_pass2, state1);
    b_pass2[32] = 0x80;
    memset(b_pass2 + 33, 0, 23);
    /* 32 bytes = 256 bits = 0x0000000000000100 */
    b_pass2[56] = 0; b_pass2[57] = 0; b_pass2[58] = 0; b_pass2[59] = 0;
    b_pass2[60] = 0; b_pass2[61] = 0; b_pass2[62] = 0x01; b_pass2[63] = 0x00;

    uint32_t state2[8];
    memcpy(state2, SHA256_H0, sizeof(SHA256_H0));

    sha256_compress_fn(state2, b_pass2);

    be32_store(hash, state2);
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

/* Forward declarations: the correctness suite exercises these Section 8b
 * helpers directly so regression tests always run the production code. */
static int stratum_parse_subscribe(const char *resp_line,
                                   char *extranonce1, size_t en1_cap,
                                   int *extranonce2_size);
static int stratum_build_coinbase(const char *coinb1_hex, const char *extranonce1_hex,
                                  const char *extranonce2_hex, const char *coinb2_hex,
                                  uint8_t *coinbase_hash);

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

    /*
     * Test 4: Regression - mining.subscribe response parsing.
     *
     * Pre-fix bug: the production parser located the first ']' after "result"
     * and took the next quoted string, extracting the literal method name
     * "mining.notify" instead of extranonce1. These vectors run the SAME
     * production parser (stratum_parse_subscribe) used by the miner.
     */
    {
        printf("Test 4: Stratum mining.subscribe response parsing\n");
        int t4_failures = 0;
        char en1[64];

        struct {
            const char *resp;
            const char *want_en1;   /* NULL -> parse must be rejected */
            int want_en2;           /* expected extranonce2_size on success */
        } t4_vecs[] = {
            /* canonical whitespace-formatted response (audit vector) */
            {"{\"id\": 1, \"result\": [[[\"mining.set_difficulty\", \"ab\"], "
             "[\"mining.notify\", \"cd\"]], \"01020304050607ff\", 4], \"error\": null}",
             "01020304050607ff", 4},
            /* canonical compact response (ckpool-style) */
            {"{\"id\":1,\"result\":[[[\"mining.set_difficulty\",\"ab\"],[\"mining.notify\",\"cd\"]],\"01020304050607ff\",4],\"error\":null}",
             "01020304050607ff", 4},
            /* different valid EN1 / 8-byte extranonce2 size */
            {"{\"id\":7,\"result\":[[[\"mining.set_difficulty\",\"k\"]],\"aabbccdd\",8],\"error\":null}",
             "aabbccdd", 8},
            /* malformed: no result member -> reject */
            {"{\"id\":1,\"error\":null}", NULL, 0},
            /* malformed: empty result array -> reject */
            {"{\"id\":1,\"result\":[],\"error\":null}", NULL, 0},
            /* regression shape: non-hex token where EN1 belongs -> reject */
            {"{\"id\":1,\"result\":[[[\"mining.set_difficulty\",\"q\"]],\"mining.notify\",4],\"error\":null}",
             NULL, 0},
            /* non-canonical flat layout (EN1 not at result[1]) -> reject */
            {"{\"id\":1,\"result\":[\"01020304050607ff\",4],\"error\":null}", NULL, 0},
        };

        for (size_t vi = 0; vi < sizeof(t4_vecs) / sizeof(t4_vecs[0]); vi++) {
            memset(en1, 0, sizeof(en1));
            int en2sz = 4;
            int rc = stratum_parse_subscribe(t4_vecs[vi].resp, en1,
                                             sizeof(en1), &en2sz);
            int ok;
            if (t4_vecs[vi].want_en1 == NULL)
                ok = (rc != 0);
            else
                ok = (rc == 0 &&
                      strcmp(en1, t4_vecs[vi].want_en1) == 0 &&
                      en2sz == t4_vecs[vi].want_en2);

            printf("  vector %zu: %s (rc=%d, en1=\"%s\", en2sz=%d)\n",
                   vi + 1, ok ? "PASS" : "FAIL", rc,
                   rc == 0 ? en1 : "(rejected)", en2sz);
            if (!ok) t4_failures++;
        }

        if (t4_failures == 0) {
            printf("  Status    : [PASS]\n\n");
        } else {
            printf("  Status    : [FAIL] (%d vector(s))\n\n", t4_failures);
            failures += t4_failures;
        }
    }

    /*
     * Test 5: extranonce1 reaches the production coinbase path unchanged.
     *
     * stratum_build_coinbase() is invoked with the exact EN1 value that the
     * fixed parser extracts and compared against an independent known-answer
     * digest (sha256d over coinb1||EN1||en2||coinb2). Also verifies the old
     * failure mode ("mining.notify", odd length) is rejected outright.
     */
    {
        printf("Test 5: extranonce1 propagation into stratum_build_coinbase()\n");
        int t5_failures = 0;

        const char *cb1 = "aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55aa55";
        const char *en2 = "00000001";
        const char *cb2 = "bb66bb66bb66bb66bb66bb66bb66bb66bb66bb66bb66bb66bb66bb66bb66bb66bb66bb66bb66bb66";

        struct {
            const char *en1;
            const char *want_hex;   /* NULL -> call must fail */
        } t5_vecs[] = {
            {"01020304050607ff", "248e4e50017edae5c063f6e02647265e167deeb8eff2975a6c7213044d2d5015"},
            {"0102030405060700", "df54649ecdab02efe6a30cdca94e0ea1b2af9af751ebf98b8df6ab53dd9bab23"},
            {"mining.notify", NULL},
        };

        for (size_t vi = 0; vi < sizeof(t5_vecs) / sizeof(t5_vecs[0]); vi++) {
            uint8_t cb_hash[32];
            char got_hex[65];
            int rc = stratum_build_coinbase(cb1, t5_vecs[vi].en1, en2, cb2, cb_hash);
            bytes_to_hex(cb_hash, 32, got_hex);

            int ok;
            if (t5_vecs[vi].want_hex == NULL)
                ok = (rc != 0);
            else
                ok = (rc == 0 && strcmp(got_hex, t5_vecs[vi].want_hex) == 0);

            printf("  vector %zu: %s (rc=%d%s%s)\n",
                   vi + 1, ok ? "PASS" : "FAIL", rc,
                   rc == 0 ? ", hash=" : "",
                   rc == 0 ? got_hex : "");
            if (!ok) t5_failures++;
        }

        if (t5_failures == 0) {
            printf("  Status    : [PASS]\n\n");
        } else {
            printf("  Status    : [FAIL] (%d vector(s))\n\n", t5_failures);
            failures += t5_failures;
        }
    }

    if (failures == 0) {
        printf("All correctness tests PASSED successfully!\n");
        return 0;
    } else {
        printf("ERROR: %d correctness test(s) FAILED!\n", failures);
        return 1;
    }
}

/* ============================================================================
 * SECTION 8b: Stratum V1 Mining Engine & Data Structures
 * ============================================================================ */

static inline long timespec_diff_ns(const struct timespec *a, const struct timespec *b) {
    return (a->tv_sec - b->tv_sec) * 1000000000L + (a->tv_nsec - b->tv_nsec);
}

#define JOB_HEADER_LEN 80
#define SHARE_QUEUE_CAP 128
#define MAX_MERKLE_BRANCHES 32

/* Convert difficulty to 256-bit big-endian target */
static void diff_to_target(double diff, uint8_t target_be[32]) {
    memset(target_be, 0, 32);
    if (diff <= 0.0) diff = 1.0;

    double current = 4294901760.0 / diff;
    int start_word = 1;

    while (current >= 4294967296.0 && start_word > 0) {
        current /= 4294967296.0;
        start_word--;
    }
    while (current < 1.0 && start_word < 7) {
        current *= 4294967296.0;
        start_word++;
    }

    for (int w = start_word; w < 8; w++) {
        uint32_t val = (uint32_t)current;
        target_be[w * 4 + 0] = (uint8_t)((val >> 24) & 0xFF);
        target_be[w * 4 + 1] = (uint8_t)((val >> 16) & 0xFF);
        target_be[w * 4 + 2] = (uint8_t)((val >> 8)  & 0xFF);
        target_be[w * 4 + 3] = (uint8_t)( val        & 0xFF);
        current = (current - (double)val) * 4294967296.0;
        if (current <= 0.0) break;
    }
}

/* Construct Coinbase and compute sha256d hash */
static int stratum_build_coinbase(const char *coinb1_hex, const char *extranonce1_hex,
                                  const char *extranonce2_hex, const char *coinb2_hex,
                                  uint8_t *coinbase_hash) {
    size_t len1 = strlen(coinb1_hex);
    size_t len_en1 = strlen(extranonce1_hex);
    size_t len_en2 = strlen(extranonce2_hex);
    size_t len2 = strlen(coinb2_hex);
    size_t total_hex_len = len1 + len_en1 + len_en2 + len2;

    if (total_hex_len % 2 != 0 || total_hex_len > 16384) return -1;

    char *coinbase_hex = (char *)malloc(total_hex_len + 1);
    if (!coinbase_hex) return -1;

    memcpy(coinbase_hex, coinb1_hex, len1);
    memcpy(coinbase_hex + len1, extranonce1_hex, len_en1);
    memcpy(coinbase_hex + len1 + len_en1, extranonce2_hex, len_en2);
    memcpy(coinbase_hex + len1 + len_en1 + len_en2, coinb2_hex, len2);
    coinbase_hex[total_hex_len] = '\0';

    size_t bin_len = total_hex_len / 2;
    uint8_t *coinbase_bin = (uint8_t *)malloc(bin_len);
    if (!coinbase_bin) {
        free(coinbase_hex);
        return -1;
    }

    hex_to_bytes(coinbase_hex, coinbase_bin, bin_len);
    free(coinbase_hex);

    sha256d(coinbase_bin, bin_len, coinbase_hash);
    free(coinbase_bin);
    return 0;
}

/* Compute Merkle Root by iteratively combining coinbase hash with merkle branches */
static int stratum_build_merkle_root(const uint8_t *coinbase_hash, int branch_count,
                                     char branches[][65], uint8_t *merkle_root) {
    memcpy(merkle_root, coinbase_hash, 32);

    for (int i = 0; i < branch_count; i++) {
        uint8_t branch_bin[32];
        hex_to_bytes(branches[i], branch_bin, 32);

        uint8_t combined[64];
        memcpy(combined, merkle_root, 32);
        memcpy(combined + 32, branch_bin, 32);

        sha256d(combined, 64, merkle_root);
    }
    return 0;
}

/* Build standard 80-byte Bitcoin block header with exact Stratum V1 endianness */
static void stratum_build_header(uint8_t header[80], uint32_t version,
                                 const char *prevhash_hex, const uint8_t *merkle_root,
                                 uint32_t ntime, uint32_t nbits, uint32_t nonce) {
    write_uint32_le(header + 0, version);

    /* Stratum V1 prevhash requires swab32 on each 4-byte chunk */
    uint8_t raw_prev[32];
    hex_to_bytes(prevhash_hex, raw_prev, 32);
    for (int i = 0; i < 8; i++) {
        header[4 + i * 4 + 0] = raw_prev[i * 4 + 3];
        header[4 + i * 4 + 1] = raw_prev[i * 4 + 2];
        header[4 + i * 4 + 2] = raw_prev[i * 4 + 1];
        header[4 + i * 4 + 3] = raw_prev[i * 4 + 0];
    }

    memcpy(header + 36, merkle_root, 32);
    write_uint32_le(header + 68, ntime);
    write_uint32_le(header + 72, nbits);
    write_uint32_le(header + 76, nonce);
}

/* Shared work structure for lock-free worker polling */
typedef struct {
    pthread_mutex_t lock;
    atomic_uint version;
    bitcoin_midstate_t midstate;
    uint8_t header[JOB_HEADER_LEN];
    uint8_t target_be[32];
    char job_id[64];
    char ntime_hex[9];
    char extranonce2_hex[32];
    atomic_int clean_job_flag;
} shared_stratum_work_t;

static void shared_stratum_work_init(shared_stratum_work_t *w) {
    pthread_mutex_init(&w->lock, NULL);
    atomic_init(&w->version, 0);
    atomic_init(&w->clean_job_flag, 0);
    memset(w->header, 0, JOB_HEADER_LEN);
    diff_to_target(1.0, w->target_be);
    w->job_id[0] = '\0';
    strcpy(w->ntime_hex, "00000000");
    strcpy(w->extranonce2_hex, "00000000");
}

static void shared_stratum_work_destroy(shared_stratum_work_t *w) {
    pthread_mutex_destroy(&w->lock);
}

static void shared_stratum_work_publish(shared_stratum_work_t *w,
                                        const uint8_t *header,
                                        const uint8_t *target_be,
                                        const char *job_id,
                                        const char *ntime_hex,
                                        const char *extranonce2_hex,
                                        int clean_job) {
    pthread_mutex_lock(&w->lock);
    memcpy(w->header, header, JOB_HEADER_LEN);
    memcpy(w->target_be, target_be, 32);
    bitcoin_midstate_init(&w->midstate, header);
    strncpy(w->job_id, job_id, sizeof(w->job_id) - 1);
    w->job_id[sizeof(w->job_id) - 1] = '\0';
    strncpy(w->ntime_hex, ntime_hex, sizeof(w->ntime_hex) - 1);
    w->ntime_hex[sizeof(w->ntime_hex) - 1] = '\0';
    strncpy(w->extranonce2_hex, extranonce2_hex, sizeof(w->extranonce2_hex) - 1);
    w->extranonce2_hex[sizeof(w->extranonce2_hex) - 1] = '\0';
    if (clean_job) {
        atomic_store(&w->clean_job_flag, 1);
    }
    pthread_mutex_unlock(&w->lock);
    atomic_fetch_add(&w->version, 1);
}

static unsigned shared_stratum_work_load(shared_stratum_work_t *w,
                                         bitcoin_midstate_t *out_midstate,
                                         uint8_t *out_target_be,
                                         char *out_job_id,
                                         char *out_ntime_hex,
                                         char *out_extranonce2_hex) {
    unsigned v = atomic_load(&w->version);
    pthread_mutex_lock(&w->lock);
    memcpy(out_midstate, &w->midstate, sizeof(bitcoin_midstate_t));
    memcpy(out_target_be, w->target_be, 32);
    strcpy(out_job_id, w->job_id);
    strcpy(out_ntime_hex, w->ntime_hex);
    strcpy(out_extranonce2_hex, w->extranonce2_hex);
    pthread_mutex_unlock(&w->lock);
    return v;
}

/* Share Queue */
typedef struct {
    uint32_t nonce;
    char job_id[64];
    char ntime_hex[9];
    char extranonce2_hex[32];
    unsigned job_version;
} stratum_share_t;

typedef struct {
    pthread_mutex_t lock;
    stratum_share_t items[SHARE_QUEUE_CAP];
    int head, tail, count;
    atomic_ulong found_total;
    atomic_ulong dropped_total;
} stratum_share_queue_t;

static void stratum_share_queue_init(stratum_share_queue_t *q) {
    pthread_mutex_init(&q->lock, NULL);
    q->head = q->tail = q->count = 0;
    atomic_init(&q->found_total, 0);
    atomic_init(&q->dropped_total, 0);
}

static void stratum_share_queue_destroy(stratum_share_queue_t *q) {
    pthread_mutex_destroy(&q->lock);
}

static void stratum_share_queue_push(stratum_share_queue_t *q, uint32_t nonce,
                                     const char *job_id, const char *ntime_hex,
                                     const char *extranonce2_hex, unsigned jv) {
    atomic_fetch_add(&q->found_total, 1);
    pthread_mutex_lock(&q->lock);
    if (q->count < SHARE_QUEUE_CAP) {
        q->items[q->tail].nonce = nonce;
        snprintf(q->items[q->tail].job_id, sizeof(q->items[q->tail].job_id), "%s", job_id);
        snprintf(q->items[q->tail].ntime_hex, sizeof(q->items[q->tail].ntime_hex), "%s", ntime_hex);
        snprintf(q->items[q->tail].extranonce2_hex, sizeof(q->items[q->tail].extranonce2_hex), "%s", extranonce2_hex);
        q->items[q->tail].job_version = jv;
        q->tail = (q->tail + 1) % SHARE_QUEUE_CAP;
        q->count++;
        pthread_mutex_unlock(&q->lock);
    } else {
        pthread_mutex_unlock(&q->lock);
        atomic_fetch_add(&q->dropped_total, 1);
    }
}

static int stratum_share_queue_pop(stratum_share_queue_t *q, stratum_share_t *out) {
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

/* URL Parser: extracts host, port from stratum+tcp://host:port or host:port */
static int parse_stratum_url(const char *url, char *host, int max_host_len, int *port) {
    *port = 3333; /* default Stratum port */
    const char *p = url;

    if (strncmp(p, "stratum+tcp://", 14) == 0) p += 14;
    else if (strncmp(p, "stratum://", 10) == 0) p += 10;
    else if (strncmp(p, "tcp://", 6) == 0) p += 6;

    const char *colon = strchr(p, ':');
    if (colon) {
        size_t hlen = (size_t)(colon - p);
        if (hlen >= (size_t)max_host_len) hlen = (size_t)max_host_len - 1;
        memcpy(host, p, hlen);
        host[hlen] = '\0';
        *port = atoi(colon + 1);
        if (*port <= 0 || *port > 65535) *port = 3333;
    } else {
        snprintf(host, (size_t)max_host_len, "%s", p);
    }
    return 0;
}

/* Connect TCP socket to Stratum pool host & port */
static int stratum_connect(const char *host, int port) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints, *res = NULL, *rp = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        return -1;
    }

    int fd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;

        int opt = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));

        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return fd;
}

/* JSON string helper */
static const char *json_skip_whitespace(const char *p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p;
}

/*
 * Parse extranonce1 and extranonce2_size out of a mining.subscribe response.
 *
 * De-facto Stratum V1 layout (Braiins spec; cf. ckpool generator.c
 * parse_subscribe): result[0] = subscription details (an arbitrarily nested
 * array of arrays), result[1] = "<extranonce1_hex>" string,
 * result[2] = extranonce2_size integer:
 *
 *   "result": [ [["mining.set_difficulty",..],["mining.notify",..]],
 *               "01020304050607ff", 4 ]
 *
 * The pre-fix implementation searched for the first ']' after "result" and
 * grabbed the following quoted string, which yielded the literal method name
 * "mining.notify" (13 chars, odd length -> stratum_build_coinbase parity
 * failure -> zero jobs ever published) or the trailing "error" key instead.
 * Here element [0] is skipped by tracking bracket depth from the start of the
 * result array; string literals are consumed atomically (escape-aware) so
 * quotes/brackets inside them cannot skew the depth count.
 *
 * Returns 0 on success: extranonce1 receives the NUL-terminated validated hex
 * string and *extranonce2_size is updated when a sane value (1..8) follows.
 * Returns -1 on any malformed response, leaving caller defaults untouched.
 */
static int stratum_parse_subscribe(const char *resp_line,
                                   char *extranonce1, size_t en1_cap,
                                   int *extranonce2_size) {
    const char *res_pos = strstr(resp_line, "\"result\":");
    if (!res_pos) res_pos = strstr(resp_line, "\"result\" :");
    if (!res_pos) return -1;

    const char *arr = strchr(res_pos, '[');
    if (!arr) return -1;

    /* Advance to result element [1]: stop at the first depth-1 comma that
     * follows element [0]; bail out if the result array closes first. */
    const char *p = arr;
    int depth = 0;
    int found_elem1 = 0;
    while (*p) {
        char c = *p;
        if (c == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) p++;
                p++;
            }
            if (!*p) break;
        } else if (c == '[' || c == '{') {
            depth++;
        } else if (c == ']' || c == '}') {
            depth--;
            if (depth <= 0) break;
        } else if (c == ',' && depth == 1) {
            found_elem1 = 1;
            break;
        }
        p++;
    }
    if (!found_elem1) return -1;

    /* Element [1] must be a non-empty hexadecimal string: extranonce1 */
    const char *q1 = strchr(p, '"');
    if (!q1) return -1;
    q1++;
    const char *q2 = strchr(q1, '"');
    if (!q2 || q2 == q1) return -1;
    size_t n = (size_t)(q2 - q1);
    if (n >= en1_cap) return -1;
    for (size_t i = 0; i < n; i++) {
        char hc = q1[i];
        int ok = ((hc >= '0' && hc <= '9') ||
                  (hc >= 'a' && hc <= 'f') ||
                  (hc >= 'A' && hc <= 'F'));
        if (!ok) return -1;
    }
    memcpy(extranonce1, q1, n);
    extranonce1[n] = '\0';

    /* Element [2], if present, is the extranonce2 size in bytes (1..8) */
    const char *comma = strchr(q2, ',');
    if (comma) {
        char *end = NULL;
        long v = strtol(comma + 1, &end, 10);
        if (end != comma + 1 && v >= 1 && v <= 8)
            *extranonce2_size = (int)v;
    }
    return 0;
}

/* Stratum Worker Thread Argument */
typedef struct {
    int cpu_id;
    int thread_idx;
    int total_threads;
    atomic_int *stop_flag;
    shared_stratum_work_t *shared_work;
    stratum_share_queue_t *share_queue;
    atomic_uint64_t *total_hashes;
} stratum_worker_arg_t;

#define HASH_CHECK_INTERVAL 8192u

static void *stratum_hash_worker(void *arg) {
    stratum_worker_arg_t *targ = (stratum_worker_arg_t *)arg;

    if (targ->cpu_id >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(targ->cpu_id, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }

    bitcoin_midstate_t local_ms;
    uint8_t local_target[32];
    char local_job_id[64] = {0};
    char local_ntime[9] = {0};
    char local_en2[32] = {0};
    unsigned local_version = 0;

    uint32_t nonce = (uint32_t)targ->thread_idx;
    uint32_t stride = (uint32_t)targ->total_threads;
    uint8_t raw_hash[32];
    uint8_t display_hash[32];

    while (!atomic_load(targ->stop_flag) && !g_miner_stop) {
        /* Wait for first job */
        if (local_version == 0 || atomic_load(&targ->shared_work->version) != local_version) {
            local_version = shared_stratum_work_load(targ->shared_work, &local_ms, local_target,
                                                    local_job_id, local_ntime, local_en2);
            if (local_version == 0 || local_job_id[0] == '\0') {
                usleep(5000);
                continue;
            }
            nonce = (uint32_t)targ->thread_idx;
        }

        /* Tight inner hashing loop */
        for (uint32_t iter = 0; iter < HASH_CHECK_INTERVAL; iter++) {
            bitcoin_hash_nonce_opt(&local_ms, nonce, raw_hash);

            /* Reverse raw hash to display hash (big-endian 256-bit value) */
            reverse_bytes(display_hash, raw_hash, 32);

            /* Compare with pool share target */
            if (memcmp(display_hash, local_target, 32) <= 0) {
                stratum_share_queue_push(targ->share_queue, nonce, local_job_id,
                                        local_ntime, local_en2, local_version);
            }

            nonce += stride;
        }

        atomic_fetch_add_explicit(targ->total_hashes, HASH_CHECK_INTERVAL, memory_order_relaxed);

        /* Check for clean job or updated work */
        if (atomic_load(&targ->shared_work->version) != local_version) {
            local_version = shared_stratum_work_load(targ->shared_work, &local_ms, local_target,
                                                    local_job_id, local_ntime, local_en2);
            nonce = (uint32_t)targ->thread_idx;
        }
    }

    return NULL;
}

/* Stratum Control Thread Argument */
typedef struct {
    int cpu_id;
    char pool_url[256];
    char user[256];
    char pass[256];
    atomic_int *stop_flag;
    shared_stratum_work_t *shared_work;
    stratum_share_queue_t *share_queue;
    atomic_uint64_t *total_hashes;
    atomic_ulong shares_accepted;
    atomic_ulong shares_rejected;
    atomic_ulong shares_stale;
    atomic_ulong reconnect_count;
    uint32_t first_submit_id;
    uint32_t auth_msg_id;
} stratum_control_arg_t;

/* Process a complete line from Stratum server */
static void process_stratum_line(const char *line, stratum_control_arg_t *carg,
                                char *extranonce1, int *extranonce2_size,
                                uint64_t *extranonce2_val, double *current_diff,
                                uint8_t *current_target) {
    /* Parse response ID if present */
    uint32_t resp_id = 0;
    const char *id_ptr = strstr(line, "\"id\":");
    if (!id_ptr) id_ptr = strstr(line, "\"id\" :");
    if (id_ptr) {
        const char *colon = strchr(id_ptr, ':');
        if (colon) {
            resp_id = (uint32_t)strtoul(colon + 1, NULL, 10);
        }
    }

    /* Check if response is for mining.authorize (do not count as share!) */
    if (resp_id > 0 && resp_id == carg->auth_msg_id) {
        if (strstr(line, "\"result\":true") || strstr(line, "\"result\": true")) {
            printf("Stratum: Authorized successfully as %s\n", carg->user);
            fflush(stdout);
        } else {
            printf("Stratum: Authorization failed for %s\n", carg->user);
            fflush(stdout);
        }
        return;
    }

    /* 1. Check for mining.set_difficulty */
    if (strstr(line, "\"mining.set_difficulty\"") || strstr(line, "mining.set_difficulty")) {
        const char *p = strstr(line, "\"params\":");
        if (!p) p = strstr(line, "\"params\" :");
        if (p) {
            const char *arr = strchr(p, '[');
            if (arr) {
                double diff = atof(arr + 1);
                if (diff > 0.0) {
                    *current_diff = diff;
                    diff_to_target(diff, current_target);
                }
            }
        }
        return;
    }

    /* 2. Check for mining.notify */
    if (strstr(line, "\"mining.notify\"") || strstr(line, "mining.notify")) {
        const char *p = strstr(line, "\"params\":");
        if (!p) p = strstr(line, "\"params\" :");
        if (!p) return;

        const char *arr = strchr(p, '[');
        if (!arr) return;

        /* Parse params array: [job_id, prevhash, coinb1, coinb2, merkle_branch, version, nbits, ntime, clean_jobs] */
        char job_id[64] = {0};
        char prevhash[65] = {0};
        char coinb1[4096] = {0};
        char coinb2[4096] = {0};
        char version[16] = {0};
        char nbits[16] = {0};
        char ntime[16] = {0};
        int clean_jobs = 0;
        char branches[MAX_MERKLE_BRANCHES][65];
        int branch_count = 0;

        const char *cur = arr + 1;
        int param_idx = 0;

        while (*cur && *cur != ']' && param_idx < 9) {
            cur = json_skip_whitespace(cur);
            if (*cur == ',') { cur++; continue; }

            if (param_idx == 4 && *cur == '[') {
                /* Merkle branch array */
                cur++;
                while (*cur && *cur != ']' && branch_count < MAX_MERKLE_BRANCHES) {
                    cur = json_skip_whitespace(cur);
                    if (*cur == ',') { cur++; continue; }
                    if (*cur == '\"') {
                        cur++;
                        const char *q = strchr(cur, '\"');
                        if (q) {
                            size_t blen = (size_t)(q - cur);
                            if (blen == 64) {
                                memcpy(branches[branch_count], cur, 64);
                                branches[branch_count][64] = '\0';
                                branch_count++;
                            }
                            cur = q + 1;
                        } else break;
                    } else cur++;
                }
                if (*cur == ']') cur++;
                param_idx++;
                continue;
            }

            if (*cur == '\"') {
                cur++;
                const char *q = strchr(cur, '\"');
                if (q) {
                    size_t slen = (size_t)(q - cur);
                    if (param_idx == 0) { size_t l = slen < 63 ? slen : 63; memcpy(job_id, cur, l); job_id[l] = '\0'; }
                    else if (param_idx == 1) { size_t l = slen < 64 ? slen : 64; memcpy(prevhash, cur, l); prevhash[l] = '\0'; }
                    else if (param_idx == 2) { size_t l = slen < 4095 ? slen : 4095; memcpy(coinb1, cur, l); coinb1[l] = '\0'; }
                    else if (param_idx == 3) { size_t l = slen < 4095 ? slen : 4095; memcpy(coinb2, cur, l); coinb2[l] = '\0'; }
                    else if (param_idx == 5) { size_t l = slen < 15 ? slen : 15; memcpy(version, cur, l); version[l] = '\0'; }
                    else if (param_idx == 6) { size_t l = slen < 15 ? slen : 15; memcpy(nbits, cur, l); nbits[l] = '\0'; }
                    else if (param_idx == 7) { size_t l = slen < 15 ? slen : 15; memcpy(ntime, cur, l); ntime[l] = '\0'; }
                    cur = q + 1;
                } else break;
            } else if (param_idx == 8) {
                if (strncmp(cur, "true", 4) == 0) clean_jobs = 1;
                else clean_jobs = 0;
                while (*cur && *cur != ']' && *cur != ',') cur++;
            } else {
                while (*cur && *cur != ']' && *cur != ',') cur++;
            }
            param_idx++;
        }

        if (job_id[0] && prevhash[0] && coinb1[0] && coinb2[0]) {
            /* Format extranonce2 */
            char en2_hex[32] = {0};
            int en2_hex_len = (*extranonce2_size > 0) ? (*extranonce2_size * 2) : 8;
            if (en2_hex_len > 16) en2_hex_len = 16;
            snprintf(en2_hex, sizeof(en2_hex), "%0*llx", en2_hex_len, (unsigned long long)(*extranonce2_val)++);

            /* Build coinbase hash */
            uint8_t cb_hash[32];
            if (stratum_build_coinbase(coinb1, extranonce1, en2_hex, coinb2, cb_hash) == 0) {
                /* Build merkle root */
                uint8_t mr[32];
                stratum_build_merkle_root(cb_hash, branch_count, branches, mr);

                /* Build header */
                uint32_t ver_val = (uint32_t)strtoul(version, NULL, 16);
                uint32_t nbits_val = (uint32_t)strtoul(nbits, NULL, 16);
                uint32_t ntime_val = (uint32_t)strtoul(ntime, NULL, 16);

                uint8_t full_hdr[80];
                stratum_build_header(full_hdr, ver_val, prevhash, mr, ntime_val, nbits_val, 0);

                /* Publish new work to all workers */
                shared_stratum_work_publish(carg->shared_work, full_hdr, current_target,
                                            job_id, ntime, en2_hex, clean_jobs);
            }
        }
        return;
    }

    /* 3. Check for share submission response (only for IDs >= first_submit_id) */
    if (resp_id > 0 && carg->first_submit_id > 0 && resp_id >= carg->first_submit_id) {
        if (strstr(line, "\"result\":true") || strstr(line, "\"result\": true")) {
            atomic_fetch_add(&carg->shares_accepted, 1);
            printf("[Share] Accepted by pool! (Total Accepted: %lu)\n", atomic_load(&carg->shares_accepted));
            fflush(stdout);
        } else if (strstr(line, "\"error\":") || strstr(line, "\"error\" :")) {
            if (!strstr(line, "\"error\":null") && !strstr(line, "\"error\": null")) {
                atomic_fetch_add(&carg->shares_rejected, 1);
                if (strstr(line, "stale") || strstr(line, "Stale") || strstr(line, "job not found")) {
                    atomic_fetch_add(&carg->shares_stale, 1);
                }
                printf("[Share] Rejected by pool! (Total Rejected: %lu)\n", atomic_load(&carg->shares_rejected));
                fflush(stdout);
            }
        }
    }
}

static void *stratum_control_worker(void *arg) {
    stratum_control_arg_t *carg = (stratum_control_arg_t *)arg;

    if (carg->cpu_id >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(carg->cpu_id, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }

    char host[256];
    int port = 3333;
    parse_stratum_url(carg->pool_url, host, sizeof(host), &port);

    char extranonce1[64] = "00000000";
    int extranonce2_size = 4;
    uint64_t extranonce2_val = 1;
    double current_diff = 1.0;
    uint8_t current_target[32];
    diff_to_target(current_diff, current_target);

    uint32_t msg_id = 1;
    int sock_fd = -1;

    char recv_buf[16384];
    int recv_len = 0;

    struct timespec last_telemetry, now;
    clock_gettime(CLOCK_MONOTONIC, &last_telemetry);
    uint64_t last_hash_count = 0;

    while (!atomic_load(carg->stop_flag) && !g_miner_stop) {
        /* Connect if disconnected */
        if (sock_fd < 0) {
            printf("Pool: %s\n", carg->pool_url);
            printf("Status: CONNECTING (%s:%d)...\n", host, port);
            sock_fd = stratum_connect(host, port);
            if (sock_fd < 0) {
                printf("Status: CONNECTION_FAILED (retrying in 3s)\n\n");
                atomic_fetch_add(&carg->reconnect_count, 1);
                for (int i = 0; i < 30 && !atomic_load(carg->stop_flag) && !g_miner_stop; i++) {
                    usleep(100000);
                }
                continue;
            }

            printf("Status: CONNECTED\n");
            recv_len = 0;

            /* Send mining.subscribe */
            uint32_t sub_req_id = msg_id++;
            char sub_req[256];
            snprintf(sub_req, sizeof(sub_req),
                     "{\"id\": %u, \"method\": \"mining.subscribe\", \"params\": [\"bitcoin_sha256d_s905x/1.0\", null]}\n",
                     sub_req_id);
            send(sock_fd, sub_req, strlen(sub_req), 0);

            /* Wait for subscribe response */
            char resp_line[4096];
            int got_sub = 0;
            while (!atomic_load(carg->stop_flag) && !g_miner_stop && !got_sub) {
                ssize_t n = recv(sock_fd, recv_buf + recv_len, sizeof(recv_buf) - 1 - (size_t)recv_len, 0);
                if (n <= 0) break;
                recv_len += (int)n;
                recv_buf[recv_len] = '\0';

                char *nl = strchr(recv_buf, '\n');
                if (nl) {
                    *nl = '\0';
                    strncpy(resp_line, recv_buf, sizeof(resp_line) - 1);
                    resp_line[sizeof(resp_line) - 1] = '\0';

                    int remaining = recv_len - (int)(nl - recv_buf + 1);
                    if (remaining > 0) {
                        memmove(recv_buf, nl + 1, (size_t)remaining);
                    }
                    recv_len = remaining;

                    /* Parse extranonce1 and extranonce2_size from subscribe response */
                    if (stratum_parse_subscribe(resp_line, extranonce1,
                                                sizeof(extranonce1),
                                                &extranonce2_size) == 0) {
                        printf("Stratum: Subscribed (extranonce1=%s, extranonce2_size=%d)\n",
                               extranonce1, extranonce2_size);
                    } else {
                        printf("Stratum: WARNING - malformed mining.subscribe response; "
                               "keeping default extranonce values\n");
                    }
                    fflush(stdout);
                    got_sub = 1;
                }
            }

            if (!got_sub) {
                close(sock_fd);
                sock_fd = -1;
                continue;
            }

            /* Send mining.authorize */
            uint32_t auth_id = msg_id++;
            carg->auth_msg_id = auth_id;
            carg->first_submit_id = msg_id; /* All future IDs are share submits */

            char auth_req[1024];
            snprintf(auth_req, sizeof(auth_req),
                     "{\"id\": %u, \"method\": \"mining.authorize\", \"params\": [\"%s\", \"%s\"]}\n",
                     auth_id, carg->user, carg->pass);
            send(sock_fd, auth_req, strlen(auth_req), 0);
        }

        /* Main Network & Share Submission Loop */
        struct pollfd pfd = { sock_fd, POLLIN, 0 };
        int pr = poll(&pfd, 1, 50);

        if (pr > 0 && (pfd.revents & (POLLIN | POLLERR | POLLHUP))) {
            ssize_t n = recv(sock_fd, recv_buf + recv_len, sizeof(recv_buf) - 1 - (size_t)recv_len, 0);
            if (n <= 0) {
                /* Connection lost */
                close(sock_fd);
                sock_fd = -1;
                atomic_fetch_add(&carg->reconnect_count, 1);
                continue;
            }
            recv_len += (int)n;
            recv_buf[recv_len] = '\0';

            /* Extract and process all complete lines */
            char *start = recv_buf;
            char *nl = NULL;
            while ((nl = strchr(start, '\n')) != NULL) {
                *nl = '\0';
                process_stratum_line(start, carg, extranonce1, &extranonce2_size,
                                     &extranonce2_val, &current_diff, current_target);
                start = nl + 1;
            }
            int remaining = recv_len - (int)(start - recv_buf);
            if (remaining > 0 && start != recv_buf) {
                memmove(recv_buf, start, (size_t)remaining);
            }
            recv_len = remaining;
        }

        /* Drain and submit found shares from worker threads */
        stratum_share_t share;
        while (sock_fd >= 0 && stratum_share_queue_pop(carg->share_queue, &share)) {
            uint32_t share_msg_id = msg_id++;
            char sub_msg[512];
            snprintf(sub_msg, sizeof(sub_msg),
                     "{\"id\": %u, \"method\": \"mining.submit\", \"params\": [\"%s\", \"%s\", \"%s\", \"%s\", \"%08x\"]}\n",
                     share_msg_id, carg->user, share.job_id, share.extranonce2_hex, share.ntime_hex, share.nonce);
            send(sock_fd, sub_msg, strlen(sub_msg), 0);
            printf("[Share] Submitting nonce 0x%08x for job %s (sub_id: %u)\n", share.nonce, share.job_id, share_msg_id);
            fflush(stdout);
        }

        /* Telemetry reporting every ~2.5 seconds */
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = timespec_diff_ns(&now, &last_telemetry) / 1000000L;
        if (elapsed_ms >= 2500) {
            uint64_t current_hashes = atomic_load(carg->total_hashes);
            uint64_t hash_delta = (current_hashes >= last_hash_count) ? (current_hashes - last_hash_count) : 0;
            double mhs = ((double)hash_delta / ((double)elapsed_ms / 1000.0)) / 1000000.0;

            printf("Pool: %s\n", carg->pool_url);
            printf("Status: %s\n", (sock_fd >= 0) ? "CONNECTED" : "DISCONNECTED");
            printf("Hash Rate: %.2f MH/s\n", mhs);
            printf("Difficulty: %.4f\n", current_diff);
            printf("Shares Found: %lu\n", atomic_load(&carg->share_queue->found_total));
            printf("Shares Accepted: %lu\n", atomic_load(&carg->shares_accepted));
            printf("Shares Rejected: %lu\n", atomic_load(&carg->shares_rejected));
            printf("Stale Shares: %lu\n", atomic_load(&carg->shares_stale));
            printf("Reconnects: %lu\n\n", atomic_load(&carg->reconnect_count));
            fflush(stdout);

            last_telemetry = now;
            last_hash_count = current_hashes;
        }
    }

    if (sock_fd >= 0) close(sock_fd);
    return NULL;
}

/* Top-level Stratum mining session coordinator */
static void run_stratum_miner(const char *pool_url, const char *user, const char *pass,
                              int num_threads, int core_offset, int pin_enabled,
                              int control_core) {
    printf("======================================================================\n");
    printf("Starting Amlogic S905X Stratum V1 Bitcoin Miner  [backend: %s]\n", backend_name);
    printf("======================================================================\n");
    printf("Pool URL          : %s\n", pool_url);
    printf("Worker User       : %s\n", user);
    printf("Hashing Threads   : %d\n", num_threads);
    printf("Control Core      : %d\n", control_core);
    printf("Core Pinning      : %s (offset: %d)\n", pin_enabled ? "ENABLED" : "DISABLED", core_offset);
    printf("======================================================================\n\n");

    signal(SIGINT, handle_miner_sig);
    signal(SIGTERM, handle_miner_sig);

    atomic_int stop_flag;
    atomic_init(&stop_flag, 0);

    shared_stratum_work_t shared_work;
    shared_stratum_work_init(&shared_work);

    stratum_share_queue_t share_queue;
    stratum_share_queue_init(&share_queue);

    atomic_uint64_t total_hashes;
    atomic_init(&total_hashes, 0);

    /* Spawn Hashing Worker Threads (pinned to cores 0..2) */
    pthread_t *worker_threads = (pthread_t *)malloc((size_t)num_threads * sizeof(pthread_t));
    stratum_worker_arg_t *worker_args = (stratum_worker_arg_t *)malloc((size_t)num_threads * sizeof(stratum_worker_arg_t));

    for (int i = 0; i < num_threads; i++) {
        worker_args[i].cpu_id = pin_enabled ? (core_offset + i) : -1;
        worker_args[i].thread_idx = i;
        worker_args[i].total_threads = num_threads;
        worker_args[i].stop_flag = &stop_flag;
        worker_args[i].shared_work = &shared_work;
        worker_args[i].share_queue = &share_queue;
        worker_args[i].total_hashes = &total_hashes;
        pthread_create(&worker_threads[i], NULL, stratum_hash_worker, &worker_args[i]);
    }

    /* Spawn Control Thread (pinned to core 3) */
    pthread_t control_thread;
    stratum_control_arg_t control_arg;
    control_arg.cpu_id = pin_enabled ? control_core : -1;
    strncpy(control_arg.pool_url, pool_url, sizeof(control_arg.pool_url) - 1);
    control_arg.pool_url[sizeof(control_arg.pool_url) - 1] = '\0';
    strncpy(control_arg.user, user, sizeof(control_arg.user) - 1);
    control_arg.user[sizeof(control_arg.user) - 1] = '\0';
    strncpy(control_arg.pass, pass, sizeof(control_arg.pass) - 1);
    control_arg.pass[sizeof(control_arg.pass) - 1] = '\0';
    control_arg.stop_flag = &stop_flag;
    control_arg.shared_work = &shared_work;
    control_arg.share_queue = &share_queue;
    control_arg.total_hashes = &total_hashes;
    atomic_init(&control_arg.shares_accepted, 0);
    atomic_init(&control_arg.shares_rejected, 0);
    atomic_init(&control_arg.shares_stale, 0);
    atomic_init(&control_arg.reconnect_count, 0);

    pthread_create(&control_thread, NULL, stratum_control_worker, &control_arg);

    /* Run control thread until stopped */
    pthread_join(control_thread, NULL);

    atomic_store(&stop_flag, 1);
    for (int i = 0; i < num_threads; i++) {
        pthread_join(worker_threads[i], NULL);
    }

    shared_stratum_work_destroy(&shared_work);
    stratum_share_queue_destroy(&share_queue);
    free(worker_threads);
    free(worker_args);
}

/* Benchmark worker thread argument */
typedef struct {
    uint64_t start_nonce;
    uint64_t count;
    int      cpu_id;
    uint8_t  final_hash[32];
} bench_thread_arg_t;

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

    const char *b125552_hex = "0100000081cd02ab7e569e8bcd9317e2fe99f2de44d49ab2b8851ba4a308000000000000e320b6c2fffc8d750423db8b1eb942ae710e951ed797f7affc8892b0f1fc122bc7f5d74df2b9441a42a14695";
    uint8_t header_buf[80];
    hex_to_bytes(b125552_hex, header_buf, 80);

    /* Precompute midstate ONCE for the benchmark range */
    bitcoin_midstate_t ms;
    bitcoin_midstate_init(&ms, header_buf);

    for (uint64_t i = 0; i < targ->count; i++) {
        uint32_t nonce = (uint32_t)(targ->start_nonce + i);
        bitcoin_hash_nonce_opt(&ms, nonce, hash);
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
                           int spin_core, int spin_duty, spin_mode_t spin_mode) {
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
    printf("Target Iterations : %lu hashes\n", (unsigned long)iterations);

    bench_thread_arg_t *args = calloc((size_t)num_threads, sizeof(bench_thread_arg_t));
    pthread_t *threads = calloc((size_t)num_threads, sizeof(pthread_t));
    if (!args || !threads) {
        fprintf(stderr, "Error: allocation failed for %d threads.\n", num_threads);
        free(args);
        free(threads);
        return;
    }

    uint64_t base = iterations / (uint64_t)num_threads;
    uint64_t remainder = iterations % (uint64_t)num_threads;
    uint64_t cursor = 0;
    for (int t = 0; t < num_threads; t++) {
        args[t].start_nonce = cursor;
        args[t].count = base + ((uint64_t)t < remainder ? 1 : 0);
        args[t].cpu_id = pin_enabled ? (int)((core_offset + t) % total_cores) : -1;
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
    printf("  Sample Hash     : %s (from last worker's final nonce)\n", final_hex);
    printf("======================================================================\n");

    free(args);
    free(threads);
}

/* ============================================================================
 * SECTION 10: CLI
 * ============================================================================ */

static void print_usage(const char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Mining Options:\n");
    printf("  -M, --mine              Run live Stratum V1 Bitcoin miner.\n");
    printf("  -P, --pool <URL>        Stratum pool URL (e.g. stratum+tcp://solo.ckpool.org:3333).\n");
    printf("  -u, -U, --user <USER>   Stratum worker username / BTC address.\n");
    printf("  -p, --pass, --password  Stratum worker password (default: x).\n");
    printf("  -j, --threads <N>       Hashing worker threads pinned to cores (default: 3 on 4-core).\n");
    printf("  -c, --control-core <N>  Control/network core (default: 3 on 4-core).\n");
    printf("  -o, --offset <N>        First CPU core to pin hash threads to (default: 0).\n");
    printf("  --no-pin                Disable CPU pinning.\n");
    printf("Diagnostic & Benchmark Options:\n");
    printf("  -t, --test              Run known Bitcoin block header correctness tests.\n");
    printf("  -b, --benchmark         Run proof-of-work nonce hashing benchmark.\n");
    printf("  -n, --iterations <N>    Number of benchmark iterations (default: 5000000).\n");
    printf("  -x, --spin-core <N>     Diagnostic: run a dummy busy-spin thread pinned to core N.\n");
    printf("  -y, --spin-duty <P>     Duty cycle percent (0-100) for the spin thread. Default: 100.\n");
    printf("  -m, --spin-mode <M>     Workload type for the spin thread: alu, loadstore, membw, neon, sha.\n");
    printf("  -s, --sw-only           Force the portable software backend (disable HW crypto).\n");
    printf("  -h, --help              Display this help message.\n");
}

int main(int argc, char *argv[]) {
    int do_mine = 0;
    char pool_url[256] = "stratum+tcp://solo.ckpool.org:3333";
    char pool_user[256] = "bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh.s905x";
    char pool_pass[256] = "x";
    int do_test = 0;
    int do_benchmark = 0;
    int force_sw = 0;
    uint64_t iterations = 5000000ULL;
    int num_threads = 0;
    int threads_specified = 0;
    int core_offset = 0;
    int pin_enabled = 1;
    int spin_core = -1;
    int spin_duty = 100;
    spin_mode_t spin_mode = SPIN_MODE_ALU;
    int control_core = -1;
    int control_specified = 0;
    int spin_specified = 0;

    if (argc < 2) {
        print_usage(argv[0]);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-M") == 0 || strcmp(argv[i], "--mine") == 0) {
            do_mine = 1;
        } else if (strcmp(argv[i], "-P") == 0 || strcmp(argv[i], "--pool") == 0) {
            if (i + 1 < argc) {
                strncpy(pool_url, argv[++i], sizeof(pool_url) - 1);
                pool_url[sizeof(pool_url) - 1] = '\0';
                do_mine = 1;
            } else {
                fprintf(stderr, "Error: Option %s requires a URL argument.\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "-U") == 0 || strcmp(argv[i], "--user") == 0) {
            if (i + 1 < argc) {
                strncpy(pool_user, argv[++i], sizeof(pool_user) - 1);
                pool_user[sizeof(pool_user) - 1] = '\0';
            } else {
                fprintf(stderr, "Error: Option %s requires a username argument.\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "--password") == 0 || strcmp(argv[i], "--pass") == 0) {
            if (i + 1 < argc) {
                strncpy(pool_pass, argv[++i], sizeof(pool_pass) - 1);
                pool_pass[sizeof(pool_pass) - 1] = '\0';
            } else {
                fprintf(stderr, "Error: Option %s requires a password argument.\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--test") == 0) {
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
                threads_specified = 1;
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
        } else if (strcmp(argv[i], "--no-pin") == 0) {
            pin_enabled = 0;
        } else if (strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "--spin-core") == 0) {
            if (i + 1 < argc) {
                spin_core = atoi(argv[++i]);
                spin_specified = 1;
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
                control_specified = 1;
                if (control_core < 0) {
                    fprintf(stderr, "Error: --control-core must be >= 0.\n");
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

    /* Set defaults based on detected core count if not explicitly specified */
    long total_cores = detect_core_count();
    if (!threads_specified) {
        if (total_cores == 4) {
            /* 4-core S905X optimal layout: 3 dedicated SHA-256d hashing worker cores */
            num_threads = 3;
        } else if (total_cores > 1) {
            num_threads = (int)total_cores - 1;
        } else {
            num_threads = 1;
        }
    }

    if (!control_specified && !spin_specified && !threads_specified) {
        if (total_cores == 4) {
            /* 4-core S905X optimal layout: 4th core (core 3) dedicated to Stratum & control plane */
            control_core = 3;
        } else if (total_cores > 1) {
            control_core = (int)total_cores - 1;
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
        run_benchmark(iterations, num_threads, core_offset, pin_enabled, spin_core, spin_duty, spin_mode);
    }

    if (do_mine) {
        run_stratum_miner(pool_url, pool_user, pool_pass, num_threads, core_offset, pin_enabled, control_core);
    }

    return test_result;
}
