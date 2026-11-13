#include "frame_camera.h"
#include "physics_memory.h"
#include "physics_vector.h"
#include "frame_player_collision.h"
#include "frame_controls.h"
#include "frame_text.h"
#include "frame_weapon_spin.h"
#include "frame_shatter.h"
#include "frame_environment.h"
#include <string.h>
int frame_camera_shake(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t amplitude,uint32_t duration)
{
    physics_memory m={read,write,ctx,0};uint32_t record,o;
    if (!read || !write) return -1;
    record=pm_load(&m,pm_load(&m,0x55c694u,4)+entity*8u,4);o=record ? pm_load(&m,record+4u,4) : 0;
    pm_store(&m,o+0x84u,duration,4);pm_store(&m,o+0x92u,amplitude,2);pm_store(&m,o+0x90u,amplitude,2);
    if (pm_sar(pm_signed(amplitude<<16),16)>2000) pm_store(&m,o+0x92u,2000,2);
    return m.error;
}

static uint32_t object_of(physics_memory *m,uint32_t id)
{
    uint32_t record=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);
    return record ? pm_load(m,record+4u,4) : 0;
}
int frame_camera_publish(frame_data_read read,frame_data_write write,void *ctx,uint32_t slot,uint32_t position,uint32_t matrix)
{
    physics_memory m={read,write,ctx,0};unsigned i;
    if (!read || !write) return -1;
    for (i=0;i<3;i++) pm_write32(&m,0x74aee0u+slot*12u+i*4u,pm_sar(pm_i32(&m,position+i*4u),3));
    for (i=0;i<12;i++) pm_store(&m,0x74af00u+slot*48u+i*4u,pm_load(&m,matrix+i*4u,4),4);
    return m.error;
}
static int replace(physics_memory *m,uint32_t slot,uint32_t entity,const frame_entity_factory_services *s)
{
    uint32_t old=pm_load(m,0x53cc38u+slot*4u,4),result,descriptor;int status;
    if (old) {
        descriptor=pm_load(m,0x545ed0u,4);if (m->error) return m->error;
        status=frame_entity_dispatch(m->read,m->write,m->ctx,old,descriptor,0,0,s->method,s->method_ctx,&result);
        if (status) return status;
    }
    pm_store(m,0x53cc38u+slot*4u,entity,4);return m->error;
}
int frame_camera_create_finish(frame_data_read read,frame_data_write write,void *ctx,uint32_t racer,uint32_t slot,
                                 const frame_entity_factory_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t old,source,target,entity,object,klass,last;unsigned i;int status;
    if (!read || !write || !s) return -1;
    old=object_of(&m,pm_load(&m,0x53cc38u+slot*4u,4));target=object_of(&m,racer);
    klass=pm_load(&m,0x53d14cu,4);if (m.error) return m.error;
    status=frame_entity_create(read,write,ctx,klass,s,&entity);if (status) return status;
    object=object_of(&m,entity);
    if (object) {
        source=pm_load(&m,old+0x98u,4)==2 ? target : old;
        for (i=0;i<8;i++) pm_store(&m,object+0x18u+i*4u,pm_load(&m,source+0x18u+i*4u,4),4);
        last=pm_load(&m,source+0x38u,4);pm_store(&m,object+0x14u,object+0x18u,4);pm_store(&m,object+0x38u,last,4);
        for (i=0;i<3;i++) pm_store(&m,object+i*4u,pm_load(&m,old+i*4u,4),4);
        pm_write32(&m,object+0x50u,pm_sar(pm_i32(&m,target+0x50u),6));pm_store(&m,object+0x54u,0,4);
        pm_write32(&m,object+0x58u,pm_sar(pm_i32(&m,target+0x58u),6));
        if (m.error) return m.error;
        return replace(&m,slot,entity,s);
    }
    return m.error;
}
int frame_camera_create_track(frame_data_read read,frame_data_write write,void *ctx,uint32_t target,uint32_t slot,
                                const frame_entity_factory_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t klass,entity,object,subject;int status;
    if (!read || !write || !s) return -1;
    klass=pm_load(&m,0x5466c4u,4);if (m.error) return m.error;
    status=frame_entity_create(read,write,ctx,klass,s,&entity);if (status) return status;
    object=object_of(&m,entity);subject=object_of(&m,target);
    if (object) {
        pm_store(&m,object+0xecu,pm_load(&m,object+0xecu,4)|4u,4);
        if (pm_load(&m,0x52ee9cu,4)==0 || pm_load(&m,0x52ee9cu,4)==1) {
            uint32_t value,matrix;int32_t vector[3];unsigned i,j;
            value=pm_load(&m,0x53cc40u,4);pm_store(&m,object+0xbcu,0,4);pm_store(&m,object+0xb8u,value,4);
            pm_write32(&m,object+0xc0u,pm_neg(pm_i32(&m,0x52e3acu)));
            matrix=pm_load(&m,subject+0x14u,4);
            if (matrix) {
                for (i=0;i<3;i++) vector[i]=pm_i32(&m,object+0xb8u+i*4u);
                for (i=0;i<3;i++) {
                    int32_t sum=0;
                    for (j=0;j<3;j++) sum=pm_add(sum,pm_mul(vector[j],pm_i32(&m,matrix+(j*3u+i)*4u)));
                    pm_write32(&m,object+0xb8u+i*4u,pm_sar(sum,12));
                }
            }
            pm_store(&m,object+0xbcu,pm_load(&m,0x52e3a8u,4),4);
        } else {
            pm_store(&m,object+0xb8u,0,4);pm_store(&m,object+0xbcu,0x600,4);pm_store(&m,object+0xc0u,0x1400,4);
            pm_store(&m,object+0x12au,0xe8f,2);pm_store(&m,object+0x12cu,0x1d1,2);
        }
        if (m.error) return m.error;
        return replace(&m,slot,entity,s);
    }
    return m.error;
}
static int quotient(int32_t numerator,int32_t denominator,int32_t *out)
{
    if (!denominator || (numerator==INT32_MIN && denominator==-1)) return -2;
    *out=numerator/denominator;return 0;
}
int frame_camera_matrix_angle_values(frame_data_read read,void *ctx,uint32_t matrix,uint32_t angles[3],uint32_t *nonsingular)
{
    physics_memory m={read,0,ctx,0};uint32_t pitch,yaw=0,roll=0;int32_t v,cosine,q,sign;int status;unsigned i;
    if (!read || !angles || !nonsingular) return -1;
    if (!matrix) { *nonsingular=0;return 0; }
    v=pm_i32(&m,matrix+28u);
    pitch=v<0 ? pm_load(&m,0x5af380u+(uint32_t)pm_neg(v)*4u,4)-0x4000u : 0x4000u-pm_load(&m,0x5af380u+(uint32_t)v*4u,4);
    cosine=pm_i32(&m,0x59b380u+(pitch&65532u));if (m.error) return m.error;
    if (cosine) for (i=0;i<2u;i++) {
        v=pm_i32(&m,matrix+(i ? 16u : 32u));if (m.error) return m.error;
        status=quotient(pm_mul(v,4096),cosine,&q);if (status) return status;
        if (q>4096) q=4096;
        if (q<-4096) q=-4096;
        v=pm_i32(&m,0x5af380u+(uint32_t)pm_abs(q)*4u);v=q<0 ? pm_sub(32768,v) : v;
        sign=pm_i32(&m,matrix+(i ? 4u : 24u));if (m.error) return m.error;
        status=quotient(pm_mul(sign,4096),cosine,&q);if (status) return status;
        if (pm_neg(q)<0) v=pm_sub(65536,v);
        if (i) roll=(uint32_t)v;
        else yaw=(uint32_t)v;
    }
    angles[1]=yaw;angles[0]=pitch;angles[2]=roll;
    *nonsingular=cosine!=0;return 0;
}
int frame_camera_matrix_angles(frame_data_read read,frame_data_write write,void *ctx,uint32_t matrix,uint32_t angles,uint32_t *nonsingular)
{
    physics_memory m={read,write,ctx,0};uint32_t values[3],result;int status;
    if (!read || !write || !nonsingular) return -1;
    status=frame_camera_matrix_angle_values(read,ctx,matrix,values,&result);if (status) return status;
    if (!matrix) { *nonsingular=0;return 0; }
    pm_store(&m,angles+4u,values[1],4);pm_store(&m,angles,values[0],4);pm_store(&m,angles+8u,values[2],4);
    if (m.error) return m.error;
    *nonsingular=result;return 0;
}
int frame_camera_create_chase(frame_data_read read,frame_data_write write,void *ctx,uint32_t target,uint32_t slot,
                                const frame_entity_factory_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t subject,object,entity,klass,unused,rng,variant,i,preset;int status;
    if (!read || !write || !s) return -1;
    subject=object_of(&m,target);klass=pm_load(&m,0x545e24u,4);if (m.error) return m.error;
    status=frame_entity_create(read,write,ctx,klass,s,&entity);if (status) return status;
    object=object_of(&m,entity);if (!object) return m.error;
    status=replace(&m,slot,entity,s);if (status) return status;
    pm_store(&m,object+0x94u,slot,1);
    status=frame_camera_matrix_angles(read,write,ctx,pm_load(&m,subject+0x14u,4),object+0x9cu,&unused);if (status) return status;
    preset=pm_load(&m,0x53e40cu,4)!=0;
    pm_store(&m,object+0xb8u,pm_load(&m,preset ? 0x53cc50u : 0x53cc40u,4),4);
    pm_store(&m,object+0xbcu,pm_load(&m,preset ? 0x52e3d8u : 0x52e3a8u,4),4);
    pm_store(&m,object+0xc0u,pm_load(&m,preset ? 0x52e3dcu : 0x52e3acu,4),4);
    pm_store(&m,object+0xecu,pm_load(&m,object+0x9cu,4),4);pm_store(&m,object+0xf4u,pm_load(&m,object+0xa4u,4),4);
    rng=pm_load(&m,0x55b6acu,4)*1664525u+1013904223u;pm_store(&m,0x55b6acu,rng,4);variant=((rng>>4)&65535u)%3u;
    pm_store(&m,object+0xf0u,pm_load(&m,object+0xa0u,4)+(variant==1u ? 65520u : variant==2u ? 0xffff0010u : 0u),4);
    pm_write32(&m,object+0xc0u,pm_sub(pm_i32(&m,object+0xc0u),variant ? 10000 : 25000));
    pm_write32(&m,object+0xbcu,pm_add(pm_i32(&m,object+0xbcu),variant ? 3000 : -5000));
    pm_store(&m,object+0xf8u,variant ? 2u : 1u,2);pm_store(&m,object+0xfau,1,1);pm_store(&m,object+0xfbu,variant ? 9u : 10u,1);
    pm_store(&m,object+0xfcu,1,2);pm_store(&m,object+0xfeu,1,1);pm_store(&m,object+0xffu,variant ? 10u : 9u,1);
    pm_store(&m,object+0x14u,object+0x18u,4);
    for (i=0;i<12u;i++) pm_store(&m,object+0x18u+i*4u,pm_load(&m,subject+0x18u+i*4u,4),4);
    for (i=0;i<3u;i++) pm_store(&m,object+i*4u,pm_load(&m,subject+i*4u,4),4);
    pm_write32(&m,object+8u,pm_add(pm_i32(&m,object+8u),pm_i32(&m,object+0xc0u)));return m.error;
}
int frame_camera_finish_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t camera,uint32_t racer,
                               const frame_entity_factory_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t object,subject,slot;int32_t timer,value;unsigned i;
    if (!read || !write || !s) return -1;
    object=object_of(&m,camera);subject=object_of(&m,racer);
    timer=pm_add(pm_i32(&m,object+0x96u),pm_i32(&m,0x5904fcu));pm_write32(&m,object+0x96u,timer);
    if (timer<1500) {
        pm_write32(&m,object+0x50u,pm_damp(pm_i32(&m,object+0x50u),pm_load(&m,0x52ecd8u,4),pm_i32(&m,0x5904fcu)));
        pm_write32(&m,object+0x58u,pm_damp(pm_i32(&m,object+0x58u),pm_load(&m,0x52ecd8u,4),pm_i32(&m,0x5904fcu)));
        value=pm_add(pm_i32(&m,object+0x54u),4);pm_write32(&m,object+0x54u,value);
        if (value>262) pm_store(&m,object+0x54u,262,4);
        for (i=0;i<3;i++) pm_write32(&m,object+i*4u,pm_add(pm_i32(&m,object+i*4u),pm_sar(pm_mul(pm_i32(&m,0x5904fcu),pm_i32(&m,object+0x50u+i*4u)),8)));
        slot=(uint32_t)pm_i8(&m,subject+0xdf0u);if (m.error) return m.error;
        return frame_camera_publish(read,write,ctx,slot,object,pm_load(&m,object+0x14u,4));
    }
    if (pm_load(&m,0x52ee9cu,4)==5) { if (!pm_load(&m,0x543d50u,4)) return m.error; }
    else if (pm_load(&m,0x52ee9cu,4)==4 && pm_load(&m,0x58e790u,4) && pm_load(&m,0x543d60u,4)) return m.error;
    slot=(uint32_t)pm_i8(&m,subject+0xdf0u);if (m.error) return m.error;
    return frame_camera_create_track(read,write,ctx,racer,slot,s);
}
int frame_camera_native(void *ctx,uint32_t target,uint32_t entity,uint32_t has_payload,uint32_t payload,uint32_t *out)
{
    frame_entity_native_context *c=(frame_entity_native_context *)ctx;
    physics_memory m;uint32_t klass,object,result;int status;
    if (!c || !c->read || !c->write || !out) return -1;
    if (target!=0x406280u && target!=0x43d250u) return frame_entity_native_base(ctx,target,entity,has_payload,payload,out);
    m.read=c->read;m.write=c->write;m.ctx=c->data_ctx;m.error=0;
    object=target==0x43d250u ? object_of(&m,entity) : 0;
    klass=pm_load(&m,target==0x406280u ? 0x53cc30u : 0x53cc58u,4);
    if (m.error) return m.error;
    status=frame_class_dispatch(c->read,c->data_ctx,klass,entity,c->services.method,c->services.method_ctx,&result);
    if (status) return status;
    if (target==0x43d250u) {
        pm_store(&m,object+0xb8u,pm_load(&m,0x53cc40u,4),4);pm_store(&m,object+0xbcu,pm_load(&m,0x52e3a8u,4),4);
        result=pm_load(&m,0x52e3acu,4);pm_store(&m,object+0xc0u,result,4);pm_store(&m,object+0x14u,object+0x18u,4);
    }
    if (m.error) return m.error;
    *out=result;return 0;
}

/* Trackcam has separate local-velocity, target-seeking and orbit modes. The
 * orbit frame publishes the PRE-update offset, then advances its oscillators;
 * moving that integration before publication introduces a one-frame drift. */
static void camera_transform(physics_memory *m,int32_t v[3],uint32_t matrix)
{
    int32_t result[3];unsigned i,j;if (!matrix) return;
    for (i=0;i<3u;i++) { result[i]=0;for (j=0;j<3u;j++) result[i]=pm_add(result[i],pm_mul(v[j],pm_i32(m,matrix+(j*3u+i)*4u)));result[i]=pm_sar(result[i],12); }
    for (i=0;i<3u;i++) v[i]=result[i];
}
static void camera_decay(physics_memory *m,uint32_t at,uint32_t amount,uint32_t shift)
{
    int32_t old=pm_i16(m,at),step=pm_sar(pm_mul((int32_t)amount,pm_i32(m,0x5904fcu)),shift&31u),value;
    if (!old) return;
    value=old>0 ? pm_sub(old,step) : pm_add(old,step);pm_store(m,at,(uint32_t)value,2);value=pm_i16(m,at);
    if ((old>0 && value<0) || (old<0 && value>0)) pm_store(m,at,0,2);
}
static void camera_linear_motion(physics_memory *m,uint32_t o,int32_t v[3])
{
    unsigned i;if (!v[0] && !v[1] && !v[2]) return;
    camera_transform(m,v,pm_load(m,o+20u,4));
    for (i=0;i<3u;i++) pm_write32(m,o+0x11au+i*4u,pm_add(pm_i32(m,o+0x11au+i*4u),pm_sar(pm_mul(v[i],pm_i32(m,0x5904fcu)),8)));
}
static void camera_motion_decay(physics_memory *m,uint32_t o,const int32_t v[3])
{
    unsigned i;static const unsigned at[]={0x104,0xfa,0xf0};
    pm_store(m,o+0x10cu,pm_load(m,o+0x10cu,2)+(uint32_t)pm_sar(pm_mul(frame_collision_change_magnitude(v),pm_i32(m,0x5904fcu)),8),2);
    if (pm_abs(pm_i16(m,o+0x10cu))>pm_abs(pm_i16(m,o+0x10au)))
        for (i=0;i<3u;i++) camera_decay(m,o+at[i],pm_load(m,o+0x10eu,1),pm_load(m,o+0x10fu,1));
}
static int camera_publish_values(physics_memory *m,uint32_t slot,const int32_t position[3],const int32_t matrix[12])
{
    unsigned i;for (i=0;i<3u;i++) pm_write32(m,0x74aee0u+slot*12u+i*4u,pm_sar(position[i],3));
    for (i=0;i<12u;i++) pm_write32(m,0x74af00u+slot*48u+i*4u,matrix[i]);
    return m->error;
}
int frame_camera_start_to_chase(frame_data_read read,frame_data_write write,void *ctx,uint32_t racer,uint32_t slot,uint32_t old_camera,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t old,target,klass,id,o,i,j,preset,unused,matrix;int32_t offset[3],value[3],sum;int status;
    if (!read || !write || !e) return -1;
    old=object_of(&m,old_camera);target=object_of(&m,racer);klass=pm_load(&m,0x53cc58u,4);if (m.error) return m.error;
    status=frame_entity_create(read,write,ctx,klass,e,&id);if (status) return status;o=object_of(&m,id);if (!o) return m.error;
    preset=pm_load(&m,0x53e40cu,4)!=0;
    pm_store(&m,o+0xb8u,pm_load(&m,preset ? 0x53cc50u : 0x53cc40u,4),4);pm_store(&m,o+0xbcu,pm_load(&m,preset ? 0x52e3d8u : 0x52e3a8u,4),4);pm_store(&m,o+0xc0u,pm_load(&m,preset ? 0x52e3dcu : 0x52e3acu,4),4);
    pm_store(&m,o+0x94u,slot,1);pm_store(&m,o+20u,o+24u,4);
    for (i=0;i<3u;i++) pm_store(&m,o+0xb8u+i*4u,pm_load(&m,old+0xb8u+i*4u,4),4);
    /*4061EC reads the outgoing camera's angles. The racer matrix below is
     * separately used to place the new camera's offset in world space. */
    matrix=pm_load(&m,target+20u,4);status=frame_camera_matrix_angles(read,write,ctx,pm_load(&m,old+20u,4),o+0x9cu,&unused);if (status) return status;
    pm_store(&m,o+0xa4u,0,4);pm_store(&m,o+0xa8u,pm_load(&m,preset ? 0x52e3e0u : 0x52e3b0u,4),4);
    if (!matrix) return -4; /* original separate output vector is undefined */
    for (i=0;i<3u;i++) offset[i]=pm_i32(&m,o+0xb8u+i*4u);
    for (i=0;i<3u;i++) { sum=0;for (j=0;j<3u;j++) sum=pm_add(sum,pm_mul(offset[j],pm_i32(&m,matrix+(j*3u+i)*4u)));value[i]=pm_sar(sum,12); }
    for (i=0;i<3u;i++) pm_write32(&m,o+i*4u,pm_add(value[i],pm_i32(&m,target+i*4u)));
    if (m.error) return m.error;
    return replace(&m,slot,id,e);
}
int frame_camera_start_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t camera,uint32_t racer,const frame_entity_factory_services *e,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t o,target,preset,i,j,all_equal=1,at,packed,slot;int32_t goal[5],value,position[3],offset[3],matrix[12],sum;int status;
    static const unsigned channels[]={0xb8,0xbc,0xc0,0xa8,0xf0};
    if (!read || !write || !e || !trig) return -1;
    o=object_of(&m,camera);target=object_of(&m,racer);preset=pm_load(&m,0x53e40cu,4)!=0;
    goal[0]=pm_i32(&m,preset ? 0x53cc50u : 0x53cc40u);goal[1]=pm_i32(&m,preset ? 0x52e3d8u : 0x52e3a8u);goal[2]=pm_i32(&m,preset ? 0x52e3dcu : 0x52e3acu);goal[3]=pm_i32(&m,preset ? 0x52e3e0u : 0x52e3b0u);goal[4]=pm_i32(&m,o+0xa0u);
    for (i=0;i<5u;i++) { at=o+channels[i];value=pm_i32(&m,at);if (value!=goal[i]) { all_equal=0;packed=i==2u ? pm_load(&m,o+0xfcu,4) : i==4u ? pm_load(&m,o+0xf8u,4) : 0x0a010001u;if (m.error) return m.error;status=frame_scalar_approach(read,write,ctx,at,goal[i],packed);if (status) return status; } }
    slot=pm_load(&m,o+0x94u,1);
    if (all_equal) { if (m.error) return m.error;status=frame_camera_start_to_chase(read,write,ctx,racer,slot,camera,e);if (status) return status; }
    for (i=0;i<12u;i++) matrix[i]=pm_i32(&m,0x532888u+i*4u);
    physics_matrix_rotate(matrix,1,pm_load(&m,o+0xf0u,4),trig);physics_matrix_rotate(matrix,0,pm_load(&m,o+0xa8u,4),trig);
    for (i=0;i<3u;i++) offset[i]=pm_i32(&m,o+0xb8u+i*4u);
    for (i=0;i<3u;i++) { sum=0;for (j=0;j<3u;j++) sum=pm_add(sum,pm_mul(offset[j],matrix[j*3u+i]));position[i]=pm_sar(sum,12); }
    for (i=0;i<3u;i++) pm_store(&m,o+i*4u,pm_load(&m,target+i*4u,4),4);
    for (i=0;i<3u;i++) { position[i]=pm_add(position[i],pm_i32(&m,o+i*4u));pm_write32(&m,o+i*4u,position[i]); }
    /*50CD20's listener is published by the host audio side channel; no game RNG.*/
    return camera_publish_values(&m,slot,position,matrix);
}
static int camera_shake_step(physics_memory *m,uint32_t o)
{
    int32_t scale,limit,value,speed,dt=pm_i32(m,0x5904fcu),word;uint32_t direction;int status;
    word=pm_i16(m,o+0x90u);status=quotient(pm_mul(word,1024),pm_i16(m,o+0x92u),&scale);if (status) return status;
    if (scale>1024) scale=1024;
    limit=pm_sar(pm_mul(pm_i32(m,o+0x84u),scale),10);
    if (word!=1) { pm_store(m,o+0x90u,(uint32_t)word-pm_load(m,0x5904fcu,2),2);if (pm_i16(m,o+0x90u)<1) pm_store(m,o+0x90u,1,2); }
    direction=pm_load(m,o+0x95u,1);speed=pm_i32(m,o+0x8cu);
    speed=direction ? pm_add(speed,pm_mul(dt,256)) : pm_sub(speed,pm_mul(dt,256));pm_write32(m,o+0x8cu,speed);
    if (direction ? speed>3000 : speed<-3000) pm_write32(m,o+0x8cu,direction ? 3000 : -3000);
    value=pm_add(pm_i32(m,o+0x88u),pm_sar(pm_mul(dt,pm_i32(m,o+0x8cu)),8));pm_write32(m,o+0x88u,value);
    if (direction ? value>0 : value<0) { pm_store(m,o+0x95u,direction ? 0u : 1u,1);if (pm_i16(m,o+0x90u)==1) { pm_store(m,o+0x90u,0,2);pm_store(m,o+0x88u,0,4);pm_store(m,o+0x8cu,0,4); } }
    if (pm_i32(m,o+0x88u)<pm_neg(limit)) pm_write32(m,o+0x88u,pm_neg(limit));
    if (pm_i32(m,o+0x88u)>limit) pm_write32(m,o+0x88u,limit);
    return m->error;
}
static int camera_button(physics_memory *m,uint32_t o,uint32_t offset,uint32_t *pressed)
{
    uint32_t slot=pm_load(m,o+0x94u,1),key,value;int status;*pressed=0;if (slot) return m->error;
    key=pm_load(m,0x592000u+offset,4);if (m->error) return m->error;status=frame_control_key(m->read,m->ctx,key,&value);if (status) return status;
    if (value==1u) { *pressed=1;return 0; }
    if (pm_load(m,0x53ccdcu,4)) { key=pm_load(m,0x592900u+offset+slot*100u,4);if (key) { status=frame_control_key(m->read,m->ctx,key+slot*12u,&value);if (status) return status;*pressed=value==1u; } }
    return m->error;
}
static int camera_wrap(int32_t value,uint32_t limit,int32_t *out)
{
    uint32_t used=0;
    while (value>32760) { if (used++>=limit) return -3;value=pm_sub(value,65520); }
    while (value<-32760) { if (used++>=limit) return -3;value=pm_add(value,65520); }
    *out=value;return 0;
}
int frame_camera_chase_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t camera,uint32_t racer,const frame_entity_factory_services *e,const physics_trig *trig,const uint32_t tail[3])
{
    physics_memory m={read,write,ctx,0};uint32_t o,target,slot,button,look=0,switched=0,mode,i,j,angles[3],matrix_at;int status;
    int32_t before[3],position[3],matrix[12],local[3],view[3],pitch,yaw,roll,delta,next,correction,high,dt,goal_y,goal_z,goal_pitch,sum,shake,temp;
#define CHASE_TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
    if (!read || !write || !e || !e->work_limit || !trig) return -1;
    o=object_of(&m,camera);target=object_of(&m,racer);slot=pm_load(&m,o+0x94u,1);dt=pm_i32(&m,0x5904fcu);for (i=0;i<3u;i++) before[i]=pm_i32(&m,o+i*4u);
    CHASE_TRY(camera_button(&m,o,0x38u,&button));
    if (button && !pm_load(&m,0x53e924u,4)) {
        if (!pm_load(&m,o+0x97u,1)) { mode=pm_load(&m,o+0x98u,4)+1u;pm_store(&m,o+0x98u,mode,4);if (mode==3u) pm_store(&m,o+0x98u,0,4);switched=1;pm_store(&m,o+0xa8u,pm_load(&m,0x52e3b0u,4),4); }pm_store(&m,o+0x97u,1,1);
    } else pm_store(&m,o+0x97u,0,1);
    CHASE_TRY(camera_button(&m,o,0x34u,&button));if (button && !pm_load(&m,0x53e924u,4)) { look=1;pm_store(&m,o+0x96u,1,1); }
    mode=pm_load(&m,o+0x98u,4);matrix_at=pm_load(&m,o+20u,4);
    if (mode==2u) {
        CHASE_TRY(physics_angle_lookup(pm_i32(&m,target+0x34u),physics_length2_approx(pm_i32(&m,target+0x30u),pm_i32(&m,target+0x38u)),trig,&pitch));
        CHASE_TRY(physics_angle_lookup(pm_i32(&m,target+0x1cu),physics_length2_approx(pm_i32(&m,target+0x18u),pm_i32(&m,target+0x20u)),trig,&roll));roll=pm_neg(roll);
        CHASE_TRY(physics_angle_lookup(pm_i32(&m,target+0x30u),pm_i32(&m,target+0x38u),trig,&yaw));pm_write32(&m,o+0xe0u,pm_neg(yaw));
        CHASE_TRY(frame_scalar_approach(read,write,ctx,o+0xdcu,pitch,pm_load(&m,0x592e68u,4)));CHASE_TRY(frame_scalar_approach(read,write,ctx,o+0xe4u,roll,pm_load(&m,0x592e74u,4)));
        for (i=0;i<3u;i++) angles[i]=pm_load(&m,o+0xdcu+i*4u,4);
        if (pm_i16(&m,o+0x90u)) { CHASE_TRY(camera_shake_step(&m,o));shake=pm_i32(&m,o+0x88u);for (i=0;i<3u;i++) angles[i]+=(uint32_t)shake; }
        physics_matrix_from_angles(matrix,angles,trig);if (!tail) return -4;for (i=0;i<3u;i++) matrix[9u+i]=pm_signed(tail[i]);
        if (look) physics_matrix_rotate(matrix,1,32760,trig);
        for (i=0;i<3u;i++) local[i]=pm_add(pm_i32(&m,o+0x78u+i*4u),pm_i32(&m,0x52e3e8u+i*4u));
        for (i=0;i<3u;i++) { sum=0;for (j=0;j<3u;j++) sum=pm_add(sum,pm_mul(local[j],matrix[j*3u+i]));view[i]=pm_sar(sum,12); }
        shake=pm_i32(&m,o+0x88u);for (i=0;i<3u;i++) { position[i]=pm_add(pm_add(pm_i32(&m,target+0x78u+i*4u),view[i]),pm_sar(shake,i==1u ? 1u : 2u));pm_write32(&m,o+i*4u,position[i]); }
        CHASE_TRY(camera_publish_values(&m,slot,position,matrix));goal_y=pm_i32(&m,0x52e3a8u);goal_z=pm_i32(&m,0x52e3acu);goal_pitch=pm_signed(camera);
    } else {
        CHASE_TRY(physics_angle_lookup(pm_i32(&m,target+0x30u),pm_i32(&m,target+0x38u),trig,&yaw));yaw=pm_neg(yaw);
        CHASE_TRY(physics_angle_lookup(pm_i32(&m,target+0x34u),physics_length2_approx(pm_i32(&m,target+0x30u),pm_i32(&m,target+0x38u)),trig,&pitch));
        local[0]=pm_i32(&m,o+0x9cu);local[1]=pm_i32(&m,o+0xa0u);local[2]=pm_i32(&m,o+0xa4u);
        CHASE_TRY(camera_wrap(pm_sub(local[1],yaw),e->work_limit,&delta));
        if (delta) { CHASE_TRY(quotient(pm_mul(pm_mul(delta,dt),pm_i32(&m,0x592e70u)),pm_i32(&m,0x592e78u),&correction));local[1]=pm_sub(local[1],correction);CHASE_TRY(camera_wrap(pm_sub(local[1],yaw),e->work_limit,&next));if ((delta>0 && next<0) || (delta<0 && next>0)) local[1]=yaw; }
        CHASE_TRY(camera_wrap(pm_sub(local[0],pitch),e->work_limit,&delta));
        if (delta) {
            temp=pm_mul(pm_mul(delta,dt),pm_i32(&m,0x592e6cu));high=pm_signed((uint32_t)((uint64_t)((int64_t)temp*(-966603629LL))>>32));correction=pm_add(pm_sar(high,10),(int32_t)((uint32_t)high>>31));local[0]=pm_add(local[0],correction);
            CHASE_TRY(camera_wrap(pm_sub(local[0],pitch),e->work_limit,&next));if ((delta>0 && next<0) || (delta<0 && next>0)) local[0]=pitch;
        }
        goal_y=pm_i32(&m,mode==0u ? 0x52e3a8u : 0x52e3b4u);goal_z=pm_i32(&m,mode==0u ? 0x52e3acu : 0x52e3b8u);goal_pitch=pm_i32(&m,mode==0u ? 0x52e3b0u : 0x52e3bcu);
        if (mode>1u) { goal_y=pitch;goal_z=goal_pitch=pm_signed(camera); }
        for (i=0;i<3u;i++) pm_write32(&m,o+0x9cu+i*4u,local[i]);
        if (look) { local[0]=pm_sub(pm_load(&m,0x53e40cu,4) ? pm_i32(&m,0x52e3e0u) : goal_pitch,local[0]);local[1]=pm_add(local[1],32760); }
        else local[0]=pm_add(local[0],pm_i32(&m,o+0xa8u));
        if (pm_i16(&m,o+0x90u)) { CHASE_TRY(camera_shake_step(&m,o));shake=pm_i32(&m,o+0x88u);for (i=0;i<3u;i++) local[i]=pm_add(local[i],shake); }
        for (i=0;i<3u;i++) angles[i]=(uint32_t)local[i];
        CHASE_TRY(frame_matrix_from_angles(read,write,ctx,matrix_at,angles,trig));if (!matrix_at) return -4;
        for (i=0;i<3u;i++) local[i]=pm_i32(&m,o+0xb8u+i*4u);
        for (i=0;i<3u;i++) { sum=0;for (j=0;j<3u;j++) sum=pm_add(sum,pm_mul(local[j],pm_i32(&m,matrix_at+(j*3u+i)*4u)));view[i]=pm_sar(sum,12); }
        shake=pm_i32(&m,o+0x88u);for (i=0;i<3u;i++) { position[i]=pm_add(pm_add(pm_i32(&m,target+i*4u),view[i]),i==1u ? shake : pm_sar(shake,1));pm_write32(&m,o+i*4u,position[i]); }
        CHASE_TRY(frame_camera_publish(read,write,ctx,slot,o,matrix_at));
    }
    if (switched) CHASE_TRY(frame_environment_shift(read,write,ctx,slot,0x53cc60u,e->work_limit));
    /* The original performs these divisions even when its listener is disabled. */
    for (i=0;i<3u;i++) { CHASE_TRY(quotient(pm_mul(pm_sub(position[i],before[i]),256),dt,&temp)); }
    if (!look && pm_load(&m,o+0x96u,1)) pm_store(&m,o+0x96u,0,1);
    if (pm_load(&m,0x53e40cu,4)) { goal_z=pm_i32(&m,0x52e3dcu);goal_pitch=pm_i32(&m,0x52e3e0u); }
    if (!pm_load(&m,target+0xdcbu,1) || pm_load(&m,0x53e928u,4)) {
        if (pm_i32(&m,o+0xc0u)!=goal_z) CHASE_TRY(frame_scalar_approach(read,write,ctx,o+0xc0u,goal_z,0x09010001u));
        if (pm_i32(&m,o+0xbcu)!=goal_y) CHASE_TRY(frame_scalar_approach(read,write,ctx,o+0xbcu,goal_y,0x09010001u));
        if (mode!=2u && pm_i32(&m,o+0xa8u)!=goal_pitch) CHASE_TRY(frame_scalar_approach(read,write,ctx,o+0xa8u,goal_pitch,0x09010001u));
    }
    return m.error;
#undef CHASE_TRY
}
static int camera_direction_values(physics_memory *m,int32_t direction[3],int32_t matrix[12],const physics_trig *trig,const uint32_t tail[3])
{
    int32_t pitch,yaw;unsigned i;int status;
    if (!trig || !trig->sine || !trig->cosine || !trig->angle) return -5;
    while (pm_abs(direction[0])>=32768 || pm_abs(direction[1])>=32768 || pm_abs(direction[2])>=32768)
        for (i=0;i<3u;i++) direction[i]=pm_sar(direction[i],1);
    status=physics_angle_lookup(pm_neg(direction[0]),direction[2],trig,&yaw);if (status) return status;
    status=physics_angle_lookup(direction[1],physics_length2_approx(direction[0],direction[2]),trig,&pitch);if (status) return status;
    for (i=0;i<9u;i++) matrix[i]=pm_i32(m,0x532888u+i*4u);
    if (m->error) return m->error;
    physics_matrix_rotate(matrix,1,(uint32_t)yaw,trig);physics_matrix_rotate(matrix,0,(uint32_t)pitch,trig);
    if (!tail) return -4;
    for (i=0;i<3u;i++) matrix[9u+i]=pm_signed(tail[i]);
    return 0;
}
static void camera_orbit_axis(physics_memory *m,uint32_t o,uint32_t velocity,uint32_t offset,uint32_t active,uint32_t reverse)
{
    int32_t amount,value,limit;uint32_t flags;
    if (pm_i16(m,o+velocity)) {
        amount=pm_sar(pm_mul(pm_i32(m,0x5904fcu),pm_i16(m,o+velocity)),8);
        pm_write32(m,o+offset,pm_sub(pm_i32(m,o+offset),amount));
        pm_store(m,o+velocity+6u,pm_load(m,o+velocity+6u,2)+(uint32_t)pm_abs(amount),2);
        if (pm_i16(m,o+velocity+6u)>=pm_i16(m,o+velocity+4u)) {
            flags=pm_load(m,o+0xecu,4);
            if (!(flags&active)) {
                flags|=active;pm_store(m,o+0xecu,flags,4);
                flags=pm_i16(m,o+velocity)>0 ? flags&~reverse : flags|reverse;pm_store(m,o+0xecu,flags,4);
            }
        }
    }
    flags=pm_load(m,o+0xecu,4);if (!(flags&active)) return;
    amount=pm_sar(pm_mul((int32_t)pm_load(m,o+velocity+8u,1),pm_i32(m,0x5904fcu)),pm_load(m,o+velocity+9u,1)&31u);
    value=(flags&reverse) ? pm_add(pm_i16(m,o+velocity),amount) : pm_sub(pm_i16(m,o+velocity),amount);
    pm_store(m,o+velocity,(uint32_t)value,2);
    if (!pm_i16(m,o+velocity)) pm_store(m,o+velocity,(flags&reverse) ? 1u : 65535u,2);
    limit=pm_abs(pm_i16(m,o+velocity+2u));value=pm_i16(m,o+velocity);
    if ((flags&reverse) ? value>limit : value<pm_neg(limit)) {
        pm_store(m,o+velocity+6u,0,2);pm_store(m,o+0xecu,flags&~active,4);
    }
}
int frame_camera_track_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t racer,
                              const frame_entity_factory_services *e,const physics_trig *trig,const uint32_t tail[3])
{
    physics_memory m={read,write,ctx,0};uint32_t o,target,flags,slot,i,parent,unused;int status;
    int32_t velocity[3],saved_velocity[3],direction[3],matrix[12],position[3],saved_position[3],step,v[3],r[3],c,s;
    if (!read || !write || !e) return -1;
    o=object_of(&m,entity);target=object_of(&m,racer);if (m.error) return m.error;if (!target) return 0;
    slot=pm_load(&m,0x53e928u,4) ? 0u : (uint32_t)pm_i8(&m,target+0xdf0u);
    flags=pm_load(&m,o+0xecu,4);
    if (flags&1u) {
        velocity[0]=pm_i16(&m,o+0x104u);velocity[1]=pm_i16(&m,o+0xfau);velocity[2]=pm_i16(&m,o+0xf0u);
        if (velocity[0] || velocity[1] || velocity[2]) { camera_linear_motion(&m,o,velocity);camera_motion_decay(&m,o,velocity); }
        if (m.error) return m.error;
        return frame_camera_publish(read,write,ctx,slot,o+0x11au,pm_load(&m,o+20u,4));
    }
    if (flags&2u) {
        saved_velocity[0]=pm_i16(&m,o+0x104u);saved_velocity[1]=pm_i16(&m,o+0xfau);saved_velocity[2]=pm_i16(&m,o+0xf0u);
        for (i=0;i<3u;i++) direction[i]=pm_sub(pm_i32(&m,target+i*4u),pm_i32(&m,o+0x11au+i*4u));
        if (pm_i16(&m,o+0x110u)) {
            for (i=0;i<3u;i++) v[i]=direction[i];
            physics_vector_normalize(v);
            pm_write32(&m,o+0x11au,pm_add(pm_i32(&m,o+0x11au),pm_sar(pm_mul(pm_mul(v[0],pm_i32(&m,0x5904fcu)),pm_i16(&m,o+0x110u)),20)));
            pm_write32(&m,o+0x122u,pm_add(pm_i32(&m,o+0x122u),pm_sar(pm_mul(pm_mul(v[2],pm_i32(&m,0x5904fcu)),pm_i16(&m,o+0x110u)),20)));
            pm_store(&m,o+0x116u,pm_load(&m,o+0x116u,2)+(uint32_t)pm_sar(pm_mul(pm_i32(&m,0x5904fcu),pm_i16(&m,o+0x110u)),8),2);
            if (pm_abs(pm_i16(&m,o+0x116u))>pm_abs(pm_i16(&m,o+0x114u))) camera_decay(&m,o+0x110u,pm_load(&m,o+0x118u,1),pm_load(&m,o+0x119u,1));
        }
        for (i=0;i<3u;i++) velocity[i]=saved_velocity[i];
        camera_linear_motion(&m,o,velocity);camera_motion_decay(&m,o,saved_velocity);
        for (i=0;i<3u;i++) position[i]=pm_i32(&m,o+0x11au+i*4u);
    } else if (flags&4u) {
        if (!trig || !trig->sine || !trig->cosine) return -5;
        if (pm_i16(&m,o+0x12au)) {
            step=pm_sar(pm_mul(pm_i32(&m,0x5904fcu),pm_i16(&m,o+0x12au)),8);c=trig->cosine[((uint32_t)step&65532u)>>2];s=trig->sine[((uint32_t)step&65532u)>>2];
            for (i=0;i<3u;i++) v[i]=pm_i32(&m,o+0xb8u+i*4u);
            r[0]=pm_trunc(pm_sub(pm_mul(v[0],c),pm_mul(v[2],s)),12);r[2]=pm_trunc(pm_add(pm_mul(v[0],s),pm_mul(v[2],c)),12);r[1]=v[1];
            pm_write32(&m,o+0xb8u,r[0]);pm_write32(&m,o+0xc0u,r[2]);pm_write32(&m,o+0xbcu,r[1]);
        }
        if (pm_i16(&m,o+0x12cu)) {
            step=pm_sar(pm_mul(pm_i32(&m,0x5904fcu),pm_i16(&m,o+0x12cu)),8);c=trig->cosine[((uint32_t)step&65532u)>>2];s=trig->sine[((uint32_t)step&65532u)>>2];
            for (i=0;i<3u;i++) v[i]=pm_i32(&m,o+0xb8u+i*4u);
            r[1]=pm_trunc(pm_add(pm_mul(v[1],c),pm_mul(v[2],s)),12);r[2]=pm_trunc(pm_sub(pm_mul(v[2],c),pm_mul(v[1],s)),12);r[0]=v[0];
            pm_write32(&m,o+0xbcu,r[1]);pm_write32(&m,o+0xc0u,r[2]);pm_write32(&m,o+0xb8u,r[0]);
        }
        for (i=0;i<3u;i++) { step=pm_i32(&m,o+0xb8u+i*4u);position[i]=pm_add(pm_i32(&m,target+i*4u),step);direction[i]=pm_neg(step); }
        camera_orbit_axis(&m,o,0xf0u,0xc0u,32,64);camera_orbit_axis(&m,o,0xfau,0xbcu,8,16);
    } else {
        for (i=0;i<3u;i++) saved_position[i]=pm_i32(&m,o+i*4u);
        for (i=0;i<3u;i++) pm_store(&m,o+i*4u,pm_load(&m,o+0x11au+i*4u,4),4);
        parent=pm_load(&m,0x53cc58u,4);if (m.error) return m.error;
        status=frame_class_dispatch_payload(read,ctx,parent,entity,racer,e->method,e->method_ctx,&unused);if (status) return status;
        for (i=0;i<3u;i++) pm_store(&m,o+0x11au+i*4u,pm_load(&m,o+i*4u,4),4);
        for (i=0;i<3u;i++) pm_write32(&m,o+i*4u,saved_position[i]);
        return m.error;
    }
    if (m.error) return m.error;
    status=camera_direction_values(&m,direction,matrix,trig,tail);if (status) return status;
    return camera_publish_values(&m,slot,position,matrix);
}

static int camera_key(physics_memory *m,uint32_t player,uint32_t action,int *down)
{
    uint32_t code,key;int status;*down=0;if (pm_i8(m,player+0xdf0u)) return m->error;
    code=pm_load(m,0x592000u+action*4u,4);if (m->error) return m->error;
    status=frame_control_key(m->read,m->ctx,code,&key);if (status) return status;
    if (key==1u) { *down=1;return 0; }
    if (pm_load(m,0x53ccdcu,4)) { code=pm_load(m,0x592900u+action*4u,4);if (code) { status=frame_control_key(m->read,m->ctx,code,&key);if (status) return status;*down=key==1u; } }
    return m->error;
}
static void camera_wrap_angle(physics_memory *m,uint32_t address)
{
    int32_t value=pm_i32(m,address);uint32_t times;
    if (value>32760) { times=((uint32_t)value+32759u)/65520u;value=pm_signed((uint32_t)value-times*65520u);pm_write32(m,address,value); }
    if (value<=-32760) { times=(32760u-(uint32_t)value)/65520u;value=pm_signed((uint32_t)value+times*65520u);pm_write32(m,address,value); }
}
int frame_camera_player_view(frame_data_read read,frame_data_write write,void *ctx,uint32_t player,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};int32_t matrix[12],position[3],v[3],result[3];uint32_t i,j,slot;int down,status;
    static const unsigned actions[]={3,4,10,11};static const uint32_t fields[]={0x544fb4,0x544fb4,0x530c84,0x530c84};
    if (!read || !write || !trig || !trig->sine || !trig->cosine) return -1;
    for (i=0;i<12u;i++) matrix[i]=pm_i32(&m,0x532888u+i*4u);
    if (pm_load(&m,0x530c70u,4)) {
        for (i=0;i<3u;i++) v[i]=pm_i32(&m,0x530c88u+i*4u);
        camera_transform(&m,v,player+0x2b2u);
        for (i=0;i<3u;i++) position[i]=pm_add(v[i],pm_i32(&m,player+0x2a6u+i*4u));
        for (i=0;i<12u;i++) matrix[i]=pm_i32(&m,player+0x2b2u+i*4u);
        physics_matrix_rotate(matrix,0,pm_load(&m,0x530c94u,4),trig);slot=0;
    } else {
        for (i=0;i<4u;i++) {
            status=camera_key(&m,player,actions[i],&down);if (status) return status;
            if (down) { int32_t amount=pm_mul(pm_i32(&m,0x530c78u),pm_i32(&m,0x5904fcu));pm_write32(&m,fields[i],i&1u ? pm_sub(pm_i32(&m,fields[i]),amount) : pm_add(pm_i32(&m,fields[i]),amount)); }
        }
        camera_wrap_angle(&m,0x530c84u);camera_wrap_angle(&m,0x544fb4u);
        physics_matrix_rotate(matrix,1,pm_load(&m,0x544fb4u,4),trig);physics_matrix_rotate(matrix,0,pm_load(&m,0x530c84u,4),trig);
        v[0]=v[1]=0;v[2]=pm_neg(pm_i32(&m,0x530c80u));
        for (i=0;i<3u;i++) { result[i]=0;for (j=0;j<3u;j++) result[i]=pm_add(result[i],pm_mul(matrix[j*3u+i],v[j]));position[i]=pm_add(pm_sar(result[i],12),pm_i32(&m,player+i*4u)); }
        status=physics_matrix_normalize(matrix);if (status) return status;slot=(uint32_t)pm_i8(&m,player+0xdf0u);
    }
    if (m.error) return m.error;
    return camera_publish_values(&m,slot,position,matrix);
}

/* Camera scripts express distance in world units, rates per second, and store
 * oscillator velocities as signed16. Parse first, then preserve each original
 * store width; a later wide write must not erase adjacent packed parameters. */
static int camera_find(physics_memory *m,uint32_t parser,const char *key,uint32_t limit,uint32_t *found)
{
    unsigned char alias[32];size_t n=0;int status;
    while (n<sizeof(alias) && key[n]) n++;
    if (n>=sizeof(alias) || n<2u) return -1;
    n++;
    status=frame_text_find_name(m->read,m->write,m->ctx,parser,(const unsigned char *)key,(uint32_t)n,limit,found);if (status || *found) return status;
    memcpy(alias,key,n);if (alias[1]>=97u && alias[1]<=122u) alias[1]-=32u;
    return frame_text_find_name(m->read,m->write,m->ctx,parser,alias,(uint32_t)n,limit,found);
}
static int camera_scalar(physics_memory *m,uint32_t parser,uint32_t destination,uint32_t width,uint32_t limit)
{
    uint32_t value=parser,more;int status=frame_text_read_value(m->read,m->write,m->ctx,parser,limit,&value,&more);if (status) return status;
    pm_store(m,destination,value,width);return m->error;
}
static int camera_float(physics_memory *m,uint32_t parser,uint32_t limit,uint32_t *out,int fixed)
{
    uint32_t bits=0,more;int status=frame_text_read_f32_value(m->read,m->write,m->ctx,parser,limit,&bits,&more);if (status) return status;if (!more) return -4;
    *out=fixed ? frame_text_fixed1024(bits) : frame_text_truncate_f32(bits);return 0;
}
int frame_camera_parameters(frame_data_read read,frame_data_write write,void *ctx,uint32_t target,uint32_t entity,uint32_t parser,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o,klass,unused,head,found,mode=0,i,j,v[3],value,at,limit;int status;
    static const char *move_names[]={":x_move_speed",":y_move_speed",":z_move_speed"};
    static const uint32_t move_offsets[]={0x104,0xfa,0xf0},move_initial[]={0x12e,0xfc,0xf2};
#define CAMERA_TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
#define CAMERA_FIND(key) camera_find(&m,parser,(key),limit,&found)
    if (!read || !write || !e || !e->work_limit) return -1;
    limit=e->work_limit;o=object_of(&m,entity);klass=pm_load(&m,target==0x43d2a0u ? 0x53cc58u : target==0x4057c0u ? 0x5466e4u : 0x53cc30u,4);
    CAMERA_TRY(frame_class_dispatch_payload(read,ctx,klass,entity,parser,e->method,e->method_ctx,&unused));
    if (target==0x406290u) return m.error;
    if (target==0x4057c0u) { head=pm_load(&m,0x54b48cu,4);CAMERA_TRY(frame_list_remove_value(read,write,ctx,entity,head,e->release,e->system_ctx,limit));return frame_entity_queue(read,write,ctx,entity,0,e->allocate,e->release,e->system_ctx,limit); }
    if (target!=0x43d2a0u) return -5;
    CAMERA_TRY(CAMERA_FIND(":range"));if (found) { CAMERA_TRY(frame_text_read_i32(read,write,ctx,parser,o+0x126u,limit,&unused));pm_store(&m,o+0x126u,pm_load(&m,o+0x126u,4)<<10,4); }
    CAMERA_TRY(CAMERA_FIND(":fixed_cam"));if (found) mode=1;
    else { CAMERA_TRY(CAMERA_FIND(":pan_cam"));if (found) mode=2;else { CAMERA_TRY(CAMERA_FIND(":follow_cam"));if (found) mode=4;else { CAMERA_TRY(CAMERA_FIND(":chase_cam"));if (found) mode=8; } } }
    if (mode==1u || mode==2u) {
        pm_store(&m,o+0x10cu,0,2);pm_store(&m,o+0xecu,pm_load(&m,o+0xecu,4)|mode,4);
        for (i=0;i<3u;i++) { CAMERA_TRY(CAMERA_FIND(move_names[i]));if (found) { CAMERA_TRY(camera_float(&m,parser,limit,&value,1));value=(uint32_t)(pm_signed(value<<8)/1000);pm_store(&m,o+move_offsets[i],value,2);pm_store(&m,o+move_initial[i],value,2); } }
        CAMERA_TRY(CAMERA_FIND(":max_move_dist"));if (found) { CAMERA_TRY(camera_float(&m,parser,limit,&value,0));pm_store(&m,o+0x10au,value<<10,2); }
        CAMERA_TRY(CAMERA_FIND(":move_slow_mul"));if (found) { CAMERA_TRY(camera_scalar(&m,parser,o+0x10eu,1,limit)); }
        CAMERA_TRY(CAMERA_FIND(":move_slow_shf"));if (found) { CAMERA_TRY(camera_scalar(&m,parser,o+0x10fu,1,limit)); }
        if (mode==2u) {
            CAMERA_TRY(CAMERA_FIND(":zoom_speed"));if (found) { CAMERA_TRY(camera_float(&m,parser,limit,&value,1));value=(uint32_t)(pm_signed(value<<8)/1000);pm_store(&m,o+0x110u,value,2);pm_store(&m,o+0x112u,value,2); }
            CAMERA_TRY(CAMERA_FIND(":max_zoom_dist"));if (found) { CAMERA_TRY(camera_float(&m,parser,limit,&value,0));pm_store(&m,o+0x114u,value<<10,2); }
            CAMERA_TRY(CAMERA_FIND(":zoom_slow_mul"));if (found) { CAMERA_TRY(camera_scalar(&m,parser,o+0x118u,1,limit)); }
            CAMERA_TRY(CAMERA_FIND(":zoom_slow_shf"));if (found) { CAMERA_TRY(camera_scalar(&m,parser,o+0x119u,1,limit)); }
        }
    } else if (mode==4u || mode==8u) {
        if (mode==4u) pm_store(&m,o+0xecu,pm_load(&m,o+0xecu,4)|4u,4);
        CAMERA_TRY(CAMERA_FIND(":disp"));
        if (found) { for (i=0;i<3u;i++) { CAMERA_TRY(camera_float(&m,parser,limit,&v[i],1)); }for (i=0;i<3u;i++) pm_store(&m,o+0xb8u+i*4u,v[i],4); }
        if (found || mode==4u) for (i=0;i<3u;i++) pm_store(&m,o+0x132u+i*4u,pm_load(&m,o+0xb8u+i*4u,4),4);
        if (mode==8u) { CAMERA_TRY(frame_camera_matrix_angles(read,write,ctx,pm_load(&m,o+20u,4),o+0x9cu,&unused)); }
        else {
            static const char *rotation[]={":y_rotspeed",":x_rotspeed"};
            for (i=0;i<2u;i++) { CAMERA_TRY(CAMERA_FIND(rotation[i]));if (found) { at=o+0x12au+i*2u;CAMERA_TRY(camera_scalar(&m,parser,at,2,limit));pm_store(&m,at,(uint32_t)(pm_mul(pm_i16(&m,at),46592)/1000),2); } }
            for (i=0;i<2u;i++) {
                const char *speed=i ? ":y_resize_speed" : ":z_resize_speed";const char *names[3]={i ? ":y_decel_mul" : ":z_decel_mul",i ? ":y_decel_shf" : ":z_decel_shf",i ? ":y_resize_range" : ":z_resize_range"};at=o+(i ? 0xfau : 0xf0u);
                CAMERA_TRY(CAMERA_FIND(speed));if (!found) continue;
                CAMERA_TRY(camera_float(&m,parser,limit,&value,1));value=(uint32_t)(pm_signed(value<<8)/1000);pm_store(&m,at,value,2);pm_store(&m,at+2u,value,2);
                for (j=0;j<3u;j++) { CAMERA_TRY(CAMERA_FIND(names[j]));if (!found) continue;if (j<2u) { CAMERA_TRY(camera_scalar(&m,parser,at+8u+j,1,limit)); }else { CAMERA_TRY(camera_float(&m,parser,limit,&value,1));pm_store(&m,at+4u,value,2); } }
            }
        }
    }
    for (i=0;i<3u;i++) pm_store(&m,o+0x11au+i*4u,pm_load(&m,o+i*4u,4),4);
    head=pm_load(&m,0x5466c8u,4);CAMERA_TRY(frame_list_prepend(read,write,ctx,entity,head,e->allocate,e->release,e->system_ctx,limit,&unused));
    if (pm_load(&m,0x52ee9cu,4)==4u) { pm_store(&m,o+0xecu,pm_load(&m,o+0xecu,4)|2u,4);if (!pm_load(&m,o+0x126u,4)) pm_store(&m,o+0x126u,51200,4);pm_store(&m,0x5466d0u,0,4);pm_store(&m,0x5466dcu,0,4);pm_store(&m,0x5466d4u,0,4); }
    return m.error;
#undef CAMERA_FIND
#undef CAMERA_TRY
}
