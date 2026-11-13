#ifndef FRAME_PLAYER_H
#define FRAME_PLAYER_H
#include "frame_player_lap.h"
#include "frame_explosion.h"
#include "physics_attitude.h"
/* Single-player player orchestration. Game methods are readable C callbacks;
 * only network and final fog are external. AI takeover remains a registered
 * dependency, never silently replaced when racers are disabled for phase1. */
int frame_player_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                         const frame_entity_factory_services *entities,const physics_trig *trig,
                         const frame_player_lap_services *lap,const frame_explosion_services *effects,
                         const uint32_t death_contact_words[25]);
/*42C840 +41AE20 clears actual owned light/line effects before death. */
int frame_player_begin_death(frame_data_read read,frame_data_write write,void *ctx,uint32_t object);
/*42C860 delayed five explosions. The original descriptor initializes only
 * normal/owner; remaining consumed ambient words must be declared explicitly. */
int frame_player_death_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                               const frame_entity_factory_services *entities,const frame_explosion_services *effects,
                               const uint32_t contact_words[25]);
/*420D8B..420EAA, complete environment mode transitions and fog cleanup. */
int frame_player_environment(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,uint32_t entity,
                                const frame_explosion_services *effects,uint32_t work_limit);
#endif
