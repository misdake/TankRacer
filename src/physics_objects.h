#ifndef PHYSICS_OBJECTS_H
#define PHYSICS_OBJECTS_H
#include "physics_geometry.h"
/* 41B9A0: 0 miss,1 crossing,2 start inside; negative arithmetic error.
 * Even a miss may update point; normal is changed only by code1. */
int physics_segment_box(const int32_t a[3],const int32_t b[3],const int32_t lower[3],
                          const int32_t upper[3],int32_t point[3],int32_t normal[3]);
typedef struct physics_object_hit {
    uint32_t entity,object;
    int32_t code,point[3],normal[3];
} physics_object_hit;
/* 41BD00 spatial-grid obstacle query. A hit returns WORLD point and normal.
 * 4071A0 deformable-list query returns LOCAL point/normal exactly as original.
 * Return status 0/1 or explicit negative data/traversal/arithmetic error. */
int physics_obstacle_query(physics_world_read read,void *ctx,uint32_t self_id,
                            const int32_t a[3],const int32_t b[3],physics_object_hit *out);
int physics_deformable_query(physics_world_read read,void *ctx,const int32_t a[3],
                              const int32_t b[3],physics_object_hit *out);
int physics_tank_overlap(physics_world_read read,void *ctx,uint32_t self,uint32_t other); /* 42DEA0 */
/* 42D500. Return0/1/2; code2 changes only object/entity metadata, preserving
 * caller point/normal; a miss explicitly clears object like the native outptr. */
int physics_tank_query(physics_world_read read,void *ctx,uint32_t self,
                        const int32_t a[3],const int32_t b[3],physics_object_hit *out);
#endif
