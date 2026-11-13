#ifndef FRAME_PRESENT_H
#define FRAME_PRESENT_H
#include "frame_data.h"
/* Read the current frame's private presentation overlay, for HUD consumers. */
int frame_present_read(void *workspace,uint32_t address,void *out,uint32_t bytes);
#include "frame_commands.h"
typedef struct frame_present_fog_settings { uint32_t enabled,near_distance,far_distance,rgba; } frame_present_fog_settings;
typedef struct frame_present_resources { int (*texture_alpha)(void *,uint32_t);void *ctx;const frame_present_fog_settings *fog; } frame_present_resources;
/*4F7810: packet Z is camera distance*8, unsigned interpolation and byte
 * truncation match the original software vertex fog, independent of alpha. */
uint32_t frame_present_vertex_fog(uint32_t specular,uint32_t depth,uint32_t near_distance,uint32_t far_distance,uint32_t minimum);
typedef struct frame_present_stats { uint32_t meshes,triangles,clipped; } frame_present_stats;
/* Read-only presentation of the live, relocated C scene. Uses published native
 * camera/poses and actual model/terrain triangles, original painter buckets
 * and D3D blend rules, fixed-point mesh clipping, fog and material layers. No game
 * write callback exists: drawing cannot advance RNG or mutate physics. */
size_t frame_present_workspace_size(void);
/* Zero once before first use and after a level reload. The workspace retains
 * visual track travel and shared shield flash colour; no game timers/poses. */
int frame_present_workspace_initialize(void *workspace,size_t workspace_bytes);
/* Original5551 diffuse expansion (7BDE is neutral textured white), and
 *4F84B0 billboard local positions using the caller's4096-unit right axis. */
uint32_t frame_present_quad_colour(uint32_t r,uint32_t g,uint32_t b,uint32_t clipped,uint32_t textured);
uint32_t frame_present_quad_mode(uint32_t style,uint32_t clipped,uint32_t additive_capability,uint32_t texture_flags);
uint32_t frame_present_colour(uint32_t packed,uint32_t textured);
/*4F7350/4F8A50: per-object shade halves each expanded RGB byte, keeps alpha. */
uint32_t frame_present_mesh_colour(uint32_t packed,uint32_t textured,uint32_t shaded);
/*4FA370 saturates textured5551 channels at15; the fast mesh path wraps.
 * The arbitrary-plane4FBD70 path retains the fast path's byte expansion. */
uint32_t frame_present_mesh_clipped_colour(uint32_t packed,uint32_t textured,uint32_t shaded);
/*4FD120: per-vertex normal dot products, Q16 UVs,32-bit wrapping math. */
void frame_present_layer_uv(const int32_t normal[3],const int32_t matrix[9],int32_t uv[2]);
/*4FB550: mode1 uses the model frame; mode2 uses4440D0/444090's
 *view-transpose/model product. Published view uses the game's Q12 layout. */
void frame_present_layer_matrix(const int32_t model[9],const int32_t view[9],uint32_t mode,int32_t out[9]);
/*4FB520/4FD9B0/4FB550:999-clamp((camera depth>>5)+signed bias,1,998).
 * The painter profile disables hardware Z and shares buckets across modes. */
uint32_t frame_present_bucket(int32_t camera_depth,int32_t bias);
/*4FB550/442ED0: local view octant selects one of eight FF-terminated
 * shape face-order lists; model triangles cannot use file storage order. */
uint32_t frame_present_octant(const int32_t object_minus_camera[3],const int32_t matrix[9]);
/*434B10 and5319F8/531AF8: local camera-minus-body/turret centers select
 * the original nine component submissions (body0 deliberately occurs twice).
 * This is separate from each shape's own eight face-order lists. */
void frame_present_vehicle_order(const int32_t body_view[3],const int32_t turret_view[3],uint32_t order[9]);
/*44C4C0/44BC30/44C240 preferred-device states. mode0/1 opaque/key,
 *2 explicit diffuse alpha,3 material alpha only,4 additive diffuse alpha,
 *5 enabled ONE/ZERO (some unclipped pool packets retain this original state). */
int frame_present_pipeline(frame_commands *,uint32_t texture_flags,uint32_t device_alpha,uint32_t mode);
void frame_present_billboard(int32_t points[9],int32_t center_x,int32_t center_z,uint32_t flags,int32_t right_x,int32_t right_z);
int frame_present_scene(frame_data_read,void *,frame_commands *,void *workspace,size_t workspace_bytes,frame_present_stats *);
int frame_present_scene_resources(frame_data_read,void *,frame_commands *,const frame_present_resources *,void *workspace,size_t workspace_bytes,frame_present_stats *);
#endif
