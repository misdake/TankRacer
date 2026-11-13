/* External DATA interface for readable game-rule modules.
 *
 * Native composition can map these logical uint32 addresses to owned RAM.
 * Differential tools map the same requests to captured input data. This API
 * has no CPU registers, instruction dispatch, system calls or OS handles.
 * Return 1 on a complete access, 0 on an unavailable/invalid span. */
#ifndef FRAME_DATA_H
#define FRAME_DATA_H
#include <stdint.h>
typedef int (*frame_data_read)(void *ctx,uint32_t address,void *dst,uint32_t size);
typedef int (*frame_data_write)(void *ctx,uint32_t address,const void *src,uint32_t size);
#endif
