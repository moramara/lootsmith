#include "simulation.h"

uint32_t rng_next(uint32_t *s) {
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

int rng_range(uint32_t *s, int lo, int hi) {
    if (hi <= lo) {
        return lo;
    }
    /* 幅は 64 ビットで計算する: hi - lo + 1 は 2^32 に達しうる (INT_MIN..INT_MAX) ので、int / uint32_t
     * ではオーバーフローする。 従来から動いていたすべての範囲で結果は同一 (rng_next() の 1 回の抽選も、剰余も同じ)。 */
    unsigned long long span = (unsigned long long)((long long)hi - (long long)lo) + 1ull;
    return (int)((long long)lo + (long long)((unsigned long long)rng_next(s) % span));
}

uint32_t guild_seed(uint32_t a, uint32_t b) { /* ギルドのストリームを `rng` と無相関にする。0 にはならない */
    uint32_t x = a * 0x9E3779B1u + b * 0x85EBCA6Bu + 0xC2B2AE35u;
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x ? x : 0x9E3779B9u;
}

uint32_t campaign_seed(uint32_t previous, uint32_t clock) {
    return previous * 1664525u + 1013904223u + clock;
}
