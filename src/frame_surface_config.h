#ifndef FRAME_SURFACE_CONFIG_H
#define FRAME_SURFACE_CONFIG_H
#include "frame_data.h"
/* Complete42FA30/42FDB0 from immutable SURFS.DAT bytes. Ten records, ten
 * factors per record, packed WORD multiplier/WORD shift, duplicate last wins,
 * then copy track into default surface0. Unknown names are invalid DATA (-4)
 * rather than the original's unbounded stack-table indexing. */
int frame_surface_configure_span(frame_data_read read,frame_data_write write,void *ctx,const unsigned char *bytes,uint32_t length,uint32_t work_limit,uint32_t *consumed);
#endif
