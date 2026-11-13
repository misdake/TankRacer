#ifndef FRAME_OBJECT_PARAMETERS_H
#define FRAME_OBJECT_PARAMETERS_H
#include "frame_objects.h"
#include "frame_shape.h"
#include "physics_attitude.h"
/* Actual native material service for a bounded token, not an x86 callback. */
typedef int (*frame_object_material_name)(void *ctx,const unsigned char *name,uint32_t name_bytes,uint32_t mode,uint32_t *out);
typedef struct frame_object_parameters_services {
    const physics_trig *trig;const frame_shape_resources *shapes;
    frame_object_material_name material;void *material_ctx;
    /* 443830 initializes9 words; original then copies12 from its local buffer.
     * These3 ambient caller words must be supplied, never guessed from outputs. */
    const uint32_t *matrix_tail;
} frame_object_parameters_services;
/* Full41E9A0 in the single-player/no-audio scope. Parameters, bounds, shared
 * RNG, shattered mesh/sprite/animation records and native parent/final dispatch.
 * Unknown dependencies stop; float conversion uses C locale, IEEE754 binary32. */
int frame_object_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,
                               const frame_entity_factory_services *entities,const frame_object_parameters_services *services,uint32_t *out);
/* Full43F5D0 world-object parent and410730 lap-visibility parent. The latter
 * is a void original method; native out is reserved and is not an EAX claim. */
int frame_world_object_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,
                                     const frame_entity_factory_services *entities,uint32_t *out);
int frame_lap_visibility_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,
                                       const frame_entity_factory_services *entities);
typedef struct frame_object_native_context {
    frame_entity_native_context base;frame_object_parameters_services parameters;
} frame_object_native_context;
/* Registry extension; delegates existing identities to the actual C base. */
int frame_object_native(void *ctx,uint32_t target,uint32_t entity,uint32_t has_payload,uint32_t payload,uint32_t *out);
#endif
