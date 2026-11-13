#include "frame_mesh_break.h"
#include "physics_memory.h"
#include "frame_player_collision.h"
#include "frame_spatial.h"

static uint32_t object_of(physics_memory *m,uint32_t entity)
{ uint32_t r=pm_load(m,pm_load(m,0x55c694u,4)+entity*8u,4);return r ? pm_load(m,r+4u,4) : 0; }
int frame_mesh_break_prepare(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t name,
                                 const frame_entity_factory_services *e,const frame_shape_resources *resources)
{
    physics_memory m={read,write,ctx,0};uint32_t o,a,source,shape,bytes,power,states,descriptor,unused;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
#define TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
    o=object_of(&m,entity);a=pm_load(&m,o+0x88u,4);
    if (!pm_load(&m,a+0x21u,1)) {
        TRY(frame_shape_load(read,write,ctx,name,0,e,resources,&source));
        TRY(frame_shape_clone_loaded(read,write,ctx,source,a+0x44u,e,&shape));
        a=pm_load(&m,o+0x88u,4);pm_store(&m,a,shape,4);
        a=pm_load(&m,o+0x88u,4);pm_store(&m,a+0x18u,pm_load(&m,pm_load(&m,a,4)+2u,2),4);
        a=pm_load(&m,o+0x88u,4);pm_store(&m,a+0x1cu,pm_load(&m,pm_load(&m,a,4)+2u,2)*32u,4);
        a=pm_load(&m,o+0x88u,4);bytes=pm_load(&m,a+0x1cu,4);power=2;
        while (power<bytes) power*=2u;
        pm_store(&m,a+0x1cu,power,4);a=pm_load(&m,o+0x88u,4);bytes=pm_load(&m,a+0x1cu,4);
        TRY(frame_pool_allocate_zero(read,write,ctx,bytes,e->allocate,e->release,e->system_ctx,e->work_limit,&states));
        a=pm_load(&m,o+0x88u,4);pm_store(&m,a+0x14u,states,4);a=pm_load(&m,o+0x88u,4);
        if (pm_load(&m,a+0x20u,1)) {
            descriptor=pm_load(&m,0x545f68u,4);
            TRY(frame_entity_dispatch(read,write,ctx,entity,descriptor,0,0,e->method,e->method_ctx,&unused));
            a=pm_load(&m,o+0x88u,4);pm_store(&m,a+0x21u,1,1);
        }
    }
    return m.error;
#undef TRY
}
static int32_t scale(physics_memory *m,uint32_t object,uint32_t field,int32_t value)
{
    uint32_t aux=pm_load(m,object+0x88u,4),multiplier=pm_load(m,aux+field,2),shift=pm_load(m,aux+field+2u,1);
    return pm_trunc(multiplier==1u ? value : pm_mul(value,(int32_t)multiplier),shift);
}
static uint32_t random_next(physics_memory *m)
{ uint32_t value=pm_load(m,0x55b6acu,4)*1664525u+1013904223u;pm_store(m,0x55b6acu,value,4);return value; }
static int32_t random_signed(physics_memory *m)
{ uint32_t sign=random_next(m),value=(random_next(m)>>5)&32767u;return sign&32u ? (int32_t)value+20000 : -20000-(int32_t)value; }
/* The branch tests the FIRST draw/product but shifts the SECOND. This is
 * observable even when the second product has the opposite sign. */
static int32_t scatter(physics_memory *m,uint32_t object,uint32_t field,int32_t speed,int angular)
{
    int32_t value,product;uint32_t aux,multiplier,shift;int negative;
    value=angular ? random_signed(m) : (int32_t)((random_next(m)>>4)&65535u)-32767;
    aux=pm_load(m,object+0x88u,4);multiplier=pm_load(m,aux+field,2);
    product=pm_mul(pm_mul(value,(int32_t)multiplier),speed);negative=product<0;
    value=angular ? random_signed(m) : (int32_t)((random_next(m)>>4)&65535u)-32767;
    aux=pm_load(m,object+0x88u,4);multiplier=pm_load(m,aux+field,2);shift=pm_load(m,aux+field+2u,1);
    product=pm_mul(pm_mul(value,(int32_t)multiplier),speed);
    return negative ? pm_neg(pm_sar(pm_neg(product),shift)) : pm_sar(product,shift);
}
int frame_mesh_break_hit(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t other,
                             const frame_entity_factory_services *e,const frame_shape_resources *resources,
                             const uint16_t *paired_vertex_tail,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t r,o,a,tank,shape,matrix,at,end,i,j,tri,second,state,used=0,tail=0,defined=0,lifetime,packed;
    int32_t velocity[3]={0,0,0},transformed[3],sum,speed,height;int status,shared;
    if (!read || !write || !e || !e->work_limit || !out) return -1;
#define CHECK() do { if (m.error) return m.error; } while (0)
#define WORK() do { CHECK();if (used++>=e->work_limit) return -3; } while (0)
    r=pm_load(&m,pm_load(&m,0x55c694u,4)+entity*8u,4);o=r ? pm_load(&m,r+4u,4) : 0;
    lifetime=(r ? pm_load(&m,r,4) : 0)==pm_load(&m,0x545094u,4) ? 1500u : 2000u;
    if (other) { tank=object_of(&m,other);for (i=0;i<3u;i++) velocity[i]=pm_i32(&m,tank+80u+i*4u); }
    a=pm_load(&m,o+0x88u,4);CHECK();
    if (pm_load(&m,a+34u,2)) { CHECK();*out=entity;return 0; }
    status=frame_mesh_break_prepare(read,write,ctx,entity,a+4u,e,resources);if (status) return status;
    shape=pm_load(&m,a,4);for (i=0;i<3u;i++) velocity[i]=pm_trunc(velocity[i],6);
    matrix=pm_load(&m,o+20u,4);
    if (matrix) {
        for (i=0;i<3u;i++) { sum=0;for (j=0;j<3u;j++) sum=pm_add(sum,pm_mul(velocity[j],pm_i32(&m,matrix+(i*3u+j)*4u)));transformed[i]=pm_sar(sum,12); }
        for (i=0;i<3u;i++) velocity[i]=transformed[i];
    }
    speed=pm_sar(frame_collision_change_magnitude(velocity),6);
    for (i=0;i<3u;i++) velocity[i]=scale(&m,o,56u,velocity[i]);
    if (!other) speed=100;
    CHECK();status=physics_terrain_height(read,ctx,pm_load(&m,0x55ae60u,4),pm_i32(&m,o),pm_i32(&m,o+8u),&height);
    if (status<0) return status;
    pm_write32(&m,a+40u,height);at=pm_load(&m,a+20u,4);end=at+pm_load(&m,a+24u,4)*32u;
    for (;at<end;at+=32u) {
        WORK();
        for (i=0;i<3u;i++) { sum=scatter(&m,o,60u,speed,0);pm_write32(&m,at+i*4u,pm_add(velocity[i],sum)); }
        for (i=0;i<3u;i++) { sum=scatter(&m,o,64u,speed,1);pm_write32(&m,at+12u+i*4u,sum); }
        i=random_next(&m);pm_store(&m,at+30u,2,2);pm_store(&m,at+28u,65535u,2);pm_store(&m,at+24u,((i>>4)&1023u)+lifetime,4);
    }
    if (paired_vertex_tail) { tail=*paired_vertex_tail;defined=1; }
    tri=pm_load(&m,shape+8u,4);state=pm_load(&m,a+20u,4);i=0;
    while (pm_signed(i)<pm_sub(pm_i32(&m,a+24u),1)) {
        WORK();second=tri+24u;
        pm_store(&m,tri+18u,pm_load(&m,tri+18u,1)|1u,1);pm_store(&m,second+18u,pm_load(&m,second+18u,1)|1u,1);
        shared=0;
        for (j=0;j<3u;j++) for (r=0;r<3u;r++) if (pm_load(&m,tri+8u+j,1)==pm_load(&m,second+8u+r,1)) shared=1;
        if (shared) {
            for (j=0;j<3u;j++) {
                r=pm_load(&m,second+8u+j,1);
                if (r!=pm_load(&m,tri+8u,1) && r!=pm_load(&m,tri+9u,1) && r!=pm_load(&m,tri+10u,1)) { tail=r;defined=1; }
            }
            pm_store(&m,state+30u,0,2);tri+=24u;state+=32u;i++;pm_store(&m,state+30u,1,2);CHECK();
            if (!defined) return -4;
            pm_store(&m,state+28u,tail,2);
        }
        tri+=24u;state+=32u;i++;
    }
    pm_store(&m,o+72u,shape,4);pm_store(&m,shape+12u,5120,2);pm_store(&m,a+36u,0,4);pm_store(&m,a+34u,1,2);
    packed=pm_load(&m,o+0x90u,4);CHECK();
    status=frame_spatial_remove(read,write,ctx,entity,packed,e->release,e->system_ctx,e->work_limit);if (status) return status;
    *out=entity;return 0;
#undef WORK
#undef CHECK
}
static int vertex(physics_memory *m,uint32_t shape,uint32_t state,uint32_t index,const int32_t step[3],
                       const int32_t center[3],const physics_trig *t)
{
    uint32_t at=pm_load(m,shape+4u,4)+index*8u,angle,i;int32_t v[3],x,y,z,c,s,dt;
    for (i=0;i<3u;i++) pm_store(m,at+i*2u,pm_load(m,at+i*2u,2)+(uint32_t)step[i],2);
    for (i=0;i<3u;i++) v[i]=pm_sub(pm_i16(m,at+i*2u),center[i]);
    if (m->error) return m->error;
    if (!t || !t->sine || !t->cosine) return -5;
    dt=pm_i32(m,0x5904fcu);angle=((uint32_t)pm_mul(dt,pm_i32(m,state+16u))&65532u)>>2;c=t->cosine[angle];s=t->sine[angle];
    x=pm_trunc(pm_sub(pm_mul(c,v[0]),pm_mul(s,v[2])),12);z=pm_trunc(pm_add(pm_mul(c,v[2]),pm_mul(s,v[0])),12);
    dt=pm_i32(m,0x5904fcu);angle=((uint32_t)pm_mul(dt,pm_i32(m,state+12u))&65532u)>>2;c=t->cosine[angle];s=t->sine[angle];
    y=pm_trunc(pm_add(pm_mul(c,v[1]),pm_mul(s,z)),12);z=pm_trunc(pm_sub(pm_mul(c,z),pm_mul(s,v[1])),12);
    dt=pm_i32(m,0x5904fcu);angle=((uint32_t)pm_mul(dt,pm_i32(m,state+20u))&65532u)>>2;c=t->cosine[angle];s=t->sine[angle];
    v[0]=pm_trunc(pm_sub(pm_mul(c,x),pm_mul(s,y)),12);v[1]=pm_trunc(pm_add(pm_mul(c,y),pm_mul(s,x)),12);v[2]=z;
    for (i=0;i<3u;i++) pm_store(m,at+i*2u,(uint32_t)pm_add(center[i],v[i]),2);
    return m->error;
}
int frame_mesh_break_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                              const frame_entity_factory_services *e,const physics_trig *trig,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,a,shape=0,state=0,tri=0,index=0,used=0,i,at,extra,descriptor,unused;
    int32_t life,minimum,value,step[3],center[3];int status;
    if (!read || !write || !e || !e->work_limit || !out) return -1;
    o=object_of(&m,entity);a=pm_load(&m,o+0x88u,4);
    if (a) {
        shape=pm_load(&m,a,4);state=pm_load(&m,a+0x14u,4);tri=pm_load(&m,shape+8u,4);
        while (pm_signed(index)<pm_i32(&m,a+0x18u)) {
            if (m.error) return m.error;
            if (used++>=e->work_limit) return -3;
            life=pm_i32(&m,state+24u);
            if (life>0) {
                life=pm_sub(life,pm_i32(&m,0x5904fcu));pm_write32(&m,state+24u,life);
                if (life<=0) {
                    pm_store(&m,a+0x24u,pm_load(&m,a+0x24u,4)+1u,4);pm_store(&m,tri+18u,pm_load(&m,tri+18u,2)|64u,2);
                    if (!pm_load(&m,state+30u,2)) { pm_store(&m,a+0x24u,pm_load(&m,a+0x24u,4)+1u,4);pm_store(&m,tri+42u,pm_load(&m,tri+42u,2)|64u,2); }
                } else {
                    at=pm_load(&m,o+0x88u,4);pm_write32(&m,state+4u,pm_sub(pm_i32(&m,state+4u),pm_i32(&m,at+0x2cu)));
                    at=pm_load(&m,shape+4u,4);minimum=pm_i16(&m,at+pm_load(&m,tri+8u,1)*8u+2u);
                    for (i=1;i<3u;i++) { value=pm_i16(&m,at+pm_load(&m,tri+8u+i,1)*8u+2u);if (value<minimum) minimum=value; }
                    if (pm_add(pm_i32(&m,o+4u),pm_mul(minimum,8))<=pm_i32(&m,a+40u) && pm_i32(&m,state+4u)<0) {
                        /* Linear X/Y/Z and angular X/Y/Z use two packed scales. */
                        for (i=0;i<6u;i++) { value=scale(&m,o,(i&1u) ? 0x30u : 0x34u,pm_i32(&m,state+i*4u));pm_write32(&m,state+i*4u,(i&1u) ? pm_neg(value) : value); }
                    }
                    for (i=0;i<3u;i++) step[i]=pm_trunc(pm_mul(pm_i32(&m,state+i*4u),pm_i32(&m,0x5904fcu)),11);
                    for (i=0;i<3u;i++) center[i]=pm_i16(&m,pm_load(&m,shape+16u+i*4u,4)+index*2u);
                    for (i=0;i<3u;i++) {
                        at=pm_load(&m,tri+8u+i,1);if (m.error) return m.error;
                        status=vertex(&m,shape,state,at,step,center,trig);if (status) return status;
                    }
                    if (!pm_load(&m,state+30u,2)) {
                        extra=(uint32_t)pm_i16(&m,pm_load(&m,a+20u,4)+index*32u+60u);if (m.error) return m.error;
                        status=vertex(&m,shape,state,extra,step,center,trig);if (status) return status;
                    }
                    for (i=0;i<3u;i++) { at=pm_load(&m,shape+16u+i*4u,4)+index*2u;pm_store(&m,at,pm_load(&m,at,2)+(uint32_t)step[i],2); }
                }
            }
            if (!pm_load(&m,state+30u,2)) { state+=32u;tri+=24u;index++; }
            state+=32u;tri+=24u;index++;
        }
    }
    if (pm_load(&m,a+36u,4)==pm_load(&m,a+24u,4)) {
        pm_store(&m,a+34u,0,2);pm_store(&m,o+72u,0,4);value=pm_i32(&m,0x52ee9cu);
        descriptor=pm_load(&m,value==0 || value==1 ? 0x545ed0u : 0x545f64u,4);if (m.error) return m.error;
        status=frame_entity_dispatch(read,write,ctx,entity,descriptor,0,0,e->method,e->method_ctx,&unused);if (status) return status;
    }
    if (m.error) return m.error;
    *out=entity;return 0;
}
int frame_mesh_break_native(void *ctx,uint32_t target,uint32_t entity,uint32_t has_payload,uint32_t payload,uint32_t *out)
{
    frame_mesh_break_native_context *c=(frame_mesh_break_native_context *)ctx;
    if (!c || !out) return -1;
    if (target==0x41d460u) return frame_mesh_break_tick(c->base.base.base.read,c->base.base.base.write,c->base.base.base.data_ctx,entity,
                                                       &c->base.base.base.services,c->base.shatter.trig,out);
    if (target==0x41dc00u) return frame_mesh_break_hit(c->base.base.base.read,c->base.base.base.write,c->base.base.base.data_ctx,entity,payload,
                                                      &c->base.base.base.services,c->base.base.parameters.shapes,c->paired_vertex_tail,out);
    return frame_shatter_native(&c->base,target,entity,has_payload,payload,out);
}
