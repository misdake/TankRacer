#ifndef PHYSICS_SAMPLING_H
#define PHYSICS_SAMPLING_H
#include "physics_terrain.h"
#include "physics_geometry.h"
#include "physics_suspension.h"

typedef struct physics_primary_point {
    int32_t local[3], world[3];
    int32_t kind, gap, previous_gap, raw_height, travel;
    uint32_t flags;
    int32_t normal[3]; /* +154: changed only by successful 501A50 fallback */
} physics_primary_point;
typedef struct physics_track_pose { int32_t matrix[9], origin[3]; } physics_track_pose;
typedef struct physics_primary_state {
    physics_primary_point point[4];
    int32_t load, airborne_count, special_count, flagged_count, has_extra, extra[4];
    /* Caller-local arrays/count retained for the subsequent material/fallback phase. */
    int32_t surface[4], flagged_ground_count;
    uint32_t node[4];
} physics_primary_state;
typedef struct physics_primary_environment {
    physics_track_pose track[2];
    uint32_t root, surface_table;
    uint16_t noise[16];
    int32_t force_surface, effect_active;
    uint32_t effect_multiplier, effect_shift;
    int32_t load_base, load_floor;
    uint32_t load_multiplier, load_shift;
    /* Resolved 4073D0 object, or zero. */
    uint32_t deformable;
} physics_primary_environment;
/* 42E920..42ED18: four transformed primary samples, surface-height disturbance,
 * contact metadata and load. All geometry is independently queried through DATA.
 * The read-only wrapper rejects required writes. Use mutable for deformer state. */
int physics_primary_sample(physics_world_read read,void *ctx,
                           physics_primary_state *state,const physics_primary_environment *env);
int physics_primary_sample_mutable(physics_world_read read,physics_world_write write,void *ctx,
                                   physics_primary_state *state,const physics_primary_environment *env);

typedef struct physics_surface_effect {
    int32_t timer, restore, active, phase, impulse0, impulse2, pose;
    uint32_t seed;
    int32_t clear_player_slot; /* notification; caller owns the external player table */
} physics_surface_effect;
/* 42ED18..42EDE7: active temporary roughness state, distinct from pure spatial
 * noise lookup. timer is signed16, dt low16, arithmetic otherwise wraps32. */
void physics_surface_effect_step(physics_surface_effect *state,int32_t dt,uint32_t frame);

typedef struct physics_sampling_state {
    physics_primary_state primary;
    physics_surface_effect effect;
    physics_auxiliary_contact auxiliary[2];
    int32_t materials[2], fallback_mode, feedback_required;
} physics_sampling_state;
typedef struct physics_sampling_environment {
    physics_primary_environment primary;
    int32_t auxiliary_local[2][3], dt;
    uint32_t frame, player;
} physics_sampling_environment;
/* 42E920 contact core: primary/deformer, temporary state, lazy material samples,
 * fallback normal. DirectInput 51DB70 is an explicit feedback notification;
 * device state/COM is outside this geometry model and is not silently stubbed. */
int physics_contact_sample(physics_world_read read,physics_world_write write,void *ctx,
                           physics_sampling_state *state,const physics_sampling_environment *env);
/* Full geometry -> material/fallback -> layout -> four-point response chain.
 * Secondary suspension samples resolve lazily at their real cache-consumption
 * point. No classifications from native post-state are supplied to this API. */
int physics_contact_pipeline(physics_world_read read,physics_world_write write,void *ctx,
                             physics_sampling_state *sampling,physics_suspension_state *suspension,
                             int32_t pivot[3],const physics_sampling_environment *env,
                             const physics_contact_config *config,
                             const physics_suspension_environment *suspension_env,
                             const int32_t layout_config[5]);
#endif
