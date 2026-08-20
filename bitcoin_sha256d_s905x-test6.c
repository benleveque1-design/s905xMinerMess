/*
 * bitcoin_sha256d_s905x.c
 *
 * Bitcoin SHA-256d (double SHA-256) block-header hashing, adapted from the
 * portable baseline to use the ARMv8 Cryptography Extensions hardware SHA-256
 * instructions found on the Amlogic S905X's Cortex-A53 cores (running under
 * Armbian, aarch64).
 *
 * The S905X's A53 cores implement the optional ARMv8 Crypto Extension
 * (AES/SHA1/SHA2), so instead of the scalar round-by-round software loop,
 * the compression function can be done with four NEON instructions per
 * four rounds: SHA256H, SHA256H2, SHA256SU0, SHA256SU1.
 *
 * This file keeps the original software implementation as a fallback and
 * picks the hardware path at runtime via getauxval(AT_HWCAP) & HWCAP_SHA2,
 * so the same binary still runs correctly (just slower) on a core/kernel
 * combo that doesn't expose the crypto extension.
 *
 * Build (native, on the S905X board itself):
 *   gcc -O3 -march=armv8-a+crypto -pthread -o bitcoin_sha256d_s905x bitcoin_sha256d_s905x.c
 *
 * The per-function __attribute__((target("+crypto"))) below also lets this
 * compile fine with a plain `gcc -O3 -o ...` (no -march needed) on a recent
 * GCC/Clang aarch64 toolchain, since only the hw compress function needs the
 * crypto feature enabled - but passing -march=armv8-a+crypto is recommended
 * so the rest of the file also gets A53-appropriate codegen. -pthread is
 * required for the multi-core benchmark.
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

/* SHA-256 round constants (first 32 bits of fractional parts of cube roots of first 64 primes) */
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
    uint32x4_t STATE0, STATE1, ABEF_SAVE, CDGH_SAVE;
    uint32x4_t MSG0, MSG1, MSG2, MSG3;
    uint32x4_t TMP0, TMP1, TMP2;

    STATE0 = vld1q_u32(&state[0]);
    STATE1 = vld1q_u32(&state[4]);

    ABEF_SAVE = STATE0;
    CDGH_SAVE = STATE1;

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

    vst1q_u32(&state[0], STATE0);
    vst1q_u32(&state[4], STATE1);
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
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
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
 * SECTION 6: SHA-256d (Double SHA-256) Wrapper
 * ============================================================================ */

static void sha256d(const uint8_t *data, size_t len, uint8_t digest[32]) {
    uint8_t intermediate[32];
    sha256(data, len, intermediate);
    sha256(intermediate, 32, digest);
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

        printf("Test 1: Block 125552 Header Hashing\n");
        printf("  Calculated: %s\n", calc_hex);
        printf("  Expected  : %s\n", expected_b125552);

        if (strcmp(calc_hex, expected_b125552) == 0) {
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
 * SECTION 9: Benchmark (multi-threaded, one worker per core)
 * ============================================================================ */

/* Each worker hashes its own disjoint slice of the nonce space, starting
 * from the same block-125552 header template, so cores never touch shared
 * mutable state (each has its own local header_buf/hash) - no locking
 * needed on the hot path. */
typedef struct {
    uint64_t start_nonce;
    uint64_t count;
    int      cpu_id;   /* -1 = no pinning, let the OS scheduler decide */
    uint8_t  final_hash[32];
} bench_thread_arg_t;

static void *bench_worker(void *arg) {
    bench_thread_arg_t *targ = (bench_thread_arg_t *)arg;

    if (targ->cpu_id >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(targ->cpu_id, &cpuset);
        /* Best-effort: if pinning fails (e.g. cpu_id out of range), just
         * fall through and let the thread run wherever the OS puts it. */
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }

    const char *b125552_hex = "0100000081cd02ab7e569e8bcd9317e2fe99f2de44d49ab2b8851ba4a308000000000000e320b6c2fffc8d750423db8b1eb942ae710e951ed797f7affc8892b0f1fc122bc7f5d74df2b9441a42a14695";
    uint8_t header_buf[80];
    hex_to_bytes(b125552_hex, header_buf, 80);

    uint8_t hash[32];
    memset(hash, 0, 32);

    for (uint64_t i = 0; i < targ->count; i++) {
        update_header_nonce_le(header_buf, (uint32_t)(targ->start_nonce + i));
        sha256d(header_buf, 80, hash);
    }

    memcpy(targ->final_hash, hash, 32);
    return NULL;
}

static long detect_core_count(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n >= 1) ? n : 1;
}

/* ----------------------------------------------------------------------
 * Diagnostic: a "busy neighbor" thread that pins to one core and exercises
 * a specific kind of workload alongside the hash workers, to isolate
 * *which* shared resource (if any) is responsible for the contention seen
 * when adding a 4th hash worker:
 *
 *   alu       - integer register arithmetic only, no memory traffic at all
 *   loadstore - repeated load/store against a small (L1-resident) buffer
 *   membw     - streaming read-modify-write across an 8MB buffer, well
 *               past shared L2, to generate real DRAM/interconnect traffic
 *   neon      - SIMD arithmetic in registers only, no memory, no crypto
 *   sha       - SHA256 crypto-extension instructions in registers only,
 *               no memory traffic (isolates the crypto unit specifically)
 *
 * duty_percent controls what fraction of each ~20ms cycle the thread
 * spends actually busy vs. sleeping, for the separate duty-cycle sweep.
 * ---------------------------------------------------------------------- */
#define SPIN_CYCLE_NS 20000000L /* 20 ms period for the duty cycle */

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
    int duty_percent; /* 0-100: 100 = always busy (old behavior), 0 = always idle */
    spin_mode_t mode;
} spin_thread_arg_t;

static inline long timespec_diff_ns(const struct timespec *a, const struct timespec *b) {
    return (a->tv_sec - b->tv_sec) * 1000000000L + (a->tv_nsec - b->tv_nsec);
}

/* volatile sink so the compiler can't prove these register-only loops are
 * dead code and eliminate them */
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

    /* loadstore scratch: small, stays L1-resident */
    enum { LS_WORDS = 64 }; /* 512 bytes */
    volatile uint64_t ls_scratch[LS_WORDS];
    for (int i = 0; i < LS_WORDS; i++) ls_scratch[i] = 0;

    /* membw buffer: bigger than the shared L2 to force real DRAM/bus traffic */
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
                            spin_batch_alu(&counter); /* malloc failed: fall back */
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

static void run_benchmark(uint64_t iterations, int num_threads, int core_offset, int pin_enabled, int spin_core, int spin_duty, spin_mode_t spin_mode) {
    if (num_threads < 1) num_threads = 1;

    long total_cores = detect_core_count();

    printf("======================================================================\n");
    printf("Running Bitcoin SHA-256d Benchmark on Amlogic S905X\n");
    printf("======================================================================\n");
    printf("Backend           : %s\n", backend_name);
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

    /* Split the iteration count into per-thread nonce ranges as evenly as
     * possible; each thread gets a disjoint [start_nonce, start_nonce+count). */
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

    /* Spin up the dummy busy-neighbor thread, if requested, before timing
     * starts, and keep it running for the entire benchmark window so it's
     * a fair "one more busy core" comparison against a real hash worker. */
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

    clock_gettime(CLOCK_MONOTONIC, &start_time);

    for (int t = 0; t < num_threads; t++) {
        if (pthread_create(&threads[t], NULL, bench_worker, &args[t]) != 0) {
            fprintf(stderr, "Error: failed to create worker thread %d.\n", t);
            num_threads = t; /* only join threads actually created */
            break;
        }
    }
    for (int t = 0; t < num_threads; t++) {
        pthread_join(threads[t], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);

    if (spin_started) {
        atomic_store(&spin_stop, 1);
        pthread_join(spin_thread, NULL);
    }

    double elapsed_sec = (double)(end_time.tv_sec - start_time.tv_sec) +
                         (double)(end_time.tv_nsec - start_time.tv_nsec) / 1e9;

    double hashes_per_sec = (elapsed_sec > 0.0) ? ((double)iterations / elapsed_sec) : 0.0;

    /* Display hash of the last worker's final nonce, just as a sanity artifact -
     * not meaningful as a single continuous hash chain across threads. */
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

    return test_result;
}
