#ifndef PHYSICS_BODY_H
#define PHYSICS_BODY_H
#include "physics_geometry.h"
int physics_body_articulation(physics_world_read read,physics_world_write write,void *ctx,uint32_t object); /* 433B70 */
int physics_player_lean(physics_world_read read,physics_world_write write,void *ctx,uint32_t object); /* 4339B0 */
int physics_contact_hull_refresh(physics_world_read read,physics_world_write write,void *ctx,uint32_t object); /* 42DFE0 */
int physics_mesh_vertex_normals(physics_world_read read,physics_world_write write,void *ctx,uint32_t mesh,uint32_t vertex); /* 4F7650 */
int physics_body_damage(physics_world_read read,physics_world_write write,void *ctx,uint32_t object,const int32_t world[3]); /* 42F940 */
int physics_turret_damage(physics_world_read read,physics_world_write write,void *ctx,uint32_t object,const int32_t world[3]); /* 438290 */
int physics_track_damage(physics_world_read read,physics_world_write write,void *ctx,uint32_t object,int32_t kind,const int32_t world[3]); /* 42DE60 */
#endif
