#ifndef FRAME_NATIVE_H
#define FRAME_NATIVE_H
#include "frame_world.h"
#include "frame_actors.h"
#include "frame_explosion.h"
#include "frame_player_lap.h"
typedef struct frame_native_context {
    frame_mesh_break_native_context base;
    uint32_t unknown_target,unknown_entity;
    frame_actor_services actors;
    frame_explosion_services explosion;
    const uint32_t *camera_matrix_tail; /*43C700/4057F0 explicit ambient matrix words */
    const uint32_t *player_death_contact_words; /*42C860's explicit ambient record */
    const frame_player_lap_services *player_network; /*optional mounted transport */
    const int32_t *inactive_miss_words; /*4333C0's explicit incoming three unwritten stack slots */
} frame_native_context;
/* Bind all nested dispatches to the same C registry; preserves supplied DATA,
 * allocation/resource/trig/device services. No original-code fallback. */
int frame_native_bind(frame_native_context *ctx);
int frame_native_dispatch(void *ctx,uint32_t target,uint32_t entity,uint32_t has_payload,uint32_t payload,uint32_t *out);
int frame_native_pair(void *ctx,uint32_t target,uint32_t entity,uint32_t payload,uint32_t second,uint32_t *out);
uint32_t frame_native_method_count(void);
int frame_native_method_at(uint32_t index,uint32_t *target);
int frame_native_has_method(uint32_t target);
#endif
