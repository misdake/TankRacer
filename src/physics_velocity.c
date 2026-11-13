#include "physics_velocity.h"

/* Spell out x86 wrap and SAR without signed overflow or implementation-defined
 * negative right shift. Shifts here are only 4 and 8. */
static int32_t signed_bits(uint32_t bits)
{
    return bits <= INT32_MAX ? (int32_t)bits : (int32_t)((int64_t)bits - INT64_C(4294967296));
}
static int32_t add32(int32_t a, int32_t b)
{
    return signed_bits((uint32_t)a + (uint32_t)b);
}
static int32_t sub32(int32_t a, int32_t b)
{
    return signed_bits((uint32_t)a - (uint32_t)b);
}
static int32_t mul32(int32_t a, int32_t b)
{
    return signed_bits((uint32_t)a * (uint32_t)b);
}
static int32_t floor_shift(int32_t a, unsigned n)
{
    uint32_t bits = (uint32_t)a;
    return signed_bits(a < 0 ? ~(~bits >> n) : bits >> n);
}

int physics_velocity_large(const int32_t v[3])
{
    /* Match native signed abs/cmp, including its INT_MIN negation overflow. */
    int32_t x = v[0] < 0 ? sub32(0, v[0]) : v[0];
    int32_t z = v[2] < 0 ? sub32(0, v[2]) : v[2];
    return x > 500000 || z > 500000;
}

void physics_velocity_transform(const int32_t src[3], const int32_t m[9],
                                int32_t dst[3], int inverse, int large)
{
    int x_is_large = src[0] > src[2]; /* Signed comparison in 4410E0/4411F0. */
    int32_t x = floor_shift(src[0], large && x_is_large ? 8 : 4);
    int32_t y = floor_shift(src[1], 4);
    int32_t z = floor_shift(src[2], large && !x_is_large ? 8 : 4);
    int32_t result[3];
    int i;
    for (i = 0; i < 3; ++i) {
        int32_t tx = mul32(m[inverse ? i : 3*i], x);
        int32_t ty = mul32(m[inverse ? 3+i : 3*i+1], y);
        int32_t tz = mul32(m[inverse ? 6+i : 3*i+2], z);
        if (large) {
            if (!x_is_large) tx = floor_shift(tx, 4);
            ty = floor_shift(ty, 4);
            if (x_is_large) tz = floor_shift(tz, 4);
        }
        result[i] = floor_shift(add32(add32(tx, tz), ty), large ? 4 : 8);
    }
    for (i = 0; i < 3; ++i) dst[i] = result[i];
}

int32_t physics_longitudinal_transfer(int32_t drive, int32_t body,
                                      int32_t budget, int32_t *applied)
{
    int32_t delta = sub32(drive, body);
    if (delta > budget) {
        *applied = budget;
        return sub32(delta, budget);
    }
    if (delta < -budget) {
        *applied = -budget;
        return add32(delta, budget);
    }
    *applied = delta;
    return 0;
}

int32_t physics_longitudinal_gate(int32_t applied, int active, int32_t airborne_count,
                                  int32_t extra)
{
    int32_t value;
    if (!active) return applied;
    value = mul32(sub32(4, airborne_count), add32(applied, extra));
    /* Native neg/SAR/neg rounds toward zero, including its INT_MIN behavior. */
    return value < 0 ? sub32(0, floor_shift(sub32(0, value), 2)) : floor_shift(value, 2);
}

static int32_t truncate_shift(int32_t value, unsigned shift)
{
    return value < 0 ? sub32(0, floor_shift(sub32(0, value), shift)) : floor_shift(value, shift);
}

void physics_yaw_rotate(int32_t matrix[9], uint32_t angle,
                        int32_t cosine, int32_t negative_sine)
{
    int i;
    if (!angle) return;
    for (i = 0; i < 3; ++i) {
        int32_t x = matrix[i];
        int32_t z = matrix[6+i];
        matrix[i] = truncate_shift(sub32(mul32(cosine, x), mul32(negative_sine, z)), 12);
        matrix[6+i] = truncate_shift(add32(mul32(negative_sine, x), mul32(cosine, z)), 12);
    }
}
