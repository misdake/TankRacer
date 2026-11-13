#ifndef PHYSICS_GEOMETRY_H
#define PHYSICS_GEOMETRY_H
#include "physics_terrain.h"

/* Model-owned data mutation. The verification host writes a separate overlay,
 * never the native emulator. No original code is executed by this interface. */
typedef int (*physics_world_write)(void *ctx,uint32_t address,const void *src,uint32_t size);

/* Original 40F830: unsigned magnitudes, largest + quarter of other two,
 * returned with x86 32-bit wrap. This is a metric approximation, not length. */
int32_t physics_approximate_distance(const int32_t delta[3]);
/* 501A50: alternate spatial grid, mode flag filter, rectangular sector test,
 * then nearest stored TRIANGLE BOUNDING-BOX CENTER by that metric (<300000).
 * It does not intersect a ray with the triangle. Miss leaves normal untouched. */
int physics_fallback_query(physics_world_read read,void *ctx,uint32_t root,
                           const int32_t world[3],int32_t mode,int32_t normal[3]);
/* 4075A0: ten times minimum signed-wrap distance to four local X/Z edges,
 * capped above by signed +B6. Bounds are +60,+6C,+68,+74, in that order. */
int32_t physics_deform_edge_weight(const int32_t local[3],const int32_t bounds[4],int32_t cap);
typedef struct physics_deform_result {
    int32_t local_raw[3], local_clamped[3], vertex_height, edge_weight, offset;
    uint32_t activated_records;
} physics_deform_result;
/* 4073D0 including 442ED0 transform, vertex search, record-state/colour writes
 * and edge attenuation. Missing data/writer or corrupt traversal fails explicitly. */
int physics_deform_query(physics_world_read read,physics_world_write write,void *ctx,
                         uint32_t object,const int32_t world[3],physics_deform_result *out);
#endif
