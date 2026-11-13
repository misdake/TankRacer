#ifndef FRAME_BOOTSTRAP_H
#define FRAME_BOOTSTRAP_H
#include "frame_classes.h"
#include "frame_object_parameters.h"
/*40DAC0: case-insensitive map identity and its position in the selected cup.
 * Missing map/cup entries retain FFFFFFFF; mode and cup are declared DATA. */
int frame_bootstrap_level_select(frame_data_read,frame_data_write,void *,const unsigned char *,uint32_t);
/*438D60 environment materials/styles and36 packed particles (180 LCG draws);
 *42C550 lightning and4427C0 route-presentation resources. */
int frame_bootstrap_environment(frame_data_read,frame_data_write,void *,uint32_t entry,frame_object_material_name,void *);
/*426E30 projectile scratch pools/atlas,42AC70 Spin scratch slots,407830
 *Smoker,415F60 Mover and430720 Tank light descriptors. */
int frame_bootstrap_transients(frame_data_read,frame_data_write,void *,uint32_t entry,frame_object_material_name,void *);
/*40AB60 /413A40 /404C10: level-specific signs, swamp light and umbrella. */
int frame_bootstrap_level_shapes(frame_data_read,frame_data_write,void *,uint32_t,const frame_entity_factory_services *,const frame_shape_resources *);
/*500F60 after a host file mount: ten background tile material/UV/color records.
 * Optional missing files still clear material handles as in the original. */
int frame_bootstrap_background_span(frame_data_read,frame_data_write,void *,const unsigned char *,uint32_t,uint32_t,const frame_object_parameters_services *);
/*40DA10 after the host mounts INGAME.SS: real shared models, ownership/style
 * fields, shield/flame geometry and homing resources. Mount/logging are I/O. */
int frame_bootstrap_ingame_shapes(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *entities,const frame_object_parameters_services *resources);
/* Install declared static class/parser-name strings and initial table/counters.
 * No PE/dump is loaded. Game pool storage remains caller-owned explicit input. */
int frame_bootstrap_static_data(frame_data_write write,void *ctx);
/* Named immutable inputs used by the modern World/TrWorld record path:
 * identity transform, sortpos/solidity keywords, shared Smoker sequence and
 * default visibility distance. These are constants, not captured scene state. */
int frame_bootstrap_world_constants(frame_data_write write,void *ctx);
/* Typed original image defaults for suspension/impact/drive/gear/body tables.
 * Later config-file overrides remain game rules, not baked snapshot values. */
int frame_bootstrap_vehicle_constants(frame_data_write write,void *ctx);
/* External C-locale classification input for text readers. The caller owns
 * the512-byte table; only the two masks used by integer parsing are populated. */
int frame_bootstrap_c_locale(frame_data_write write,void *ctx,uint32_t table);
/* Full4F4400/42C580/5044B0/4260D0 registration:37 descriptors,178 bindings,
 *13 lists and constant class/global defaults. Binds identities, not yet all
 * native method bodies. Unknown game methods still stop when invoked. */
int frame_bootstrap_classes(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *entities,uint32_t *out);
#endif
