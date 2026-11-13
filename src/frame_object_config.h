#ifndef FRAME_OBJECT_CONFIG_H
#define FRAME_OBJECT_CONFIG_H
#include "frame_entity_factory.h"
/* Complete423820 OBPARAMS two-pass loading in the C-locale/no-audio scope:
 * class coefficients and conversions, actual zeroed76-byte object templates,
 * boolean flags, sound-name metadata and table globals. FILE bytes/allocator
 * are external; no original method or parsed output is supplied by the host. */
int frame_object_configure_span(frame_data_read read,frame_data_write write,void *ctx,const unsigned char *bytes,uint32_t length,const frame_entity_factory_services *entities,uint32_t *consumed);
#endif
