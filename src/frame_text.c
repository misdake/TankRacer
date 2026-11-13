#include "frame_text.h"
#include "physics_memory.h"
#include <stdlib.h>
#include <string.h>
#include <float.h>

static int budget(uint32_t *used,uint32_t limit)
{ if (*used>=limit) return -3;(*used)++;return 0; }
static void token_byte(physics_memory *m,unsigned char *token,uint32_t destination,uint32_t index,uint32_t value,int data)
{ token[index]=(unsigned char)value;if (data) pm_store(m,destination+index,value,1); }
static int next_to(physics_memory *m,uint32_t parser,unsigned char *token,uint32_t capacity,
                       uint32_t *used,uint32_t limit,uint32_t *more,uint32_t destination,int data)
{
    uint32_t cursor=pm_load(m,parser+4u,4),c,quoted,n=0;
    do {
        if (budget(used,limit)) return -3;
        c=pm_load(m,parser+cursor+8u,1);cursor++;pm_store(m,parser+4u,cursor,4);
        if (c!=32u && c!=9u) break;
    } while (pm_signed(cursor)<pm_i32(m,parser));
    if (m->error) return m->error;
    if (c==40u || c==41u) {
        if (capacity<2u) return -3;
        token_byte(m,token,destination,0,c,data);token_byte(m,token,destination,1,0,data);
    } else {
        quoted=c==34u;
        if (!quoted) { if (capacity<2u) return -3;token_byte(m,token,destination,n++,c,data); }
        token_byte(m,token,destination,n,0,data);c=pm_load(m,parser+pm_load(m,parser+4u,4)+8u,1);
        while (c!=(quoted ? 34u : 32u) && c!=9u && c!=40u && c!=41u && pm_i32(m,parser+4u)<pm_i32(m,parser)) {
            if (m->error) return m->error;
            if (budget(used,limit) || n+1u>=capacity) return -3;
            token_byte(m,token,destination,n++,c,data);token_byte(m,token,destination,n,0,data);
            cursor=pm_load(m,parser+4u,4)+1u;pm_store(m,parser+4u,cursor,4);c=pm_load(m,parser+cursor+8u,1);
        }
        if (quoted && c==34u && pm_i32(m,parser+4u)<pm_i32(m,parser))
            pm_store(m,parser+4u,pm_load(m,parser+4u,4)+1u,4);
    }
    if (m->error) return m->error;
    c=pm_i32(m,parser+4u)<pm_i32(m,parser);if (m->error) return m->error;
    *more=c;return 0;
}
static int next(physics_memory *m,uint32_t parser,unsigned char *token,uint32_t capacity,uint32_t *used,uint32_t limit,uint32_t *more)
{ return next_to(m,parser,token,capacity,used,limit,more,0,0); }
int frame_text_next_data(frame_data_read read,frame_data_write write,void *ctx,uint32_t parser,
                             uint32_t destination,uint32_t capacity,uint32_t limit,uint32_t *more)
{
    physics_memory m={read,write,ctx,0};unsigned char token[1024];uint32_t used=0;
    if (!read || !write || !capacity || capacity>1024u || !limit || !more) return -1;
    return next_to(&m,parser,token,capacity,&used,limit,more,destination,1);
}
int frame_text_next(frame_data_read read,frame_data_write write,void *ctx,uint32_t parser,
                        unsigned char *token,uint32_t capacity,uint32_t limit,uint32_t *more)
{
    physics_memory m={read,write,ctx,0};uint32_t used=0;
    if (!read || !write || !token || !capacity || !limit || !more) return -1;
    return next(&m,parser,token,capacity,&used,limit,more);
}
int frame_text_find(frame_data_read read,frame_data_write write,void *ctx,uint32_t parser,
                        uint32_t name,uint32_t limit,uint32_t *found)
{
    physics_memory m={read,write,ctx,0};unsigned char token[1024];uint32_t used=0,more,i,c;int status,equal;
    if (!read || !write || !limit || !found) return -1;
    pm_store(&m,parser+4u,0,4);
    for (;;) {
        status=next(&m,parser,token,1024,&used,limit,&more);if (status) return status;
        equal=1;
        for (i=0;i<1024u;i++) {
            c=pm_load(&m,name+i,1);if (m.error) return m.error;
            if (c!=token[i]) { equal=0;break; }
            if (!c) break;
        }
        if (equal && more) { *found=1;return 0; }
        if (!more) { pm_store(&m,parser+4u,0,4);if (m.error) return m.error;*found=0;return 0; }
    }
}
int frame_text_find_name(frame_data_read read,frame_data_write write,void *ctx,uint32_t parser,
                             const unsigned char *name,uint32_t bytes,uint32_t limit,uint32_t *found)
{
    physics_memory m={read,write,ctx,0};unsigned char token[1024];uint32_t used=0,more,i;int status,equal;
    if (!read || !write || !name || !bytes || !limit || !found) return -1;
    pm_store(&m,parser+4u,0,4);
    for (;;) {
        status=next(&m,parser,token,1024,&used,limit,&more);if (status) return status;
        equal=1;
        for (i=0;i<1024u;i++) {
            if (i>=bytes) return -4;
            if (name[i]!=token[i]) { equal=0;break; }
            if (!name[i]) break;
        }
        if (equal && more) { *found=1;return 0; }
        if (!more) { pm_store(&m,parser+4u,0,4);if (m.error) return m.error;*found=0;return 0; }
    }
}
static uint32_t category(physics_memory *m,uint32_t c,uint32_t mask)
{
    if (pm_i32(m,0x538a44u)>=2) { m->error=-5;return 0; }
    return pm_load(m,pm_load(m,0x538838u,4)+c*2u,1)&mask;
}
int frame_text_read_value(frame_data_read read,frame_data_write write,void *ctx,uint32_t parser,
                              uint32_t limit,uint32_t *out,uint32_t *more)
{
    physics_memory m={read,write,ctx,0};unsigned char token[1024];uint32_t used=0,available,c,n=0,negative,value=0;int status;
    if (!read || !write || !limit || !out || !more) return -1;
    status=next(&m,parser,token,1024,&used,limit,&available);if (status) return status;
    if (available) {
        c=token[0];
        if (c==116u || c==84u) value=1;
        else if (c!=102u && c!=70u) {
            while (n<1024u && category(&m,token[n],8u)) n++;
            if (m.error) return m.error;
            if (n==1024u) return -3;
            c=token[n++];negative=c==45u;
            if (negative || c==43u) { if (n==1024u) return -3;c=token[n++]; }
            while (category(&m,c,4u)) {
                value=value*10u+c-48u;if (n==1024u) return -3;c=token[n++];
            }
            if (m.error) return m.error;
            if (negative) value=0u-value;
        }
    }
    if (m.error) return m.error;
    if (available) *out=value;
    *more=available;return 0;
}
int frame_text_read_i32(frame_data_read read,frame_data_write write,void *ctx,uint32_t parser,
                            uint32_t destination,uint32_t limit,uint32_t *more)
{
    physics_memory m={read,write,ctx,0};uint32_t value=0,available;int status;
    if (!more) return -1;
    status=frame_text_read_value(read,write,ctx,parser,limit,&value,&available);if (status) return status;
    if (available) pm_store(&m,destination,value,4);
    if (m.error) return m.error;
    *more=available;return 0;
}
int frame_text_read_pair(frame_data_read read,frame_data_write write,void *ctx,uint32_t parser,uint32_t destination,uint32_t limit)
{
    physics_memory m={read,write,ctx,0};uint32_t a=1,b=0,more;int status;
    status=frame_text_read_value(read,write,ctx,parser,limit,&a,&more);if (status) return status;
    status=frame_text_read_value(read,write,ctx,parser,limit,&b,&more);if (status) return status;
    pm_store(&m,destination,a,2);pm_store(&m,destination+2u,b,2);return m.error;
}
int frame_text_read_f32_value(frame_data_read read,frame_data_write write,void *ctx,uint32_t parser,
                                  uint32_t limit,uint32_t *bits,uint32_t *more)
{
    physics_memory m={read,write,ctx,0};unsigned char token[1024];char number[1024];uint32_t available,used=0,i=0,n=0,c;float value;int status;
    if (!read || !write || !bits || !more || !limit || sizeof(float)!=4u || FLT_MANT_DIG!=24 || DBL_MANT_DIG!=53) return -1;
    status=next(&m,parser,token,1024,&used,limit,&available);if (status) return status;
    if (available) {
        while (i<1024u && category(&m,token[i],8u)) i++;
        if (m.error) return m.error;
        if (i==1024u) return -3;
        c=token[i];if (c==43u || c==45u) { number[n++]=(char)c;i++; }
        /* Old CRT decimal grammar: no host hex/NaN extensions. A malformed
         * exponent is left to ISO C strtod's end-of-number handling. */
        while (i<1024u && token[i]>=48u && token[i]<=57u) number[n++]=(char)token[i++];
        if (i<1024u && token[i]==46u) {
            number[n++]=(char)token[i++];
            while (i<1024u && token[i]>=48u && token[i]<=57u) number[n++]=(char)token[i++];
        }
        if (i<1024u && (token[i]==101u || token[i]==69u || token[i]==100u || token[i]==68u)) {
            number[n++]='e';i++;if (i<1024u && (token[i]==43u || token[i]==45u)) number[n++]=(char)token[i++];
            while (i<1024u && token[i]>=48u && token[i]<=57u) number[n++]=(char)token[i++];
        }
        if (n>=1024u) return -3;
        number[n]=0;value=(float)strtod(number,0);memcpy(bits,&value,4);
    }
    *more=available;return 0;
}
int frame_text_read_f32(frame_data_read read,frame_data_write write,void *ctx,uint32_t parser,
                            uint32_t destination,uint32_t limit,uint32_t *more)
{
    physics_memory m={read,write,ctx,0};uint32_t bits=0,available;int status;
    if (!more) return -1;
    status=frame_text_read_f32_value(read,write,ctx,parser,limit,&bits,&available);if (status) return status;
    if (available) pm_store(&m,destination,bits,4);
    if (m.error) return m.error;
    *more=available;return 0;
}

uint32_t frame_text_fixed1024(uint32_t bits)
{
    uint32_t exponent=(bits>>23)&255u,mantissa=(bits&0x7fffffu)|0x800000u;uint64_t magnitude;
    if (!exponent || exponent==255u || exponent<117u || exponent>=180u) return 0;
    magnitude=exponent>=140u ? (uint64_t)mantissa<<(exponent-140u) : mantissa>>(140u-exponent);
    if (magnitude>INT64_MAX && !((bits>>31) && magnitude==(UINT64_C(1)<<63))) magnitude=0;
    return (bits>>31) ? 0u-(uint32_t)magnitude : (uint32_t)magnitude;
}

uint32_t frame_text_truncate_f32(uint32_t bits)
{
    uint32_t exponent=(bits>>23)&255u,mantissa=(bits&0x7fffffu)|0x800000u;uint64_t magnitude;
    if (!exponent || exponent==255u || exponent<127u || exponent>=190u) return 0;
    magnitude=exponent>=150u ? (uint64_t)mantissa<<(exponent-150u) : mantissa>>(150u-exponent);
    if (magnitude>INT64_MAX && !((bits>>31) && magnitude==(UINT64_C(1)<<63))) magnitude=0;
    return (bits>>31) ? 0u-(uint32_t)magnitude : (uint32_t)magnitude;
}
