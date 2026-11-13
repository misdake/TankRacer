/* Integer velocity transforms used by 0x430A30. No OS/game-memory dependency. */
#ifndef PHYSICS_VELOCITY_H
#define PHYSICS_VELOCITY_H
#include <stdint.h>

/* Matrix is nine row-major Q12 words. inverse=0: world -> body (M*v),
 * inverse=1: body -> world (transpose(M)*v). large selects the ORIGINAL
 * reduced-precision arithmetic, not an algebraically equivalent float product.
 * Every multiply/add wraps at 32 bits; every shift floors toward -infinity. */
void physics_velocity_transform(const int32_t src[3], const int32_t matrix[9],
                                int32_t dst[3], int inverse, int large);

/* 0x430C09..23 tests horizontal VELOCITY, not position/map bounds. */
int physics_velocity_large(const int32_t world_velocity[3]);

/* Normal traction branch at 0x431CD2..CFE, with a nonnegative per-step budget.
 * Returns unresolved drive/body longitudinal velocity difference (+A0 before
 * the frame-end rescale), and writes the part applied to body velocity (+EC).
 * Special player low-speed double-budget branch is intentionally separate. */
int32_t physics_longitudinal_transfer(int32_t drive_velocity, int32_t body_velocity,
                                      int32_t budget, int32_t *applied);
/* 0x431CFE..431D43: optional DEA/DE1/CAA gate between the budget and EC.
 * Returned value is what is actually added to body longitudinal velocity. */
int32_t physics_longitudinal_gate(int32_t applied, int active, int32_t airborne_count,
                                  int32_t extra);
/* 0x443C00: update orientation rows 0/2 around local Y. Coefficients are the
 * ORIGINAL Q12 lookup words at angle&0xfffc, not newly computed sin/cos.
 * A complete turn is 65536 raw angle units; table sampling discards low 2 bits. */
void physics_yaw_rotate(int32_t matrix[9], uint32_t angle,
                        int32_t cosine, int32_t negative_sine);
#endif
