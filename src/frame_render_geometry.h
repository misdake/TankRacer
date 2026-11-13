#ifndef FRAME_RENDER_GEOMETRY_H
#define FRAME_RENDER_GEOMETRY_H
#include "frame_data.h"
/* The original renderer clips camera Q12 positions, quarter-pixel screen
 * coordinates and unsigned Q16 UVs. It is a polygon clipper, not a BSP tree. */
typedef struct frame_render_vertex { int32_t xyz[3],screen[2];uint32_t uv[2]; } frame_render_vertex;
typedef struct frame_render_clip_settings {
    int32_t near_z,far_z,focal,center[2],bounds[4],side[4][3];
} frame_render_clip_settings;
void frame_render_clip_initialize(frame_render_clip_settings *,uint32_t width,uint32_t height,int32_t near_z,int32_t far_z);
/*4440D0/444090/443A90 and4F8A50: all products wrap to32 bits,
 * matrix products divide toward zero; positions stay Q12 until projection. */
void frame_render_transform(const int32_t point[3],const int32_t combined[9],const int32_t translation[3],int32_t out[3]);
/* near -> optional world half-plane transformed to camera -> horizontal ->
 * vertical; original far rule rejects a whole face with any far vertex.
 * Returns a bounded convex polygon, or a negative error. */
int frame_render_clip_triangle(const frame_render_clip_settings *,const frame_render_vertex in[3],const int32_t plane[4],frame_render_vertex out[16]);
int32_t frame_render_plane_distance(const int32_t xyz[3],const int32_t plane[4]);
void frame_render_damage_uv(const int32_t point[3],uint32_t face_sides,uint32_t quantized,uint32_t uv[2]);
void frame_render_cloud_uv(const int32_t camera_q12[3],const int32_t view[9],const int32_t origin[3],uint32_t uv[2]);
#endif
