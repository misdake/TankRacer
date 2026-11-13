#ifndef FRAME_SHATTER_H
#define FRAME_SHATTER_H
#include "frame_object_parameters.h"
#include "frame_feedback.h"
typedef struct frame_shatter_services {
    const physics_trig *trig;
    frame_native_method_pair pair_method;void *pair_ctx;
    frame_effect_sink feedback;void *feedback_ctx;
} frame_shatter_services;
/* Full41CF80/41C980/41CEF0, no sound. Actual motion/rotation/retirement, RNG,
 * blast propagation and class/list policy remain C. Unknown native methods stop. */
int frame_shatter_activate(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t other,
                              const frame_entity_factory_services *entities,const frame_shatter_services *services,uint32_t *out);
int frame_shatter_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                          const frame_entity_factory_services *entities,const frame_shatter_services *services,uint32_t *out);
int frame_shatter_animation(frame_data_read read,frame_data_write write,void *ctx,uint32_t animation);
/* Full443990 DATA initializer, shared with world-file construction: identity
 * copy, Y/X/Z rotations with observable intermediates, translation reset. */
int frame_matrix_initialize(frame_data_read read,frame_data_write write,void *ctx,uint32_t matrix,const uint32_t angles[3],const physics_trig *trig);
/*443830 initializes exactly nine words; the three trailing words stay owned
 * by the caller. NULL matrix is the original no-op. */
int frame_matrix_from_angles(frame_data_read read,frame_data_write write,void *ctx,uint32_t matrix,const uint32_t angles[3],const physics_trig *trig);
/*444120: row normalization followed by column normalization, preserving
 * intermediate DATA stores and division-fault prefixes. */
int frame_matrix_normalize(frame_data_read read,frame_data_write write,void *ctx,uint32_t matrix);
/*4414B0 direction orientation, with strict32768 reduction and original
 * identity/Y/X intermediate DATA publication. */
int frame_matrix_direction(frame_data_read read,frame_data_write write,void *ctx,uint32_t matrix,const int32_t direction[3],const physics_trig *trig);
/*443B00/C00/D10, including observable unshifted intermediate writes. */
int frame_matrix_rotate(frame_data_read read,frame_data_write write,void *ctx,uint32_t matrix,uint32_t axis,uint32_t angle,const physics_trig *trig);
/* Full438540 player impulse and41F730 health marker (within the former), with
 * named DI51DA10 output. AI wrapper403DE0 lives in frame_ai and the shared
 * native registry; this module retains the common Tank response. */
int frame_shatter_player_impulse(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t source,uint32_t mode,
                                    frame_effect_sink sink,void *sink_ctx);
int frame_shatter_player_impulse_values(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const int32_t source[3],uint32_t mode,frame_effect_sink sink,void *sink_ctx);

typedef struct frame_shatter_native_context { frame_object_native_context base;frame_shatter_services shatter; } frame_shatter_native_context;
int frame_shatter_native(void *ctx,uint32_t target,uint32_t entity,uint32_t has_payload,uint32_t payload,uint32_t *out);
int frame_shatter_pair_native(void *ctx,uint32_t target,uint32_t entity,uint32_t payload,uint32_t second,uint32_t *out);
#endif
