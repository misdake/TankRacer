#ifndef FRAME_SURFACE_H
#define FRAME_SURFACE_H
#include <stdint.h>
typedef struct frame_surface_info {
    uint32_t version,width,height,mip_levels,scale,format,kind,bits,compression,pixel_offset,flags;
} frame_surface_info;
typedef struct frame_surface_plan {
    uint32_t flags,width,height,mip_levels,scale,skip,alias_offset;
} frame_surface_plan;
/*508F10 metadata policy after509220. min_width=5329D4, trim=55FEE0,
 * requested_skip=53CC74. Original WORD wrap and x86 shift masks retained.
 * out unchanged on rejected small image(1), invalid args(-1), budget(-3). */
int frame_surface_prepare(const frame_surface_info *info,uint32_t flags,uint32_t min_width,uint32_t trim,uint32_t requested_skip,uint32_t work_limit,frame_surface_plan *out);
/*509220 v2/3 selection. override is536F74 (-1 uses device preference);
 * preference is explicit446660/446670 device capability input, never guessed.
 * Returns0 valid,1 invalid/unsupported file,-1 bad API args,-4 truncated bytes.
 * version1 is a separate legacy format, explicitly-5 until implemented. */
int frame_surface_open(const unsigned char *bytes,uint32_t length,uint32_t flags,int32_t override,int32_t preference,frame_surface_info *out);
/*508DD0 PCX packet kernel: C0 is a literal, C1..FF introduce run/value.
 * count0 still consumes one packet, matching original do/while. stride may4.
 * Out and consumed publish only on success; buffer contains valid prefix on
 * short source/capacity error. capacity is in bytes including strided writes. */
int frame_surface_rle(const unsigned char *bytes,uint32_t length,unsigned char *out,uint32_t capacity,uint32_t count,uint32_t stride,uint32_t *consumed);
/* Decode509D30 storage forms (indexed8 or planar RGBA32), compression0/1/2.
 * Caller supplies working output for one selected mip. Palette is BGRA256;
 * index0 alpha0, others255, preserving5095A0's original palette rule.
 * This produces source pixels; GPU conversion/upload is outside this API. */
int frame_surface_decode(const unsigned char *bytes,uint32_t length,const frame_surface_info *info,uint32_t mip,
                             unsigned char *pixels,uint32_t capacity,unsigned char *palette,uint32_t palette_capacity,uint32_t *consumed);
/*4556D0/455710 RGB565 and455810/455860/4558B0 ARGB4444 conversion.
 * This is the display device format, independent of indexed/BGRA source kind.
 * flags low0=opaque,1=colour key,2=alpha. argb4444 is an explicit device input. */
uint32_t frame_surface_pixel16(const unsigned char bgra[4],uint32_t flags,uint32_t argb4444);
/* Native web device profile: preferARGB4444 for flags2, RGB565 otherwise.
 * Convert a decoded mip (BGRA or indexed+BGRA palette) to normalized GPU RGBA.
 * Pixel count is explicit, so mip selection remains the caller's responsibility.
 * has_alpha describes the uploaded device format, not the SUR source kind. */
int frame_surface_upload_rgba(const frame_surface_info *,uint32_t flags,uint32_t count,
                             const unsigned char *pixels,uint32_t pixel_bytes,const unsigned char *palette,uint32_t palette_bytes,
                             unsigned char *rgba,uint32_t rgba_bytes,uint32_t *has_alpha);
#endif
