#ifndef PHYSICS_POSE_H
#define PHYSICS_POSE_H
#include "physics_attitude.h"
#include "physics_geometry.h"
/* Complete 42F220: refresh body/track/attachment poses and physical positions.
 * Reads original resource matrices through a DATA interface, executes no guest
 * helper, and writes only through model-owned storage. */
int physics_pose_update(physics_world_read read,physics_world_write write,void *ctx,
                         uint32_t object,const physics_trig *trig);
#endif
