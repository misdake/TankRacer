#ifndef FRAME_SPATIAL_H
#define FRAME_SPATIAL_H
#include "frame_pool.h"
/*41C350: derive the object-grid origin/shift from the relocated BSM terrain
 * root and create all64 real list heads. Allocation failure stays observable. */
int frame_spatial_initialize(frame_data_read read,frame_data_write write,void *ctx,
                              frame_pool_system_alloc allocate,frame_pool_system_free release,
                              void *system_ctx,uint32_t work_limit);
/* Full 41C3F0/41C590: class-independent 8x8 object membership. Returned
 * coordinate packs clamped X in high16 and Z in low16, or FFFFFFFF for no object.
 * Intersections are strict, radius comes from shape DATA; insertion prepends. */
int frame_spatial_insert(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                           frame_pool_system_alloc allocate,frame_pool_system_free release,
                           void *system_ctx,uint32_t work_limit,uint32_t *packed);
int frame_spatial_remove(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t packed,
                           frame_pool_system_free release,void *release_ctx,uint32_t work_limit);
/* Full 422010, including unconditional 503C90 LCG advancement. */
int frame_object_variant(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,uint32_t work_limit);
/* Full 422B50 reset/restore of the special class: pose/fields, list and grid
 * removal, then deterministic variant selection. No original-code callback. */
int frame_object_respawn(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                           frame_pool_system_free release,void *release_ctx,uint32_t work_limit);
/* Full 40AB90 ordinary-object restoration and spatial reinsertion. */
int frame_object_restore(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                           frame_pool_system_alloc allocate,frame_pool_system_free release,
                           void *system_ctx,uint32_t work_limit);
#endif
