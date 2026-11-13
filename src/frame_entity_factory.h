#ifndef FRAME_ENTITY_FACTORY_H
#define FRAME_ENTITY_FACTORY_H
#include "frame_entities.h"
#include "frame_audio.h"
/* The standard calloc boundary guarantees a newly owned, zeroed DATA block.
 * Argument order/count are explicit; no game ID/table policy lives here. */
typedef int (*frame_entity_system_calloc)(void *ctx,uint32_t count,uint32_t bytes,uint32_t *out);
typedef struct frame_entity_factory_services {
    frame_native_method method;void *method_ctx;
    frame_pool_system_alloc allocate;frame_entity_system_calloc zero_allocate;
    frame_pool_system_free release;void *system_ctx;uint32_t work_limit;
    frame_audio_sink sound;void *sound_ctx;
} frame_entity_factory_services;
static inline void frame_entity_sound(const frame_entity_factory_services *s,uint32_t cue,uint32_t object,uint32_t variant,const int32_t *position)
{ if (s && s->sound) s->sound(s->sound_ctx,cue,object,variant,position,0); }
static inline void frame_entity_sound_name(const frame_entity_factory_services *s,uint32_t object,uint32_t index,const unsigned char *name)
{ if (s && s->sound) s->sound(s->sound_ctx,0x41b740u,object,index,0,name); }
/* Full 504700 / 5047B0 / 504670. Signed ID limits, 128-slot growth, ordered
 * DATA copy and explicit free-ID unlink. -4 is original calloc-null fault,
 * -3 incomplete traversal; never fabricate a valid ID. */
int frame_entity_table_grow(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *services);
/* Full504D50 startup/reset: explicit calloc(8,capacity), reserved ID0, free
 * chain begins1. Different from adding128 slots to an existing table. */
int frame_entity_table_initialize(frame_data_read read,frame_data_write write,void *ctx,uint32_t capacity,const frame_entity_factory_services *services);
int frame_entity_unlink_free_id(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t work_limit,uint32_t *out);
int frame_entity_bind_record(frame_data_read read,frame_data_write write,void *ctx,uint32_t record,uint32_t requested,
                                const frame_entity_factory_services *services,uint32_t *out);
/* Full 504630 record allocation and 5045C0 creation/constructor dispatch.
 * Constructors must be actual registered native C methods, not OS stubs. */
int frame_entity_new_record(frame_data_read read,frame_data_write write,void *ctx,uint32_t requested,
                               const frame_entity_factory_services *services,uint32_t *out);
int frame_entity_create(frame_data_read read,frame_data_write write,void *ctx,uint32_t klass,
                           const frame_entity_factory_services *services,uint32_t *out);
/*504810: fill a reserved empty record and run its actual two constructors.
 * Original diagnostic-only invalid/in-use cases return-4 with no DATA writes. */
int frame_entity_fill_reserved(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t klass,
                                  const frame_entity_factory_services *services);
/* Full 500B10 static-level bump allocation; fatal original diagnostic paths
 * return -4 and retain the original DATA prefix. */
int frame_level_allocate(frame_data_read read,frame_data_write write,void *ctx,uint32_t bytes,uint32_t *out);
/* Full 504E50/504EB0 base object storage constructor/destructor. Constructor
 * returns1 even on ordinary dynamic OOM; object pointer is independently DATA.
 * Static flag8 prevents pool release. Neither function guesses class fields. */
int frame_entity_allocate_object(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                                    const frame_entity_factory_services *services,uint32_t *out);
int frame_entity_release_object(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                                   const frame_entity_factory_services *services,uint32_t *out);
/* Portable registry for qualified base methods only. Unknown targets return-5.
 * An application registry can delegate these identities and supply further C
 * classes. This contains no CPU, PE loader or original-code callback. */
typedef struct frame_entity_native_context {
    frame_data_read read;frame_data_write write;void *data_ctx;
    frame_entity_factory_services services;
} frame_entity_native_context;
int frame_entity_native_base(void *ctx,uint32_t target,uint32_t entity,uint32_t has_payload,
                               uint32_t payload,uint32_t *out);
#endif
