#ifndef FRAME_TEXTURE_H
#define FRAME_TEXTURE_H
#include "frame_entity_factory.h"
enum { FRAME_TEXTURE_PALETTE_CREATE=1,FRAME_TEXTURE_CREATE=2,FRAME_TEXTURE_PALETTE_RELEASE=3,
       FRAME_TEXTURE_RELEASE=4,FRAME_TEXTURE_PALETTE_SET=5,FRAME_TEXTURE_PALETTE_UPLOAD=6 };
/* Graphics backend only. Operands name caller-owned logical DATA; no COM or
 * original function pointer. Return status; result is original success value. */
typedef int (*frame_texture_device)(void *ctx,uint32_t operation,uint32_t object,uint32_t start,uint32_t count,uint32_t *result);
/* Registered native image reload rules (509590/507DC0), not system callbacks.
 * Unknown identity must return-5; no all-success reload placeholder. */
typedef int (*frame_texture_reload)(void *ctx,uint32_t identity,uint32_t descriptor,uint32_t user_data,uint32_t *result);
typedef struct frame_texture_services {
    frame_texture_device device;void *device_ctx;frame_texture_reload reload;void *reload_ctx;
} frame_texture_services;
typedef struct frame_texture_parameters { uint32_t flags,width,height,mip_levels,scale,reload,user_data; } frame_texture_parameters;
/* Full454FE0/4551C0 descriptor/palette/list lifetime and cleanup paths. */
int frame_texture_create(frame_data_read read,frame_data_write write,void *ctx,const frame_texture_parameters *parameters,
                             const frame_entity_factory_services *entities,const frame_texture_services *services,uint32_t *out);
int frame_texture_destroy(frame_data_read read,frame_data_write write,void *ctx,uint32_t descriptor,
                              const frame_entity_factory_services *entities,const frame_texture_services *services);
/* Full454E00: shared palette copy-on-write, range diagnostic, alias-aware
 * DWORD copy and two distinct backend notifications. */
int frame_texture_palette(frame_data_read read,frame_data_write write,void *ctx,uint32_t descriptor,uint32_t start,uint32_t count,uint32_t colors,
                              const frame_entity_factory_services *entities,const frame_texture_services *services);
#endif
