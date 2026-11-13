#ifndef PHYSICS_TERRAIN_H
#define PHYSICS_TERRAIN_H
#include <stdint.h>

/* Read DATA only from the loaded world. Return nonzero on a complete read.
 * This boundary executes no original code and supplies no missing bytes. */
typedef int (*physics_world_read)(void *ctx, uint32_t address, void *dst, uint32_t size);
typedef struct physics_terrain_hit {
    int32_t height;
    uint32_t node;
    int32_t normal[3];
    uint32_t surface;
    int32_t special, extra[4];
    uint32_t flags_word; /* Original writes low16 only; upper16 remains intact. */
} physics_terrain_hit;

enum { PHYSICS_TERRAIN_MISS=0, PHYSICS_TERRAIN_HIT=1,
       PHYSICS_TERRAIN_READ_ERROR=-1, PHYSICS_TERRAIN_ARITH_ERROR=-2,
       PHYSICS_TERRAIN_LIMIT=-3 };

/* Independent 501530 selection + 501910 plane-height evaluation.
 * Root is [55AE60], x/z are original raw world coordinates. Miss changes only
 * height to -2147483647, exactly like the original output contract.
 * Invalid read/division or a missing terminator is reported, never zero-filled. */
int physics_terrain_query(physics_world_read read, void *ctx, uint32_t root,
                          int32_t x, int32_t z, physics_terrain_hit *out);

/* Full501770: same501530 selection, height-only result. The original has a
 * distinct ny==0 branch using the first vertex's X word; main501910 does not. */
int physics_terrain_height(physics_world_read read,void *ctx,uint32_t root,int32_t x,int32_t z,int32_t *out);
/* Full501830: height with the same ny==0 special case as501770, plus the
 * triangle normal. On miss only height changes; normal remains untouched. */
int physics_terrain_normal_height(physics_world_read read,void *ctx,uint32_t root,int32_t x,int32_t z,int32_t *out,int32_t normal[3]);

typedef struct physics_auxiliary_contact {
    int32_t world[3], kind, cached, surface;
} physics_auxiliary_contact;
/* 42EE80: secondary sample transformed by a track-part pose, classified against
 * the independently queried plane. force_surface selects the original player
 * material override. Even a miss sets cached=1, with kind=3. */
int physics_auxiliary_query(physics_world_read read,void *ctx,uint32_t root,
                            const int32_t local[3],const int32_t matrix[9],
                            const int32_t origin[3],int force_surface,
                            physics_auxiliary_contact *out);
/* Exact 42EE05..2B rule: the SAME primary [0]/[2] comparison gates both
 * material outputs; primary [1]/[3] are not read in this block. */
void physics_contact_materials(int32_t primary0,int32_t primary2,
                                const int32_t auxiliary[2],int32_t output[2]);
#endif
