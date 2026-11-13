#ifndef FRAME_MATH_H
#define FRAME_MATH_H
#include "frame_data.h"
#include "physics_attitude.h"
/*444570 lookup constants generated from its documented mathematical formulas,
 * including its nonideal angle increment and signed0.9 rounding. No fixture,
 * PE loader, platform transcendental or floating runtime is needed. */
const physics_trig *frame_math_trig(void);
/* Same table layout/order:20480 sine,4096 acos,terminal0,4096 tan,256 atan.
 * Cosine aliases sine+4096; atan's index256 aliases sine[0] in DATA. */
int frame_math_initialize(frame_data_write write,void *ctx);
#endif
