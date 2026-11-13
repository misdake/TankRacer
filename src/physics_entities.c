#include "physics_entities.h"
#include "physics_memory.h"
#include "frame_deformable.h"

static uint32_t object(physics_memory *m,uint32_t id)
{ uint32_t link=pm_load(m,pm_load(m,0x55c694,4)+id*8u,4);return link ? pm_load(m,link+4,4) : 0; }
static int operation(physics_entity_environment *e,int phase,uint32_t o,uint32_t id,int32_t arg)
{ int status;if (!e->scene) return -8;status=e->scene(e->scene_ctx,phase,o,id,arg);return status>0 ? -8 : status; }
static int dispatch(physics_entity_environment *e,uint32_t id,uint32_t interface_address,uint32_t arg)
{ uint32_t result;return physics_entity_collision_dispatch(e->read,e->write,e->ctx,id,interface_address,arg,e->services,&result); }
static int obstacle_hit(physics_entity_environment *e,uint32_t id,uint32_t argument)
{
    physics_memory memory={e->read,e->write,e->ctx,0},*m=&memory;uint32_t o=object(m,id),tank=object(m,argument),flags,sentinel,node,count;int32_t v[3],speed,rate,cap;unsigned i;int status;
    if (pm_i32(m,o+0x80) || (pm_load(m,o+0xea,4)&1u)) return m->error;
    flags=(pm_load(m,o+0xea,4)&UINT32_C(0xfffffff3))|1u;pm_store(m,o+0xea,flags,4);
    for (i=0;i<3;++i) v[i]=pm_i32(m,tank+0x50+i*4u);
    speed=physics_approximate_distance(v);rate=pm_neg(speed)>=0 ? pm_sar(pm_neg(speed),3) : pm_neg(pm_sar(speed,3));
    pm_write32(m,o+0xee,rate);
    if (pm_i32(m,tank+0xec)<0) { pm_write32(m,o+0xee,pm_neg(rate));pm_store(m,o+0xea,pm_load(m,o+0xea,4)|8u,4); }
    if (!pm_i32(m,o+0x80)) {
        if (pm_load(m,o+0x84,4)) { if (m->error) return m->error;status=operation(e,PHYSICS_SCENE_OBJECT_SOUND,o,id,0);if (status) return status; }
        pm_write32(m,o+0x80,1);if (m->error) return m->error;
        status=operation(e,PHYSICS_SCENE_ENTITY_ACTIVATE,o,id,0);if (status) return status;
    }
    flags=pm_load(m,o+0xea,4)|2u;pm_store(m,o+0xea,flags,4);pm_write32(m,o+0xf2,0);pm_store(m,o+0xda,argument,4);
    rate=pm_trunc(pm_abs(pm_i32(m,tank+0xec)),3);pm_write32(m,o+0xe2,rate);cap=pm_i32(m,0x52ec94);
    if (rate<cap) pm_write32(m,o+0xe2,cap);else { cap=pm_i32(m,0x52ec98);if (rate>cap) pm_write32(m,o+0xe2,cap); }
    pm_store(m,o+0xea,flags&UINT32_C(0xffffffef),4);
    if (pm_load(m,o+0x88,4)) { if (m->error) return m->error;status=dispatch(e,id,pm_load(m,0x545ee4,4),argument);if (status) return status;pm_write32(m,o+0x80,0); }
    if (pm_load(m,o+0x8c,4)) {
        if (m->error) return m->error;
        status=dispatch(e,id,pm_load(m,0x545eec,4),argument);if (status) return status;
        if (!(pm_load(m,pm_load(m,o+0x8c,4)+0x18,1)&16u)) pm_write32(m,o+0x80,0);
        if (m->error) return m->error;
        status=operation(e,PHYSICS_SCENE_REMOVE_GRID,o,id,pm_i32(m,o+0x90));if (status) return status;
    }
    sentinel=pm_load(m,o+0xfa,4);node=sentinel ? pm_load(m,sentinel,4) : 0;count=0;
    while (sentinel && node!=sentinel) {
        uint32_t data=pm_load(m,node+8,4),target=pm_load(m,data+4,4);
        if (m->error) return m->error;
        if (count++==65536u) return -3;
        if (target && object(m,target)) { status=dispatch(e,target,pm_load(m,0x545ec4,4),argument);if (status) return status; }
        node=pm_load(m,node,4);
    }
    if (pm_load(m,tank+0xde5,1) && pm_load(m,0x58e7cc,4) && pm_load(m,0x58e7ec,4) && !pm_load(m,0x53e928,4)) {
        if (m->error) return m->error;
        status=operation(e,PHYSICS_SCENE_FORCE_FEEDBACK,o,id,(int32_t)pm_load(m,pm_load(m,o+0x48,4)+0xc,2));if (status) return status;
    }
    return m->error;
}
int physics_known_entity_method(void *ctx,uint32_t fn,uint32_t entity,uint32_t argument,uint32_t *result)
{
    physics_entity_environment *e=(physics_entity_environment *)ctx;int status;
    if (!e) return -6;
    if (fn==0x408170u) return frame_deformable_hit(e->read,e->write,e->ctx,entity,argument,result);
    if (fn!=UINT32_C(0x40a3e0)) return -6;
    status=obstacle_hit(e,entity,argument);*result=0;return status;
}
