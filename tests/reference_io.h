/* Little-endian, field-by-field oracle protocol; no host-struct ABI dependency. */
#ifndef TEST_REFERENCE_IO_H
#define TEST_REFERENCE_IO_H
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
static inline void oracle_binary(void)
{
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
}
static inline uint32_t oracle_get(const unsigned char *p,unsigned n)
{
    uint32_t value=0;unsigned i;for (i=0;i<n;i++) value|=(uint32_t)p[i]<<(i*8u);return value;
}
static inline void oracle_set(unsigned char *p,uint32_t value,unsigned n)
{unsigned i;for (i=0;i<n;i++) p[i]=(unsigned char)(value>>(i*8u));}
static inline int oracle_word(uint32_t *out)
{
    unsigned char b[4];if (fread(b,1,4,stdin)!=4) return 0;*out=oracle_get(b,4);return 1;
}
static inline uint32_t oracle_require(void)
{uint32_t value;if (!oracle_word(&value)) exit(3);return value;}
static inline void oracle_put(uint32_t value)
{unsigned char b[4];oracle_set(b,value,4);if (fwrite(b,1,4,stdout)!=4) exit(4);}
static inline int32_t oracle_signed(uint32_t value)
{return value<=INT32_MAX ? (int32_t)value : (int32_t)((int64_t)value-INT64_C(4294967296));}
#endif
