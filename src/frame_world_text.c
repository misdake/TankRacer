#include "frame_world_text.h"
#include "physics_memory.h"
#include <stdlib.h>
#include <float.h>

static int delimiter(uint32_t c) { return c==32u || c==13u || c==10u; }
static int token(physics_memory *m,unsigned char *out,uint32_t capacity,uint32_t limit,uint32_t destination,int data)
{
    unsigned char local[256];uint32_t base=pm_load(m,0x55c6a0u,4),cursor=pm_load(m,0x55c6a4u,4),c,n=0,i,used=0;
    if (!capacity) return -4;
    c=pm_load(m,base+cursor,1);
    while (delimiter(c)) {
        if (m->error) return m->error;
        if (used++>=limit) return -3;
        cursor++;pm_store(m,0x55c6a4u,pm_load(m,0x55c6a4u,4)+1u,4);c=pm_load(m,base+cursor,1);
    }
    while (!delimiter(c)) {
        if (m->error) return m->error;
        if (used++>=limit) return -3;
        if (n>=255u) return -4; /* original local256 would overrun */
        local[n++]=(unsigned char)c;cursor++;pm_store(m,0x55c6a4u,pm_load(m,0x55c6a4u,4)+1u,4);c=pm_load(m,base+cursor,1);
    }
    if (m->error) return m->error;
    local[n]=0;for (i=0;i<n;i++) if (!local[i]) { n=i;break; }
    i=0;
    if (data) while (capacity-1u-i>=4u) {
        uint32_t j,word=0;
        if (used++>=limit) return -3;
        for (j=0;j<4u;j++) if (i+j<n) word|=(uint32_t)local[i+j]<<(j*8u);
        pm_store(m,destination+i,word,4);i+=4u;
    }
    for (;i<capacity-1u;i++) {
        c=i<n ? local[i] : 0;
        if (data) pm_store(m,destination+i,c,1);else out[i]=(unsigned char)c;
    }
    if (data) pm_store(m,destination+capacity-1u,0,1);else out[capacity-1u]=0;
    return m->error;
}
int frame_world_token(frame_data_read read,frame_data_write write,void *ctx,unsigned char *out,uint32_t capacity,uint32_t limit)
{ physics_memory m={read,write,ctx,0};if (!read || !write || !out || !limit) return -1;return token(&m,out,capacity,limit,0,0); }
int frame_world_token_data(frame_data_read read,frame_data_write write,void *ctx,uint32_t destination,uint32_t capacity,uint32_t limit)
{ physics_memory m={read,write,ctx,0};if (!read || !write || !limit) return -1;return token(&m,0,capacity,limit,destination,1); }
int frame_world_group(frame_data_read read,frame_data_write write,void *ctx,uint32_t destination,uint32_t capacity,uint32_t limit,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t base,cursor,c,n=0,depth=1,iterations=0,next_iterations,used=0;
    if (!read || !write || !limit || !out) return -1;
    base=pm_load(&m,0x55c6a0u,4);cursor=base+pm_load(&m,0x55c6a4u,4);c=pm_load(&m,cursor,1);
    while (c!=40u) {
        if (m.error) return m.error;
        if (used++>=limit) return -3;
        cursor++;pm_store(&m,0x55c6a4u,pm_load(&m,0x55c6a4u,4)+1u,4);c=pm_load(&m,cursor,1);
    }
    pm_store(&m,0x55c6a4u,pm_load(&m,0x55c6a4u,4)+1u,4);
    for (;;) {
        cursor++;next_iterations=iterations;
        if (n>=capacity-1u || iterations>=1024u) break;
        next_iterations=iterations+1u;c=pm_load(&m,cursor,1);
        if (c==40u) depth++;
        if (c==41u) depth--;
        if (!depth) break;
        if (m.error) return m.error;
        if (used++>=limit) return -3;
        pm_store(&m,destination+n,c,1);n++;pm_store(&m,destination+n,0,1);
        pm_store(&m,0x55c6a4u,pm_load(&m,0x55c6a4u,4)+1u,4);iterations=next_iterations;
    }
    pm_store(&m,0x55c6a4u,pm_load(&m,0x55c6a4u,4)+1u,4);if (m.error) return m.error;
    *out=n<capacity-1u && next_iterations<1024u ? n : UINT32_MAX;return 0;
}
int frame_world_integer(frame_data_read read,frame_data_write write,void *ctx,uint32_t limit,uint32_t *out)
{
    unsigned char text[32];physics_memory m={read,write,ctx,0};uint32_t i=0,negative,value=0;int status;
    if (!read || !write || !out || !limit) return -1;
    status=token(&m,text,32,limit,0,0);if (status) return status;
    if (pm_i32(&m,0x538a44u)>=2) return m.error ? m.error : -5;
    while (i<31u && (pm_load(&m,pm_load(&m,0x538838u,4)+text[i]*2u,1)&8u)) i++;
    negative=text[i]==45u;if (negative || text[i]==43u) i++;
    while (i<32u && text[i]>=48u && text[i]<=57u) value=value*10u+text[i++]-48u;
    if (m.error) return m.error;
    *out=negative ? 0u-value : value;return 0;
}
int frame_world_decimal(frame_data_read read,frame_data_write write,void *ctx,uint32_t limit,double *out)
{
    unsigned char text[32];char number[32];uint32_t i=0,n=0;int status;double value;
    if (!out || sizeof(double)!=8u || DBL_MANT_DIG!=53) return -1;
    status=frame_world_token(read,write,ctx,text,32,limit);if (status) return status;
    while (i<31u && (text[i]==9u || text[i]==11u || text[i]==12u || text[i]==32u || text[i]==10u || text[i]==13u)) i++;
    if (text[i]==43u || text[i]==45u) number[n++]=(char)text[i++];
    while (i<31u && text[i]>=48u && text[i]<=57u) number[n++]=(char)text[i++];
    if (i<31u && text[i]==46u) { number[n++]=(char)text[i++];while (i<31u && text[i]>=48u && text[i]<=57u) number[n++]=(char)text[i++]; }
    if (i<31u && (text[i]==101u || text[i]==69u || text[i]==100u || text[i]==68u)) {
        number[n++]='e';i++;if (i<31u && (text[i]==43u || text[i]==45u)) number[n++]=(char)text[i++];
        while (i<31u && text[i]>=48u && text[i]<=57u) number[n++]=(char)text[i++];
    }
    number[n]=0;value=strtod(number,0);*out=value;return 0;
}
