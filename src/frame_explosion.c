#include "frame_explosion.h"
#include "physics_memory.h"
#include "physics_vector.h"
static uint32_t object_of(physics_memory *m,uint32_t id)
{ uint32_t r=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);return r ? pm_load(m,r+4u,4) : 0; }
static uint32_t random_next(physics_memory *m)
{ uint32_t v=pm_load(m,0x55b6acu,4)*1664525u+1013904223u;pm_store(m,0x55b6acu,v,4);return v; }
int frame_explosion_fog_host_submit(void *ctx,uint32_t enabled,uint32_t near_distance,uint32_t far_distance,uint32_t rgba)
{
    frame_explosion_fog_host *h=(frame_explosion_fog_host *)ctx;
    if (!h || enabled>1u || (enabled && !((near_distance==0xa000u && far_distance==0x16800u && rgba==0xff000000u) || (!near_distance && far_distance==0xa000u && rgba==UINT32_MAX))) || (!enabled && (near_distance || far_distance || rgba))) return -1;
    h->commands++;h->enabled=enabled;h->near_distance=near_distance;h->far_distance=far_distance;h->rgba=rgba;return 0;
}
int frame_explosion_fog(frame_data_read read,frame_data_write write,void *ctx,const frame_explosion_services *s)
{
    physics_memory m={read,write,ctx,0};static const char *maps[]={"swamp","bm3","bm6","circuit","bio"};uint32_t i,j,c,enabled=0;int status;
    if (!read || !write) return -1;
    for (i=0;i<5u;i++) {
        for (j=0;j<16u;j++) { c=pm_load(&m,0x590540u+j,1);if (c>=128u) return m.error ? m.error : -5;if (c>=65u && c<=90u) c+=32u;if (c!=(unsigned char)maps[i][j]) break;if (!c) { enabled=1;break; } }
        if (enabled) break;
    }
    if (m.error) return m.error;
    if (!s || !s->fog) return -5;
    status=s->fog(s->fog_ctx,enabled,enabled ? 0xa000u : 0u,enabled ? 0x16800u : 0u,enabled ? 0xff000000u : 0u);if (status) return status;
    pm_store(&m,0x54b4c8u,enabled,4);return m.error;
}
int frame_explosion_reset(frame_data_read read,frame_data_write write,void *ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t i;if (!read || !write) return -1;
    pm_store(&m,0x53d12cu,0,4);for (i=0;i<15u;i++) pm_store(&m,0x590580u+i*8u,0,4);return m.error;
}
static void normalize(physics_memory *m,uint32_t at)
{
    int32_t v[3],a,b,c,length;uint32_t i;
    for (i=0;i<3u;i++) v[i]=pm_i32(m,at+i*4u);
    a=pm_abs(v[0]);b=pm_abs(v[1]);c=pm_abs(v[2]);
    if (b>a && b>c) length=pm_add(pm_add(b,pm_sar(a,2)),pm_sar(c,2));
    else if (a>=b && a>c) length=pm_add(pm_add(a,pm_sar(b,2)),pm_sar(c,2));
    else length=pm_add(pm_add(c,pm_sar(a,2)),pm_sar(b,2));
    if (length<=0) return;
    physics_vector_normalize(v);for (i=0;i<3u;i++) pm_write32(m,at+i*4u,v[i]);
}
static uint32_t collision_load(physics_memory *m,uint32_t collision,const uint32_t *words,uint32_t offset)
{ return words ? words[offset/4u] : pm_load(m,collision+offset,4); }
static int attach(frame_data_read read,frame_data_write write,void *ctx,uint32_t o,uint32_t collision,const uint32_t *words,uint32_t count,uint32_t limit)
{
    physics_memory m={read,write,ctx,0};uint32_t tank,a,b,p,i,j,shape;int32_t spread;
    if (!read || !write || !limit) return -1;
    tank=object_of(&m,collision_load(&m,collision,words,72u));a=(random_next(&m)>>4)&65535u;b=(random_next(&m)>>4)&65535u;
    if (pm_i32(&m,tank+0xdafu)>=1) return m.error;
    pm_store(&m,o+0x579u,count,1);shape=pm_load(&m,tank+0x2feu,4);pm_store(&m,o+0x56du,pm_load(&m,pm_load(&m,shape+8u,4)+20u,4),4);pm_store(&m,o+0x36bu,collision_load(&m,collision,words,72u),4);
    pm_store(&m,o+0x571u,a%111u,1);pm_store(&m,o+0x573u,a%111u+16u,1);pm_store(&m,o+0x572u,b%111u,1);pm_store(&m,o+0x574u,b%111u+16u,1);
    for (i=0;pm_signed(i)<pm_signed(count);i++) {
        if (i>=limit) return -3;
        p=o+0x36fu+i*34u;for (j=0;j<3u;j++) pm_store(&m,p+j*4u,collision_load(&m,collision,words,48u+j*4u),4);pm_store(&m,p+24u,175,2);pm_store(&m,p+26u,175,2);
        pm_store(&m,p+12u,collision_load(&m,collision,words,12u),4);pm_store(&m,p+20u,collision_load(&m,collision,words,20u),4);pm_store(&m,p+16u,0,4);normalize(&m,p+12u);
        spread=(int32_t)(a%2500u)-1250;pm_write32(&m,p+12u,pm_sar(pm_mul(pm_add(spread,pm_i32(&m,p+12u)),12000),12));pm_write32(&m,p+20u,pm_sar(pm_mul(pm_sub(pm_i32(&m,p+20u),spread),12000),12));
        pm_write32(&m,p+16u,pm_sar(pm_mul((int32_t)(b%30u)-15,1024),2));pm_store(&m,p+28u,(a&1u) ? 81900u : (uint32_t)-81900,4);a=(random_next(&m)>>4)&65535u;b=(random_next(&m)>>4)&65535u;
    }
    return m.error;
}
int frame_explosion_attach(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,uint32_t collision,uint32_t count,uint32_t limit)
{ return attach(read,write,ctx,object,collision,0,count,limit); }
int frame_explosion_debris_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t o)
{
    physics_memory m={read,write,ctx,0};uint32_t n,i,j,p;int32_t dt,omega,step;
    if (!read || !write) return -1;
    n=pm_load(&m,o+0x579u,1);if (!n) return m.error;
    dt=pm_i32(&m,0x5904fcu);pm_store(&m,o+0x575u,pm_load(&m,o+0x575u,4)+(uint32_t)dt,4);
    for (i=0;i<n;i++) {
        p=o+0x36fu+i*34u;
        for (j=0;j<3u;j++) pm_write32(&m,p+j*4u,pm_add(pm_i32(&m,p+j*4u),pm_sar(pm_mul(pm_i32(&m,p+12u+j*4u),dt),8)));
        pm_write32(&m,p+16u,pm_sub(pm_i32(&m,p+16u),pm_mul(dt,4)));omega=pm_i32(&m,p+28u);pm_store(&m,p+32u,pm_load(&m,p+32u,2)+(uint32_t)pm_sar(pm_mul(dt,omega),8),2);
        step=omega<1 ? pm_neg(dt) : dt;pm_write32(&m,p+28u,pm_sub(omega,pm_mul(step,64)));
        pm_store(&m,p+24u,pm_load(&m,p+24u,2)+(uint32_t)pm_sar(pm_i32(&m,0x597380u+pm_load(&m,o+0x575u,1)*256u),6),2);
        pm_store(&m,p+26u,350u-(uint32_t)pm_i16(&m,p+24u),2);
        if (pm_i16(&m,p+24u)<30) pm_store(&m,p+24u,30,2);
        if (pm_i16(&m,p+24u)>350) pm_store(&m,p+24u,350,2);
        if (pm_i16(&m,p+26u)<30) pm_store(&m,p+26u,30,2);
        if (pm_i16(&m,p+26u)>350) pm_store(&m,p+26u,350,2);
    }
    return m.error;
}
#define TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
typedef struct mine_source { uint32_t vehicle;const uint32_t *words;const uint32_t *record; } mine_source;
static int spawn(frame_data_read read,frame_data_write write,void *ctx,uint32_t origin,const int32_t *origin_values,uint32_t collision,const mine_source *source,uint32_t kind,const frame_entity_factory_services *e,const frame_explosion_services *s,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t klass,entity,o,a,b,i,head,unused,tank,shape,count,slot=0,time,oldest,d,mode;int status;
    if (!read || !write || !e || !e->work_limit || !out) return -1;
    klass=pm_load(&m,0x53d128u,4);TRY(frame_entity_create(read,write,ctx,klass,e,&entity));a=random_next(&m)>>4;o=object_of(&m,entity);
    if (o) {
        TRY(frame_entity_activate(read,write,ctx,entity,e->method,e->method_ctx,&unused));head=pm_load(&m,0x54b488u,4);TRY(frame_list_append(read,write,ctx,entity,head,e->allocate,e->release,e->system_ctx,e->work_limit,&unused));
        for (i=0;i<3u;i++) pm_store(&m,o+0x7eu+i*4u,(origin_values ? (uint32_t)origin_values[i] : pm_load(&m,origin+i*4u,4)),4);
        for (i=0;i<3u;i++) pm_store(&m,o+i*4u,(origin_values ? (uint32_t)origin_values[i] : pm_load(&m,origin+i*4u,4)),4);
        pm_store(&m,o+0x9au,0,1);pm_store(&m,o+0x9bu,0,1);pm_store(&m,o+0x9cu,31,1);pm_store(&m,o+0x9du,31,1);
        b=(random_next(&m)>>4)&65535u;pm_store(&m,o+0xa8u,((b%40u+55u)*0xb600u)/1000u,4);pm_store(&m,o+0xa2u,a*182u,2);b=pm_load(&m,0x54b480u,4) ? 2000u : 3000u;
        pm_store(&m,o+0x92u,b,4);pm_store(&m,o+0x96u,b,4);pm_store(&m,o+0x8eu,0xfffffffau,4);pm_store(&m,o+0x9eu,128,1);pm_store(&m,o+0x9fu,128,1);pm_store(&m,o+0xa0u,128,1);pm_store(&m,o+0x8au,1,4);pm_store(&m,o+0x7au,pm_load(&m,0x5905fcu,4),4);
        if (collision || source) {
            tank=object_of(&m,source ? source->vehicle : pm_load(&m,collision+72u,4));for (i=0;i<3u;i++) pm_write32(&m,o+80u,pm_sar(pm_i32(&m,tank+80u+i*4u),6));
            pm_store(&m,o+0x35eu,source ? source->vehicle : pm_load(&m,collision+72u,4),4);for (i=0;i<3u;i++) pm_store(&m,o+0x34eu+i*4u,source ? source->words[i] : pm_load(&m,collision+60u+i*4u,4),4);normalize(&m,o+0x34eu);
            for (i=0;i<3u;i++) { shape=pm_load(&m,tank+72u,4);pm_write32(&m,o+0x34eu+i*4u,pm_sar(pm_mul(pm_i16(&m,shape+38u),pm_i32(&m,o+0x34eu+i*4u)),9)); }
        }
    }
    count=pm_load(&m,0x53d12cu,4);
    if (pm_signed(count)<15) {
        pm_store(&m,0x53d12cu,count+1u,4);
        for (i=0;i<15u;i++) if (!pm_load(&m,0x590580u+i*8u,4)) {
            pm_store(&m,0x590580u+i*8u,entity,4);pm_store(&m,0x590584u+i*8u,pm_load(&m,0x590564u,4),4);if (!o) return m.error ? m.error : -4;pm_store(&m,o+120u,i,2);break;
        }
    } else {
        oldest=pm_load(&m,0x590584u,4);
        for (i=1;i<15u;i++) { time=pm_load(&m,0x590584u+i*8u,4);if (time<oldest) { oldest=time;slot=i; } }
        head=pm_load(&m,0x54b488u,4);b=pm_load(&m,0x590580u+slot*8u,4);TRY(frame_list_remove_value(read,write,ctx,b,head,e->release,e->system_ctx,e->work_limit));d=pm_load(&m,0x545ed0u,4);
        b=pm_load(&m,0x590580u+slot*8u,4);TRY(frame_entity_dispatch(read,write,ctx,b,d,0,0,e->method,e->method_ctx,&unused));
        pm_store(&m,0x590580u+slot*8u,entity,4);pm_store(&m,0x590584u+slot*8u,pm_load(&m,0x590564u,4),4);if (!o) return m.error ? m.error : -4;
        pm_store(&m,o+120u,slot,2);pm_store(&m,0x53d134u,1,1);TRY(frame_explosion_fog(read,write,ctx,s));
    }
    if (m.error) return m.error;
    if (!o) return -4;
    pm_store(&m,o+0xadu,1,1);pm_store(&m,o+0x363u,UINT32_MAX,4);pm_store(&m,o+0x367u,UINT32_MAX,4);
    if (kind==1u) {
        mode=pm_load(&m,0x52ee9cu,4);TRY(origin_values ? frame_blast_band_start_values(read,write,ctx,origin_values,(mode==2u || mode==3u) ? 0x2800u : 0x5000u,entity,1,e->work_limit,&b) : frame_blast_band_start(read,write,ctx,origin,(mode==2u || mode==3u) ? 0x2800u : 0x5000u,entity,1,e->work_limit,&b));pm_store(&m,o+0x363u,b,4);
        if (collision || (source && source->record)) TRY(attach(read,write,ctx,o,collision,source ? source->record : 0,15,e->work_limit));pm_store(&m,0x53d130u,50,2);pm_store(&m,0x52ec68u,1,1);pm_store(&m,0x53d134u,0,1);
    } else if (kind==2u) { TRY(origin_values ? frame_blast_band_start_values(read,write,ctx,origin_values,0x3c00u,entity,0,e->work_limit,&b) : frame_blast_band_start(read,write,ctx,origin,0x3c00u,entity,0,e->work_limit,&b));pm_store(&m,o+0x363u,b,4); }
    else if (kind==3u || kind==4u || kind==5u) { TRY(origin_values ? frame_blast_ring_start_values(read,write,ctx,origin_values,entity,kind==3u ? 1u : kind==4u ? 3u : 2u,e->work_limit,&b) : frame_blast_ring_start(read,write,ctx,origin,entity,kind==3u ? 1u : kind==4u ? 3u : 2u,e->work_limit,&b));pm_store(&m,o+0x367u,b,4);pm_store(&m,o+0xadu,0,1); }
    else if ((collision || (source && source->record)) && (a&1u)) TRY(attach(read,write,ctx,o,collision,source ? source->record : 0,kind==6u ? 11u : 7u,e->work_limit));
    if (collision || source) { tank=object_of(&m,source ? source->vehicle : pm_load(&m,collision+72u,4));if (pm_load(&m,tank+0x10deu,1) && (source ? source->words[3] : pm_load(&m,collision+92u,4))==UINT32_MAX) pm_store(&m,o+0xadu,0,1); }
    if (m.error) return m.error;
    *out=entity;return 0;
}
int frame_explosion_spawn(frame_data_read read,frame_data_write write,void *ctx,uint32_t origin,uint32_t collision,uint32_t kind,const frame_entity_factory_services *e,const frame_explosion_services *s,uint32_t *out)
{ return spawn(read,write,ctx,origin,0,collision,0,kind,e,s,out); }
int frame_explosion_spawn_mine(frame_data_read read,frame_data_write write,void *ctx,uint32_t origin,uint32_t vehicle,uint32_t kind,const frame_entity_factory_services *e,const frame_explosion_services *s,uint32_t *out)
{
    mine_source source;if (kind!=3u && kind!=4u) return -1;
    if (!s || !s->mine_collision_words) return -4;
    source.vehicle=vehicle;source.words=s->mine_collision_words;source.record=0;return spawn(read,write,ctx,origin,0,0,&source,kind,e,s,out);
}
int frame_explosion_spawn_values(frame_data_read read,frame_data_write write,void *ctx,uint32_t origin,const uint32_t words[25],uint32_t kind,const frame_entity_factory_services *e,const frame_explosion_services *s,uint32_t *out)
{
    mine_source source;uint32_t normal_and_visibility[4];uint32_t i;
    if (!words) return spawn(read,write,ctx,origin,0,0,0,kind,e,s,out);
    for (i=0;i<3u;i++) { normal_and_visibility[i]=words[15u+i]; }normal_and_visibility[3]=words[23];
    source.vehicle=words[18];source.words=normal_and_visibility;source.record=words;return spawn(read,write,ctx,origin,0,0,&source,kind,e,s,out);
}
int frame_explosion_spawn_native(frame_data_read read,frame_data_write write,void *ctx,const int32_t origin[3],const uint32_t words[25],uint32_t kind,const frame_entity_factory_services *e,const frame_explosion_services *s,uint32_t *out)
{
    mine_source source;uint32_t normal_and_visibility[4];uint32_t i;
    if (!origin) return -1;
    if (!words) return spawn(read,write,ctx,0,origin,0,0,kind,e,s,out);
    for (i=0;i<3u;i++) { normal_and_visibility[i]=words[15u+i]; }normal_and_visibility[3]=words[23];
    source.vehicle=words[18];source.words=normal_and_visibility;source.record=words;return spawn(read,write,ctx,0,origin,0,&source,kind,e,s,out);
}
static void attached_transform(physics_memory *m,uint32_t source,uint32_t destination,uint32_t matrix)
{
    uint32_t i,j;int32_t v[3],sum;if (!matrix) return;
    for (i=0;i<3u;i++) v[i]=pm_i32(m,source+i*4u);
    for (i=0;i<3u;i++) { sum=0;for (j=0;j<3u;j++) sum=pm_add(sum,pm_mul(v[j],pm_i32(m,matrix+(j*3u+i)*4u)));pm_write32(m,destination+i*4u,pm_sar(sum,12)); }
}
int frame_explosion_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o,p,i,j,slot,head,d,unused,active=0,index,a,b,owner,target,matrix;int32_t dt,value,omega,step;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,entity);TRY(frame_explosion_debris_tick(read,write,ctx,o));dt=pm_i32(&m,0x5904fcu);value=pm_add(pm_i32(&m,o+0xa4u),dt);pm_write32(&m,o+0xa4u,value);
    if (value>30) {
        pm_store(&m,o+0xa4u,0,4);
        if (pm_load(&m,o+0x9au,1)<95u) { pm_store(&m,o+0x9au,pm_load(&m,o+0x9au,1)+32u,1);pm_store(&m,o+0x9cu,pm_load(&m,o+0x9cu,1)+32u,1); }
        else if (pm_load(&m,o+0x9bu,1)<95u) { pm_store(&m,o+0x9au,0,1);pm_store(&m,o+0x9bu,pm_load(&m,o+0x9bu,1)+32u,1);pm_store(&m,o+0x9cu,31,1);pm_store(&m,o+0x9du,pm_load(&m,o+0x9du,1)+32u,1); }
        else {
            pm_store(&m,o+0xadu,0,1);for (i=0;i<10u;i++) if (pm_load(&m,o+0xecu+i*66u,2)) { active=1;break; }
            if (!active) {
                pm_store(&m,0x590580u+(uint32_t)pm_i16(&m,o+120u)*8u,0,4);head=pm_load(&m,0x54b488u,4);TRY(frame_list_remove_value(read,write,ctx,entity,head,e->release,e->system_ctx,e->work_limit));
                d=pm_load(&m,0x545ed0u,4);TRY(frame_entity_dispatch(read,write,ctx,entity,d,0,0,e->method,e->method_ctx,&unused));
                slot=pm_load(&m,o+0x363u,4);if (slot!=UINT32_MAX) TRY(frame_blast_slot_release(read,write,ctx,0,slot,e->work_limit));
                slot=pm_load(&m,o+0x367u,4);if (slot!=UINT32_MAX) TRY(frame_blast_slot_release(read,write,ctx,1,slot,e->work_limit));
                pm_store(&m,0x53d12cu,pm_load(&m,0x53d12cu,4)-1u,4);return m.error;
            }
        }
    }
    value=pm_sub(pm_i32(&m,o+0xa8u),dt);pm_write32(&m,o+0xa8u,value);pm_store(&m,o+0xa2u,pm_load(&m,o+0xa2u,2)+(uint32_t)pm_sar(pm_mul(dt,value),8),2);
    for (i=0;i<3u;i++) pm_write32(&m,o+0x7eu+i*4u,pm_add(pm_i32(&m,o+0x7eu+i*4u),pm_sar(pm_mul(pm_i32(&m,o+80u+i*4u),dt),8)));
    pm_write32(&m,o+80u,pm_damp(pm_i32(&m,o+80u),pm_load(&m,0x52ec64u,4),pm_i32(&m,0x5904fcu)));pm_write32(&m,o+88u,pm_damp(pm_i32(&m,o+88u),pm_load(&m,0x52ec64u,4),pm_i32(&m,0x5904fcu)));
    slot=pm_load(&m,o+0x363u,4);if (slot!=UINT32_MAX) { TRY(frame_blast_band_tick(read,write,ctx,slot,e->work_limit,&unused));pm_store(&m,o+0x363u,unused,4); }
    slot=pm_load(&m,o+0x367u,4);if (slot!=UINT32_MAX) { TRY(frame_blast_ring_tick(read,write,ctx,slot,e->work_limit,&unused));pm_store(&m,o+0x367u,unused,4); }
    index=pm_load(&m,o+0x362u,1);
    if (index<10u) {
        owner=pm_load(&m,o+0x35eu,4);value=pm_add(pm_i32(&m,o+0x35au),pm_i32(&m,0x5904fcu));pm_write32(&m,o+0x35au,value);
        if (value>=(owner ? 65 : 97)) {
            p=o+0xaeu+index*66u;a=(random_next(&m)>>4)&65535u;b=(random_next(&m)>>4)&65535u;pm_store(&m,o+0x35au,0,4);owner=pm_load(&m,o+0x35eu,4);
            if (!owner) { pm_store(&m,p+4u,pm_load(&m,o,4),4);pm_write32(&m,p+8u,pm_sub(pm_i32(&m,o+4u),1024));pm_store(&m,p+12u,pm_load(&m,o+8u,4),4); }
            else {
                target=object_of(&m,owner);
                if (!target) for (i=0;i<3u;i++) pm_store(&m,p+4u+i*4u,pm_load(&m,o+i*4u,4),4);
                else { matrix=pm_load(&m,target+20u,4);attached_transform(&m,o+0x34eu,p+4u,matrix);for (i=0;i<3u;i++) pm_store(&m,p+4u+i*4u,pm_load(&m,p+4u+i*4u,4)+pm_load(&m,target+i*4u,4),4); }
            }
            pm_store(&m,p,pm_load(&m,0x5905f8u,4),4);pm_store(&m,p+34u,63,1);pm_store(&m,p+35u,63,1);pm_store(&m,p+16u,1,4);pm_store(&m,p+24u,300,4);pm_store(&m,p+28u,300,4);pm_store(&m,p+20u,0xfffffff0u,4);
            for (i=0;i<3u;i++) pm_store(&m,p+36u+i,128,1);
            pm_store(&m,p+32u,0,1);pm_store(&m,p+33u,0,1);pm_store(&m,p+40u,0,2);
            index=pm_load(&m,o+0x362u,1);p=o+0xaeu+index*66u;pm_write32(&m,p+42u,pm_sar(pm_sub((int32_t)(a&1u)*1024,1024),3));pm_write32(&m,p+46u,pm_sar((int32_t)(a%3u+2u)*3072,4));
            pm_write32(&m,p+50u,pm_sar(pm_sub((int32_t)(b&1u)*1024,1024),3));pm_write32(&m,p+58u,pm_sar(pm_mul((int32_t)(b%360u)-180,182),2));pm_store(&m,p+62u,1,2);
            if (!pm_load(&m,o+0x35eu,4)) pm_store(&m,p+54u,1536,4);
            pm_store(&m,o+0x362u,pm_load(&m,o+0x362u,1)+1u,1);
        }
    }
    for (i=0;i<10u;i++) {
        p=o+0xaeu+i*66u;if (!pm_load(&m,p+62u,2)) continue;
        for (j=0;j<3u;j++) { value=pm_i32(&m,p+42u+j*4u);if (j==1u) value=pm_add(value,pm_i32(&m,p+54u));pm_write32(&m,p+4u+j*4u,pm_add(pm_i32(&m,p+4u+j*4u),pm_sar(pm_mul(value,pm_i32(&m,0x5904fcu)),8))); }
        value=pm_i32(&m,p+54u);if (value) { value=pm_sub(value,pm_mul(pm_i32(&m,0x5904fcu),8));pm_write32(&m,p+54u,value);if (value<0) pm_store(&m,p+54u,0,4); }
        dt=pm_i32(&m,0x5904fcu);pm_store(&m,p+24u,pm_load(&m,p+24u,4)+(uint32_t)pm_sar(dt,1),4);pm_store(&m,p+28u,pm_load(&m,p+28u,4)+(uint32_t)pm_sar(dt,1),4);
        omega=pm_i32(&m,p+58u);pm_store(&m,p+40u,pm_load(&m,p+40u,2)+(uint32_t)pm_sar(pm_mul(dt,omega),8),2);step=pm_mul(dt,2);pm_write32(&m,p+58u,pm_sub(omega,omega<1 ? pm_neg(step) : step));step=pm_sar(dt,4);
        if (step<pm_signed(pm_load(&m,p+36u,1))) for (j=0;j<3u;j++) pm_store(&m,p+36u+j,pm_load(&m,p+36u+j,1)-(uint32_t)step,1);
        else pm_store(&m,p+62u,0,2);
    }
    return m.error;
}
#undef TRY
