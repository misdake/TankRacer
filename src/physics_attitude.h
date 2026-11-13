#ifndef PHYSICS_ATTITUDE_H
#define PHYSICS_ATTITUDE_H
#include <stdint.h>

typedef struct physics_trig {
    const int32_t *sine;   /* 16384 entries from 597380, raw angle & FFFC */
    const int32_t *cosine; /* 16384 entries from 59B380 */
    const int32_t *angle;  /* 257 entries from 596F80; preserve actual resource */
} physics_trig;
enum { PHYSICS_ATTITUDE_OK=0,PHYSICS_ATTITUDE_ARITH_ERROR=-2 };

/* 4442F0: integer quadrant/ratio lookup. INT_MIN magnitudes are rejected;
 * no host atan2 and no float angle calculation. */
int physics_angle_lookup(int32_t numerator,int32_t denominator,const physics_trig *trig,int32_t *out);
int32_t physics_length2_approx(int32_t x,int32_t y); /* 443550 */
/* Full443830 initial Euler matrix, uint16 pitch/yaw and uint32 roll test. */
void physics_matrix_from_angles(int32_t matrix[9],const uint32_t angles[3],const physics_trig *trig);
void physics_matrix_rotate(int32_t matrix[9],unsigned axis,uint32_t angle,const physics_trig *trig);
int physics_matrix_normalize(int32_t matrix[9]); /* 444120, row then column */
void physics_pivot_rotate(int32_t matrix[9],int32_t position[3],const int32_t pivot[3],
                           const int32_t angles[3],const physics_trig *trig);

typedef struct physics_attitude_state {
    int32_t reference[9]; /* old main +18; guards do not use newly rotated rows */
    int32_t matrix[9],position[3]; /* +2B2, +2A6 */
    int32_t angular[3]; /* +B4,+B8,+BC */
    int32_t longitudinal_delta,lateral_delta; /* +CC,+C4 */
    int32_t pivot[3]; /* +D8,+DC,+E0 */
} physics_attitude_state;
typedef struct physics_attitude_config {
    uint32_t longitudinal_multiplier,longitudinal_shift; /* +6F6/+6F8 */
    uint32_t lateral_multiplier,lateral_shift; /* +6FA/+6FC */
} physics_attitude_config;
/* Complete 42EF50, including original cross-axis flip guards, pivot rotation,
 * and normalization every unsigned frame%10==0. No machine-code callback. */
int physics_attitude_step(physics_attitude_state *state,const physics_attitude_config *config,
                           int32_t dt,uint32_t frame,const physics_trig *trig);
#endif
