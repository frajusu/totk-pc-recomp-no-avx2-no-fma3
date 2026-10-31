// Integer fused multiply-add for hosts without FMA. Align an exact product with sticky bits and
// round once in the current MXCSR mode. The surrounding ARM helpers restore NaN payloads.
#pragma once
#include <stdint.h>
#include <string.h>
#include <xmmintrin.h>
typedef unsigned __int128 SoftWide;
static inline int soft_top(SoftWide x) {
    uint64_t hi = (uint64_t)(x >> 64);
    return hi ? 127 - __builtin_clzll(hi) : 63 - __builtin_clzll((uint64_t)x);
}
static inline SoftWide soft_jam(SoftWide x, int shift) {
    if (shift <= 0) return x;
    if (shift >= 128) return x != 0;
    return (x >> shift) | ((x << (128 - shift)) != 0);
}
static __attribute__((noinline)) uint64_t soft_fma_bits(uint64_t x, uint64_t y, uint64_t z, int frac, int ebits) {
    unsigned csr = _mm_getcsr(), mode = (csr >> 13) & 3;
    uint64_t signbit = 1ull << (frac + ebits), fm = (1ull << frac) - 1;
    int em = (1 << ebits) - 1, bias = em >> 1, emin = 1 - bias;
    int ex = (int)((x >> frac) & em), ey = (int)((y >> frac) & em), ez = (int)((z >> frac) & em);
    uint64_t mx = x & fm, my = y & fm, mz = z & fm;
    int negp = !!((x ^ y) & signbit), negz = !!(z & signbit);
    uint64_t inf = (uint64_t)em << frac, nan = inf | (1ull << (frac - 1));
    if ((ex == em && mx) || (ey == em && my) || (ez == em && mz)) return nan;
    if (csr & (1u << 6)) {
        if (!ex) mx = 0;
        if (!ey) my = 0;
        if (!ez) mz = 0;
    }
    if (ex == em || ey == em) {
        if ((!ex && !mx) || (!ey && !my) || (ez == em && negp != negz)) return nan;
        return inf | (negp ? signbit : 0);
    }
    if (ez == em) return z;
    if (ex) mx |= 1ull << frac;
    if (ey) my |= 1ull << frac;
    if (ez) mz |= 1ull << frac;
    SoftWide product = (SoftWide)mx * my, addend = mz;
    int pe = (ex ? ex - bias : emin) + (ey ? ey - bias : emin) - 2 * frac;
    int ze = (ez ? ez - bias : emin) - frac;
    if (!product && !addend) return (negp == negz ? negp : mode == 1) ? signbit : 0;
    int pt = product ? soft_top(product) : 0, zt = addend ? soft_top(addend) : 0;
    int top = !product ? ze + zt : !addend ? pe + pt : (pe + pt > ze + zt ? pe + pt : ze + zt);
    SoftWide p = product ? soft_jam(product << (120 - pt), top - pe - pt) : 0;
    SoftWide a = addend ? soft_jam(addend << (120 - zt), top - ze - zt) : 0;
    SoftWide sum;
    int neg;
    if (negp == negz) sum = p + a, neg = negp;
    else if (p >= a) sum = p - a, neg = negp;
    else sum = a - p, neg = negz;
    if (!sum) return mode == 1 ? signbit : 0;
    int scale = top - 120, st = soft_top(sum), exponent = scale + st;
    int target = (exponent < emin ? emin : exponent) - frac;
    int shift = target - scale;
    SoftWide q, rem = 0, half = 0;
    if (shift <= 0) q = sum << -shift;
    else if (shift >= 128) q = 0, rem = sum;
    else q = sum >> shift, rem = sum & (((SoftWide)1 << shift) - 1), half = (SoftWide)1 << (shift - 1);
    int increment = mode == 0 ? (half && (rem > half || (rem == half && (q & 1))))
                             : rem && ((mode == 1 && neg) || (mode == 2 && !neg));
    q += increment;
    if (q >= ((SoftWide)1 << (frac + 1))) q >>= 1, target++;
    int encoded = target + frac + bias;
    uint64_t sign = neg ? signbit : 0;
    if (encoded >= em) {
        int to_inf = mode == 0 || (mode == 1 && neg) || (mode == 2 && !neg);
        return sign | (to_inf ? inf : inf - 1);
    }
    if (q < ((SoftWide)1 << frac)) {
        if (csr & (1u << 15)) return sign;
        return sign | (uint64_t)q;
    }
    return sign | ((uint64_t)encoded << frac) | ((uint64_t)q & fm);
}
#define SOFT_OVERLOAD static inline __attribute__((overloadable, always_inline))
SOFT_OVERLOAD float soft_fma(float a, float b, float c) {
    uint32_t x, y, z, out;
    memcpy(&x, &a, 4); memcpy(&y, &b, 4); memcpy(&z, &c, 4);
    out = (uint32_t)soft_fma_bits(x, y, z, 23, 8);
    float r; memcpy(&r, &out, 4); return r;
}
SOFT_OVERLOAD double soft_fma(double a, double b, double c) {
    uint64_t x, y, z, out;
    memcpy(&x, &a, 8); memcpy(&y, &b, 8); memcpy(&z, &c, 8);
    out = soft_fma_bits(x, y, z, 52, 11);
    double r; memcpy(&r, &out, 8); return r;
}
SOFT_OVERLOAD vf32 soft_fma(vf32 a, vf32 b, vf32 c) {
    vf32 r;
    for (int i = 0; i < 4; i++) r[i] = soft_fma(a[i], b[i], c[i]);
    return r;
}
SOFT_OVERLOAD vf64 soft_fma(vf64 a, vf64 b, vf64 c) {
    vf64 r;
    for (int i = 0; i < 2; i++) r[i] = soft_fma(a[i], b[i], c[i]);
    return r;
}
#undef SOFT_OVERLOAD
