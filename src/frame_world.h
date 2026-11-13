#ifndef FRAME_WORLD_H
#define FRAME_WORLD_H
#include "frame_mesh_break.h"
/* External byte-stream read only. The callback writes at most requested bytes
 * to destination DATA, retaining a short read's untouched tail. Return0 for
 * completed I/O, negative for host fault. Game ignores ordinary short reads. */
typedef int (*frame_world_file_read)(void *ctx,uint32_t file,uint32_t destination,uint32_t requested,uint32_t *received);
/* A qualified native game method accepting a native string. This is for the
 * script/Unknown-class name interface, never a CPU or system callback. */
typedef int (*frame_world_name_dispatch)(void *ctx,uint32_t entity,uint32_t descriptor,const unsigned char *name,uint32_t bytes);
typedef struct frame_world_loader_services {
    frame_world_file_read read;void *read_ctx;
    frame_world_name_dispatch name;void *name_ctx;
} frame_world_loader_services;
/*504020 version>=5 global world record -> parameters and final native class
 * dispatch. Legacy versions use fscanf on a CRT FILE, currently return-5. */
int frame_world_object_read(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t legacy_record,
                                const frame_entity_factory_services *entities,const frame_object_parameters_services *resources,
                                uint32_t *out);
/*505430 modern object-file driver. Reads40000 into[5367A8], terminates at
 * case-sensitive Paths, resolves/reuses named reserved objects, and dispatches
 * actual native constructors/record readers. An optional list receives only
 * successful record results. Legacy FILE formats return-5 explicitly. */
int frame_world_load_objects(frame_data_read read,frame_data_write write,void *ctx,uint32_t file,uint32_t list,
                                 const frame_entity_factory_services *entities,const frame_world_loader_services *files,uint32_t *out);
/*505330 header validation and modern505430 from caller-owned actual file
 * bytes. Header version is written even if its magic is invalid, as original.
 * The OS open/close boundary is owned by the caller. consumed reports stream
 * bytes read (up to40000 after header), not the lexer cursor at Paths. */
int frame_world_load_span(frame_data_read read,frame_data_write write,void *ctx,uint32_t file,uint32_t list,
                              const unsigned char *bytes,uint32_t length,const frame_entity_factory_services *entities,
                              uint32_t *out,uint32_t *consumed);
#endif
