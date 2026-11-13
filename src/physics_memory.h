#ifndef PHYSICS_MEMORY_H
#define PHYSICS_MEMORY_H
#include "physics_geometry.h"

/* Little-endian, checked DATA access shared by independent vehicle stages.
 * These are values/addresses, not an instruction or guest-register emulator. */
typedef struct physics_memory { physics_world_read read;physics_world_write write;void *ctx;int error; } physics_memory;
static inline int32_t pm_signed(uint32_t x) { return x<=INT32_MAX ? (int32_t)x : (int32_t)((int64_t)x-INT64_C(4294967296)); }
static inline int32_t pm_add(int32_t a,int32_t b) { return pm_signed((uint32_t)a+(uint32_t)b); }
static inline int32_t pm_sub(int32_t a,int32_t b) { return pm_signed((uint32_t)a-(uint32_t)b); }
static inline int32_t pm_mul(int32_t a,int32_t b) { return pm_signed((uint32_t)a*(uint32_t)b); }
static inline int32_t pm_neg(int32_t a) { return pm_sub(0,a); }
static inline int32_t pm_abs(int32_t a) { return a<0 ? pm_neg(a) : a; }
static inline int32_t pm_sar(int32_t a,unsigned n) { uint32_t x=(uint32_t)a;n&=31u;return pm_signed(a<0 ? ~(~x>>n) : x>>n); }
static inline int32_t pm_trunc(int32_t a,unsigned n) { return a<0 ? pm_neg(pm_sar(pm_neg(a),n)) : pm_sar(a,n); }
static inline uint32_t pm_load(physics_memory *m,uint32_t a,unsigned n)
{
    unsigned char bytes[4];unsigned i;uint32_t v=0;
    if (m->error) return 0;
    if (!m->read(m->ctx,a,bytes,n)) { m->error=-1;return 0; }
    for (i=0;i<n;++i) v|=(uint32_t)bytes[i]<<(i*8u);
    return v;
}
static inline int32_t pm_i32(physics_memory *m,uint32_t a) { return pm_signed(pm_load(m,a,4)); }
static inline int32_t pm_i16(physics_memory *m,uint32_t a) { uint32_t v=pm_load(m,a,2);return v>32767u ? (int32_t)v-65536 : (int32_t)v; }
static inline int32_t pm_i8(physics_memory *m,uint32_t a) { uint32_t v=pm_load(m,a,1);return v>127u ? (int32_t)v-256 : (int32_t)v; }
static inline void pm_store(physics_memory *m,uint32_t a,uint32_t value,unsigned n)
{
    unsigned char bytes[4];unsigned i;
    if (m->error) return;
    for (i=0;i<n;++i) bytes[i]=(unsigned char)(value>>(i*8u));
    if (!m->write || !m->write(m->ctx,a,bytes,n)) m->error=-1;
}
static inline void pm_write32(physics_memory *m,uint32_t a,int32_t value) { pm_store(m,a,(uint32_t)value,4); }
static inline int32_t pm_rate_step(int32_t value,int32_t force,int32_t dt)
{
    int32_t amount=pm_mul(force,dt);
    return value>amount ? pm_sub(value,amount) : value<pm_neg(amount) ? pm_add(value,amount) : 0;
}
static inline int32_t pm_decay(int32_t value,uint32_t packed,int32_t dt)
{
    int32_t result=pm_sub(value,pm_sar(pm_mul(value,dt),packed>>24));
    return (value<0 && result>0) || (value>0 && result<0) ? 0 : result;
}
static inline int32_t pm_damp(int32_t value,uint32_t packed,int32_t dt)
{
    uint32_t low=packed&65535u;int32_t force=low>32767u ? (int32_t)low-65536 : (int32_t)low;
    return pm_decay(pm_rate_step(value,force,dt),packed,dt);
}
#endif
