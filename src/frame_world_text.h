#ifndef FRAME_WORLD_TEXT_H
#define FRAME_WORLD_TEXT_H
#include "frame_data.h"
/* Global world lexer505190: buffer[55C6A0], cursor[55C6A4]. Only space,
 * CR/LF delimit tokens; tabs and parentheses are ordinary token bytes.
 * Missing delimiters/oversized original local buffers stop explicitly. */
int frame_world_token(frame_data_read read,frame_data_write write,void *ctx,unsigned char *out,uint32_t capacity,uint32_t work_limit);
int frame_world_token_data(frame_data_read read,frame_data_write write,void *ctx,uint32_t destination,uint32_t capacity,uint32_t work_limit);
/* Full505220 nesting/cursor/overflow semantics, including repeated NUL writes.
 * On original size failure returns success status with out=UINT32_MAX. */
int frame_world_group(frame_data_read read,frame_data_write write,void *ctx,uint32_t destination,uint32_t capacity,uint32_t work_limit,uint32_t *out);
/* Full5052D0 integer token conversion; no true/false syntax. */
int frame_world_integer(frame_data_read read,frame_data_write write,void *ctx,uint32_t work_limit,uint32_t *out);
/*505300 old CRT decimal -> binary64, C locale. No x87 ABI or private CRT DATA. */
int frame_world_decimal(frame_data_read read,frame_data_write write,void *ctx,uint32_t work_limit,double *out);
#endif
