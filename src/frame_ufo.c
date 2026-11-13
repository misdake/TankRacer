#include "frame_ufo.h"
#include "frame_weapon_spin.h"
#include "frame_overlay.h"
#include "physics_memory.h"
static uint32_t object_of(physics_memory *m,uint32_t id)
{ uint32_t r=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);return r ? pm_load(m,r+4u,4) : 0; }
static uint32_t class_of(physics_memory *m,uint32_t id)
{ uint32_t r=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);return r ? pm_load(m,r,4) : 0; }
#define TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
int frame_ufo_initialize(frame_data_read read,frame_data_write write,void *ctx)
{ physics_memory m={read,write,ctx,0};if (!read || !write) return -1;pm_store(&m,0x546720u,0,4);pm_store(&m,0x546724u,0,4);return m.error; }
int frame_ufo_reset(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity)
{ physics_memory m={read,write,ctx,0};uint32_t o;if (!read || !write) return -1;o=object_of(&m,entity);if (o) { pm_store(&m,o+0xcau,0,2);pm_store(&m,o+0x78u,(pm_load(&m,o+0x78u,4)&~8u)|1u,4); }return m.error; }
int frame_ufo_player_request(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o,id;int status;
    if (!read || !write || !e) return -1;
    o=object_of(&m,entity);if (!o) return m.error;id=pm_load(&m,o+0x10d6u,4);
    if (!id) { TRY(frame_ufo_spawn_values(read,write,ctx,entity,0,e,&id));pm_store(&m,o+0x10d6u,id,4); }
    else { pm_store(&m,o+0x10efu,1,1);pm_store(&m,o+0x10ebu,pm_load(&m,0x59054cu,4),4);TRY(frame_ufo_reset(read,write,ctx,id)); }
    return m.error;
}
int frame_ufo_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t parent,shape,unused;int status;
    if (!read || !write || !e) return -1;
    parent=pm_load(&m,0x5466e4u,4);TRY(frame_class_dispatch(read,ctx,parent,entity,e->method,e->method_ctx,&unused));
    if (!pm_load(&m,0x546738u,4)) { shape=pm_load(&m,0x546730u,4);pm_write32(&m,0x546738u,pm_mul(pm_sub(pm_i16(&m,shape+36u),pm_i16(&m,shape+30u)),8)); }return m.error;
}
int frame_ufo_stop(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o,parent,owner,tank,unused;int status;
    if (!read || !write || !e) return -1;
    o=object_of(&m,entity);parent=pm_load(&m,0x5466e4u,4);TRY(frame_class_dispatch(read,ctx,parent,entity,e->method,e->method_ctx,&unused));
    if (pm_load(&m,0x58e7acu,4)) { owner=pm_load(&m,o+0xc6u,4);tank=object_of(&m,owner);if (tank && pm_load(&m,tank+0x10d6u,4)==entity && owner==pm_load(&m,0x53e8b8u,4)) pm_store(&m,tank+0x10d6u,0,4); }return m.error;
}
static int spawn(frame_data_read read,frame_data_write write,void *ctx,uint32_t vehicle,uint32_t offset,const int32_t *values,const frame_entity_factory_services *e,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t tank,klass,entity,o,unused,i,head;int status;
    if (!read || !write || !e || !e->work_limit || !out) return -1;
    tank=object_of(&m,vehicle);klass=pm_load(&m,0x54672cu,4);TRY(frame_entity_create(read,write,ctx,klass,e,&entity));o=object_of(&m,entity);
    if (o) {
        pm_store(&m,o+0x78u,pm_load(&m,o+0x78u,4)|1u,4);pm_store(&m,o+0xc6u,vehicle,4);pm_store(&m,o+72u,pm_load(&m,0x546730u,4),4);TRY(frame_entity_activate(read,write,ctx,entity,e->method,e->method_ctx,&unused));
        head=pm_load(&m,0x54b488u,4);TRY(frame_list_append(read,write,ctx,entity,head,e->allocate,e->release,e->system_ctx,e->work_limit,&unused));pm_store(&m,o+76u,0x5800,2);
        pm_store(&m,o+0x9cu,pm_load(&m,tank,4),4);pm_write32(&m,o+0xa0u,pm_add(pm_i32(&m,tank+4u),102400));pm_store(&m,o+0xa4u,pm_load(&m,tank+8u,4),4);pm_store(&m,o+0x98u,pm_load(&m,0x546734u,4),4);
        pm_store(&m,o+0xbau,63,1);pm_store(&m,o+0xbbu,63,1);pm_store(&m,o+0xa8u,UINT32_MAX,4);pm_store(&m,o+0xb0u,7000,4);pm_store(&m,o+0xb4u,7000,4);
        pm_store(&m,o+0xb8u,0,1);pm_store(&m,o+0xb9u,0,1);pm_store(&m,o+0xbcu,128,1);pm_store(&m,o+0xbdu,128,1);pm_store(&m,o+0xbeu,128,1);pm_store(&m,o+0xacu,0xffffffecu,4);
        if (offset || values) for (i=0;i<3u;i++) pm_store(&m,o+0x88u+i*4u,values ? (uint32_t)values[i] : pm_load(&m,offset+i*4u,4),4);
        if (pm_load(&m,0x52ee9cu,4)==4u) { pm_store(&m,o+0xd5u,0x4409a0,4);pm_store(&m,tank+0x10cau,entity,4); }else pm_store(&m,o+0xd5u,0x4402d0,4);
        if (vehicle==pm_load(&m,0x53e8b8u,4) || vehicle==pm_load(&m,0x53e8bcu,4)) { pm_store(&m,o+0xd0u,vehicle==pm_load(&m,0x53e8b8u,4) ? 0u : 1u,1);pm_store(&m,o+0x78u,pm_load(&m,o+0x78u,4)|32u,4); }
        else if (class_of(&m,vehicle)==pm_load(&m,0x53b698u,4)) { pm_store(&m,tank+0x1137u,0,4);pm_store(&m,tank+0x10c6u,pm_load(&m,tank+0x10c6u,4)+2500u,4); }
    }
    if (m.error) { return m.error; }*out=entity;return 0;
}
int frame_ufo_spawn(frame_data_read read,frame_data_write write,void *ctx,uint32_t vehicle,uint32_t offset,const frame_entity_factory_services *e,uint32_t *out)
{ return spawn(read,write,ctx,vehicle,offset,0,e,out); }
int frame_ufo_spawn_values(frame_data_read read,frame_data_write write,void *ctx,uint32_t vehicle,const int32_t offset[3],const frame_entity_factory_services *e,uint32_t *out)
{ return spawn(read,write,ctx,vehicle,0,offset,e,out); }
int frame_ufo_overlaps(frame_data_read read,void *ctx,uint32_t vehicle,uint32_t *out)
{
    physics_memory m={read,0,ctx,0};uint32_t o,target,i,id,radius,other;int32_t limit;
    if (!read || !out) return -1;
    o=object_of(&m,vehicle);radius=pm_load(&m,pm_load(&m,o+72u,4)+12u,2);
    for (i=0;i<9u;i++) { id=pm_load(&m,0x53e8e0u+i*4u,4);if (id==vehicle) continue;target=object_of(&m,id);if (!target) continue;other=pm_load(&m,pm_load(&m,target+72u,4)+12u,2);limit=(int32_t)((radius+other)*8u);if (pm_abs(pm_sub(pm_i32(&m,o),pm_i32(&m,target)))<limit && pm_abs(pm_sub(pm_i32(&m,o+8u),pm_i32(&m,target+8u)))<limit) { if (m.error) return m.error;*out=1;return 0; } }
    if (m.error) { return m.error; }*out=0;return 0;
}
static int dispatch(physics_memory *m,const frame_entity_factory_services *e,uint32_t entity,uint32_t global,uint32_t has_payload,uint32_t payload,uint32_t *out)
{ uint32_t d=pm_load(m,global,4);if (m->error) return m->error;return frame_entity_dispatch(m->read,m->write,m->ctx,entity,d,has_payload,payload,e->method,e->method_ctx,out); }
static int append(physics_memory *m,const frame_entity_factory_services *e,uint32_t value,uint32_t global)
{ uint32_t head=pm_load(m,global,4),unused;if (m->error) return m->error;return frame_list_append(m->read,m->write,m->ctx,value,head,e->allocate,e->release,e->system_ctx,e->work_limit,&unused); }
static int remove_value(physics_memory *m,const frame_entity_factory_services *e,uint32_t value,uint32_t global)
{ uint32_t head=pm_load(m,global,4);if (m->error) return m->error;return frame_list_remove_value(m->read,m->write,m->ctx,value,head,e->release,e->system_ctx,e->work_limit); }
static int health(physics_memory *m,uint32_t tank)
{
    int32_t half=pm_sar(pm_i32(m,0x531ba4u),1),value;
    if (pm_i32(m,tank+0x10c2u)<=half) { value=pm_mul(pm_i32(m,tank+0x10c2u),175);if (m->error) return m->error;if (!half || (value==INT32_MIN && half==-1)) return -2;pm_store(m,tank+0x1124u,(uint32_t)(value/half),2); }return m->error;
}
static int overlay(physics_memory *m,uint32_t o,uint32_t enabled)
{ frame_overlay_params p={1,0,255,255,255};p.alpha=(uint32_t)pm_i16(m,o+0xceu);if (m->error) return m->error;return frame_overlay_set(m->read,m->write,m->ctx,enabled,pm_load(m,o+0xd0u,1),&p); }
static int tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e,const physics_trig *trig,uint32_t variant)
{
    physics_memory m={read,write,ctx,0};uint32_t o,owner,tank,flags,unused,klass,i,overlap;int32_t goal,time,x,z,a,b,old_x,old_z,length,numerator;int status;
    if (!read || !write || !e || !e->work_limit || !trig) return -1;
    o=object_of(&m,entity);owner=pm_load(&m,o+0xc6u,4);tank=object_of(&m,owner);flags=pm_load(&m,o+0x78u,4);
    if (flags&1u) {
        pm_store(&m,o+0x9cu,pm_load(&m,tank,4),4);pm_store(&m,o+0xa4u,pm_load(&m,tank+8u,4),4);goal=pm_sub(pm_add(pm_i32(&m,tank+4u),pm_i32(&m,0x546738u)),1500);
        TRY(frame_scalar_approach(read,write,ctx,o+0xa0u,goal,pm_load(&m,0x53276cu,4)));pm_store(&m,o+0xcau,pm_load(&m,o+0xcau,2)+pm_load(&m,0x5904fcu,2),2);
        if (pm_i16(&m,o+0xcau)>800) {
            klass=class_of(&m,pm_load(&m,o+0xc6u,4));time=klass!=pm_load(&m,0x544f98u,4) ? 2500 : 1000;
            if (!(pm_load(&m,o+0x78u,1)&8u)) { TRY(dispatch(&m,e,pm_load(&m,o+0xc6u,4),0x545f50u,1,1,&unused));if (!variant) TRY(dispatch(&m,e,pm_load(&m,o+0xc6u,4),0x545f60u,1,0,&unused));pm_store(&m,o+0x78u,pm_load(&m,o+0x78u,4)|12u,4); }
            if (variant) {
                if (pm_i16(&m,o+0xcau)>2300) { pm_store(&m,o+0x78u,(pm_load(&m,o+0x78u,4)&~5u)|2u,4);TRY(dispatch(&m,e,pm_load(&m,o+0xc6u,4),0x545f50u,1,0,&unused)); }
                if (pm_load(&m,o+0xc6u,4)==pm_load(&m,0x53e8b8u,4) && pm_load(&m,0x53e3fcu,4)) { pm_write32(&m,tank+0x10c2u,pm_sub(pm_i32(&m,tank+0x10c2u),pm_mul(pm_i32(&m,0x5904fcu),4)));if (pm_i32(&m,tank+0x10c2u)<0) pm_store(&m,tank+0x10c2u,0,4);TRY(health(&m,tank)); }
            } else {
                if (pm_i16(&m,o+0xcau)>time && (!(pm_load(&m,o+0x78u,1)&32u) || pm_i16(&m,o+0xceu)==255)) {
                    TRY(dispatch(&m,e,pm_load(&m,o+0xc6u,4),0x545f58u,1,0,&unused));TRY(dispatch(&m,e,pm_load(&m,o+0xc6u,4),0x545f5cu,0,0,&unused));TRY(dispatch(&m,e,pm_load(&m,o+0xc6u,4),0x545f54u,1,o+0x88u,&unused));pm_store(&m,o+0x8cu,unused,4);
                    owner=pm_load(&m,o+0xc6u,4);klass=class_of(&m,owner);if (klass==pm_load(&m,0x544f98u,4) || klass==pm_load(&m,0x53b698u,4)) { pm_store(&m,o+0xd1u,pm_load(&m,object_of(&m,owner)+0x790u,4),4);TRY(append(&m,e,pm_load(&m,o+0xd1u,4),0x546740u)); }
                    pm_store(&m,o+0xcau,0,2);pm_store(&m,o+0x78u,(pm_load(&m,o+0x78u,4)&~5u)|2u,4);TRY(remove_value(&m,e,pm_load(&m,o+0xc6u,4),0x54b488u));
                }
                if ((pm_load(&m,o+0x78u,1)&32u) && pm_i16(&m,o+0xcau)>1400) { pm_store(&m,o+0xceu,pm_load(&m,o+0xceu,2)+pm_load(&m,0x5904fcu,2),2);if (pm_i16(&m,o+0xceu)>255) pm_store(&m,o+0xceu,255,2);if (!pm_load(&m,0x53e944u,4)) TRY(overlay(&m,o,1)); }
            }
        }
    } else if (!variant && (flags&2u)) {
        if (!pm_load(&m,o+0xc2u,4)) {
            if (class_of(&m,owner)==pm_load(&m,0x544f98u,4)) { pm_store(&m,o+0xcau,pm_load(&m,o+0xcau,2)+pm_load(&m,0x5904fcu,2),2);if (pm_i16(&m,o+0xcau)>200) { pm_store(&m,o+0xc2u,1,4);pm_store(&m,o+0x78u,flags|4u,4);pm_store(&m,o+0xcau,0,2); } }
            else {
                x=pm_sub(pm_i32(&m,o+0x9cu),pm_i32(&m,tank));z=pm_sub(pm_i32(&m,o+0xa4u),pm_i32(&m,tank+8u));a=pm_sar(x,2);b=pm_sar(z,2);while (a>3000 || b>3000) { a=pm_sar(a,1);b=pm_sar(b,1); }
                if (a<-1500) { a=-1500; }if (a>1500) { a=1500; }if (b<-1500) { b=-1500; }if (b>1500) { b=1500; }
                x=old_x=pm_sub(x,a);z=old_z=pm_sub(z,b);length=physics_length2_approx(x,z);
                if (length) { numerator=pm_mul(pm_mul(x,pm_i32(&m,0x5904fcu)),-20);if (numerator==INT32_MIN && length==-1) return m.error ? m.error : -2;x=pm_add(x,numerator/length);numerator=pm_mul(pm_mul(z,pm_i32(&m,0x5904fcu)),-20);if (numerator==INT32_MIN && length==-1) return m.error ? m.error : -2;z=pm_add(z,numerator/length); }
                if ((x<0 && old_x>0) || (x>0 && old_x<0)) { x=0; }if ((z<0 && old_z>0) || (z>0 && old_z<0)) z=0;
                pm_write32(&m,o+0x9cu,pm_add(pm_i32(&m,tank),x));pm_write32(&m,o+0xa4u,pm_add(pm_i32(&m,tank+8u),z));goal=pm_sub(pm_add(pm_i32(&m,o+0x8cu),pm_i32(&m,0x546738u)),1500);TRY(frame_scalar_approach(read,write,ctx,o+0xa0u,goal,pm_load(&m,0x53276cu,4)));
                if (!x && !z) { pm_store(&m,o+0xc2u,1,4);pm_store(&m,o+0x78u,pm_load(&m,o+0x78u,4)|4u,4); }
            }
        } else {
            pm_store(&m,o+0x9cu,pm_load(&m,tank,4),4);goal=pm_sub(pm_add(pm_i32(&m,o+0x8cu),pm_i32(&m,0x546738u)),1500);TRY(frame_scalar_approach(read,write,ctx,o+0xa0u,goal,pm_load(&m,0x53276cu,4)));pm_store(&m,o+0xa4u,pm_load(&m,tank+8u,4),4);
            if (!(pm_load(&m,o+0x78u,1)&16u)) { TRY(dispatch(&m,e,pm_load(&m,o+0xc6u,4),0x545f58u,1,1,&unused));pm_store(&m,o+0x78u,pm_load(&m,o+0x78u,4)|16u,4);TRY(append(&m,e,pm_load(&m,o+0xc6u,4),0x54b488u)); }
            pm_store(&m,o+0xcau,pm_load(&m,o+0xcau,2)+pm_load(&m,0x5904fcu,2),2);
            if (pm_i16(&m,o+0xcau)>700 && !pm_i16(&m,o+0xceu)) { TRY(frame_ufo_overlaps(read,ctx,pm_load(&m,o+0xc6u,4),&overlap));if (!overlap) { TRY(dispatch(&m,e,pm_load(&m,o+0xc6u,4),0x545f60u,1,1,&unused));TRY(dispatch(&m,e,pm_load(&m,o+0xc6u,4),0x545f5cu,0,0,&unused));TRY(dispatch(&m,e,pm_load(&m,o+0xc6u,4),0x545f50u,1,0,&unused));pm_store(&m,o+0xccu,0,2);pm_store(&m,o+0x78u,pm_load(&m,o+0x78u,4)&~6u,4); } }
            if ((pm_load(&m,o+0x78u,1)&32u) && pm_i16(&m,o+0xceu)) { pm_store(&m,o+0xceu,pm_load(&m,o+0xceu,2)-pm_load(&m,0x5904fcu,4),2);if (pm_i16(&m,o+0xceu)<0) pm_store(&m,o+0xceu,0,2);if (!pm_load(&m,0x53e944u,4)) TRY(overlay(&m,o,pm_load(&m,o+0xceu,2)!=0)); }
        }
    } else {
        pm_write32(&m,o+0x94u,pm_add(pm_i32(&m,o+0x94u),pm_mul(pm_i32(&m,0x5904fcu),2)));pm_write32(&m,o+0xa0u,pm_add(pm_i32(&m,o+0xa0u),pm_sar(pm_mul(pm_i32(&m,0x5904fcu),pm_i32(&m,o+0x94u)),5)));
        if (variant) { pm_store(&m,o+0x9cu,pm_load(&m,tank,4),4);pm_store(&m,o+0xa4u,pm_load(&m,tank+8u,4),4); }
        if (pm_i32(&m,o+0xa0u)>102400) { if (!variant && pm_load(&m,o+0xd1u,4)) TRY(remove_value(&m,e,pm_load(&m,o+0xd1u,4),0x546740u));TRY(dispatch(&m,e,entity,0x545ed0u,0,0,&unused));TRY(remove_value(&m,e,entity,0x54b488u));if (variant) pm_store(&m,tank+0x10cau,0,4); }
    }
    if (pm_load(&m,o+0x78u,1)&4u) {
        TRY(frame_matrix_rotate(read,write,ctx,pm_load(&m,o+20u,4),1,(uint32_t)pm_sar(pm_mul(pm_i32(&m,0x532768u),pm_i32(&m,0x5904fcu)),8),trig));
        pm_store(&m,o,pm_load(&m,o+0x9cu,4),4);if (!variant) { pm_store(&m,o+8u,pm_load(&m,o+0xa4u,4),4);pm_write32(&m,o+4u,pm_sub(pm_i32(&m,o+0xa0u),pm_i32(&m,0x546738u))); }
        else { pm_write32(&m,o+4u,pm_sub(pm_i32(&m,o+0xa0u),pm_i32(&m,0x546738u)));pm_store(&m,o+8u,pm_load(&m,o+0xa4u,4),4); }
    }
    (void)i;return m.error;
}
int frame_ufo_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t o,phase;if (!read || !write) return -1;o=object_of(&m,entity);phase=pm_load(&m,o+0xd5u,4);if (m.error) return m.error;if (phase!=0x4402d0u && phase!=0x4409a0u) return -5;return tick(read,write,ctx,entity,e,trig,phase==0x4409a0u);
}
