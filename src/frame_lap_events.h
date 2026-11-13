#ifndef FRAME_LAP_EVENTS_H
#define FRAME_LAP_EVENTS_H
#include "frame_entities.h"
/* Complete 441610: square X/Z proximity in render units, object coordinates
 * arithmetic-shifted by3. camera index is signed8; Y is intentionally ignored. */
int frame_entity_near_camera(frame_data_read read,void *ctx,uint32_t object,int32_t camera,
                               int32_t threshold,uint32_t *out);
/* Complete normal 4214B0 lap-gated object-event scheduling. Near-camera events
 * are appended to ordered deferred lists; far objects dispatch native methods.
 * Full game allocation/list rules are C; malloc/free are external services.
 * This is not the whole player update. */
int frame_lap_object_events(frame_data_read read,frame_data_write write,void *ctx,uint32_t player,
                              frame_native_method method,void *method_ctx,
                              frame_pool_system_alloc allocate,frame_pool_system_free release,
                              void *system_ctx,uint32_t work_limit);
/* Complete 4215C0: special class resets away from both cameras; all other
 * classes follow normal lap scheduling. Class identity is DATA 545094. */
int frame_lap_object_events_special(frame_data_read read,frame_data_write write,void *ctx,uint32_t player,
                                      frame_native_method method,void *method_ctx,
                                      frame_pool_system_alloc allocate,frame_pool_system_free release,
                                      void *system_ctx,uint32_t work_limit);
/* 42033B..420442: process both deferred lists. Uses second-player presence,
 * not the split-screen flag. Preserves original double advance after removal. */
int frame_lap_flush_deferred(frame_data_read read,frame_data_write write,void *ctx,
                               frame_native_method method,void *method_ctx,
                               frame_pool_system_free release,void *release_ctx,uint32_t work_limit);
#endif
