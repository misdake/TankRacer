#ifndef FRAME_TEXT_H
#define FRAME_TEXT_H
#include "frame_data.h"
/* 504F60/5050C0. Parser DATA: signed length, signed cursor, bytes at+8.
 * Token output is caller-owned native memory, not an emulated stack. The
 * original return is 'cursor < length', NOT 'a token was produced': the last
 * token can be present when more==0. Spaces/tabs only; parentheses delimit;
 * quoted strings still stop at tabs/parentheses. Bound exhaustion returns-3. */
int frame_text_next(frame_data_read read,frame_data_write write,void *ctx,uint32_t parser,
                        unsigned char *token,uint32_t capacity,uint32_t work_limit,uint32_t *more);
/* Same token into DATA, retaining each character/NUL write and aliases. */
int frame_text_next_data(frame_data_read read,frame_data_write write,void *ctx,uint32_t parser,
                             uint32_t destination,uint32_t capacity,uint32_t work_limit,uint32_t *more);
/* Full 5050D0: reset cursor and exact case-sensitive token search; failure
 * resets cursor again. Name is a DATA C string, bounded at1024 bytes. */
int frame_text_find(frame_data_read read,frame_data_write write,void *ctx,uint32_t parser,
                        uint32_t name,uint32_t work_limit,uint32_t *found);
/* Same case-sensitive search using a bounded, NUL-terminated native name. */
int frame_text_find_name(frame_data_read read,frame_data_write write,void *ctx,uint32_t parser,
                             const unsigned char *name,uint32_t bytes,uint32_t work_limit,uint32_t *found);
/* Full 504EE0: consume token, bool from first t/T/f/F byte or wrapping signed
 * decimal via the captured single-byte CRT classification table. Locale>=2
 * needs an explicit implementation and returns-5, never guessed as ASCII.
 * If more==0 the destination is not written, even for a complete final token. */
int frame_text_read_i32(frame_data_read read,frame_data_write write,void *ctx,uint32_t parser,
                            uint32_t destination,uint32_t work_limit,uint32_t *more);
/* Identical reader with a native value destination. On more==0 leaves *value
 * unchanged, allowing a constructor to track a reused temporary explicitly. */
int frame_text_read_value(frame_data_read read,frame_data_write write,void *ctx,uint32_t parser,
                              uint32_t work_limit,uint32_t *value,uint32_t *more);
/* Full441700: defaults(1,0), consume two integers, then write low16 pair. */
int frame_text_read_pair(frame_data_read read,frame_data_write write,void *ctx,uint32_t parser,uint32_t destination,uint32_t work_limit);
/* 505080 decimal reader: old CRT binary64 -> binary32; outputs unchanged on
 * EOF. bits is the IEEE754 payload, not a host float ABI. C locale is required. */
int frame_text_read_f32_value(frame_data_read read,frame_data_write write,void *ctx,uint32_t parser,
                                  uint32_t work_limit,uint32_t *bits,uint32_t *more);
int frame_text_read_f32(frame_data_read read,frame_data_write write,void *ctx,uint32_t parser,
                            uint32_t destination,uint32_t work_limit,uint32_t *more);
/* Exact binary32 *1024, x87 signed64 truncation/indefinite then low32. */
uint32_t frame_text_fixed1024(uint32_t bits);
/* Same signed64 truncation before any scale. */
uint32_t frame_text_truncate_f32(uint32_t bits);
#endif
