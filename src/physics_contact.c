#include "physics_contact.h"

static int32_t s32(uint32_t value)
{
    return value <= INT32_MAX ? (int32_t)value : (int32_t)((int64_t)value - INT64_C(4294967296));
}
static int32_t add32(int32_t a, int32_t b) { return s32((uint32_t)a + (uint32_t)b); }
static int32_t neg32(int32_t a) { return s32(0u - (uint32_t)a); }
static int32_t mul32(int32_t a, int32_t b) { return s32((uint32_t)a * (uint32_t)b); }
static int32_t sar32(int32_t a, unsigned shift)
{
    uint32_t value = (uint32_t)a;
    shift &= 31u;
    return s32(a < 0 ? ~(~value >> shift) : value >> shift);
}
static int32_t coefficient(int32_t value, uint32_t packed)
{
    return sar32(mul32(value, (int32_t)(packed & 65535u)), packed >> 16);
}

void physics_contact_step(physics_contact_state *s, const physics_contact_config *p,
                          int32_t force, int32_t dt, uint32_t corner, int aux_type)
{
    int32_t square, restoring, previous, step, tilt;
    if (force > 0) s->rate = add32(s->rate, coefficient(force, p->force_positive));
    else if (force < 0) s->rate = add32(s->rate, coefficient(force, p->force_negative));
    else s->output = 0;

    square = sar32(s->travel, p->shape_shift);
    square = mul32(square, square);
    restoring = mul32(coefficient(square, s->travel <= 0 ? p->restore_negative : p->restore_positive), dt);
    s->rate = add32(s->rate, s->travel <= 0 ? restoring : neg32(restoring));
    previous = s->rate;
    s->rate = add32(previous, neg32(sar32(mul32(previous, dt), p->rate_damping >> 24)));
    if ((previous < 0 && s->rate > 0) || (previous > 0 && s->rate < 0)) s->rate = 0;

    s->travel = add32(s->travel, s->rate);
    step = (int32_t)(p->travel_dead_step & 65535u);
    if (step > 32767) step -= 65536;
    step = mul32(step, dt);
    if (step < s->travel) s->travel = add32(s->travel, neg32(step));
    else if (s->travel < neg32(step)) s->travel = add32(s->travel, step);
    else s->travel = 0;
    if (s->travel < p->lower) s->travel = p->lower;
    if (s->travel > p->upper) s->travel = p->upper;

    /* The native repeats the bounds check here; with lower<=upper its extra
     * overflow correction is zero. Keep that precondition explicit. */
    if (force > 0) force = coefficient(s->travel, p->output_gain);
    s->output = sar32(force, 14);
    tilt = coefficient(force, p->tilt_gain);
    if (corner >= 4u || (tilt <= 0 && aux_type != 2)) return;
    tilt = sar32(tilt, 9);
    s->tilt0 = add32(s->tilt0, corner == 0u || corner == 2u ? neg32(tilt) : tilt);
    s->tilt1 = add32(s->tilt1, corner < 2u ? neg32(tilt) : tilt);
}
