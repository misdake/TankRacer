#ifndef FRAME_PLAYER_LAP_H
#define FRAME_PLAYER_LAP_H
#include "frame_lap_events.h"
#include "frame_network.h"
typedef struct frame_player_lap_services {
    frame_native_method method;void *method_ctx;
    frame_pool_system_alloc allocate;frame_pool_system_free release;void *system_ctx;
    frame_network_service network;void *network_ctx;
    unsigned char *workspace;uint32_t capacity,work_limit;
} frame_player_lap_services;
/* Complete 42009B..42033B gameplay region. The caller's player object/entity
 * are explicit data. Checkpoint/lap records, duel results/splits, native object
 * events/restores and network finish are composed from readable C modules.
 * No x86 execution or trajectory input. -3 is an incomplete bounded traversal.
 * Deferred events begin at 42033B and are a separate qualified C module. */
int frame_player_lap_update(frame_data_read read,frame_data_write write,void *ctx,
                              uint32_t player,uint32_t entity,const frame_player_lap_services *services);
#endif
