#ifndef FRAME_MATERIAL_H
#define FRAME_MATERIAL_H
#include "frame_shape.h"
/* Explicit native game dependency508F10, NOT a system/I/O success stub.
 * Supplies a real texture descriptor or0. Missing implementation returns-5. */
typedef int (*frame_material_texture_load)(void *ctx,uint32_t name_space,const unsigned char *name,uint32_t name_bytes,uint32_t flags,uint32_t *out);
typedef struct frame_material_services { frame_material_texture_load texture;void *ctx; } frame_material_services;
/* Full501290/503AD0 cache/list/namespace policy. Caller-owned bounded native
 * token has no emulated stack address. Names exceeding original locals fail. */
int frame_material_resolve(frame_data_read read,frame_data_write write,void *ctx,const unsigned char *name,uint32_t name_bytes,uint32_t mode,
                               const frame_entity_factory_services *entities,const frame_material_services *services,uint32_t *out);
#endif
