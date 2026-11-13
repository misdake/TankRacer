#ifndef FRAME_TANK_MODEL_H
#define FRAME_TANK_MODEL_H
#include "frame_tank_config.h"
#include "frame_shape.h"
/*437550 and437340: track UV reference tables and body/turret quadrant
 * vertex selection. These initialize data consumed by later updates. */
int frame_tank_track_uv(frame_data_read read,frame_data_write write,void *ctx,uint32_t shape,uint32_t output,uint32_t work_limit);
int frame_tank_corner_vertices(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,uint32_t work_limit);
/*4350A0 complete load from opened config bytes:57-field rules, model cache/
 * owning copies, offsets, track UVs, hull/collision bounds, native pose call,
 * bottom vertices, shadow geometry and mode-specific coefficients. */
int frame_tank_model_load_span(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                                   const unsigned char *bytes,uint32_t length,const frame_entity_factory_services *entities,
                                   const frame_shape_resources *resources,const frame_tank_config_ambient *ambient,uint32_t *out);
/* Same method with DATA filename and external immutable-byte acquisition. */
int frame_tank_model_load(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t filename,
                              const frame_entity_factory_services *entities,const frame_shape_resources *resources,uint32_t *out);
#endif
