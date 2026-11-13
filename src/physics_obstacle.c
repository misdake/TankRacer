#include "physics_obstacle.h"
#include "physics_objects.h"
#include "physics_memory.h"

static void vector_read(physics_memory *m,uint32_t a,int32_t *v,unsigned n)
{ unsigned i;for (i=0;i<n;++i) v[i]=pm_i32(m,a+i*4u); }
static uint32_t entity_class(physics_memory *m,uint32_t id)
{
    uint32_t link=pm_load(m,pm_load(m,0x55c694,4)+id*8u,4);
    return link ? pm_load(m,link,4) : 0;
}
int physics_entity_collision_dispatch(physics_world_read read,physics_world_write write,void *ctx,
    uint32_t entity,uint32_t interface_address,uint32_t argument,const physics_obstacle_services *services,uint32_t *result)
{
    physics_memory memory={read,write,ctx,0},*m=&memory;uint32_t old=pm_load(m,0x55c698,4),slot=pm_load(m,interface_address+0x1c,4),class_address,method;int status;
    pm_store(m,0x55c698,slot,4);class_address=entity_class(m,entity);
    if (m->error) return m->error;
    *result=0;
    /* An out-of-range slot leaks the new global slot in the original. */
    if (pm_signed(slot)>pm_i32(m,class_address+0x1c)) return m->error;
    method=pm_load(m,pm_load(m,class_address+0x24,4)+slot*4u,4);
    if (m->error) return m->error;
    if (method) {
        if (!services || !services->method) return -6;
        status=services->method(services->ctx,method,entity,argument,result);if (status) return status<0 ? status : -6;
    }
    pm_store(m,0x55c698,old,4);return m->error;
}
static void query_segment(physics_memory *m,uint32_t o,int32_t start[3],int32_t end[3])
{
    unsigned i;vector_read(m,o+0x78,start,3);
    for (i=0;i<3;++i) end[i]=pm_sub(pm_mul(start[i],2),pm_i32(m,o+0x84+i*4u));
}
static int respond(physics_memory *m,uint32_t o,int32_t normal[3],const physics_trig *trig)
{
    physics_collision_scale local,final;physics_collision_response response;int32_t velocity[3];unsigned i;int status;
    local.multiplier=(uint16_t)pm_load(m,0x531b98,2);local.shift=(uint8_t)pm_load(m,0x531b9a,1);
    final.multiplier=(uint16_t)pm_load(m,0x531b94,2);final.shift=(uint8_t)pm_load(m,0x531b96,1);
    vector_read(m,o+0x50,velocity,3);if (m->error) return m->error;
    status=physics_collision_respond(normal,velocity,&local,&final,pm_i32(m,0x52ee9c),0,trig,&response);if (status) return status;
    for (i=0;i<3;++i) { normal[i]=response.normal[i];pm_write32(m,o+0x50+i*4u,response.velocity[i]); }
    return m->error;
}
static void blend_velocity(physics_memory *m,uint32_t o,uint32_t other,const int32_t old[3])
{
    unsigned i;for (i=0;i<3;++i) {
        int32_t weight=pm_i32(m,other+0x5c),value=pm_add(pm_mul(weight,pm_i32(m,o+0x50+i*4u)),pm_mul(pm_sub(128,weight),old[i]));
        pm_write32(m,o+0x50+i*4u,pm_trunc(value,7));
    }
}
int physics_obstacle_collision_step(physics_world_read read,physics_world_write write,void *ctx,
    uint32_t o,uint32_t entity,const physics_trig *trig,const physics_obstacle_services *services)
{
    physics_memory memory={read,write,ctx,0},*m=&memory;int32_t start[3],end[3],old[3],threshold;physics_object_hit hit={0};int status;uint32_t result;
    query_segment(m,o,start,end);if (m->error) return m->error;
    status=physics_obstacle_query(read,ctx,entity,start,end,&hit);if (status<=0) return status;
    vector_read(m,o+0x50,old,3);threshold=pm_i32(m,hit.object+0x94);if (m->error) return m->error;
    status=respond(m,o,hit.normal,trig);if (status) return status;
    if (threshold && physics_approximate_distance(old)<threshold) return m->error;
    blend_velocity(m,o,hit.object,old);
    if (entity_class(m,entity)==pm_load(m,0x544f98,4) && pm_i32(m,hit.object+0x5c)!=128 && physics_approximate_distance(old)>200000) {
        if (entity_class(m,hit.entity)!=pm_load(m,0x53d13c,4) || !pm_i32(m,hit.object+0x80) || !pm_i32(m,hit.object+0xea)) {
            pm_write32(m,o+0x118,pm_add(pm_i32(m,o+0x118),70));
            pm_store(m,o+0x107e,pm_load(m,o+0x107e,4)+(pm_load(m,pm_load(m,hit.object+0x48,4)+0xc,2)>>5),4);
        }
    }
    if (m->error) return m->error;
    return physics_entity_collision_dispatch(read,write,ctx,hit.entity,pm_load(m,0x545ec4,4),entity,services,&result);
}
static int inside_deformable(physics_memory *m,uint32_t other,uint32_t point)
{
    int32_t v[3],matrix[9],local[3];unsigned i,j;uint32_t mesh=pm_load(m,other+0x48,4);
    for (i=0;i<3;++i) v[i]=pm_sub(pm_i32(m,point+i*4u),pm_i32(m,other+i*4u));
    vector_read(m,other+0x18,matrix,9);
    for (i=0;i<3;++i) { int32_t sum=0;for (j=0;j<3;++j) sum=pm_add(sum,pm_mul(v[j],matrix[i*3u+j]));local[i]=pm_sar(sum,12); }
    return local[0]>pm_mul(pm_i16(m,mesh+0x1c),8) && local[2]>pm_mul(pm_i16(m,mesh+0x20),8) &&
        local[0]<pm_mul(pm_i16(m,mesh+0x22),8) && local[2]<pm_mul(pm_i16(m,mesh+0x26),8);
}
int physics_deformable_collision_step(physics_world_read read,physics_world_write write,void *ctx,
    uint32_t o,uint32_t entity,const physics_trig *trig,const physics_obstacle_services *services)
{
    physics_memory memory={read,write,ctx,0},*m=&memory;int32_t start[3],end[3],old[3];physics_object_hit hit={0};int status;unsigned i;uint32_t result;
    query_segment(m,o,start,end);pm_store(m,o+0xd95,0,4);if (m->error) return m->error;
    status=physics_deformable_query(read,ctx,start,end,&hit);if (status<0) return status;
    pm_store(m,o+0xd8c,status ? hit.entity : 0u,4);if (m->error || !status) return m->error;
    vector_read(m,o+0x50,old,3);
    if (pm_load(m,o+0xde5,1) && pm_load(m,0x58e7cc,4) && pm_load(m,0x58e7ec,4) && !pm_load(m,0x53e928,4)) {
        if (!services || !services->feedback) return -7;
        status=services->feedback(services->ctx,10);if (status) return status<0 ? status : -7;
    }
    if (m->error) return m->error;
    status=respond(m,o,hit.normal,trig);if (status) return status;
    blend_velocity(m,o,hit.object,old);if (m->error) return m->error;
    status=physics_entity_collision_dispatch(read,write,ctx,hit.entity,pm_load(m,0x545f04,4),entity,services,&result);if (status) return status;
    for (i=0;i<4;++i) {
        int inside=inside_deformable(m,hit.object,o+0x128+i*70u);int32_t velocity[3];
        if (m->error) return m->error;
        vector_read(m,o+0x50,velocity,3);
        if (inside && pm_load(m,o+0xde5,1) && physics_approximate_distance(velocity)>200000 && pm_i32(m,0x52ee9c)!=2 && pm_i32(m,0x52ee9c)!=3) {
            uint32_t random=pm_load(m,0x55b6ac,4)*UINT32_C(0x19660d)+UINT32_C(0x3c6ef35f);pm_store(m,0x55b6ac,random,4);
            if (!(random&0x30u)) {
                const physics_collision_services *collision=services ? services->collision : 0;
                uint32_t first=o+(pm_i32(m,o+0xec)<0 ? 0x11cu : 0x162u);
                if (!collision || !collision->impact) return -5;
                status=collision->impact(collision->ctx,o,first,5);if (status) return status<0 ? status : -5;
                status=collision->impact(collision->ctx,o,first+140u,6);if (status) return status<0 ? status : -5;
                pm_write32(m,o+0x118,pm_add(pm_i32(m,o+0x118),pm_i32(m,0x5904fc)));
            }
        }
        if (m->error) return m->error;
    }
    return m->error;
}
