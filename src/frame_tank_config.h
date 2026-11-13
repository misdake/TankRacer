#ifndef FRAME_TANK_CONFIG_H
#define FRAME_TANK_CONFIG_H
#include "frame_data.h"
/* Two local override words in435840 are undefined if their fields are absent
 * in battle mode2. Supply ambient inputs explicitly; never use saved outputs. */
typedef struct frame_tank_config_ambient { uint32_t max_turn_battle,traction_limit_battle; } frame_tank_config_ambient;
/*435840 from an opened immutable file span. All57 keys, duplicate/casefold
 * policy, widths, node-name/offset fields, special/mode overrides and DD2
 * capture remain C. OS open/close is caller-owned. Unknown keys return-4;
 * malformed names/high bytes-5, work limit-3. No original code callback. */
int frame_tank_configure_span(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,
                                  const unsigned char *bytes,uint32_t length,uint32_t work_limit,
                                  const frame_tank_config_ambient *ambient,uint32_t *consumed);
/*4357F0/4390F0 post-load derived suspension extents and particle-pool reset. */
int frame_tank_configure_derived(frame_data_read read,frame_data_write write,void *ctx,uint32_t object);
int frame_tank_effects_initialize(frame_data_read read,frame_data_write write,void *ctx,uint32_t object);
#endif
