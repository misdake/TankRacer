#ifndef FRAME_ENTITIES_H
#define FRAME_ENTITIES_H
#include "frame_data.h"
#include "frame_pool.h"

/* Native game-method registry, NOT a CPU/original-code callback. target is an
 * opaque method identity retained from the resource class table. Only ported C
 * methods may be registered. Return 0 on success, negative on incomplete/error.
 * has_payload distinguishes 504AD0 (no arg) from 504B20 (EDX payload). */
typedef int (*frame_native_method)(void *ctx,uint32_t target,uint32_t entity,
                                    uint32_t has_payload,uint32_t payload,uint32_t *out);
typedef int (*frame_native_method_pair)(void *ctx,uint32_t target,uint32_t entity,
                                         uint32_t payload,uint32_t second_payload,uint32_t *out);
/* Full504B70: two payloads, including the same signed-slot early-return leak.
 * Pair methods are qualified native game code, never original CPU callbacks. */
int frame_entity_dispatch_pair(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t descriptor,
                                 uint32_t payload,uint32_t second_payload,frame_native_method_pair method,void *method_ctx,uint32_t *out);
/* Complete 504AD0 / 504B20 dispatch semantics. Strict signed slot check, global
 * slot restoration after a method/empty slot, and original early-return leak.
 * -1 unavailable DATA, -5 no registered native method. Prefix must stop. */
int frame_entity_dispatch(frame_data_read read,frame_data_write write,void *ctx,
                            uint32_t entity,uint32_t descriptor,uint32_t has_payload,uint32_t payload,
                            frame_native_method method,void *method_ctx,uint32_t *out);
/* Full 504BC0: call the supplied CLASS at the already selected global slot.
 * Unlike entity dispatch this does not select/restore a slot or resolve a class
 * from the entity table. Native method receives the explicit entity identity. */
int frame_class_dispatch(frame_data_read read,void *ctx,uint32_t klass,uint32_t entity,
                           frame_native_method method,void *method_ctx,uint32_t *out);
/* Full504BE0: same current-slot class dispatch, forwarding the stack payload
 * to the native method as its second argument. No slot save/restore. */
int frame_class_dispatch_payload(frame_data_read read,void *ctx,uint32_t klass,uint32_t entity,uint32_t payload,
                                   frame_native_method method,void *method_ctx,uint32_t *out);
/* Full 503CB0, with a work limit for malformed/mutating lists. -3 incomplete. */
int frame_entities_tick(frame_data_read read,frame_data_write write,void *ctx,
                         frame_native_method method,void *method_ctx,uint32_t work_limit);
/* Full 503D80: descriptor 545F3C dispatch; successful wrapper returns 1. */
int frame_entity_activate(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                           frame_native_method method,void *method_ctx,uint32_t *out);
/* Full 504540: return an ID to the entity-table free-ID chain. */
int frame_entity_release_id(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity);
/* Full 504570 / 503DA0 destruction and two ordered message queues. Native
 * methods remain registered game code, standard free is the external service.
 * Queue entity0 terminates that phase even when more nodes follow (original). */
int frame_entity_destroy(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                           frame_native_method method,void *method_ctx,
                           frame_pool_system_free release,void *release_ctx);
int frame_entities_drain_queues(frame_data_read read,frame_data_write write,void *ctx,
                                  frame_native_method method,void *method_ctx,
                                  frame_pool_system_free release,void *release_ctx,uint32_t work_limit);
/* Full 503CF0 / 503D30 update-list membership. Enabling sets its flag even
 * after ordinary node OOM; disabling also repairs the saved next-tick cursor. */
int frame_entity_enable_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                               frame_pool_system_alloc allocate,frame_pool_system_free release,
                               void *system_ctx,uint32_t work_limit,uint32_t *out);
int frame_entity_disable_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                                frame_pool_system_free release,void *release_ctx,uint32_t work_limit,uint32_t *out);
/* Full 503E10/503E40/503E70. destroy!=0 enqueues deletion once under flag20;
 * destroy==0 always writes the flag then unique-prepends the notification. */
int frame_entity_queue(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t destroy,
                         frame_pool_system_alloc allocate,frame_pool_system_free release,
                         void *system_ctx,uint32_t work_limit);
int frame_entity_cancel_notification(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                                       frame_pool_system_free release,void *release_ctx,uint32_t work_limit);
/* Full 503EA0: point to the inline transform and copy twelve identity words. */
int frame_entity_reset_transform(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity);
#endif
