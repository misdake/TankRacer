#include "frame_math.h"
#include "physics_memory.h"
#include "frame_math_tables.h"
const physics_trig *frame_math_trig(void)
{ static const physics_trig trig={math_sine,math_sine+4096,math_angle};return &trig; }
int frame_math_initialize(frame_data_write write,void *ctx)
{
    physics_memory m={0,write,ctx,0};uint32_t i;
    if (!write) return -1;
    for (i=0;i<20480u;i++) pm_write32(&m,0x597380u+i*4u,math_sine[i]);
    for (i=0;i<4096u;i++) pm_write32(&m,0x5af380u+i*4u,math_cosine_angle[i]);
    pm_store(&m,0x5b3380u,0,4);
    for (i=0;i<4096u;i++) pm_write32(&m,0x5ab380u+i*4u,math_tangent[i]);
    for (i=0;i<256u;i++) pm_write32(&m,0x596f80u+i*4u,math_angle[i]);
    return m.error;
}
