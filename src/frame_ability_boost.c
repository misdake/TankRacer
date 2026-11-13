#include "frame_ability_boost.h"
#include "physics_memory.h"

static uint32_t object_of(physics_memory *m,uint32_t entity)
{
    uint32_t record=pm_load(m,pm_load(m,0x55c694u,4)+entity*8u,4);
    return record ? pm_load(m,record+4u,4) : 0;
}
int frame_ability_boost_start(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity)
{
    physics_memory m={read,write,ctx,0};uint32_t object;
    if (!read || !write) return -1;
    object=object_of(&m,entity);pm_store(&m,object+0xdbfu,0,4);
    pm_store(&m,object+0xdc3u,pm_load(&m,0x53e40cu,4) ? 1000u : 0u,4);
    pm_store(&m,object+0xdc7u,entity,4);pm_store(&m,object+0xdcdu,1,1);
    pm_store(&m,object+0xdccu,0,1);pm_store(&m,object+0xdcbu,1,1);
    pm_write32(&m,0x546704u,pm_i16(&m,object+0x4eu));pm_store(&m,0x546708u,1300,4);pm_store(&m,0x54670cu,110,4);
    return m.error;
}
int frame_ability_boost_apply(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t object,config;int32_t amount;
    if (!read || !write || !out) return -1;
    object=object_of(&m,entity);amount=pm_add(pm_i32(&m,object+0xc9eu),pm_i32(&m,object+0x70eu));
    pm_store(&m,object+0xdd6u,5000,2);pm_write32(&m,object+0xc9eu,amount);
    config=pm_load(&m,object+0xde5u,1) ? 0x531958u : 0x53195cu;
    pm_store(&m,object+0xcaau,pm_load(&m,config,4),4);pm_store(&m,object+0xcb2u,pm_load(&m,config+8u,4),4);
    if (amount>500000) pm_store(&m,object+0xc9eu,500000,4);
    if (m.error) return m.error;
    *out=object;return 0;
}
typedef struct boost_native_context { frame_data_read read;frame_data_write write;void *ctx; } boost_native_context;
static int known_boost(void *ctx,uint32_t target,uint32_t entity,uint32_t has_payload,uint32_t payload,uint32_t *out)
{
    boost_native_context *c=(boost_native_context *)ctx;(void)has_payload;(void)payload;
    if (target!=0x437bf0u) return -5;
    return frame_ability_boost_apply(c->read,c->write,c->ctx,entity,out);
}
static int apply(physics_memory *m,uint32_t object,frame_native_method method,void *method_ctx)
{
    uint32_t entity=pm_load(m,object+0xdc7u,4),descriptor=pm_load(m,0x545f00u,4),result;
    boost_native_context context={m->read,m->write,m->ctx};
    if (m->error) return m->error;
    return frame_entity_dispatch(m->read,m->write,m->ctx,entity,descriptor,0,0,
                                 method ? method : known_boost,method ? method_ctx : &context,&result);
}
static void integrate_camera(physics_memory *m,uint32_t camera,uint32_t object)
{
    pm_write32(m,camera+0xc0u,pm_add(pm_i32(m,camera+0xc0u),pm_sar(pm_mul(pm_i32(m,0x5904fcu),pm_i32(m,object+0xdbfu)),8)));
}
int frame_ability_boost_camera(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,uint32_t variant,
                                 frame_native_method method,void *method_ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t camera,flags;int32_t value;int status;
    if (!read || !write) return -1;
    camera=object_of(&m,pm_load(&m,0x53cc38u+(uint32_t)pm_i8(&m,object+0xdf0u)*4u,4));
    if (pm_load(&m,0x53e40cu,4)) {
        if (!variant) {
            value=pm_i32(&m,object+0xdc3u);
            if (value) {
                if (value<=pm_i32(&m,0x5904fcu)) {
                    pm_store(&m,object+0xdc3u,0,4);pm_store(&m,object+0xdccu,1,1);
                    if (m.error) return m.error;
                    status=apply(&m,object,method,method_ctx);if (status) return status;
                    pm_store(&m,object+0xdcdu,8,1);
                } else pm_write32(&m,object+0xdc3u,pm_sub(value,pm_i32(&m,0x5904fcu)));
            }
        }
        return m.error;
    }
    flags=pm_load(&m,object+0xdcdu,1);
    if (flags&1u) {
        if (pm_i32(&m,camera+0xc0u)<-7199) {
            if (pm_i32(&m,camera+0xa8u)<1)
                pm_write32(&m,camera+0xa8u,pm_add(pm_i32(&m,camera+0xa8u),pm_mul(pm_i32(&m,0x5904fcu),variant ? 8 : 1)));
            if (pm_i32(&m,camera+0xbcu)>(variant ? 450 : 2000))
                pm_write32(&m,camera+0xbcu,pm_sub(pm_i32(&m,camera+0xbcu),pm_mul(pm_i32(&m,0x5904fcu),variant ? 8 : 1)));
            if (pm_i32(&m,camera+0xc0u)<-7799) {
                value=pm_add(pm_i32(&m,object+0xdbfu),pm_mul(pm_i32(&m,0x5904fcu),16));pm_write32(&m,object+0xdbfu,value);
                if (value>5000) pm_store(&m,object+0xdbfu,5000,4);
            } else {
                value=pm_sub(pm_i32(&m,object+0xdbfu),pm_mul(pm_i32(&m,0x5904fcu),32));pm_write32(&m,object+0xdbfu,value);
                if (value<100) pm_store(&m,object+0xdbfu,100,4);
            }
            integrate_camera(&m,camera,object);return m.error;
        }
        value=pm_sub(pm_i32(&m,object+0xdbfu),pm_mul(pm_i32(&m,0x5904fcu),8));pm_write32(&m,object+0xdbfu,value);
        integrate_camera(&m,camera,object);value=pm_add(pm_i32(&m,object+0xdc3u),pm_i32(&m,0x5904fcu));pm_write32(&m,object+0xdc3u,value);
        if (value>=200) {
            if (!pm_load(&m,object+0xdccu,1)) {
                if (m.error) return m.error;
                status=apply(&m,object,method,method_ctx);if (status) return status;
                pm_store(&m,object+0xdccu,1,1);
            }
            if (pm_i32(&m,object+0xdc3u)>=350) { pm_store(&m,object+0xdcdu,2,1);pm_store(&m,object+0xdc3u,0,4); }
        }
    } else if (flags&2u) {
        value=pm_sub(pm_i32(&m,object+0xdbfu),pm_mul(pm_i32(&m,0x5904fcu),variant ? 64 : 128));pm_write32(&m,object+0xdbfu,value);
        if (value<-15000) pm_write32(&m,object+0xdbfu,-15000);
        if (variant) {
            value=pm_add(pm_i32(&m,camera+0xbcu),pm_mul(pm_i32(&m,0x5904fcu),10));pm_write32(&m,camera+0xbcu,value);
            if (value>3000) pm_store(&m,camera+0xbcu,3000,4);
        } else {
            pm_write32(&m,camera+0xa8u,pm_sub(pm_i32(&m,camera+0xa8u),pm_i32(&m,0x5904fcu)));
            if (pm_i32(&m,camera+0xbcu)>5500) pm_store(&m,camera+0xbcu,5500,4);
            if (pm_i32(&m,camera+0xa8u)<-1638) pm_write32(&m,camera+0xa8u,-1638);
        }
        integrate_camera(&m,camera,object);
        if (pm_i32(&m,camera+0xc0u)<(variant ? -8999 : -8199)) pm_store(&m,object+0xdcdu,4,1);
    } else if (flags&4u) {
        if (pm_i32(&m,object+0xdbfu)) {
            value=pm_add(pm_i32(&m,object+0xdbfu),pm_mul(pm_i32(&m,0x5904fcu),128));pm_write32(&m,object+0xdbfu,value);
            if (value>0) pm_store(&m,object+0xdbfu,0,4);
            integrate_camera(&m,camera,object);
        } else pm_store(&m,object+0xdcdu,8,1);
    }
    return m.error;
}
int frame_ability_boost_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,
                               frame_native_method method,void *method_ctx)
{
    physics_memory m={read,write,ctx,0};int status;uint32_t frame,seed,delta;
    if (!read || !write) return -1;
    if (!pm_load(&m,object+0xde5u,1)) {
        if (!pm_load(&m,object+0xdccu,1)) {
            pm_store(&m,object+0xdccu,1,1);if (m.error) return m.error;
            status=apply(&m,object,method,method_ctx);if (status) return status;
        }
    } else if (!(pm_load(&m,object+0xdcdu,1)&8u) && pm_i16(&m,object+0xddau)!=3) {
        uint32_t variant=pm_load(&m,0x54671cu,4);if (m.error) return m.error;
        status=frame_ability_boost_camera(read,write,ctx,object,variant,method,method_ctx);if (status) return status;
    }
    if (!pm_load(&m,object+0xdccu,1)) return m.error;
    frame=pm_load(&m,0x590564u,4);
    if (pm_load(&m,0x5466e8u,4)!=frame) {
        pm_store(&m,0x5466e8u,frame,4);seed=pm_load(&m,0x55b6acu,4)*1664525u+1013904223u;pm_store(&m,0x55b6acu,seed,4);
        delta=(seed>>4)&63u;pm_store(&m,0x58ee12u,delta,2);
        pm_store(&m,pm_load(&m,0x58ee00u,4)+4u,pm_load(&m,0x58ee10u,2)-delta,2);
        pm_store(&m,pm_load(&m,0x58ee04u,4)+4u,pm_load(&m,0x58ee08u,2)-delta,2);
    }
    return m.error;
}
