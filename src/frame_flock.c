#include "frame_flock.h"
#include "frame_shatter.h"
#include "frame_text.h"
#include "physics_terrain.h"
#include "physics_vector.h"
#include "physics_memory.h"
static uint32_t object_of(physics_memory *m,uint32_t id)
{ uint32_t r=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);return r ? pm_load(m,r+4u,4) : 0; }
static uint32_t random_next(physics_memory *m)
{ uint32_t seed=pm_load(m,0x55b6acu,4)*1664525u+1013904223u;pm_store(m,0x55b6acu,seed,4);return seed; }
#define TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
static void normalize_at(physics_memory *m,uint32_t at)
{
    int32_t v[3],a,b,c,n;uint32_t i;for (i=0;i<3u;i++) v[i]=pm_i32(m,at+i*4u);a=pm_abs(v[0]);b=pm_abs(v[1]);c=pm_abs(v[2]);
    n=a<b && c<b ? pm_add(pm_add(pm_sar(c,2),pm_sar(a,2)),b) : a>=b && c<a ? pm_add(pm_add(a,pm_sar(b,2)),pm_sar(c,2)) : pm_add(pm_add(pm_sar(b,2),pm_sar(a,2)),c);
    if (n<=0) return;
    physics_vector_normalize(v);for (i=0;i<3u;i++) pm_write32(m,at+i*4u,v[i]);
}
static int matrix_angles(frame_data_read read,frame_data_write write,void *ctx,uint32_t at,const int32_t v[3],const physics_trig *trig)
{
    uint32_t angles[3]={0,0,0};int32_t pitch,yaw;int status=physics_angle_lookup(pm_neg(v[0]),v[2],trig,&yaw);if (status) return status;
    status=physics_angle_lookup(v[1],v[2],trig,&pitch);if (status) return status;
    if (pitch>16384) pitch=pm_sub(32768,pitch);else if (pitch< -16384) pitch=pm_sub(-32768,pitch);
    angles[0]=(uint32_t)pitch;angles[1]=(uint32_t)yaw;return frame_matrix_from_angles(read,write,ctx,at,angles,trig);
}
static void approach(physics_memory *m,uint32_t at,uint32_t target,int32_t acceleration)
{
    int32_t old=pm_i32(m,at),goal=pm_i32(m,target),value;
    if (old==goal) return;
    value=old<goal ? pm_add(old,acceleration) : pm_sub(old,acceleration);pm_write32(m,at,value);goal=pm_i32(m,target);
    if ((old<goal && value>goal) || (old>goal && value<goal)) pm_write32(m,at,goal);
}
static int wing_tick(physics_memory *m,uint32_t flag,uint32_t first,uint32_t second)
{
    int32_t amount=pm_mul(pm_i32(m,0x5904fcu),2);uint32_t up=pm_load(m,flag,1);int32_t old=pm_i16(m,first+2u),value;
    pm_store(m,first+2u,(uint32_t)(up ? pm_add(old,amount) : pm_sub(old,amount)),2);
    amount=pm_mul(pm_i32(m,0x5904fcu),2);old=pm_i16(m,second+2u);pm_store(m,second+2u,(uint32_t)(up ? pm_add(old,amount) : pm_sub(old,amount)),2);
    value=pm_i16(m,first+2u);
    if ((up && value>50) || (!up && value< -50)) { pm_store(m,first+2u,up ? 50u : (uint32_t)-50,2);pm_store(m,second+2u,up ? 50u : (uint32_t)-50,2);pm_store(m,flag,!up,1); }
    return m->error;
}
static int flying_particles(frame_data_read read,frame_data_write write,void *ctx,uint32_t o,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t i,j,p;int32_t v[3];int status;
    for (i=0;i<8u;i++) {
        p=o+0x7cu+i*80u;
        if (pm_load(&m,p+28u,4)) { if (((random_next(&m)>>4)&65535u)%3u==0) pm_store(&m,p+28u,0,4);continue; }
        for (j=0;j<3u;j++) approach(&m,p+12u+j*4u,o+0x304u+j*4u,pm_i32(&m,p+24u));
        for (j=0;j<3u;j++) pm_write32(&m,p+j*4u,pm_add(pm_i32(&m,p+j*4u),pm_trunc(pm_mul(pm_i32(&m,0x5904fcu),pm_i32(&m,p+12u+j*4u)),8)));
        for (j=0;j<3u;j++) v[j]=pm_i32(&m,p+12u+j*4u);
        TRY(matrix_angles(read,write,ctx,p+32u,v,trig));
    }
    return m.error;
}
static int scatter_start(physics_memory *m,uint32_t o)
{
    uint32_t i,p;pm_store(m,o+0x300u,3000,4);normalize_at(m,o+0x304u);
    for (i=0;i<8u;i++) { p=o+0x88u+i*80u;pm_store(m,p+4u,(random_next(m)>>4)&511u,4);pm_store(m,p,pm_load(m,o+0x304u,4),4);pm_store(m,p+8u,pm_load(m,o+0x30cu,4),4); }
    return m->error;
}
static int flight_start(physics_memory *m,uint32_t o)
{
    uint32_t i;pm_store(m,o+0x308u,0,4);normalize_at(m,o+0x304u);pm_store(m,o+0x304u,pm_load(m,o+0x304u,4)*2u,4);pm_store(m,o+0x30cu,pm_load(m,o+0x30cu,4)*2u,4);pm_store(m,o+0x308u,2048,4);pm_store(m,o+0x300u,1000,4);
    for (i=0;i<8u;i++) if (((random_next(m)>>4)&65535u)%3u==0) pm_store(m,o+0x98u+i*80u,0,4);
    return m->error;
}
int frame_flock_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t o,state,player,i,j,p,seed,angles[3]={0,0,0},unused,head,descriptor;int32_t dt,timer,value,v[3],local[9],result[3],yaw;int status;
    if (!read || !write || !e || !e->work_limit || !trig) return -1;
    o=object_of(&m,entity);state=pm_load(&m,o+0x33cu,4);dt=pm_i32(&m,0x5904fcu);
    if (state==0x40b5e0u) {
        if ((entity^pm_load(&m,0x590564u,4))&7u) return m.error;
        player=object_of(&m,pm_load(&m,0x53e8b8u,4));if (!player) return m.error ? m.error : -4;
        if (pm_abs(pm_sub(pm_i32(&m,player),pm_i32(&m,o)))>=pm_i32(&m,o+0x32cu) || pm_abs(pm_sub(pm_i32(&m,player+8u),pm_i32(&m,o+8u)))>=pm_i32(&m,o+0x32cu)) return m.error;
        pm_store(&m,o+0x2fcu,0,4);i=pm_load(&m,o+0x334u,4);
        if (i==3u || i==1u) { pm_write32(&m,o+0x304u,pm_sub(pm_i32(&m,o),pm_i32(&m,player)));if (i==3u) pm_store(&m,o+0x33cu,0x40b850,4);pm_write32(&m,o+0x30cu,pm_sub(pm_i32(&m,o+8u),pm_i32(&m,player+8u))); }
        else if (i==2u) { pm_write32(&m,o+0x304u,pm_sub(pm_i32(&m,player),pm_i32(&m,o)));pm_write32(&m,o+0x30cu,pm_sub(pm_i32(&m,player+8u),pm_i32(&m,o+8u))); }
        if (i==3u) { status=scatter_start(&m,o);if (status) return status; }
        else if (i==1u || i==2u) { pm_store(&m,o+0x33cu,0x40b710,4);status=flight_start(&m,o);if (status) return status; }
        /* Optional take-off sound has no gameplay state or random draws. */
        return m.error;
    }
    if (state!=0x40b710u && state!=0x40b850u) return -5;
    timer=pm_i32(&m,o+0x300u);
    if (state==0x40b710u) {
        i=pm_load(&m,o+0x2fcu,4);if (i>1u) return m.error;
        if (timer<dt) {
            if (i==1u) {
                descriptor=pm_load(&m,0x545ed0u,4);TRY(frame_entity_dispatch(read,write,ctx,entity,descriptor,0,0,e->method,e->method_ctx,&unused));head=pm_load(&m,0x54b488u,4);TRY(frame_list_remove_value(read,write,ctx,entity,head,e->release,e->system_ctx,e->work_limit));
            } else {
                pm_store(&m,o+0x2fcu,1,4);pm_store(&m,o+0x300u,3000,4);pm_store(&m,o+0x30cu,8192,4);pm_store(&m,o+0x304u,0,4);pm_store(&m,o+0x308u,0,4);
                angles[1]=(random_next(&m)>>4)&65535u;angles[0]=((random_next(&m)>>4)&2047u)-1024u;physics_matrix_from_angles(local,angles,trig);v[0]=v[1]=0;v[2]=8192;
                for (i=0;i<3u;i++) { result[i]=0;for (j=0;j<3u;j++) result[i]=pm_add(result[i],pm_mul(local[j*3u+i],v[j]));pm_write32(&m,o+0x304u+i*4u,pm_sar(result[i],12)); }
                for (i=0;i<8u;i++) pm_store(&m,o+0x98u+i*80u,0,4);
            }
        } else pm_write32(&m,o+0x300u,pm_sub(timer,dt));
        TRY(flying_particles(read,write,ctx,o,trig));
        TRY(wing_tick(&m,o+0x332u,pm_load(&m,o+0x314u,4),pm_load(&m,o+0x318u,4)));TRY(wing_tick(&m,o+0x333u,pm_load(&m,o+0x31cu,4),pm_load(&m,o+0x320u,4)));return m.error;
    }
    if (timer<=dt) { descriptor=pm_load(&m,0x545ed0u,4);TRY(frame_entity_dispatch(read,write,ctx,entity,descriptor,0,0,e->method,e->method_ctx,&unused));head=pm_load(&m,0x54b488u,4);TRY(frame_list_remove_value(read,write,ctx,entity,head,e->release,e->system_ctx,e->work_limit)); }
    else pm_write32(&m,o+0x300u,pm_sub(timer,dt));
    for (i=0;i<8u;i++) {
        p=o+0x8cu+i*80u;seed=random_next(&m);
        if (!(seed&0x70u)) {
            seed=random_next(&m);
            if ((seed&0x30u) && pm_i32(&m,p)<=0) {
                pm_store(&m,p,pm_load(&m,p,4)+((random_next(&m)>>4)&2047u),4);value=(int32_t)((random_next(&m)>>4)&127u)-64;pm_write32(&m,p-4u,pm_add(pm_i32(&m,p-4u),value));value=(int32_t)((random_next(&m)>>4)&127u)-64;
            } else {
                pm_store(&m,p,pm_load(&m,p,4)+((random_next(&m)>>4)&127u),4);value=(int32_t)((random_next(&m)>>4)&2047u)-1024;pm_write32(&m,p-4u,pm_add(pm_i32(&m,p-4u),value));value=(int32_t)((random_next(&m)>>4)&2047u)-1024;
            }
            pm_write32(&m,p+4u,pm_add(pm_i32(&m,p+4u),value));
        }
        pm_write32(&m,p,pm_sub(pm_i32(&m,p),pm_sar(pm_mul(dt,1000),8)));
        pm_write32(&m,p-16u,pm_add(pm_i32(&m,p-16u),pm_trunc(pm_mul(dt,pm_i32(&m,p-4u)),8)));pm_write32(&m,p-12u,pm_add(pm_i32(&m,p-12u),pm_trunc(pm_mul(dt,pm_i32(&m,p)),8)));
        if (pm_i32(&m,p-12u)<pm_i32(&m,o+0x310u)) { pm_store(&m,p-12u,pm_load(&m,o+0x310u,4),4);if (pm_i32(&m,p)<0) pm_store(&m,p,0,4); }
        pm_write32(&m,p-8u,pm_add(pm_i32(&m,p-8u),pm_trunc(pm_mul(dt,pm_i32(&m,p+4u)),8)));TRY(physics_angle_lookup(pm_neg(pm_i32(&m,p-4u)),pm_i32(&m,p+4u),trig,&yaw));angles[1]=(uint32_t)pm_sub(yaw,16380);TRY(frame_matrix_from_angles(read,write,ctx,p+16u,angles,trig));
    }
    return m.error;
}
int frame_flock_start(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t head,unused;int status;if (!read || !write || !e || !e->work_limit) return -1;head=pm_load(&m,0x54b488u,4);TRY(frame_list_append(read,write,ctx,entity,head,e->allocate,e->release,e->system_ctx,e->work_limit,&unused));return frame_entity_activate(read,write,ctx,entity,e->method,e->method_ctx,&unused);
}
int frame_flock_destroy(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o,shape,bytes,parent,unused,i;int status;if (!read || !write || !e || !e->work_limit) return -1;o=object_of(&m,entity);
    for (i=0;i<2u;i++) { shape=pm_load(&m,o+(i ? 0x324u : 72u),4);bytes=pm_load(&m,o+0x328u,4);if (shape && bytes) { TRY(frame_shape_release(read,write,ctx,shape,bytes,e));pm_store(&m,o+(i ? 0x324u : 72u),0,4); } }
    parent=pm_load(&m,0x53e4b0u,4);TRY(frame_class_dispatch(read,ctx,parent,entity,e->method,e->method_ctx,&unused));return m.error;
}

static int vertices_extreme(physics_memory *m,uint32_t shape,uint32_t minimum,uint32_t maximum,uint32_t limit)
{
    uint32_t at=pm_load(m,shape+4u,4),i,count=pm_load(m,shape,2);pm_store(m,minimum,at,4);pm_store(m,maximum,pm_load(m,shape+4u,4),4);
    for (i=0;i<count;i++,at+=8u) { if (i>=limit) return -3;if (pm_i16(m,at)>pm_i16(m,pm_load(m,maximum,4))) pm_store(m,maximum,at,4);else if (pm_i16(m,at)<pm_i16(m,pm_load(m,minimum,4))) pm_store(m,minimum,at,4); }
    return m->error;
}
static int clone_shape(physics_memory *m,const unsigned char *name,uint32_t length,uint32_t size,const frame_entity_factory_services *e,const frame_object_parameters_services *s,uint32_t *out)
{
    uint32_t source;int status=frame_shape_load_name(m->read,m->write,m->ctx,name,length,pm_load(m,0x55ae40u,4),e,s->shapes,&source);if (status) return status;
    return frame_shape_clone_loaded(m->read,m->write,m->ctx,source,size,e,out);
}
int frame_flock_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *e,const frame_object_parameters_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t o,parent,found,more,unused,value,i,p,shape,angles[3]={0,0,0};int32_t ground;int status;unsigned char name[64];
#define FIND_FLOCK(key) frame_text_find(read,write,ctx,parser,(key),e->work_limit,&found)
    if (!read || !write || !e || !e->work_limit || !s || !s->trig) return -1;
    o=object_of(&m,entity);parent=pm_load(&m,0x53e4b0u,4);TRY(frame_class_dispatch_payload(read,ctx,parent,entity,parser,e->method,e->method_ctx,&unused));pm_store(&m,o+0x330u,68,2);pm_store(&m,o+0x32cu,20480,4);
    TRY(FIND_FLOCK(0x52ed3c));if (found) { TRY(frame_text_read_i32(read,write,ctx,parser,o+0x32cu,e->work_limit,&more));pm_store(&m,o+0x32cu,pm_load(&m,o+0x32cu,4)<<10,4); }
    TRY(FIND_FLOCK(0x52ed34));if (found) { value=0;TRY(frame_text_read_value(read,write,ctx,parser,e->work_limit,&value,&more));if (!more) return -4;pm_store(&m,o+0x330u,value,2); }
    TRY(FIND_FLOCK(0x52ed24));if (found) { TRY(frame_text_next(read,write,ctx,parser,name,sizeof(name),e->work_limit,&more));pm_store(&m,o+0x338u,0,4); }
    TRY(FIND_FLOCK(0x52ed1c));
    if (found) {
        TRY(clone_shape(&m,(const unsigned char *)"batty.sm",9,o+0x328u,e,s,&shape));pm_store(&m,o+72u,shape,4);TRY(clone_shape(&m,(const unsigned char *)"batty.sm",9,o+0x328u,e,s,&shape));pm_store(&m,o+0x324u,shape,4);pm_store(&m,o+0x334u,2,4);pm_store(&m,o+0x340u,0x40ba10,4);
    } else {
        TRY(FIND_FLOCK(0x52ed04));
        if (found) { TRY(frame_shape_load_name(read,write,ctx,(const unsigned char *)"chicken.sm",11,pm_load(&m,0x55ae40u,4),e,s->shapes,&shape));pm_store(&m,o+72u,shape,4);pm_store(&m,o+0x334u,3,4);pm_store(&m,o+0x340u,0x40ba60,4); }
        else { TRY(clone_shape(&m,(const unsigned char *)"birdy2.sm",10,o+0x328u,e,s,&shape));pm_store(&m,o+72u,shape,4);TRY(clone_shape(&m,(const unsigned char *)"birdy2.sm",10,o+0x328u,e,s,&shape));pm_store(&m,o+0x324u,shape,4);pm_store(&m,o+0x334u,1,4);pm_store(&m,o+0x340u,0x40ba10,4); }
    }
    status=physics_terrain_height(read,ctx,pm_load(&m,0x55ae60u,4),pm_i32(&m,o),pm_i32(&m,o+8u),&ground);if (status<0) return status;pm_write32(&m,o+4u,ground);pm_write32(&m,o+0x310u,ground);
    shape=pm_load(&m,o+72u,4);pm_store(&m,shape+12u,pm_load(&m,shape+12u,2)<<1,2);pm_store(&m,shape+14u,pm_load(&m,o+0x4cu,2),2);
    if (pm_load(&m,o+0x334u,4)!=3u) {
        shape=pm_load(&m,o+0x324u,4);pm_store(&m,shape+12u,pm_load(&m,shape+12u,2)<<1,2);pm_store(&m,shape+14u,pm_load(&m,o+0x4cu,2),2);
        if (pm_load(&m,o+0x334u,4)!=3u) { TRY(vertices_extreme(&m,pm_load(&m,o+72u,4),o+0x314u,o+0x318u,e->work_limit));TRY(vertices_extreme(&m,pm_load(&m,o+0x324u,4),o+0x31cu,o+0x320u,e->work_limit)); }
    }
    ground=pm_add(ground,250);
    for (i=0;i<8u;i++) {
        p=o+0x80u+i*80u;value=(random_next(&m)>>4)&8191u;pm_write32(&m,p-4u,pm_add((int32_t)value-4096,pm_i32(&m,o)));pm_write32(&m,p,ground);value=(random_next(&m)>>4)&8191u;pm_write32(&m,p+4u,pm_add((int32_t)value-4096,pm_i32(&m,o+8u)));
        angles[1]=(random_next(&m)>>4)&65535u;TRY(frame_matrix_from_angles(read,write,ctx,p+28u,angles,s->trig));value=(random_next(&m)>>4)&63u;pm_store(&m,p+20u,value+pm_load(&m,o+0x330u,2),4);pm_store(&m,p+24u,1,4);pm_store(&m,p+16u,0,4);pm_store(&m,p+12u,0,4);pm_store(&m,p+8u,0,4);
    }
    pm_store(&m,o+0x330u,pm_load(&m,o+0x330u,2)+32u,2);pm_store(&m,o+0x332u,0,1);pm_store(&m,o+0x333u,1,1);pm_store(&m,o+0x33cu,0x40b5e0,4);return m.error;
#undef FIND_FLOCK
}
#undef TRY
