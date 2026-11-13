#include "frame_mover.h"
#include "frame_camera.h"
#include "frame_lap_events.h"
#include "frame_objects.h"
#include "frame_spatial.h"
#include "frame_text.h"
#include "physics_memory.h"
static uint32_t object_of(physics_memory *m,uint32_t id)
{ uint32_t r=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);return r ? pm_load(m,r+4u,4) : 0; }
#define TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
static int send(physics_memory *m,uint32_t id,uint32_t global,uint32_t has,uint32_t payload,const frame_entity_factory_services *e)
{ uint32_t unused,d=pm_load(m,global,4);if (m->error) return m->error;return frame_entity_dispatch(m->read,m->write,m->ctx,id,d,has,payload,e->method,e->method_ctx,&unused); }
static int find(physics_memory *m,uint32_t parser,const unsigned char *name,uint32_t n,uint32_t limit,uint32_t *found)
{
    unsigned char alias[32];uint32_t i;int status;
    if (n>sizeof(alias)) return -3;
    status=frame_text_find_name(m->read,m->write,m->ctx,parser,name,n,limit,found);if (status || *found) return status;
    for (i=0;i<n;i++) alias[i]=name[i];
    if (n>1u && alias[1]>='a' && alias[1]<='z') alias[1]-=32;
    return frame_text_find_name(m->read,m->write,m->ctx,parser,alias,n,limit,found);
}
static int velocity(physics_memory *m,uint32_t o)
{
    uint32_t matrix=pm_load(m,o+20u,4),i;int32_t speed=pm_i32(m,o+0xfeu);
    /*442E40 uses matrix columns and SAR, and copies only if matrix exists. */
    if (matrix) for (i=0;i<3u;i++) pm_write32(m,o+0x50u+i*4u,pm_sar(pm_mul(speed,pm_i32(m,matrix+24u+i*4u)),12));
    return m->error;
}
static int append_unique(physics_memory *m,uint32_t id,uint32_t head,const frame_entity_factory_services *e)
{ uint32_t found,unused;int status=frame_list_contains(m->read,m->ctx,id,head,e->work_limit,&found);if (status || found) return status;return frame_list_append(m->read,m->write,m->ctx,id,head,e->allocate,e->release,e->system_ctx,e->work_limit,&unused); }
int frame_mover_initialize(frame_data_read read,frame_data_write write,void *ctx,uint32_t id,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o,parent,unused;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,id);parent=pm_load(&m,0x545e20u,4);TRY(frame_class_dispatch(read,ctx,parent,id,e->method,e->method_ctx,&unused));
    pm_store(&m,o+0xf6u,10240,4);pm_store(&m,o+0xfeu,1310,4);pm_store(&m,o+0xf2u,pm_load(&m,o+0xf2u,4)|60u,4);pm_store(&m,o+0x102u,51200,4);return m.error;
}
int frame_mover_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t id,uint32_t parser,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o,parent,unused,found,more,i,j,base,count,v,flags,equal,a,b;int status;unsigned char name[32],key[32];
    static const struct { const char *key;unsigned n,offset,width; } fields[]={
        {":elasticity",12,0xb2,2},{":friction",10,0xb4,1},{":threshold",11,0x7c,4},{":solidity",10,0x5c,4},
        {":dist",6,0xf6,4},{":speed",7,0xfe,4},{":move_range",12,0x102,4}};
    static const struct { const char *key;unsigned n,mask; } bools[]={
        {":snap_to_track",15,8},{":collidable",12,4},{":oscillating",13,16},{":flip_180",10,32},{":explode",9,128}};
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,id);parent=pm_load(&m,0x545e20u,4);TRY(frame_class_dispatch_payload(read,ctx,parent,id,parser,e->method,e->method_ctx,&unused));
    TRY(find(&m,parser,(const unsigned char *)":type",6,e->work_limit,&found));
    if (found) {
        base=pm_load(&m,0x544f84u,4);count=pm_load(&m,0x544f88u,4);TRY(frame_text_next(read,write,ctx,parser,name,32,e->work_limit,&more));
        for (i=0;pm_signed(i)<pm_signed(count);i++) {
            if (i>=e->work_limit) return -3;
            equal=0;for (j=0;j<32u;j++) { a=name[j];b=pm_load(&m,base+i*76u+j,1);if (a>=128 || b>=128) return -5;if (a>=65 && a<=90) a+=32;if (b>=65 && b<=90) b+=32;if (a!=b) break;if (!a) { equal=1;break; } }
            if (equal) {
                a=base+i*76u;pm_store(&m,o+0xb2u,pm_load(&m,a+0x1au,2),2);pm_store(&m,o+0xb4u,pm_load(&m,a+0x19u,1),1);
                pm_write32(&m,o+0x7cu,pm_i8(&m,a+0x1du));pm_write32(&m,o+0x5cu,pm_i16(&m,a+0x20u));
                for (j=0;j<4u;j++) { v=(uint32_t[]){8,4,16,32}[j];flags=pm_load(&m,o+0xf2u,4);pm_store(&m,o+0xf2u,flags^((flags^pm_load(&m,a+0x3au,4))&v),4); }break;
            }
        }
    }
    for (i=0;i<7u;i++) {
        TRY(find(&m,parser,(const unsigned char *)fields[i].key,fields[i].n,e->work_limit,&found));if (!found) continue;
        if (fields[i].width==4) TRY(frame_text_read_i32(read,write,ctx,parser,o+fields[i].offset,e->work_limit,&more));
        else { v=parser;TRY(frame_text_read_value(read,write,ctx,parser,e->work_limit,&v,&more));pm_store(&m,o+fields[i].offset,v,fields[i].width); }
        if (i==4 || i==6) pm_store(&m,o+fields[i].offset,pm_load(&m,o+fields[i].offset,4)<<10,4);
        if (i==5) pm_write32(&m,o+0xfeu,pm_signed(pm_load(&m,o+0xfeu,4)<<18)/1000);
    }
    for (i=0;i<5u;i++) {
        TRY(find(&m,parser,(const unsigned char *)bools[i].key,bools[i].n,e->work_limit,&found));equal=0;
        if (found) { TRY(frame_text_next(read,write,ctx,parser,key,32,e->work_limit,&more));equal=(key[0]=='t' || key[0]=='T') && (key[1]=='r' || key[1]=='R') && (key[2]=='u' || key[2]=='U') && (key[3]=='e' || key[3]=='E') && !key[4]; }
        flags=pm_load(&m,o+0xf2u,4);pm_store(&m,o+0xf2u,equal ? flags|bools[i].mask : flags&~bools[i].mask,4);
    }
    TRY(frame_text_find_name(read,write,ctx,parser,(const unsigned char *)":lights",8,e->work_limit,&found));
    if (found) { TRY(frame_list_remove_value(read,write,ctx,id,pm_load(&m,0x54b48cu,4),e->release,e->system_ctx,e->work_limit));TRY(append_unique(&m,id,pm_load(&m,0x54b488u,4),e)); }
    TRY(frame_text_find_name(read,write,ctx,parser,(const unsigned char *)":beep",6,e->work_limit,&found));
    if (found) { TRY(frame_text_next(read,write,ctx,parser,name,32,e->work_limit,&more));pm_store(&m,o+0x106u,0,4); }
    TRY(velocity(&m,o));TRY(frame_entity_cancel_notification(read,write,ctx,id,e->release,e->system_ctx,e->work_limit));pm_store(&m,o+0x80u,1,4);return m.error;
}
int frame_mover_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t id,const frame_entity_factory_services *e,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t o,flags,aux,parent,unused,timer,near,i,matrix,p;int32_t dt,travel,range,a,b,n[3],local[3],sum,angle[2],ground;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,id);flags=pm_load(&m,o+0xf2u,4);
    if (!pm_load(&m,o+0x80u,4)) { aux=pm_load(&m,o+0x8cu,4);if ((flags&64u) && aux && (pm_load(&m,aux+24u,1)&1u)) TRY(send(&m,id,0x545ef0u,0,0,e));return m.error; }
    if (flags&64u) { parent=pm_load(&m,0x545e20u,4);TRY(frame_class_dispatch(read,ctx,parent,id,e->method,e->method_ctx,&unused));return m.error; }
    if (flags&2u) return m.error;
    timer=pm_load(&m,o+0xb5u,1);
    if (!(flags&1u)) {
        pm_store(&m,o+0xb5u,timer+1u,1);if (timer%5u) return m.error;
        for (i=0;i<2u;i++) { p=object_of(&m,pm_load(&m,0x53e8b8u+i*4u,4));range=10240000;if (p) { a=pm_abs(pm_sub(pm_i32(&m,p),pm_i32(&m,o)));b=pm_abs(pm_sub(pm_i32(&m,p+8u),pm_i32(&m,o+8u)));range=b<=a ? pm_add(a,pm_sub(b,pm_sar(b,1))) : pm_add(b,pm_sub(a,pm_sar(a,1))); }if (range<pm_i32(&m,o+0x102u)) near=1;else continue;break; }
        if (i<2u) pm_store(&m,o+0xf2u,flags|257u,4);
        return m.error;
    }
    TRY(frame_entity_near_camera(read,ctx,o,0,12800,&near));if (!near) { pm_store(&m,o+0xf2u,flags&~256u,4);return m.error; }
    pm_store(&m,o+0xf2u,flags|256u,4);dt=pm_i32(&m,0x5904fcu);
    pm_write32(&m,o,pm_add(pm_i32(&m,o),pm_sar(pm_mul(dt,pm_i32(&m,o+0x50u)),8)));pm_write32(&m,o+8u,pm_add(pm_i32(&m,o+8u),pm_sar(pm_mul(dt,pm_i32(&m,o+0x58u)),8)));
    if (flags&8u) {
        pm_store(&m,o+0xb5u,timer+1u,1);
        if (!(timer%3u)) {
            status=physics_terrain_normal_height(read,ctx,pm_load(&m,0x55ae60u,4),pm_i32(&m,o),pm_i32(&m,o+8u),&ground,n);if (status<0) return status;
            if (ground!=-2147483647) {
                for (i=0;i<3u;i++) { pm_write32(&m,o+0xc2u+i*4u,n[i]); }pm_write32(&m,o+4u,ground);matrix=pm_load(&m,o+20u,4);if (!matrix || !trig) return -4;
                for (i=0;i<3u;i++) { sum=pm_add(pm_add(pm_mul(n[0],pm_i32(&m,matrix+i*12u)),pm_mul(n[1],pm_i32(&m,matrix+i*12u+4u))),pm_mul(n[2],pm_i32(&m,matrix+i*12u+8u)));local[i]=pm_sar(sum,12); }
                TRY(physics_angle_lookup(local[1],local[2],trig,&angle[0]));TRY(physics_angle_lookup(pm_neg(local[0]),local[1],trig,&angle[1]));
                TRY(frame_matrix_rotate(read,write,ctx,matrix,0,(uint32_t)pm_sar(pm_mul(pm_sub(angle[0],16384),pm_i16(&m,o+0xecu)),pm_load(&m,o+0xeeu,1)),trig));
                TRY(frame_matrix_rotate(read,write,ctx,matrix,2,(uint32_t)pm_sar(pm_mul(pm_neg(angle[1]),pm_i16(&m,o+0xecu)),pm_load(&m,o+0xeeu,1)),trig));
            }
        }
    }
    travel=pm_add(pm_i32(&m,o+0xfau),pm_sar(pm_mul(dt,pm_i32(&m,o+0xfeu)),8));pm_write32(&m,o+0xfau,travel);range=pm_i32(&m,o+0xf6u);
    if (pm_abs(travel)>range) {
        flags=pm_load(&m,o+0xf2u,4);
        if (!(flags&16u)) { flags&=~1u;pm_store(&m,o+0xf2u,flags,4);if (!(flags&2u)) { pm_store(&m,o+0xf2u,flags|2u,4);pm_store(&m,o+0x80u,0,4);TRY(frame_entity_queue(read,write,ctx,id,0,e->allocate,e->release,e->system_ctx,e->work_limit)); } }
        else { pm_write32(&m,o+0xfau,travel>0 ? pm_sub(travel,range) : travel<0 ? pm_add(travel,range) : 0);if (flags&32u) TRY(frame_matrix_rotate(read,write,ctx,pm_load(&m,o+20u,4),1,32760,trig));else pm_write32(&m,o+0xfeu,pm_neg(pm_i32(&m,o+0xfeu)));TRY(velocity(&m,o)); }
    }
    /*106/10A are disabled audio resources, no gameplay or RNG in driver calls. */
    return m.error;
}
int frame_mover_hit(frame_data_read read,frame_data_write write,void *ctx,uint32_t id,uint32_t vehicle,const frame_entity_factory_services *e,const frame_explosion_services *effects,frame_effect_sink feedback,void *feedback_ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t o,tank,flags,parent,unused,head;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,id);flags=pm_load(&m,o+0xf2u,4);if (!(flags&4u)) return m.error;
    if (!(flags&128u)) { parent=pm_load(&m,0x545e20u,4);TRY(frame_class_dispatch_payload(read,ctx,parent,id,vehicle,e->method,e->method_ctx,&unused));pm_store(&m,o+0xf2u,pm_load(&m,o+0xf2u,4)|64u,4); }
    else {
        TRY(frame_explosion_spawn(read,write,ctx,o,0,0,e,effects,&unused));
        /*40F720 is solely disabled positional audio, with no RNG. */
        if (!pm_load(&m,o+0x8cu,4)) { TRY(send(&m,id,0x545ed0u,0,0,e)); }
        else { TRY(send(&m,id,0x545eecu,1,vehicle,e));pm_store(&m,o+0x80u,0,4);TRY(frame_spatial_remove(read,write,ctx,id,pm_load(&m,o+0x90u,4),e->release,e->system_ctx,e->work_limit));pm_store(&m,o+0xf2u,pm_load(&m,o+0xf2u,4)|64u,4);head=pm_load(&m,0x54b48cu,4);TRY(frame_list_remove_value(read,write,ctx,id,head,e->release,e->system_ctx,e->work_limit));head=pm_load(&m,0x54b488u,4);TRY(append_unique(&m,id,head,e)); }
    }
    tank=object_of(&m,vehicle);if (pm_load(&m,tank+0xde5u,1)) { head=pm_load(&m,o+0x48u,4);TRY(frame_feedback_impact(read,write,ctx,pm_load(&m,head+12u,2),feedback,feedback_ctx)); }return m.error;
}
#undef TRY
