#ifndef FRAME_SHAPE_H
#define FRAME_SHAPE_H
#include "frame_entity_factory.h"
/* Loaded shape-cache lookup, the cache-hit portion of500850. ASCII casefold;
 * cache miss returns-5 (file load/shape decode must not be guessed). */
int frame_shape_lookup_cached(frame_data_read read,void *ctx,uint32_t name,uint32_t work_limit,uint32_t *out);
int frame_shape_lookup_name(frame_data_read read,void *ctx,const unsigned char *name,uint32_t name_bytes,uint32_t work_limit,uint32_t *out);
/* Full500D20 from its loaded-source result onward. Finds the last matching
 * cache record, allocates/relocates an independent copy and publishes original
 * size convention (512-byte rounding vs static sentinel999999). Fatal original
 * diagnostics return-4 at their entry, preserving their DATA prefix. */
int frame_shape_clone_loaded(frame_data_read read,frame_data_write write,void *ctx,uint32_t source,
                                uint32_t size_destination,const frame_entity_factory_services *services,uint32_t *out);
int frame_shape_copy_cached(frame_data_read read,frame_data_write write,void *ctx,uint32_t name,
                               uint32_t size_destination,const frame_entity_factory_services *services,uint32_t *out);
/* Same cached operation for a bounded native token; no synthetic stack DATA. */
int frame_shape_copy_name(frame_data_read read,frame_data_write write,void *ctx,const unsigned char *name,uint32_t name_bytes,
                             uint32_t size_destination,const frame_entity_factory_services *services,uint32_t *out);
/* Full500E90 resource-cache record/list removal and pool release. Sentinel
 *999999 suppresses underlying data release; cache flag4 also protects data. */
int frame_shape_release(frame_data_read read,frame_data_write write,void *ctx,uint32_t shape,uint32_t size_override,
                           const frame_entity_factory_services *services);
/* Full5009A0 pointer fixup. Material resolution is a native game service,
 * never arbitrary original code; nonzero material without a resolver is-5. */
typedef int (*frame_shape_material_resolver)(void *ctx,uint32_t name,uint32_t mode,uint32_t *out);
int frame_shape_relocate(frame_data_read read,frame_data_write write,void *ctx,uint32_t shape,
                            frame_shape_material_resolver resolver,void *resolver_ctx,uint32_t work_limit);
/* The file boundary supplies a complete immutable byte span, or found=0.
 * Namespace is the original archive/directory identity, not a game callback.
 * A successful span stays valid until this synchronous call returns. */
typedef int (*frame_shape_fetch)(void *ctx,uint32_t name_space,const unsigned char *name,uint32_t name_bytes,
                                   const unsigned char **bytes,uint32_t *length,uint32_t *found);
typedef struct frame_shape_resources {
    frame_shape_fetch fetch;void *file_ctx;
    frame_shape_material_resolver material;void *material_ctx;
} frame_shape_resources;
/* Full500850 game policy with file acquisition external: create cache, lookup,
 * choose namespace/override, allocate/read/cache and relocate. Missing file,
 * empty file or data OOM returns out=0; metadata/null-sentinel faults return-4
 * at their original fault prefix. No decoder or original-code fallback. */
int frame_shape_load(frame_data_read read,frame_data_write write,void *ctx,uint32_t name,uint32_t name_space,
                         const frame_entity_factory_services *services,const frame_shape_resources *resources,uint32_t *out);
int frame_shape_load_name(frame_data_read read,frame_data_write write,void *ctx,const unsigned char *name,uint32_t name_bytes,
                              uint32_t name_space,const frame_entity_factory_services *services,
                              const frame_shape_resources *resources,uint32_t *out);
/* Full500770 BSM linked meshes. Material mode2 and consecutive-offset reuse
 * span mesh boundaries (zero materials do not reset the cache). */
int frame_shape_relocate_world_meshes(frame_data_read read,frame_data_write write,void *ctx,uint32_t base,
                                         frame_shape_material_resolver resolver,void *resolver_ctx,uint32_t work_limit);
/* Full5013E0/501430/501470/5014A0: publish and relocate primary/secondary
 * collision-node lists and the two terrain bucket pointer arrays. */
int frame_shape_bind_world(frame_data_read read,frame_data_write write,void *ctx,uint32_t base,
                              uint32_t primary,uint32_t secondary,uint32_t terrain,uint32_t work_limit);
/* 500610 post-read game path. Caller mounts original BSM bytes at5DAB40;
 * version/size diagnostics, size output, meshes, collision lookup graphs and
 * static-arena cursor are C. File reading/debug timing are outside this entry. */
int frame_shape_prepare_world(frame_data_read read,frame_data_write write,void *ctx,uint32_t size_destination,
                                 frame_shape_material_resolver resolver,void *resolver_ctx,uint32_t work_limit,uint32_t *out);
#endif
