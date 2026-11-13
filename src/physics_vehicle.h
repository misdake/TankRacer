#ifndef PHYSICS_VEHICLE_H
#define PHYSICS_VEHICLE_H
#include "physics_attitude.h"
#include "physics_geometry.h"
/* Original-layout DATA adapter for the independent contact->layout->suspension
 * chain. No guest ABI/register state and no native instruction execution. */
int physics_vehicle_contact(physics_world_read read,physics_world_write write,void *ctx,
                             uint32_t object,uint32_t *feedback_required);
#endif
