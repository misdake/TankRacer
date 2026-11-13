#include "frame_deformable.h"
#include "physics_memory.h"
#include "frame_lap_events.h"
#include "frame_shape.h"
#include "frame_text.h"
#include "physics_terrain.h"

static uint32_t object_of(physics_memory *m,uint32_t entity)
{
    uint32_t record=pm_load(m,pm_load(m,0x55c694u,4)+entity*8u,4);
    return record ? pm_load(m,record+4u,4) : 0;
}
static uint32_t random_next(physics_memory *m)
{
    uint32_t seed=pm_load(m,0x55b6acu,4)*1664525u+1013904223u;
    pm_store(m,0x55b6acu,seed,4);return seed;
}
int frame_deformable_initialize(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o;
    if (!read || !write || !out) return -1;
    o=object_of(&m,entity);pm_store(&m,o+0x5cu,8,4);pm_store(&m,o+0xaeu,50,4);
    pm_store(&m,o+0xb2u,1,4);pm_store(&m,o+0xb6u,45000,4);pm_store(&m,o+0xe5u,1,4);
    if (m.error) return m.error;
    *out=entity;return 0;
}
int frame_deformable_set_flag(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t mode,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o;
    if (!read || !write || !out) return -1;
    o=object_of(&m,entity);pm_store(&m,o+0xcbu,mode==1u,1);
    if (m.error) return m.error;
    *out=o;return 0;
}
int frame_deformable_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                            const frame_entity_factory_services *s,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,near=0,at,end,state,retired=0,used=0;int32_t timer,dt;int status;
    if (!read || !write || !out || !s || !s->work_limit) return -1;
    o=object_of(&m,entity);
    if (pm_load(&m,o+0xf9u,4)) {
        if (m.error) return m.error;
        status=frame_entity_near_camera(read,ctx,o,0,0x2800,&near);if (status) return status;
    }
    if (near || pm_load(&m,o+0xf1u,4)) pm_store(&m,o+0xf1u,0,4);
    if (pm_load(&m,o+0xccu,1) && (timer=pm_i32(&m,o+0xf9u))!=0) {
        if (timer<11026) { pm_store(&m,o+0xf1u,0,4);pm_store(&m,o+0xf9u,0,4); }
        else pm_write32(&m,o+0xf9u,pm_sub(timer,pm_mul(pm_i32(&m,0x5904fcu),8)));
    }
    end=pm_load(&m,o+0xcdu,4)*32u;at=pm_load(&m,o+0xd5u,4);end+=at;
    for (;at<end;at+=32u) {
        if (m.error) return m.error;
        if (used++>=s->work_limit) return -3;
        state=pm_load(&m,at+0x18u,1);
        if (state<2u) {
            timer=pm_i32(&m,at+0x1cu);dt=pm_i32(&m,0x5904fcu);
            if (timer>dt) pm_write32(&m,at+0x1cu,pm_sub(timer,dt));
            else { pm_store(&m,at+0x18u,state ? 0u : 1u,1);pm_write32(&m,at+0x1cu,pm_add(timer,500)); }
        } else if (state==2u) {
            dt=pm_i32(&m,0x5904fcu);timer=pm_i8(&m,at+0x19u);
            if (timer<=dt) { pm_store(&m,at+0x18u,3,1);retired++; }
            else {
                state=pm_load(&m,at+0x19u,1)-(uint32_t)dt;
                pm_store(&m,at+0x19u,state,1);pm_store(&m,at+0x1au,state,1);pm_store(&m,at+0x1bu,state,1);
            }
        } else if (state==3u) retired++;
    }
    if (m.error) return m.error;
    if (retired==pm_load(&m,o+0xcdu,4) && !pm_load(&m,o+0xf9u,4)) {
        pm_store(&m,o+0xd5u,0,4);if (m.error) return m.error;
        status=frame_entity_queue(read,write,ctx,entity,0,s->allocate,s->release,s->system_ctx,s->work_limit);
        if (status) return status;
    }
    if (m.error) return m.error;
    *out=entity;return 0;
}
int frame_deformable_enter(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                             const frame_entity_factory_services *s,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,node,result=0;int status;
    if (!read || !write || !out || !s || !s->work_limit) return -1;
    o=object_of(&m,entity);node=pm_load(&m,0x53cd18u,4);if (m.error) return m.error;
    status=frame_list_append(read,write,ctx,entity,node,s->allocate,s->release,s->system_ctx,s->work_limit,&node);if (status) return status;
    node=pm_load(&m,0x54b488u,4);if (m.error) return m.error;
    status=frame_list_append(read,write,ctx,entity,node,s->allocate,s->release,s->system_ctx,s->work_limit,&node);if (status) return status;
    if (pm_load(&m,o+0xd5u,4)) {
        if (m.error) return m.error;
        status=frame_entity_activate(read,write,ctx,entity,s->method,s->method_ctx,&result);if (status) return status;
    }
    if (m.error) return m.error;
    *out=result;return 0;
}
int frame_deformable_leave(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                             const frame_entity_factory_services *s,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,head,node,result=0,used=0;int status;
    if (!read || !write || !out || !s || !s->work_limit) return -1;
    o=object_of(&m,entity);head=pm_load(&m,0x53cd18u,4);if (m.error) return m.error;
    status=frame_list_remove_value(read,write,ctx,entity,head,s->release,s->system_ctx,s->work_limit);if (status) return status;
    head=pm_load(&m,0x54b488u,4);if (m.error) return m.error;
    status=frame_list_remove_value(read,write,ctx,entity,head,s->release,s->system_ctx,s->work_limit);if (status) return status;
    if (pm_load(&m,o+0xd5u,4)) {
        if (m.error) return m.error;
        status=frame_entity_queue(read,write,ctx,entity,0,s->allocate,s->release,s->system_ctx,s->work_limit);if (status) return status;
        /* 503E40 returns the existing/new notification node through EAX. */
        head=pm_load(&m,0x55b6bcu,4);node=pm_load(&m,head,4);
        while (node!=head) {
            if (m.error) return m.error;
            if (used++>=s->work_limit) return -3;
            if (pm_load(&m,node+8u,4)==entity) { result=node;break; }
            node=pm_load(&m,node,4);
        }
    }
    if (m.error) return m.error;
    *out=result;return 0;
}
int frame_deformable_destroy(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                               const frame_entity_factory_services *s,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,shape,bytes,klass,unused;int status;
    if (!read || !write || !out || !s || !s->work_limit) return -1;
    o=object_of(&m,entity);
    if (pm_load(&m,o+0xddu,4)) pm_store(&m,o+0xddu,0,4);
    shape=pm_load(&m,o+0x48u,4);
    if (shape && (bytes=pm_load(&m,o+0xedu,4))!=0) {
        if (m.error) return m.error;
        status=frame_shape_release(read,write,ctx,shape,bytes,s);if (status) return status;
        pm_store(&m,o+0x48u,0,4);
    }
    if (pm_load(&m,o+0xd5u,4)) pm_store(&m,o+0xd5u,0,4);
    klass=pm_load(&m,0x544f80u,4);if (m.error) return m.error;
    status=frame_class_dispatch(read,ctx,klass,entity,s->method,s->method_ctx,&unused);if (status) return status;
    *out=entity;return 0;
}
static int parameter(physics_memory *m,uint32_t parser,uint32_t name,uint32_t destination,uint32_t limit,uint32_t *found)
{
    uint32_t more;int status=frame_text_find(m->read,m->write,m->ctx,parser,name,limit,found);
    if (status || !*found) return status;
    return frame_text_read_i32(m->read,m->write,m->ctx,parser,destination,limit,&more);
}
static int temporary(physics_memory *m,uint32_t parser,uint32_t limit,uint32_t *value,uint32_t *defined)
{
    uint32_t more;int status=frame_text_read_value(m->read,m->write,m->ctx,parser,limit,value,&more);
    if (status) return status;
    if (more) *defined=1;
    return *defined ? 0 : -4;
}
static int power_capacity(physics_memory *m,uint32_t destination,uint32_t required,uint32_t limit,uint32_t *used,uint32_t *out)
{
    uint32_t capacity=2;pm_store(m,destination,required,4);
    while (capacity<required) { if ((*used)++>=limit) return -3;capacity*=2u; }
    pm_store(m,destination,capacity,4);if (m->error) return m->error;*out=capacity;return 0;
}
int frame_deformable_construct_resources(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,
                                 const frame_entity_factory_services *s,const frame_shape_resources *resources,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,klass,unused,found,more,shape,capacity,storage,i,j,k,at,matrix,seed,used=0;
    uint32_t temp=0,defined=0;unsigned char name[32];int32_t bounds[4],height,vector[3],value;int status;
    static const uint32_t raw_bounds[]={0x60,0x6c,0x68,0x74};
    if (!read || !write || !out || !s || !s->work_limit) return -1;
    o=object_of(&m,entity);klass=pm_load(&m,0x544f80u,4);if (m.error) return m.error;
    status=frame_class_dispatch_payload(read,ctx,klass,entity,parser,s->method,s->method_ctx,&unused);if (status) return status;
    status=parameter(&m,parser,0x52ec54u,o+0xaeu,s->work_limit,&found);if (status) return status;
    if (found) pm_write32(&m,o+0xaeu,pm_sar(pm_i32(&m,o+0xaeu),3));
    status=frame_text_find(read,write,ctx,parser,0x52ec44u,s->work_limit,&found);if (status) return status;
    if (found) {
        status=frame_text_next(read,write,ctx,parser,name,32,s->work_limit,&more);if (status) return status;
        if (resources) {
            status=frame_shape_load_name(read,write,ctx,name,32,pm_load(&m,0x55ae40u,4),s,resources,&shape);if (status) return status;
            status=frame_shape_clone_loaded(read,write,ctx,shape,o+0xedu,s,&shape);
        } else status=frame_shape_copy_name(read,write,ctx,name,32,o+0xedu,s,&shape);
        if (status) return status;
        pm_store(&m,o+0x48u,shape,4);
        for (i=0;i<6u;i++) pm_write32(&m,o+0x60u+i*4u,pm_add(pm_mul(pm_i16(&m,shape+0x1cu+i*2u),8),i<3u ? -1536 : 1536));
    } else pm_store(&m,o+0xedu,0,4);
    if (m.error) return m.error;
    status=frame_text_find(read,write,ctx,parser,0x52ec38u,s->work_limit,&found);if (status) return status;
    if (found) {
        status=temporary(&m,parser,s->work_limit,&temp,&defined);if (status) return status;
        shape=pm_load(&m,o+0x48u,4);pm_store(&m,o+0x4cu,temp,2);if (shape) pm_store(&m,shape+0xeu,temp,2);
    }
    status=parameter(&m,parser,0x52ec28u,o+0xb2u,s->work_limit,&found);if (status) return status;
    status=parameter(&m,parser,0x52ec18u,o+0xe5u,s->work_limit,&found);if (status) return status;
    status=parameter(&m,parser,0x52ec08u,o+0xcdu,s->work_limit,&found);if (status) return status;
    if (found) {
        status=power_capacity(&m,o+0xd1u,pm_load(&m,o+0xcdu,4)*32u,s->work_limit,&used,&capacity);if (status) return status;
        status=frame_level_allocate(read,write,ctx,capacity,&storage);if (status) return status;
        pm_store(&m,o+0xd5u,storage,4);
        for (i=0;i<pm_load(&m,o+0xcdu,4);i++) {
            if (m.error) return m.error;
            if (used++>=s->work_limit) return -3;
            for (j=0;j<3u;j++) {
                status=temporary(&m,parser,s->work_limit,&temp,&defined);if (status) return status;
                pm_store(&m,pm_load(&m,o+0xd5u,4)+i*32u+25u+j,temp,1);
            }
            for (j=0;j<3u;j++) {
                status=frame_text_read_i32(read,write,ctx,parser,pm_load(&m,o+0xd5u,4)+i*32u+j*4u,s->work_limit,&more);if (status) return status;
            }
            at=pm_load(&m,o+0xd5u,4)+i*32u;matrix=pm_load(&m,o+0x14u,4);
            if (matrix) {
                for (j=0;j<3u;j++) vector[j]=pm_i32(&m,at+j*4u);
                for (j=0;j<3u;j++) {
                    value=0;for (k=0;k<3u;k++) value=pm_add(value,pm_mul(vector[k],pm_i32(&m,matrix+(k*3u+j)*4u)));
                    pm_write32(&m,at+12u+j*4u,pm_sar(value,12));
                }
            }
            for (j=0;j<3u;j++) {
                at=pm_load(&m,o+0xd5u,4)+i*32u+12u+j*4u;pm_write32(&m,at,pm_add(pm_i32(&m,at),pm_i32(&m,o+j*4u)));
            }
            seed=random_next(&m);pm_store(&m,pm_load(&m,o+0xd5u,4)+i*32u+24u,(seed&16u)!=0,1);
            seed=random_next(&m);pm_store(&m,pm_load(&m,o+0xd5u,4)+i*32u+28u,(seed>>4)&511u,4);
        }
        status=frame_text_find(read,write,ctx,parser,0x52ec00u,s->work_limit,&found);if (status) return status;
        if (found) { pm_store(&m,o+0xf5u,0,4);pm_store(&m,o+0xf9u,22050,4); }
        if (!pm_load(&m,o+0x78u,2)) {
            if (m.error) return m.error;
            status=frame_entity_cancel_notification(read,write,ctx,entity,s->release,s->system_ctx,s->work_limit);if (status) return status;
        }
    }
    status=parameter(&m,parser,0x52ebf0u,o+0xb6u,s->work_limit,&found);if (status) return status;
    if (pm_i32(&m,o+0xb6u)<0) pm_store(&m,o+0xb6u,0,4);
    if (pm_i32(&m,o+0xb6u)>65535) pm_store(&m,o+0xb6u,65535,4);
    for (i=0;i<4u;i++) bounds[i]=pm_i32(&m,o+raw_bounds[i]);
    for (i=0;i<4u;i++) pm_write32(&m,o+0xbau+i*4u,bounds[i]);
    for (i=0;i<4u;i++) pm_write32(&m,o+raw_bounds[i],pm_trunc(pm_mul(bounds[i],1300),10));
    if (m.error) return m.error;
    status=physics_terrain_height(read,ctx,pm_load(&m,0x55ae60u,4),pm_i32(&m,o),pm_i32(&m,o+8u),&height);if (status<0) return status;
    pm_write32(&m,o+0xd9u,height);shape=pm_load(&m,o+0x48u,4);
    status=power_capacity(&m,o+0xe1u,pm_load(&m,shape,2),s->work_limit,&used,&capacity);if (status) return status;
    status=frame_level_allocate(read,write,ctx,capacity,&storage);if (status) return status;
    pm_store(&m,o+0xddu,storage,4);
    for (i=0;i<pm_load(&m,pm_load(&m,o+0x48u,4),2);i++) {
        if (m.error) return m.error;
        if (used++>=s->work_limit) return -3;
        if ((random_next(&m)>>4)&1u) {
            at=pm_load(&m,o+0xddu,4)+i;pm_store(&m,at,pm_load(&m,at,1)|4u,1);
            if ((random_next(&m)>>4)&1u) { at=pm_load(&m,o+0xddu,4)+i;pm_store(&m,at,pm_load(&m,at,1)|1u,1); }
            if ((random_next(&m)>>4)&1u) { at=pm_load(&m,o+0xddu,4)+i;pm_store(&m,at,pm_load(&m,at,1)|2u,1); }
        }
    }
    if (m.error) return m.error;
    *out=entity;return 0;
}
int frame_deformable_hit(frame_data_read read,frame_data_write write,void *ctx,
                           uint32_t entity,uint32_t vehicle,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,tank,mesh,flags,vertex,end,matrix,i,j,word,changed=0;
    int32_t relative[3],local[3],dx,dz,height,level,amount,timer,dt;
    if (!read || !write || !out) return -1;
    o=object_of(&m,entity);tank=object_of(&m,vehicle);
    mesh=pm_load(&m,o+0x48u,4);flags=pm_load(&m,o+0xddu,4);vertex=pm_load(&m,mesh+4u,4);
    pm_store(&m,o+0xcau,1,1);pm_store(&m,o+0xaau,vehicle,4);
    for (i=0;i<3u;i++) relative[i]=pm_sub(pm_i32(&m,tank+i*4u),pm_i32(&m,o+i*4u));
    matrix=pm_load(&m,o+0x14u,4);
    for (i=0;i<3u;i++) {
        local[i]=relative[i];
        if (matrix) {
            local[i]=0;
            for (j=0;j<3u;j++) local[i]=pm_add(local[i],pm_mul(pm_i32(&m,matrix+(i*3u+j)*4u),relative[j]));
            local[i]=pm_sar(local[i],12);
        }
        local[i]=pm_trunc(local[i],3);
    }
    mesh=pm_load(&m,o+0x48u,4);end=vertex+pm_load(&m,mesh,2)*8u;
    if (m.error) return m.error;
    for (;vertex<end;vertex+=8u,flags++) {
        dx=pm_sub(local[0],pm_i16(&m,vertex));
        if (dx<256 && dx>-256) {
            dz=pm_sub(local[2],pm_i16(&m,vertex+4u));
            if (dz<256 && dz>-256) {
                height=pm_i16(&m,vertex+2u);
                if (height>pm_i32(&m,o+0xaeu)) {
                    level=pm_sar(height,5);if (level<0) level=0;if (level>14) level=14;
                    amount=pm_sar(pm_mul(pm_mul(pm_i32(&m,o+0xb2u),level+1),pm_i32(&m,0x5904fcu)),6);
                    word=(uint32_t)pm_sub(height,amount)&65535u;pm_store(&m,vertex+2u,word,2);
                    height=word>32767u ? (int32_t)word-65536 : (int32_t)word;
                    if (height>pm_i32(&m,o+0xaeu)) changed=1;
                    else pm_store(&m,vertex+2u,pm_load(&m,o+0xaeu,2),2);
                    if (pm_load(&m,flags,1)&4u) {
                        amount=pm_sar(pm_i32(&m,0x5904fcu),pm_load(&m,o+0xe5u,4));
                        word=pm_load(&m,vertex,2);
                        pm_store(&m,vertex,(pm_load(&m,flags,1)&1u) ? word-(uint32_t)amount : word+(uint32_t)amount,2);
                        amount=pm_sar(pm_i32(&m,0x5904fcu),pm_load(&m,o+0xe5u,4));word=pm_load(&m,vertex+4u,2);
                        pm_store(&m,vertex+4u,(pm_load(&m,flags,1)&2u) ? word-(uint32_t)amount : word+(uint32_t)amount,2);
                    }
                }
            }
        }
        if (m.error) return m.error;
    }
    pm_write32(&m,tank+0xd95u,pm_i16(&m,o+0x4eu));
    if (pm_load(&m,o+0x84u,4)) {
        timer=pm_i32(&m,o+0xe9u);dt=pm_i32(&m,0x5904fcu);
        pm_write32(&m,o+0xe9u,timer<dt ? 0 : pm_sub(timer,dt));
        if (!pm_load(&m,o+0xe9u,4) && changed) {
            /* First draw selected pitch; even without audio it is shared RNG. */
            (void)random_next(&m);word=random_next(&m);
            pm_store(&m,o+0xe9u,((word>>4)&511u)+400u,4);
        }
    }
    if (m.error) return m.error;
    *out=entity;return 0;
}

int frame_deformable_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *s,uint32_t *out)
{ return frame_deformable_construct_resources(read,write,ctx,entity,parser,s,0,out); }
