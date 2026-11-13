#include "frame_tank_config.h"
#include "frame_file.h"
#include "physics_memory.h"
struct config_field { const char *name;uint32_t kind,reference,offset; };
#include "frame_tank_config_schema.h"
static int equal(const unsigned char *a,const char *b)
{
    uint32_t i,x,y;for (i=0;i<32u;i++) {
        x=a[i];y=(unsigned char)b[i];if (x>=128u || y>=128u) return -5;
        if (x>=65u && x<=90u) x+=32u;
        if (y>=65u && y<=90u) y+=32u;
        if (x!=y) return 0;
        if (!x) return 1;
    }
    return 0;
}
int frame_tank_configure_span(frame_data_read read,frame_data_write write,void *ctx,uint32_t o,
                                  const unsigned char *bytes,uint32_t length,uint32_t limit,
                                  const frame_tank_config_ambient *ambient,uint32_t *consumed)
{
    physics_memory m={read,write,ctx,0};frame_file f={bytes,length,0,limit};unsigned char key[32];
    uint32_t temporary[2]={0,0},defined[2]={0,0},i,present,index,dest,value,j,assigned,used=0;int status,match;
    if (!read || !write || (!bytes && length) || !limit || !consumed) return -1;
#define TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
    if (ambient) { temporary[0]=ambient->max_turn_battle;temporary[1]=ambient->traction_limit_battle;defined[0]=defined[1]=1; }
    for (;;) {
        if (used++>=limit) return -3;
        TRY(frame_file_token(&f,key,32,&present));if (!present) break;index=57;
        for (i=0;i<57u;i++) { match=equal(key,config_fields[i].name);if (match<0) return match;if (match) index=i; }
        if (index==57u) return -4;
        {
            const struct config_field *q=&config_fields[index];dest=o+q->offset;
            if (q->kind==0u) {
                TRY(frame_file_integer(&f,&value,&present));
                if (q->reference) { temporary[q->reference-1u]=value;defined[q->reference-1u]=1; }
                else pm_store(&m,dest,value,4);
            } else if (q->kind==1u || q->kind==2u) {
                for (j=0;j<(q->kind==1u ? 2u : 3u);j++) {
                    TRY(frame_file_integer(&f,&value,&present));
                    pm_store(&m,dest+(j ? 2u+(j-1u)*(q->kind==1u ? 2u : 1u) : 0u),value,j==0u || q->kind==1u ? 2 : 1);
                }
            } else if (q->kind==3u || q->kind==4u) {
                TRY(frame_file_token_data(&f,write,ctx,dest,16,&present));
                if (q->kind==3u) TRY(frame_file_vector_data(&f,write,ctx,dest+28u,&assigned));
            } else if (q->kind==5u) TRY(frame_file_vector_data(&f,write,ctx,dest,&assigned));
            else return -5;
        }
    }
    if (pm_load(&m,0x53e414u,4) && pm_load(&m,o+0xde5u,1)) {
        pm_store(&m,o+0x6a8u,pm_load(&m,o+0x6a8u,2)+1u,2);
        pm_store(&m,o+0x666u,pm_load(&m,o+0x666u,4)+3000000u,4);pm_store(&m,o+0x66eu,pm_load(&m,o+0x66eu,4)+2000000u,4);
        pm_store(&m,o+0x672u,pm_load(&m,o+0x672u,4)-80u,4);pm_store(&m,o+0x68eu,pm_load(&m,o+0x68eu,4)+150u,4);
    }
    value=pm_load(&m,0x52ee9cu,4);
    if (value==2u) {
        if (!defined[0] || !defined[1]) return m.error ? m.error : -4;
        pm_store(&m,o+0x66au,temporary[0],4);pm_store(&m,o+0x68eu,temporary[1],4);
    } else if (value==5u && pm_load(&m,o+0xde5u,1)) {
        pm_store(&m,o+0x1116u,pm_load(&m,o+0x68eu,4),4);pm_store(&m,o+0x111au,pm_load(&m,o+0x66eu,4),4);
    }
    if (pm_load(&m,0x52ed90u,4)==7u) pm_store(&m,o+0x672u,pm_load(&m,0x5319c4u,4),4);
    pm_store(&m,o+0xdd2u,pm_load(&m,o+0x6a6u,4),4);if (m.error) return m.error;
    *consumed=f.cursor;return 0;
#undef TRY
}
int frame_tank_configure_derived(frame_data_read read,frame_data_write write,void *ctx,uint32_t o)
{
    physics_memory m={read,write,ctx,0};uint32_t v;
    if (!read || !write) return -1;
    v=pm_load(&m,o+0x702u,4)*pm_load(&m,o+0x6feu,2);pm_store(&m,o+0x64au,v<<(pm_load(&m,o+0x700u,1)&31u),4);
    v=pm_load(&m,o+0x706u,4)*pm_load(&m,o+0x6feu,2);pm_store(&m,o+0x64eu,v<<(pm_load(&m,o+0x700u,1)&31u),4);return m.error;
}
int frame_tank_effects_initialize(frame_data_read read,frame_data_write write,void *ctx,uint32_t o)
{
    physics_memory m={read,write,ctx,0};uint32_t i;
    if (!read || !write) return -1;
    for (i=0;i<15u;i++) pm_store(&m,o+0x7b0u+i*32u,0,2);
    pm_store(&m,o+0x978u,0,4);pm_store(&m,o+0x98cu,0,4);pm_store(&m,o+0x988u,0,4);
    for (i=0;i<8u;i++) pm_store(&m,o+0x9a8u+i*34u,0,2);
    pm_store(&m,o+0xaa0u,0,4);pm_store(&m,o+0xaa4u,0,4);pm_store(&m,o+0xc54u,0,1);pm_store(&m,o+0xc55u,0,1);
    pm_store(&m,o+0xc44u,0,4);pm_store(&m,o+0xdd0u,0,2);pm_store(&m,o+0xc96u,0,4);pm_store(&m,o+0xdceu,65535,2);return m.error;
}
