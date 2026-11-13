#ifndef PHYSICS_CONTACT_H
#define PHYSICS_CONTACT_H
#include <stdint.h>

/* One point of the four-point suspension, 0x42E400. Names describe arithmetic
 * roles; no claim is made about SI spring constants or force units. Coefficients
 * pack a ushort multiplier and a shift byte at bit16, except rate_damping
 * (shift at bit24) and travel_dead_step (signed low16). */
typedef struct physics_contact_config {
    int32_t lower, upper;
    uint32_t force_positive, force_negative, output_gain, tilt_gain;
    uint32_t shape_shift, restore_positive, restore_negative;
    uint32_t rate_damping, travel_dead_step;
} physics_contact_config;
typedef struct physics_contact_state {
    int32_t travel, rate, output, tilt0, tilt1;
} physics_contact_state;

/* aux_type is the result of the ORIGINAL secondary geometry query/cache.
 * This function models the point's arithmetic, not that world-space query.
 * Requires lower<=upper. corner>=4 suppresses tilt output, as in the original. */
void physics_contact_step(physics_contact_state *state,
                          const physics_contact_config *config,
                          int32_t force, int32_t dt, uint32_t corner, int aux_type);
#endif
