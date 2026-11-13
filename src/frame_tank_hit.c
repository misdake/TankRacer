#include "frame_tank_hit.h"
#include "physics_memory.h"
#include "physics_vector.h"
#include "physics_body.h"
#include "frame_scene_effects.h"
static uint32_t object_of(physics_memory *m,uint32_t id)
{ uint32_t r=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);return r ? pm_load(m,r+4u,4) : 0; }
/* Native collision records are explicit values, never mapped fake stack DATA.
 * Read DATA lazily in legacy entry points so aliasing/fault prefixes survive. */
static uint32_t contact_load(physics_memory *m,uint32_t address,const uint32_t *words,uint32_t offset,unsigned bytes)
{ uint32_t value;if (!words) return pm_load(m,address+offset,bytes);value=words[offset/4u]>>((offset&3u)*8u);return bytes==1u ? value&255u : bytes==2u ? value&65535u : value; }
static void transform(physics_memory *m,int32_t v[3],uint32_t matrix,int transpose)
{
    int32_t out[3];unsigned i,j;if (!matrix) return;
    for (i=0;i<3u;i++) { out[i]=0;for (j=0;j<3u;j++) out[i]=pm_add(out[i],pm_mul(v[j],pm_i32(m,matrix+(transpose ? j*3u+i : i*3u+j)*4u)));out[i]=pm_sar(out[i],12); }
    for (i=0;i<3u;i++) v[i]=out[i];
}
int frame_tank_health_marker(frame_data_read read,frame_data_write write,void *ctx,uint32_t o)
{
    physics_memory m={read,write,ctx,0};int32_t half,numerator;
    if (!read || !write) return -1;
    pm_store(&m,o+0x1122u,220,2);half=pm_sar(pm_i32(&m,0x531ba4u),1);
    if (pm_i32(&m,o+0x10c2u)<=half) {
        numerator=pm_mul(pm_i32(&m,o+0x10c2u),175);if (m.error) return m.error;
        if (!half || (numerator==INT32_MIN && half==-1)) return -2;
        pm_store(&m,o+0x1124u,(uint32_t)(numerator/half),2);
    }
    return m.error;
}
static void velocity(physics_memory *m,uint32_t o,int32_t local_impulse)
{
    int32_t v[3];unsigned i;uint32_t matrix=pm_load(m,o+0x14u,4);
    for (i=0;i<3u;i++) v[i]=pm_sar(pm_i32(m,o+0x50u+i*4u),6);
    transform(m,v,matrix,0);v[0]=pm_add(v[0],local_impulse);
    if (matrix) { transform(m,v,matrix,1);for (i=0;i<3u;i++) pm_write32(m,o+0x50u+i*4u,v[i]); }
    for (i=0;i<3u;i++) pm_write32(m,o+0x50u+i*4u,pm_mul(pm_i32(m,o+0x50u+i*4u),64));
}
static int frame_tank_contact_velocity_core(frame_data_read read,frame_data_write write,void *ctx,uint32_t o,uint32_t p,const uint32_t *words)
{
    physics_memory m={read,write,ctx,0};int32_t impulse[3];unsigned i;
    if (!read || !write) return -1;
    if (!pm_load(&m,o+0x14u,4)) return m.error ? m.error : -4;
    for (i=0;i<3u;i++) { impulse[i]=pm_signed(contact_load(&m,p,words,12u+i*4u,4)); }transform(&m,impulse,pm_load(&m,o+0x14u,4),0);
    for (i=1;i<3u;i++) impulse[i]=0;
    impulse[0]=pm_sar(pm_mul(impulse[0],(int32_t)contact_load(&m,p,words,0x54u,2)),contact_load(&m,p,words,0x56u,1));
    velocity(&m,o,impulse[0]);return m.error;
}
static void selected_marker(physics_memory *m,uint32_t entity,uint32_t o,uint32_t marker)
{
    if ((entity==pm_load(m,0x53e8b8u,4) && entity!=pm_load(m,0x53b69cu,4)) || entity==pm_load(m,0x53e8bcu,4)) pm_store(m,o+0x107eu,marker,4);
}
static int frame_tank_hit_core(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t p,const uint32_t *words,frame_effect_sink feedback,void *feedback_ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t o,kind=0,axis,ability,i,part,count,upper;int32_t normal[3],amount=0,turn=0,value,coefficient,mode,world[3];int status;
    if (!read || !write) return -1;
    o=object_of(&m,entity);ability=pm_load(&m,o+0xd0fu,4);
    if (ability!=1u && ability!=2u && ability!=7u && ability!=8u && pm_i32(&m,o+0xd14u)<900) pm_store(&m,o+0xd14u,900,4);
    for (i=0;i<3u;i++) normal[i]=pm_neg(pm_signed(contact_load(&m,p,words,12u+i*4u,4)));
    if (m.error) return m.error;
    physics_vector_normalize(normal);
    for (i=0;i<3u;i++) amount=pm_add(amount,pm_trunc(pm_mul(pm_signed(contact_load(&m,p,words,24u+i*4u,4)),normal[i]),12));
    if (pm_signed(contact_load(&m,p,words,0x24u,4))) { axis=2;kind=pm_signed(contact_load(&m,p,words,0x24u,4))<0 ? 2u : 1u;turn=pm_signed(contact_load(&m,p,words,0x44u,4));if (kind==2u) turn=pm_neg(turn); }
    else if (pm_signed(contact_load(&m,p,words,0x28u,4))) { axis=0;kind=pm_signed(contact_load(&m,p,words,0x28u,4))<0 ? 4u : 3u; }
    else if (pm_signed(contact_load(&m,p,words,0x2cu,4))) { axis=1;kind=pm_signed(contact_load(&m,p,words,0x2cu,4))<0 ? 6u : 5u;turn=pm_signed(contact_load(&m,p,words,0x3cu,4));if (kind==5u) turn=pm_neg(turn); }
    else axis=0;
    if (axis) {
        if (pm_load(&m,o+0xdedu,1)) {
            value=pm_trunc(pm_mul(pm_mul(amount,(int32_t)contact_load(&m,p,words,0x4cu,2)),200),contact_load(&m,p,words,0x4eu,1));
            i=axis==2u ? 0xbcu : 0xb4u;
            pm_write32(&m,o+i,kind==2u || kind==5u ? pm_add(pm_i32(&m,o+i),value) : pm_sub(pm_i32(&m,o+i),value));
        }
        if (pm_load(&m,0x58e7acu,4)) pm_store(&m,o+(axis==2u ? 0x10dcu : 0x10dau),kind==2u || kind==5u ? 1u : 2u,2);
    }
    turn=pm_trunc(pm_mul(pm_mul(turn,(int32_t)contact_load(&m,p,words,0x50u,2)),110),contact_load(&m,p,words,0x52u,1));pm_write32(&m,o+0xb8u,pm_add(pm_i32(&m,o+0xb8u),turn));
    mode=pm_i32(&m,0x52ee9cu);
    if (mode==5 || pm_signed(contact_load(&m,p,words,0x60u,4))>pm_i32(&m,0x5319c0u) || pm_load(&m,o+0xde5u,1))
        for (i=0;i<3u;i++) pm_write32(&m,o+0x50u+i*4u,pm_trunc(pm_mul(pm_i32(&m,o+0x50u+i*4u),(int32_t)contact_load(&m,p,words,0x58u,2)),contact_load(&m,p,words,0x5au,1)));
    if (mode==3) {
        coefficient=pm_load(&m,0x530c50u,4) ? pm_i32(&m,o+0x111eu) : 900;
        for (i=0;i<3u;i++) pm_write32(&m,o+0x50u+i*4u,pm_trunc(pm_mul(pm_i32(&m,o+0x50u+i*4u),coefficient),10));
    }
    if (m.error) return m.error;
    status=frame_tank_contact_velocity_core(read,write,ctx,o,p,words);if (status) return status;
    if (mode==4 && pm_load(&m,0x53e3fcu,4)) {
        value=pm_signed(contact_load(&m,p,words,0x5cu,4));
        if (value==0 || value==2 || value==5) pm_write32(&m,o+0x10c2u,pm_sub(pm_i32(&m,o+0x10c2u),value==0 ? 4000 : value==2 ? 10000 : 3000));
        if (pm_i32(&m,o+0x10c2u)<0) pm_store(&m,o+0x10c2u,0,4);
        if (m.error) return m.error;
        status=frame_tank_health_marker(read,write,ctx,o);if (status) return status;
    }
    if (kind) {
        upper=pm_signed(contact_load(&m,p,words,0x40u,4))>1024;part=o+kind+upper*104u+0x28du;count=pm_load(&m,part,1);
        if (count<10u) {
            pm_store(&m,part,++count,1);
            if (count>6u && count>pm_load(&m,o+(upper ? 0xdf1u : 0xdf2u),1)) { pm_store(&m,o+(upper ? 0xdf1u : 0xdf2u),count,1);pm_store(&m,o+(upper ? 0xdf3u : 0xdf4u),kind-1u,1); }
            if (count>1u && pm_load(&m,o+0xde5u,1)) {
                for (i=0;i<3u;i++) { world[i]=pm_signed(contact_load(&m,p,words,0x30u+i*4u,4)); }if (m.error) return m.error;
                status=upper ? physics_turret_damage(read,write,ctx,o,world) : physics_body_damage(read,write,ctx,o,world);if (status) return status;
            }
        }
    }
    selected_marker(&m,entity,o,128);if (m.error) return m.error;
    if (pm_load(&m,o+0xde5u,1)) {
        kind=contact_load(&m,p,words,0x5cu,4);if (m.error) return m.error;
        return frame_feedback_collision(read,write,ctx,kind,0,feedback,feedback_ctx);
    }
    return m.error;
}
int frame_tank_point_hit(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t point)
{
    physics_memory m={read,write,ctx,0};uint32_t o,matrix,i;int32_t d[3],impulse[3],value;int status;
    if (!read || !write) return -1;
    o=object_of(&m,entity);matrix=pm_load(&m,o+0x14u,4);
    for (i=0;i<3u;i++) { d[i]=pm_sub(pm_i32(&m,point+i*4u),pm_i32(&m,o+i*4u));impulse[i]=pm_mul(pm_neg(d[i]),4); }
    transform(&m,d,matrix,0);value=pm_sub(pm_i32(&m,o+0xbcu),pm_mul(d[0],700));pm_write32(&m,o+0xbcu,value);
    if (pm_abs(value)<800000) { value=value>0 ? 800000 : -800000;pm_write32(&m,o+0xbcu,value);if (pm_load(&m,0x58e7acu,4)) pm_store(&m,o+0x10dcu,value>0 ? 1u : 2u,2); }
    value=pm_i32(&m,o+0xbcu);
    if (pm_abs(value)>1200000) { value=value>0 ? 1200000 : -1200000;pm_write32(&m,o+0xbcu,value);if (pm_load(&m,0x58e7acu,4)) pm_store(&m,o+0x10dcu,value>0 ? 1u : 2u,2); }
    pm_write32(&m,o+0xb4u,pm_add(pm_i32(&m,o+0xb4u),pm_mul(d[2],200)));
    if (pm_load(&m,0x58e7acu,4) && pm_abs(d[2])>250) pm_store(&m,o+0x10dau,d[2]>0 ? 1u : 2u,2);
    if ((d[0]<0 && d[2]>0) || (d[0]>0 && d[2]<0)) pm_write32(&m,o+0xb8u,pm_add(pm_i32(&m,o+0xb8u),150000));
    else if ((d[0]<0 && d[2]<0) || (d[0]>0 && d[2]>0)) pm_write32(&m,o+0xb8u,pm_sub(pm_i32(&m,o+0xb8u),150000));
    for (i=0;i<3u;i++) pm_write32(&m,o+0x50u+i*4u,pm_trunc(pm_mul(pm_i32(&m,o+0x50u+i*4u),850),10));
    if (!matrix) return m.error ? m.error : -4;
    transform(&m,impulse,matrix,0);if (pm_abs(impulse[0])<8000) impulse[0]=impulse[0]>0 ? 8000 : -8000;
    velocity(&m,o,pm_sar(impulse[0],1));
    if (pm_load(&m,0x52ee9cu,4)==4u && pm_load(&m,0x53e3fcu,4)) {
        pm_write32(&m,o+0x10c2u,pm_sub(pm_i32(&m,o+0x10c2u),5000));if (m.error) return m.error;
        status=frame_tank_health_marker(read,write,ctx,o);if (status) return status;
    }
    if (pm_i32(&m,o+0x10c2u)<0) pm_store(&m,o+0x10c2u,0,4);
    selected_marker(&m,entity,o,200);return m.error;
}
static int frame_tank_shell_hit_core(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t contact,const uint32_t *words,frame_effect_sink feedback,void *feedback_ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t o,i;int status;
    if (!read || !write) return -1;
    o=object_of(&m,entity);if (m.error) return m.error;status=frame_tank_hit_core(read,write,ctx,entity,contact,words,feedback,feedback_ctx);if (status) return status;
    if (!pm_load(&m,o+0xde5u,1)) {
        pm_store(&m,o+0x6a6u,1,2);pm_store(&m,o+0x6a8u,30,2);
        if (pm_signed(contact_load(&m,contact,words,0x60u,4))>pm_i32(&m,0x5319c0u)) for (i=0;i<3u;i++) pm_write32(&m,o+0x50u+i*4u,pm_trunc(pm_mul(pm_i32(&m,o+0x50u+i*4u),(int32_t)contact_load(&m,contact,words,0x58u,2)),contact_load(&m,contact,words,0x5au,1)));
    } else pm_store(&m,o+0x6a8u,pm_load(&m,o+0x6a8u,2)+1u,2);
    if (m.error) return m.error;
    status=frame_temp_effects_start(read,write,ctx,o);if (status) return status;pm_store(&m,o+0xdd0u,6000,2);return m.error;
}

int frame_tank_contact_velocity(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,uint32_t contact)
{ return frame_tank_contact_velocity_core(read,write,ctx,object,contact,0); }
int frame_tank_hit(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t contact,frame_effect_sink feedback,void *feedback_ctx)
{ return frame_tank_hit_core(read,write,ctx,entity,contact,0,feedback,feedback_ctx); }
int frame_tank_shell_hit(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t contact,frame_effect_sink feedback,void *feedback_ctx)
{ return frame_tank_shell_hit_core(read,write,ctx,entity,contact,0,feedback,feedback_ctx); }
int frame_tank_hit_values(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const uint32_t words[25],frame_effect_sink feedback,void *feedback_ctx)
{ if (!words) return -1;return frame_tank_hit_core(read,write,ctx,entity,0,words,feedback,feedback_ctx); }
int frame_tank_shell_hit_values(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const uint32_t words[25],frame_effect_sink feedback,void *feedback_ctx)
{ if (!words) return -1;return frame_tank_shell_hit_core(read,write,ctx,entity,0,words,feedback,feedback_ctx); }
