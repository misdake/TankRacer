#ifndef FRAME_FILE_H
#define FRAME_FILE_H
#include <stdint.h>
#include "frame_data.h"
/* Immutable file-byte stream; OS handles/stdio remain outside the library.
 * This is the text portion of507490/507500/5074B0/507770/507400. Comments,
 * quoting, backslash pairs and unget are game file syntax, not host policy. */
typedef struct frame_file { const unsigned char *bytes;uint32_t length,cursor,work_limit; } frame_file;
int frame_file_token(frame_file *file,unsigned char *token,uint32_t capacity,uint32_t *present);
int frame_file_integer(frame_file *file,uint32_t *value,uint32_t *present);
/*507970/5078C0: comment-aware names/keywords, consuming the terminating
 * whitespace. word!=0 requires an ASCII alphanumeric first byte. A full
 * capacity without a delimiter returns-3 instead of exposing an unterminated
 * original caller buffer. EOF after a nonterminated word returns present=0. */
int frame_file_name(frame_file *file,unsigned char *token,uint32_t capacity,uint32_t word,uint32_t *present);
/*5075C0: C-locale binary64 decimal conversion rounded to binary32. Failed
 * conversion leaves *bits unchanged. consumed bytes follow scanf's number. */
int frame_file_real32(frame_file *file,uint32_t *bits,uint32_t *present);
/* Same token stores directly into DATA, preserving initial NUL, escaped
 * character overwrite and final NUL. No emulated local stack is needed. */
int frame_file_token_data(frame_file *file,frame_data_write write,void *ctx,uint32_t destination,uint32_t capacity,uint32_t *present);
/*507A30: game comment skip, then standard %d%d%d conversion. A failed
 * conversion preserves its destination and later fields. Values wrap32. */
int frame_file_vector_data(frame_file *file,frame_data_write write,void *ctx,uint32_t destination,uint32_t *assigned);
#endif
