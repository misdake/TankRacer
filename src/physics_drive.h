#ifndef PHYSICS_DRIVE_H
#define PHYSICS_DRIVE_H
#include "physics_geometry.h"
/* 430C51..431E56: all body damping, gear/engine, traction budget and steering.
 * Inputs are already transformed body velocity +E4/E8/EC and sampled contacts.
 * This stage ends before the separate 4336D0 yaw correction. */
int physics_vehicle_drive(physics_world_read read,physics_world_write write,void *ctx,
                           uint32_t object,uint32_t entity_id);
#endif
