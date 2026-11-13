#ifndef FRAME_MESH_BREAK_H
#define FRAME_MESH_BREAK_H
#include "frame_shatter.h"
/* Full41E7D0: independent shape, power-of-two zeroed32-byte triangle states,
 * real class notification. Only immutable shape bytes are an external input. */
int frame_mesh_break_prepare(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t name,
                                 const frame_entity_factory_services *entities,const frame_shape_resources *resources);
/* Full41D460: per-triangle lifetime, ground bounce, fixed-point vertex motion,
 * paired triangles and real stop dispatch. Missing trig is an explicit error. */
int frame_mesh_break_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                              const frame_entity_factory_services *entities,const physics_trig *trig,uint32_t *out);
/* Full41DC00. Degenerate paired triangles can consume an unassigned original
 * stack WORD: optional explicit ambient input, never inferred or zero-filled. */
int frame_mesh_break_hit(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t other,
                             const frame_entity_factory_services *entities,const frame_shape_resources *resources,
                             const uint16_t *paired_vertex_tail,uint32_t *out);
typedef struct frame_mesh_break_native_context { frame_shatter_native_context base;const uint16_t *paired_vertex_tail; } frame_mesh_break_native_context;
int frame_mesh_break_native(void *ctx,uint32_t target,uint32_t entity,uint32_t has_payload,uint32_t payload,uint32_t *out);
#endif
