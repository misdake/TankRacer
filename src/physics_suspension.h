#ifndef PHYSICS_SUSPENSION_H
#define PHYSICS_SUSPENSION_H
#include "physics_contact.h"

typedef struct physics_suspension_point {
    int32_t kind, gap, corner, travel, rate, output;
} physics_suspension_point;

typedef struct physics_suspension_state {
    physics_suspension_point point[4];
    int32_t angular0, angular2;       /* +B4, +BC */
    int32_t tilt0, tilt1;             /* +D0, +D4 */
    int32_t penetrating_input_sum;    /* +F0, not all four inputs */
    int32_t travel_sum;               /* +F8 */
    int32_t vertical_delta;           /* +C8 */
    int32_t world_vy, position_y;     /* +54, +7C */
    int32_t timer;                    /* +D14 */
    int32_t auxiliary_type[2], auxiliary_cached[2]; /* +24C/+269, +250/+26D */
    /* Diagnostics: max gates correction, LAST overflow supplies lift amount. */
    int32_t max_penetration, last_penetration;
} physics_suspension_state;

typedef struct physics_suspension_environment {
    int32_t dt, environment_flag, session_active, mode, is_player;
    int32_t base_x, base_z, region_x0, region_z0, region_x1, region_z1;
    uint32_t player_multiplier, player_shift, other_multiplier, other_shift;
    uint32_t penetration_shift;
    /* Query results, consumed only when force<=0 and that side is uncached.
     * Current auxiliary type/cache belong to state and may be stale at entry.
     * Positive-input points do NOT force a refresh before tilt evaluation. */
    int32_t auxiliary_query_type[2];
} physics_suspension_environment;

/* 42E700: same-sign smaller magnitude, otherwise zero; no abs(INT_MIN). */
int32_t physics_contact_agree(int32_t a, int32_t b);
/* 42E690: apply tilt impulses and combine four point outputs into +C8. */
void physics_suspension_combine(physics_suspension_state *state);
/* 42E200 including four 42E400 point updates and 42E690 aggregation.
 * Input contacts/auxiliary geometry classifications are explicit dependencies.
 * Requires point_config.lower<=upper. All arithmetic is x86-width exact. */
void physics_suspension_step(physics_suspension_state *state,
                            const physics_contact_config *point_config,
                            const physics_suspension_environment *environment);
/* Resolve geometry exactly when an uncached nonpositive-input point needs it.
 * Resolver returns zero on success, negative on an explicit data error. */
typedef int (*physics_auxiliary_resolver)(void *ctx,unsigned side,int32_t *kind);
int physics_suspension_step_resolved(physics_suspension_state *state,
                                     const physics_contact_config *point_config,
                                     const physics_suspension_environment *environment,
                                     physics_auxiliary_resolver resolve,void *ctx);

/* 42E740: choose contact-dependent pivot coefficients. Config order is
 * +632,+636,+63A,+63E,+646; output order is +D8,+DC,+E0.
 * Ground contact means kind 0 or 1, not simply kind!=2. */
void physics_contact_layout(const int32_t kinds[4], const int32_t config[5],
                             int32_t output[3]);
/* 42E920 +F4: grounded-point load proxy, computed BEFORE 42E200 changes travel.
 * base/floor correspond to +69A/+69E; multiplier/shift to +6A2/+6A4. */
int32_t physics_contact_load(const int32_t kinds[4],const int32_t travel[4],
                             int32_t base,int32_t floor,uint32_t multiplier,uint32_t shift);
#endif
