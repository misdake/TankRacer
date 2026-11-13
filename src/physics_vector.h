#ifndef PHYSICS_VECTOR_H
#define PHYSICS_VECTOR_H
#include "physics_attitude.h"
#include "physics_geometry.h"
void physics_vector_normalize(int32_t vector[3]); /* 443610, signed approximate norm */
void physics_vector_cross(const int32_t a[3],const int32_t b[3],int32_t out[3]); /* 4436D0 */
void physics_vector_yaw(const int32_t in[3],int32_t out[3],uint32_t angle,const physics_trig *trig);
int physics_yaw_correction(physics_world_read read,physics_world_write write,void *ctx,uint32_t object); /* 4336D0 */
typedef struct physics_collision_scale { uint16_t multiplier;uint8_t shift; } physics_collision_scale;
typedef struct physics_collision_response {
    int32_t normal[3],velocity[3],approach,speed_units;
} physics_collision_response;
/* 42CA20; optional local_z/final descriptors mirror original pointer guards. */
int physics_collision_respond(const int32_t normal[3],const int32_t velocity[3],
                               const physics_collision_scale *local_z,const physics_collision_scale *final,
                               int32_t mode,int32_t is_player,const physics_trig *trig,
                               physics_collision_response *out);
/* Original x87 binary64 constant1.7 -> extended product -> truncation. For
 * integer multiples of ten, binary1.7 lies below17/10, so +10 becomes16. */
int32_t physics_binary_one_point_seven(int32_t value);
int physics_tank_pair_response(physics_world_read read,physics_world_write write,void *ctx,
                                uint32_t self,uint32_t other,const int32_t normal[3],
                                const int32_t other_velocity[3],int32_t *approach); /* 42CFB0 */
#endif
