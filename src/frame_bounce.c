#include "frame_bounce.h"
#include "frame_text.h"
#include "frame_spatial.h"
#include "physics_memory.h"
#include "physics_vector.h"
static uint32_t object_of(physics_memory *m,uint32_t entity)
{ uint32_t r=pm_load(m,pm_load(m,0x55c694u,4)+entity*8u,4);return r ? pm_load(m,r+4u,4) : 0; }
#define TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
static int dispatch(physics_memory *m,uint32_t entity,uint32_t global,const frame_entity_factory_services *e)
{ uint32_t unused,d=pm_load(m,global,4);if (m->error) return m->error;return frame_entity_dispatch(m->read,m->write,m->ctx,entity,d,0,0,e->method,e->method_ctx,&unused); }
static int transform(physics_memory *m,uint32_t matrix,int32_t vector[3])
{
    unsigned i,j;int32_t out[3];if (!matrix) return m->error;
    for (i=0;i<3u;i++) { out[i]=0;for (j=0;j<3u;j++) out[i]=pm_add(out[i],pm_mul(pm_i32(m,matrix+(i*3u+j)*4u),vector[j]));out[i]=pm_sar(out[i],12); }
    for (i=0;i<3u;i++) vector[i]=out[i];
    return m->error;
}
static int height(physics_memory *m,uint32_t o,int32_t *out)
{ if (m->error) return m->error;return physics_terrain_height(m->read,m->ctx,pm_load(m,0x55ae60u,4),pm_i32(m,o),pm_i32(m,o+8u),out); }
static int find(physics_memory *m,uint32_t parser,uint32_t first,uint32_t second,uint32_t limit,uint32_t *found)
{ int status;if (m->error) return m->error;status=frame_text_find(m->read,m->write,m->ctx,parser,first,limit,found);if (status || *found) return status;return frame_text_find(m->read,m->write,m->ctx,parser,second,limit,found); }
int frame_bounce_initialize(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o,parent,unused;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,entity);parent=pm_load(&m,0x544f80u,4);TRY(frame_class_dispatch(read,ctx,parent,entity,e->method,e->method_ctx,&unused));
    pm_store(&m,o+0xb8u,35,2);pm_store(&m,o+0xbeu,10,1);pm_store(&m,o+0xbfu,20,1);pm_store(&m,o+0x7cu,10,4);pm_store(&m,o+0x5cu,0,4);return m.error;
}
int frame_bounce_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *e,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,parent,unused,found,more,base,count,i,j,a,b,equal,value;
    unsigned char name[28];int32_t ground,x,z;int status;
    static const uint32_t fields[][4]={
        {0x52e380u,0x52e374u,0xb8u,2},{0x52e368u,0x52e35cu,0xbeu,1},{0x52e34cu,0x52e33cu,0xbfu,1},
        {0x52e330u,0x52e324u,0x7cu,4},{0x52e318u,0x52e30cu,0x5cu,4},{0x52e2fcu,0x52e2ecu,0xb4u,2},
        {0x52e2dcu,0x52e2ccu,0xb6u,2},{0x52e2bcu,0x52e2acu,0xb0u,2},{0x52e29cu,0x52e28cu,0xb2u,2},{0x52e27cu,0x52e26cu,0xaeu,2}};
    static const unsigned schema[][3]={{0xb8,0x1a,2},{0xbe,0x19,1},{0xbf,0x1c,1},{0x7c,0x1d,0},{0x5c,0x20,3},{0xb4,0x2e,2},{0xb6,0x30,2},{0xb0,0x22,2},{0xb2,0x24,2},{0xae,0x38,2}};
    if (!read || !write || !e || !e->work_limit || !out) return -1;
    o=object_of(&m,entity);parent=pm_load(&m,0x544f80u,4);TRY(frame_class_dispatch_payload(read,ctx,parent,entity,parser,e->method,e->method_ctx,&unused));
    TRY(find(&m,parser,0x52e394u,0x52e38cu,e->work_limit,&found));
    if (found) {
        base=pm_load(&m,0x544f84u,4);count=pm_load(&m,0x544f88u,4);TRY(frame_text_next(read,write,ctx,parser,name,sizeof(name),e->work_limit,&more));
        for (i=0;pm_signed(i)<pm_signed(count);i++) {
            if (i>=e->work_limit) return -3;
            equal=0;
            for (j=0;j<sizeof(name);j++) {
                a=name[j];b=pm_load(&m,base+i*76u+j,1);if (m.error) return m.error;
                if (a>=128u || b>=128u) return -5;
                if (a>=65u && a<=90u) a+=32u;
                if (b>=65u && b<=90u) b+=32u;
                if (a!=b) break;
                if (!a) { equal=1;break; }
            }
            if (equal) {
                for (j=0;j<10u;j++) {
                    a=base+i*76u+schema[j][1];b=schema[j][2];
                    value=b==0u ? (uint32_t)pm_i8(&m,a) : b==3u ? (uint32_t)pm_i16(&m,a) : pm_load(&m,a,b);
                    pm_store(&m,o+schema[j][0],value,b==0u || b==3u ? 4u : b);
                }
                break;
            }
        }
    }
    for (i=0;i<10u;i++) {
        TRY(find(&m,parser,fields[i][0],fields[i][1],e->work_limit,&found));
        if (found) {
            if (fields[i][3]==4u) TRY(frame_text_read_i32(read,write,ctx,parser,o+fields[i][2],e->work_limit,&more));
            else { value=0;TRY(frame_text_read_value(read,write,ctx,parser,e->work_limit,&value,&more));if (!more) return -4;pm_store(&m,o+fields[i][2],value,fields[i][3]); }
            if (i==9u) pm_store(&m,o+0xaeu,pm_load(&m,o+0xaeu,2)*182u,2);
        }
    }
    status=height(&m,o,&ground);if (status<0) return status;
    pm_write32(&m,o+0xbau,ground);pm_store(&m,o+0x80u,0,4);x=pm_i32(&m,o);z=pm_i32(&m,o+8u);
    if (pm_load(&m,0x52ed90u,4)==5u && x>=pm_i32(&m,0x531be8u) && x<=pm_i32(&m,0x531bf8u) && z>=pm_i32(&m,0x531bf0u) && z<=pm_i32(&m,0x531c00u)) {
        pm_store(&m,o+0xc6u,1,1);pm_store(&m,o+0xc7u,pm_load(&m,0x52e25cu,4),4);pm_store(&m,o+0xcbu,20,1);
    } else { value=pm_load(&m,0x530ac8u,4);pm_store(&m,o+0xcbu,4,1);pm_store(&m,o+0xc7u,value,4); }
    value=pm_load(&m,0x53cc28u,4);
    if (value && pm_load(&m,o+0x48u,4)==value) { pm_store(&m,o+0xccu,1,1);pm_write32(&m,o+4u,pm_add(pm_i32(&m,o+4u),750)); }
    if (pm_load(&m,0x546000u,4) && !pm_load(&m,o+0xc6u,1)) {
        pm_store(&m,o+0xc6u,1,1);value=pm_load(&m,0x52e25cu,4);pm_store(&m,o+0xcbu,20,1);pm_write32(&m,o+0xc7u,pm_sar(pm_mul(pm_signed(value),5),1));
    }
    *out=0;return m.error;
}
int frame_bounce_hit(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t vehicle,const frame_entity_factory_services *e,const physics_trig *trig,frame_effect_sink feedback,void *feedback_ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t o,tank,matrix,i,unused,seed;int32_t relative[3],velocity[3],rotated[3],angle,bounce,split,speed;int status;
    if (!read || !write || !e || !e->work_limit || !trig || !trig->sine || !trig->cosine) return -1;
    o=object_of(&m,entity);tank=object_of(&m,vehicle);for (i=0;i<3u;i++) relative[i]=pm_sub(pm_i32(&m,o+i*4u),pm_i32(&m,tank+i*4u));
    TRY(transform(&m,pm_load(&m,tank+0x14u,4),relative));angle=pm_sar(pm_neg(pm_mul(pm_i16(&m,o+0xaeu),relative[0])),11);
    speed=pm_abs(pm_i32(&m,tank+0xecu));bounce=pm_i16(&m,o+0xb8u);split=pm_i8(&m,o+0xbfu);
    pm_write32(&m,o+0x54u,pm_sar(pm_mul(pm_mul(pm_sar(speed,6),split),bounce),11));
    pm_write32(&m,o+0x58u,pm_sar(pm_mul(pm_mul(pm_sar(pm_i32(&m,tank+0x58u),6),pm_sub(128,split)),bounce),11));
    pm_write32(&m,o+0x50u,pm_sar(pm_mul(pm_i32(&m,tank+0x50u),pm_sar(bounce,4)),6));
    for (i=0;i<3u;i++) velocity[i]=pm_i32(&m,o+0x50u+i*4u);
    physics_vector_yaw(velocity,rotated,(uint32_t)angle,trig);
    pm_write32(&m,o+0x50u,rotated[0]);pm_write32(&m,o+0x58u,rotated[2]);pm_write32(&m,o+0x54u,rotated[1]);
    for (i=0;i<9u;i++) pm_store(&m,o+0x18u+i*4u,pm_load(&m,tank+0x18u+i*4u,4),4);
    matrix=pm_load(&m,o+0x14u,4);TRY(frame_matrix_rotate(read,write,ctx,matrix,1,(uint32_t)angle,trig));pm_store(&m,o+0xc4u,1,1);
    if (!pm_load(&m,o+0x80u,4)) {
        pm_store(&m,o+0x80u,1,4);TRY(frame_entity_activate(read,write,ctx,entity,e->method,e->method_ctx,&unused));
    }
    pm_store(&m,o+0xc2u,0,2);for (i=0;i<3u;i++) velocity[i]=pm_i32(&m,o+0x50u+i*4u);
    pm_write32(&m,o+0xaau,pm_sar(pm_neg(pm_mul(physics_approximate_distance(velocity),pm_i16(&m,o+0xb0u))),pm_load(&m,o+0xb2u,1)));
    speed=pm_abs(pm_i32(&m,tank+0xecu));
    if (speed>250000) { seed=pm_load(&m,0x55b6acu,4)*1664525u+1013904223u;pm_store(&m,0x55b6acu,seed,4);pm_store(&m,o+0xc5u,(seed>>4)&1u,1); }
    else pm_store(&m,o+0xc5u,0,1);
    if (pm_load(&m,tank+0xde5u,1)) { seed=pm_load(&m,o+0x48u,4);TRY(frame_feedback_impact(read,write,ctx,pm_load(&m,seed+12u,2),feedback,feedback_ctx)); }
    return m.error;
}
int frame_bounce_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t o,i,matrix,timer,period,packed;int32_t dt,spin,ground,normal[3],local[3],angle,velocity[3],dot,x,z,bounce;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,entity);if (!pm_load(&m,o+0x80u,4)) return m.error;
    if (pm_i16(&m,o+0xc2u)>20000) { TRY(dispatch(&m,entity,0x545ed0u,e));return m.error; }
    dt=pm_i32(&m,0x5904fcu);pm_store(&m,o+0xc2u,pm_load(&m,o+0xc2u,2)+(uint32_t)dt,2);
    for (i=0;i<3u;i++) pm_write32(&m,o+i*4u,pm_add(pm_i32(&m,o+i*4u),pm_sar(pm_mul(dt,pm_i32(&m,o+0x50u+i*4u)),8)));
    spin=pm_add(pm_i32(&m,o+0xaau),pm_sar(pm_mul(pm_i16(&m,o+0xb4u),dt),pm_load(&m,o+0xb6u,1)));pm_write32(&m,o+0xaau,spin);if (spin>0) pm_store(&m,o+0xaau,0,4);
    spin=pm_i32(&m,o+0xaau);if (pm_load(&m,o+0xc5u,1)) spin=pm_neg(spin);matrix=pm_load(&m,o+0x14u,4);
    TRY(frame_matrix_rotate(read,write,ctx,matrix,0,(uint32_t)spin,trig));timer=(pm_load(&m,o+0xc0u,2)+1u)&65535u;pm_store(&m,o+0xc0u,timer,2);period=pm_load(&m,o+0xcbu,1);
    if (m.error) return m.error;
    if (!period) return -2;
    if (!(timer%period)) {
        status=height(&m,o,&ground);if (status<0) return status;
        if (ground!=-2147483647) { pm_write32(&m,o+0xbau,ground);if (pm_load(&m,o+0xccu,1)) pm_write32(&m,o+0xbau,pm_add(ground,750)); }
    } else ground=pm_i32(&m,o+0xbau);
    if (!pm_load(&m,o+0xc4u,1)) {
        x=pm_mul(pm_i8(&m,o+0xbeu),pm_i32(&m,o+0x7cu));pm_store(&m,o+4u,pm_load(&m,o+0xbau,4),4);
        if (pm_i32(&m,o+0x50u)<=x && pm_i32(&m,o+0x58u)<=x) {
            status=physics_terrain_normal_height(read,ctx,pm_load(&m,0x55ae60u,4),pm_i32(&m,o),pm_i32(&m,o+8u),&ground,normal);if (status<0) return status;
            if (ground==-2147483647) TRY(dispatch(&m,entity,0x545ed0u,e));
            else {
                if (!pm_load(&m,o+0xccu,1)) {
                    if (!matrix) return -4; /* original separate stack output is undefined */
                    for (i=0;i<3u;i++) local[i]=normal[i];
                    TRY(transform(&m,matrix,local));if (!trig) return -5;TRY(physics_angle_lookup(local[1],local[2],trig,&angle));
                    TRY(frame_matrix_rotate(read,write,ctx,matrix,0,(uint32_t)angle,trig));
                }
                packed=pm_load(&m,o+0x90u,4);pm_store(&m,o+0x80u,0,4);
                TRY(frame_spatial_remove(read,write,ctx,entity,packed,e->release,e->system_ctx,e->work_limit));
                TRY(frame_spatial_insert(read,write,ctx,entity,e->allocate,e->release,e->system_ctx,e->work_limit,&packed));pm_store(&m,o+0x90u,packed,4);
                TRY(frame_entity_queue(read,write,ctx,entity,0,e->allocate,e->release,e->system_ctx,e->work_limit));
            }
        }
    } else if (pm_i32(&m,o+4u)<pm_i32(&m,o+0xbau)) {
        pm_store(&m,o+4u,pm_load(&m,o+0xbau,4),4);bounce=pm_i16(&m,o+0xb8u);x=pm_sar(pm_neg(pm_mul(pm_i32(&m,o+0x54u),bounce)),7);pm_write32(&m,o+0x54u,x);
        if (x<pm_mul(pm_i32(&m,o+0x7cu),bounce)) { pm_store(&m,o+0x54u,0,4);pm_store(&m,o+0xc4u,0,1); }
    }
    if (ground==-2147483647) {
        for (i=0;i<3u;i++) velocity[i]=pm_i32(&m,o+i*4u);
        status=physics_fallback_query(read,ctx,pm_load(&m,0x55ae60u,4),velocity,0,normal);if (status<0) return status;
        if (status) {
            dot=pm_add(pm_trunc(pm_mul(normal[0],pm_i32(&m,o+0x50u)),11),pm_trunc(pm_mul(normal[2],pm_i32(&m,o+0x58u)),11));
            if (dot<0) {
                x=pm_sub(pm_i32(&m,o+0x50u),pm_trunc(pm_mul(dot,normal[0]),12));pm_write32(&m,o+0x50u,x);
                z=pm_trunc(pm_mul(dot,normal[2]),12);bounce=pm_i16(&m,o+0xb8u);pm_write32(&m,o+0x50u,pm_sar(pm_mul(x,bounce),7));
                pm_write32(&m,o+0x58u,pm_sar(pm_mul(pm_sub(pm_i32(&m,o+0x58u),z),bounce),7));
                if (!matrix) return -4;
                for (i=0;i<3u;i++) local[i]=pm_i32(&m,o+0x50u+i*4u);
                TRY(transform(&m,matrix,local));if (!trig) return -5;TRY(physics_angle_lookup(local[0],local[2],trig,&angle));
                TRY(frame_matrix_rotate(read,write,ctx,matrix,1,(uint32_t)pm_neg(angle),trig));
            }
        }
    }
    pm_write32(&m,o+0x54u,pm_sub(pm_i32(&m,o+0x54u),pm_mul(pm_i32(&m,o+0xc7u),pm_i32(&m,0x5904fcu))));
    x=pm_i32(&m,o+0x50u);pm_write32(&m,o+0x50u,pm_sub(x,pm_sar(pm_mul(pm_i8(&m,o+0xbeu),x),7)));
    z=pm_i32(&m,o+0x58u);pm_write32(&m,o+0x58u,pm_sub(z,pm_sar(pm_mul(pm_i8(&m,o+0xbeu),z),7)));return m.error;
}
#undef TRY
