#ifndef FRAME_EXPLOSION_H
#define FRAME_EXPLOSION_H
#include "frame_blast_pool.h"
/* Final fog-device adapter454420. Game map selection and54B4C8 publication
 * stay in C; graphics backend caches/driver calls are external. */
typedef int (*frame_explosion_fog_sink)(void *ctx,uint32_t enabled,uint32_t near_distance,uint32_t far_distance,uint32_t rgba);
typedef struct frame_explosion_services {
    frame_explosion_fog_sink fog;void *fog_ctx;
    /*415050 initializes only collision.owner in its local descriptor. These
     * four original caller words (normal XYZ, visibility word) are explicit
     * inputs; they affect attached smoke, never the vehicle impulse. */
    const uint32_t *mine_collision_words;
} frame_explosion_services;
typedef struct frame_explosion_fog_host { uint32_t commands,enabled,near_distance,far_distance,rgba; } frame_explosion_fog_host;
int frame_explosion_fog_host_submit(void *ctx,uint32_t enabled,uint32_t near_distance,uint32_t far_distance,uint32_t rgba);
/*42C1F0/40A350: map-specific fog policy and15-owner registry reset. */
int frame_explosion_fog(frame_data_read read,frame_data_write write,void *ctx,const frame_explosion_services *services);
int frame_explosion_reset(frame_data_read read,frame_data_write write,void *ctx);
/*408410 attached vehicle debris. The first two RNG draws precede immunity. */
int frame_explosion_attach(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,uint32_t collision,uint32_t count,uint32_t work_limit);
/*4085F0 complete creation, including unconditional RNG, attached-source
 * preparation,15-owner eviction and the two shared effect types. */
int frame_explosion_spawn(frame_data_read read,frame_data_write write,void *ctx,uint32_t origin,uint32_t collision,uint32_t kind,const frame_entity_factory_services *entities,const frame_explosion_services *services,uint32_t *out);
/* Same creation from an explicit native100-byte collision record; no virtual
 * scratch address and no extra allocation/guest DATA mutation. NULL=no hit. */
int frame_explosion_spawn_values(frame_data_read read,frame_data_write write,void *ctx,uint32_t origin,const uint32_t contact_words[25],uint32_t kind,const frame_entity_factory_services *entities,const frame_explosion_services *services,uint32_t *out);
int frame_explosion_spawn_native(frame_data_read read,frame_data_write write,void *ctx,const int32_t origin[3],const uint32_t contact_words[25],uint32_t kind,const frame_entity_factory_services *entities,const frame_explosion_services *services,uint32_t *out);
/* Native local descriptor, no synthetic guest stack or original callback.
 * Only mine explosion kinds3/4 use this source contract. */
int frame_explosion_spawn_mine(frame_data_read read,frame_data_write write,void *ctx,uint32_t origin,uint32_t vehicle,uint32_t kind,const frame_entity_factory_services *entities,const frame_explosion_services *services,uint32_t *out);
/*408F40 attached debris: integration/gravity/angular approach and two
 * independently clamped dimensions. No RNG or graphics callback. */
int frame_explosion_debris_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t object);
/*4089E0 complete owner update: UV/lifetime, packed damping, both shared
 * effect kinds, attached-source transforms, ten smoke particles and retirement. */
int frame_explosion_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *entities);
#endif
