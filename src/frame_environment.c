#include "frame_environment.h"
#include "physics_memory.h"
#include "physics_attitude.h"
#include "frame_lap_events.h"
#include "frame_scene_effects.h"

static uint32_t weather_random(physics_memory *m)
{ uint32_t v=pm_load(m,0x55b6acu,4)*1664525u+1013904223u;pm_store(m,0x55b6acu,v,4);return v; }
static int lightning(physics_memory *m,const int32_t source[3],uint32_t *work,uint32_t limit)
{
    int32_t next[3];uint32_t a,b,axis,id,n,i;int status;
    if ((*work)++>=limit) return -3;
    if (source[1]<=pm_sub(pm_mul(pm_i32(m,0x74aee4u),8),51200)) return m->error;
    a=(weather_random(m)>>4)&65535u;axis=pm_load(m,0x545e9cu,4);if (axis>3u) return -4;
    next[1]=pm_sub(pm_sub(source[1],(int32_t)(a%20u)*1024),20480);b=(weather_random(m)>>4)&65535u;
    next[0]=source[0];next[2]=source[2];i=(axis&1u) ? 2u : 0u;next[i]=pm_add(next[i],((int32_t)(b%20u)-10)*1024);
    pm_write32(m,0x531920u,source[0]);pm_write32(m,0x531924u,source[1]);pm_write32(m,0x531930u,next[1]);pm_write32(m,0x531928u,source[2]);pm_write32(m,0x53192cu,next[0]);pm_write32(m,0x531934u,next[2]);if (m->error) return m->error;
    status=frame_line_allocate(m->read,m->write,m->ctx,0x531920u,&id);if (status) return status;
    n=pm_load(m,0x545eb4u,4);pm_store(m,0x545e28u+n,id,1);pm_store(m,0x545eb4u,n+1u,4);
    status=lightning(m,next,work,limit);if (status) return status;
    if ((weather_random(m)&240u)==80u) { status=lightning(m,next,work,limit);if (status) return status; }
    return m->error;
}
int frame_environment_weather(frame_data_read read,frame_data_write write,void *ctx,const physics_trig *trig,const frame_explosion_services *s,uint32_t limit)
{
    physics_memory m={read,write,ctx,0};uint32_t i,n,work=0,axis,a;int32_t dt,timer,angle,v,sign,source[3];int status;
    if (!read || !write || !limit || !trig) return -1;
    if (!pm_load(&m,0x545ea4u,4)) return m.error;
    dt=pm_i32(&m,0x5904fcu);timer=pm_i32(&m,0x545eacu);
    if (timer) { timer=pm_sub(timer,dt);pm_write32(&m,0x545eacu,timer);if (timer<1) { (void)weather_random(&m);pm_store(&m,0x545eacu,0,4); } }
    if (pm_load(&m,0x545ea8u,4)) {
        if (pm_load(&m,0x545eb0u,4)) { pm_store(&m,0x545eb0u,0,4);n=pm_load(&m,0x545eb4u,4);if (n>limit) return -3;for (i=0;i<n;i++) { status=frame_line_release(read,write,ctx,(uint32_t)pm_i8(&m,0x545e28u+i));if (status) return status; } }
        timer=pm_add(pm_i32(&m,0x545ea8u),dt);pm_write32(&m,0x545ea8u,timer);
        if (timer<101) {
            if (!pm_load(&m,0x545eb0u,4)) { pm_store(&m,0x545eb4u,0,4);for (i=0;i<3u;i++) source[i]=pm_i32(&m,0x545e90u+i*4u);status=lightning(&m,source,&work,limit);if (status) return status;pm_store(&m,0x545eb0u,1,4); }
            return m.error;
        }
        if (!pm_load(&m,0x545eacu,4)) { a=(weather_random(&m)>>4)&65535u;pm_store(&m,0x545eacu,a%1750u+250u,4); }
        if (m.error) return m.error;
        status=frame_explosion_fog(read,write,ctx,s);if (status) return status;pm_store(&m,0x545ea8u,0,4);a=(weather_random(&m)>>4)&65535u;pm_store(&m,0x531918u,a%10000u+1000u,4);return m.error;
    }
    timer=pm_sub(pm_i32(&m,0x531918u),dt);pm_write32(&m,0x531918u,timer);if (timer>=1) return m.error;
    /*42C280 affects only swamp. The device adapter keeps explicit fog color
     * and distances; its approximate renderer does not use legacy density128.*/
    for (i=0;i<6u;i++) { a=pm_load(&m,0x590540u+i,1);if (a>=65u && a<=90u) a+=32u;if (a!=(unsigned char)"swamp"[i]) break; }
    if (i==6u) { if (!s || !s->fog) return -5;status=s->fog(s->fog_ctx,1,0,40960,UINT32_MAX);if (status) return status;pm_store(&m,0x54b4c8u,1,4); }
    source[1]=pm_add(pm_mul(pm_i32(&m,0x74aee4u),8),102400);pm_store(&m,0x545ea8u,1,4);pm_write32(&m,0x545e94u,source[1]);
    status=physics_angle_lookup(pm_neg(pm_i32(&m,0x74af18u)),pm_i32(&m,0x74af20u),trig,&angle);if (status) return status;
    v=pm_sub(73726,angle);sign=pm_sar(v,31);v=pm_sub(pm_signed(((uint32_t)pm_abs(v)&65535u)^(uint32_t)sign),sign);axis=(uint32_t)(v/16384);pm_store(&m,0x545e9cu,axis,4);
    if (axis>3u) return m.error;
    if (!(axis&1u)) {
        a=(weather_random(&m)>>4)&65535u;pm_write32(&m,0x545e90u,pm_mul(pm_add(((int32_t)(a%200u)-100)*128,pm_i32(&m,0x74aee0u)),8));pm_write32(&m,0x545e98u,pm_add(pm_mul(pm_i32(&m,0x74aee8u),8),axis ? -204800 : 204800));
    } else {
        pm_write32(&m,0x545e90u,pm_add(pm_mul(pm_i32(&m,0x74aee0u),8),axis==1u ? 204800 : -204800));a=(weather_random(&m)>>4)&65535u;pm_write32(&m,0x545e98u,pm_mul(pm_add(((int32_t)(a%200u)-100)*128,pm_i32(&m,0x74aee8u)),8));
    }
    return m.error;
}

int frame_environment_player_view(frame_data_read read,frame_data_write write,void *ctx,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t near=0,angle,i;int32_t matrix[9];int status=0;
    if (!read || !write) return -1;
    if (pm_load(&m,0x53e8b4u,1)) {
        if (m.error) return m.error;
        status=frame_entity_near_camera(read,ctx,0x5302e8u,0,0x4b00,&near);if (status) return status;
    }
    if (!near) { pm_store(&m,0x53e8b0u,0,1);return m.error; }
    angle=(uint32_t)pm_sar(pm_mul(pm_i32(&m,0x5302f4u),pm_i32(&m,0x5904fcu)),8);
    if (angle || !(pm_load(&m,0x590564u,1)&31u)) {
        for (i=0;i<9u;i++) matrix[i]=pm_i32(&m,0x5302f8u+i*4u);
        if (m.error) return m.error;
        if (angle) {
            if (!trig || !trig->sine || !trig->cosine) return -1;
            physics_matrix_rotate(matrix,1,angle,trig);
            for (i=0;i<9u;i++) pm_write32(&m,0x5302f8u+i*4u,matrix[i]);
        }
        if (!(pm_load(&m,0x590564u,1)&31u)) {
            status=physics_matrix_normalize(matrix);
            for (i=0;i<9u;i++) pm_write32(&m,0x5302f8u+i*4u,matrix[i]);
            if (m.error) return m.error;
            if (status) return status;
        }
    }
    pm_store(&m,0x53e8b0u,1,1);return m.error;
}

int frame_environment_in_zone(frame_data_read read,void *ctx,uint32_t object,uint32_t *inside)
{
    physics_memory m={read,0,ctx,0};uint32_t result;
    if (!read || !inside) return -1;
    result=pm_i32(&m,0x531c08u)<=pm_i32(&m,object) && pm_i32(&m,object)<=pm_i32(&m,0x531c18u) &&
        pm_i32(&m,0x531c10u)<=pm_i32(&m,object+8u) && pm_i32(&m,object+8u)<=pm_i32(&m,0x531c20u);
    if (m.error) return m.error;
    *inside=result;return 0;
}
int frame_player_palette_update(frame_data_read read,frame_data_write write,void *ctx,uint32_t object)
{
    physics_memory m={read,write,ctx,0};uint32_t inside,source,i;int status;
    if (!read || !write) return -1;
    if (pm_load(&m,0x52ed90u,4)!=5) return m.error;
    status=frame_environment_in_zone(read,ctx,object,&inside);if (status) return status;
    if (pm_load(&m,0x53e420u,4)==inside) return m.error;
    source=pm_load(&m,inside ? 0x53e41cu : 0x53e418u,4);
    for (i=0;i<80;i++) pm_store(&m,0x5367b0u+i*4u,pm_load(&m,source+i*4u,4),4);
    pm_store(&m,0x53e420u,inside,4);return m.error;
}

int frame_environment_at(frame_data_read read,void *ctx,uint32_t progress,uint32_t *flags)
{
    physics_memory mem={read,0,ctx,0},*m=&mem;
    uint32_t lap=progress/10240000u,value=UINT32_MAX;
    if (!read || !flags) return -1;
    if (lap<3) value=pm_load(m,0x58ed40u+lap*64u+(progress%10240000u)/204800u,1);
    if (m->error) return m->error;
    *flags=value;return 0;
}
static void reset_pool(physics_memory *m)
{
    unsigned i;
    for (i=0;i<300 && !m->error;i++) pm_store(m,0x74a0c4u+i*12u,0xfffce000u,4);
}
int frame_environment_reset(frame_data_read read,frame_data_write write,void *ctx)
{
    physics_memory mem={read,write,ctx,0};
    if (!read || !write) return -1;
    reset_pool(&mem);return mem.error;
}
int frame_environment_config(frame_data_read read,frame_data_write write,void *ctx,
                               uint32_t style,uint32_t count,uint32_t reset)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    if (!read || !write) return -1;
    pm_store(m,0x54b4b0u,style,4);
    pm_store(m,0x5366d0u,count,4);
    if (count>299u) pm_store(m,0x5366d0u,300,4);
    if (reset) reset_pool(m);
    return m->error;
}
int frame_environment_shift(frame_data_read read,frame_data_write write,void *ctx,
                              uint32_t slot,uint32_t delta_address,uint32_t work_limit)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    uint32_t remaining,position=0x74a0c0u,work=0;
    unsigned i;
    if (!read || !write || !work_limit) return -1;
    remaining=pm_load(m,0x5366d0u+slot*4u,4);
    while (remaining) {
        if (m->error) return m->error;
        if (work++>=work_limit) return -3;
        for (i=0;i<3;i++)
            pm_write32(m,position+i*4u,pm_add(pm_i32(m,position+i*4u),pm_i32(m,delta_address+i*4u)));
        remaining--;position+=12u;
    }
    for (i=0;i<3;i++) pm_store(m,0x6ba920u+slot*12u+i*4u,pm_load(m,0x74aee0u+slot*12u+i*4u,4),4);
    return m->error;
}
