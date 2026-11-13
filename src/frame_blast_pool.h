#ifndef FRAME_BLAST_POOL_H
#define FRAME_BLAST_POOL_H
#include "frame_scene_effects.h"
#include "frame_actors.h"
#include "frame_object_parameters.h"
/*40A100 material names, template styles, sprite fields and shared counters. */
int frame_blast_pool_initialize(frame_data_read read,frame_data_write write,void *ctx,frame_object_material_name material,void *material_ctx);
/*409F40/409F10 plus4097B0/409EB0: two shared slots across shard/ring
 * kinds, oldest signed timestamps, actual owner fields and quad releases. */
int frame_blast_slot_allocate(frame_data_read read,frame_data_write write,void *ctx,uint32_t kind,uint32_t work_limit,uint32_t *out);
int frame_blast_slot_release(frame_data_read read,frame_data_write write,void *ctx,uint32_t kind,uint32_t slot,uint32_t work_limit);
/*409D20: four camera-facing radial corners, truncating radial rotation but
 * arithmetic-shifting camera transform. Does not submit graphics. */
int frame_blast_quad(frame_data_read read,frame_data_write write,void *ctx,uint32_t destination,uint32_t parameters,uint32_t angle,uint32_t origin,uint32_t matrix);
/*409820/409A60/409B50: shock-ring creation, RNG/lifetime, colour/geometry
 * publication and explicit final sprite sink. All game DATA stays in C. */
int frame_blast_ring_start(frame_data_read read,frame_data_write write,void *ctx,uint32_t origin,uint32_t owner,uint32_t kind,uint32_t work_limit,uint32_t *out);
int frame_blast_ring_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t slot,uint32_t work_limit,uint32_t *out);
int frame_blast_ring_project(frame_data_read read,frame_data_write write,void *ctx,uint32_t slot,const frame_actor_services *graphics);
/*4091F0/4093A0/4093E0: radial expanding band, same shared slots, template
 * geometry and original full/partial allocation failure cleanup. */
int frame_blast_band_start(frame_data_read read,frame_data_write write,void *ctx,uint32_t origin,uint32_t outer,uint32_t owner,uint32_t flipped,uint32_t work_limit,uint32_t *out);
int frame_blast_band_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t slot,uint32_t work_limit,uint32_t *out);
int frame_blast_band_project(frame_data_read read,frame_data_write write,void *ctx,uint32_t slot,uint32_t work_limit);
/* Explicit native positions for computed collision origins; no scratch DATA. */
int frame_blast_ring_start_values(frame_data_read read,frame_data_write write,void *ctx,const int32_t origin[3],uint32_t owner,uint32_t kind,uint32_t limit,uint32_t *out);
int frame_blast_band_start_values(frame_data_read read,frame_data_write write,void *ctx,const int32_t origin[3],uint32_t outer,uint32_t owner,uint32_t flipped,uint32_t limit,uint32_t *out);
#endif
