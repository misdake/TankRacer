#ifndef FRAME_RESULTS_H
#define FRAME_RESULTS_H
#include "frame_data.h"
/*40C790 default lap/race records;441780 route-animation random table. */
int frame_results_defaults(frame_data_write,void *);
int frame_results_route_random(frame_data_read,frame_data_write,void *);
/* Named immutable cup/score/vehicle-category inputs, plus new-profile values.
 * A loaded profile can override them. No PE or captured memory is required. */
int frame_results_constants(frame_data_write,void *);
/*40EED0/40EF80 sorted record insertion. mode0 total, mode1 lap. */
int frame_results_insert(frame_data_read,frame_data_write,void *,uint32_t mode,uint32_t time,uint32_t vehicle,uint32_t place,uint32_t cup,uint32_t map,uint32_t player);
/*40E390 no-opponent-AI single-player result path (modes0/1). Returns action1
 * to publish results and close the level,2 for the resident/replay reload path.
 * Rival result synthesis and split/network modes remain explicit-5. */
int frame_results_collect(frame_data_read,frame_data_write,void *,uint32_t *action);
/*40E320 /40E250 initials/unlock and six-slot record-name updates. */
int frame_results_initials(frame_data_read,frame_data_write,void *,uint32_t player);
/*411F50/411EF0 game state hidden inside HUD rendering. No drawing calls. */
int frame_results_lap_display(frame_data_read,frame_data_write,void *,uint32_t object);
int frame_results_rank_display(frame_data_read,frame_data_write,void *,uint32_t object,uint32_t *rank);
int frame_results_hud(frame_data_read,frame_data_write,void *,uint32_t slot);
#endif
