#ifndef FRAME_OBJECTS_H
#define FRAME_OBJECTS_H
#include "frame_entity_factory.h"
/* Full41F6E0 parent initialization, then mark spatial membership absent. */
int frame_object_initialize(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                               const frame_entity_factory_services *services,uint32_t *out);
/* Full41C230/41C270 and41C300. Class methods have no defined return value;
 * these expose status only. Spatial duplicates and list order are preserved. */
int frame_object_enter(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                          const frame_entity_factory_services *services);
int frame_object_leave(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                          const frame_entity_factory_services *services);
int frame_object_deactivate(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                               const frame_entity_factory_services *services);
/* Full41C7A0: switch render membership, mode1 selects54B488; other values
 * select54B48C. It deliberately does not change physical grid membership. */
int frame_object_set_mode(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t mode,
                             const frame_entity_factory_services *services);
/*41C620 moves from a DATA vector and rebuilds grid membership, returning the
 * source Y after the rebuild (aliases are observable).41C660 inserts on mode1,
 * otherwise removes without clearing the cached packed coordinate. */
int frame_object_move(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t position,const frame_entity_factory_services *services,uint32_t *out);
int frame_object_collidable(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t mode,const frame_entity_factory_services *services);
/* Full41C0A0/41C190/41E960: auxiliary mesh/effect storage creation, buffer and
 * copied-shape release, then header release. Storage fields are intentionally
 * reread after pool writes; this preserves observable alias/order semantics. */
int frame_object_auxiliary_create(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                                     const frame_entity_factory_services *services);
/*41C0E0 allocate owned vertex/three-center-array backups, power-of-two
 * buffers of at least2 bytes. The capture itself is a class-specific method. */
int frame_object_auxiliary_backup_allocate(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *services);
int frame_object_auxiliary_clear(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                                    const frame_entity_factory_services *services);
int frame_object_auxiliary_release(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                                      const frame_entity_factory_services *services);
/* Full41E890: both auxiliary representations, spatial membership removal and
 * native current-slot parent destructor. Does not treat game cleanup as I/O. */
int frame_object_destroy(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                            const frame_entity_factory_services *services,uint32_t *out);
#endif
