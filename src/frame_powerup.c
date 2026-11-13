#include "frame_powerup.h"
#include "frame_spatial.h"
#include "physics_memory.h"
#include "physics_vector.h"
#include "physics_pose.h"
#include "frame_text.h"
#include "frame_vehicle_scene.h"
#include <string.h>
static uint32_t object_of(physics_memory *m,uint32_t entity)
{ uint32_t r=pm_load(m,pm_load(m,0x55c694u,4)+entity*8u,4);return r ? pm_load(m,r+4u,4) : 0; }
static void copy(physics_memory *m,uint32_t destination,uint32_t source,uint32_t bytes)
{ uint32_t i=0;for (;i+4u<=bytes;i+=4u) pm_store(m,destination+i,pm_load(m,source+i,4),4);for (;i<bytes;i++) pm_store(m,destination+i,pm_load(m,source+i,1),1); }
#define TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
int frame_powerup_load_resources(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *e,const frame_shape_resources *resources)
{
    physics_memory m={read,write,ctx,0};uint32_t i,j,shape,mode,difficulty,at;int status;
    static const struct { uint32_t type,length;const unsigned char *name; } items[]={
        {1,9,(const unsigned char *)"cmine.sm"},{3,10,(const unsigned char *)"chomer.sm"},{4,8,(const unsigned char *)"cufo.sm"},
        {5,9,(const unsigned char *)"cconc.sm"},{7,10,(const unsigned char *)"cturbo.sm"},{8,11,(const unsigned char *)"cshield.sm"},
        {9,10,(const unsigned char *)"cshell.sm"},{2,10,(const unsigned char *)"cemine.sm"},{10,9,(const unsigned char *)"cwarp.sm"}};
    if (!read || !write || !e || !e->work_limit) return -1;
    for (i=0;i<9u;i++) {
        mode=pm_load(&m,0x52ee9cu,4);difficulty=pm_load(&m,0x53d18cu,4);
        if ((i==6u && (mode<2u || mode>5u)) || (i==7u && difficulty!=1u && difficulty!=2u) || (i==8u && difficulty!=2u)) continue;
        TRY(frame_shape_load_name(read,write,ctx,items[i].name,items[i].length,pm_load(&m,0x55ae40u,4),e,resources,&shape));
        at=0x544fc0u+items[i].type*19u;pm_store(&m,at+15u,shape,4);for (j=0;j<items[i].length;j++) pm_store(&m,at+j,items[i].name[j],1);
        if (i==8u) pm_store(&m,0x5450acu,0,1);
    }
    return m.error;
}
int frame_powerup_setup(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t o,a,i,j,name,bytes,angles[3],index;int status;
    static const uint32_t defaults[]={150,8,1,10,5,18};
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,entity);TRY(frame_object_auxiliary_create(read,write,ctx,entity,e));
    for (i=0;i<11u;i++) if (pm_load(&m,o+72u,4)==pm_load(&m,0x544fcfu+i*19u,4)) {
        pm_store(&m,o+0xaeu,i,4);a=pm_load(&m,o+0x88u,4);name=0x544fc0u+i*19u;
        for (bytes=1u;pm_load(&m,name+bytes-1u,1);bytes++) { if (m.error) return m.error;if (bytes>=e->work_limit) return -3; }
        copy(&m,a+4u,name,bytes);break;
    }
    a=pm_load(&m,o+0x88u,4);pm_store(&m,a+44u,75,4);
    a=pm_load(&m,o+0x88u,4);pm_store(&m,a+48u,pm_load(&m,0x530adcu,4),4);
    a=pm_load(&m,o+0x88u,4);pm_store(&m,a+52u,pm_load(&m,0x530ae0u,4),4);
    for (j=0;j<6u;j++) { a=pm_load(&m,o+0x88u,4);pm_store(&m,a+56u+j*2u,defaults[j],2); }
    pm_store(&m,o+0xf6u,0x20800,4);i=(pm_load(&m,0x58fb00u+pm_load(&m,0x53d18cu,4)*4u,4)+130u)*1024u;pm_store(&m,o+0xf6u,i,4);
    if (pm_load(&m,0x52ed90u,4)==7u) pm_store(&m,o+0xf6u,i+pm_load(&m,0x530cb8u,4)*1024u,4);
    pm_store(&m,o+4u,pm_load(&m,o+4u,4)+0x5000u,4);index=pm_load(&m,0x5450acu,4)&255u;
    angles[0]=((pm_load(&m,0x530cc0u+index,1)&3u)+8u)*182u;angles[1]=0;angles[2]=angles[0];
    index=(pm_load(&m,0x5450acu,1)+1u)&255u;pm_store(&m,0x5450acu,index,1);if (index==31u) pm_store(&m,0x5450acu,0,1);
    TRY(frame_matrix_from_angles(read,write,ctx,pm_load(&m,o+0x14u,4),angles,trig));return m.error;
}
int frame_powerup_select(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t object,const frame_entity_factory_services *e,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};if (!read || !write) return -1;
    pm_store(&m,object+72u,pm_load(&m,0x544fcfu+pm_load(&m,object+0xaeu,4)*19u,4),4);
    if (m.error) return m.error;
    return frame_powerup_setup(read,write,ctx,entity,e,trig);
}
int frame_powerup_activate_near(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t target,const frame_entity_factory_services *e,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,head,packed,unused;int32_t radius;int status;
    if (!read || !write || !e || !e->work_limit || !out) return -1;
    o=object_of(&m,entity);if (m.error) return m.error;if (!o || !target) return -4;
    radius=pm_i32(&m,o+0xf6u);
    if (pm_abs(pm_sub(pm_i32(&m,o),pm_i32(&m,target)))<radius && pm_abs(pm_sub(pm_i32(&m,o+8u),pm_i32(&m,target+8u)))<radius) {
        pm_store(&m,o+0xaau,pm_load(&m,o+0xaau,4)|1u,4);head=pm_load(&m,0x54b488u,4);
        TRY(frame_list_append(read,write,ctx,entity,head,e->allocate,e->release,e->system_ctx,e->work_limit,&unused));
        TRY(frame_spatial_insert(read,write,ctx,entity,e->allocate,e->release,e->system_ctx,e->work_limit,&packed));pm_store(&m,o+0x90u,packed,4);*out=1;
    } else *out=0;
    return m.error;
}
int frame_powerup_respawn_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t o,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t target,unused;int32_t delay;
    if (!read || !write || !e || !e->work_limit) return -1;
    if (pm_load(&m,0x58e7acu,4)) return m.error ? m.error : -5;
    if (pm_load(&m,o+0x16fu,4)) return m.error;
    if (pm_load(&m,o+0xb6u,4)!=0xffffffffu && pm_load(&m,o+0xb2u,4)) {
        delay=pm_sub(pm_i32(&m,o+0xb2u),pm_i32(&m,0x5904fcu));pm_write32(&m,o+0xb2u,delay);if (delay>0) return m.error;pm_store(&m,o+0xb2u,0,4);
    }
    target=object_of(&m,pm_load(&m,pm_load(&m,0x53e928u,4) ? 0x590410u : 0x53e8b8u,4));if (m.error) return m.error;
    return frame_powerup_activate_near(read,write,ctx,entity,target,e,&unused);
}
static int transform(physics_memory *m,uint32_t matrix,const int32_t in[3],int32_t out[3],unsigned transpose)
{
    unsigned i,j;if (!matrix) return -4;
    for (i=0;i<3u;i++) { out[i]=0;for (j=0;j<3u;j++) out[i]=pm_add(out[i],pm_mul(in[j],pm_i32(m,matrix+4u*(transpose ? j*3u+i : i*3u+j))));out[i]=pm_sar(out[i],12); }
    return m->error;
}
static int pivot_rotate(physics_memory *m,uint32_t o,const physics_trig *trig)
{
    uint32_t matrix=pm_load(m,o+20u,4),angles[3],i;int32_t pivot[3],before[3],after[3];int status;
    for (i=0;i<3u;i++) pivot[i]=pm_i32(m,0x545098u+i*4u);
    if (m->error) return m->error;
    status=transform(m,matrix,pivot,before,1);if (status) return status;
    for (i=0;i<3u;i++) angles[i]=pm_load(m,o+0x137u+i*4u,4);
    for (i=0;i<3u;i++) { unsigned axis=i==0u ? 1u : i==1u ? 0u : 2u;status=frame_matrix_rotate(m->read,m->write,m->ctx,matrix,axis,angles[axis],trig);if (status) return status; }
    status=transform(m,matrix,pivot,after,1);if (status) return status;
    pm_write32(m,o+4u,pm_add(pm_i32(m,o+4u),pm_sub(before[1],after[1])));
    pm_write32(m,o,pm_add(pm_i32(m,o),pm_sub(before[0],after[0])));
    pm_write32(m,o+8u,pm_add(pm_i32(m,o+8u),pm_sub(before[2],after[2])));return m->error;
}
int frame_powerup_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t o,flags,a,matrix,d,unused,i,previous;int32_t dt,ground,normal[3],local[3],pitch,roll,cap,angle;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,entity);flags=pm_load(&m,o+0xaau,4);a=pm_load(&m,o+0x88u,4);dt=pm_i32(&m,0x5904fcu);
    if (!(flags&1u) && !pm_load(&m,a+34u,2)) { TRY(frame_powerup_respawn_tick(read,write,ctx,entity,o,e)); }
    else if (!(flags&1u)) {
        d=pm_load(&m,0x545ee8u,4);TRY(frame_entity_dispatch(read,write,ctx,entity,d,0,0,e->method,e->method_ctx,&unused));
    } else if (!(flags&8u)) {
        matrix=pm_load(&m,o+20u,4);
        if (!(flags&4u)) {
            ground=pm_add(pm_i32(&m,o+0x14fu),pm_i32(&m,0x5450a4u));
            if (ground<pm_i32(&m,o+4u)) pm_write32(&m,o+4u,pm_add(pm_i32(&m,o+4u),pm_sar(pm_mul(pm_i32(&m,0x530cb0u),dt),8)));
            else {
                pm_write32(&m,o+4u,ground);copy(&m,o+0x107u,o+24u,48);pm_store(&m,o+0xaau,flags|4u,4);copy(&m,o+0xfbu,o,12);
                for (i=0;i<3u;i++) normal[i]=pm_i32(&m,o+0x143u+i*4u);
                if (m.error) return m.error;
                status=physics_terrain_normal_height(read,ctx,pm_load(&m,0x55ae60u,4),pm_i32(&m,o),pm_i32(&m,o+8u),&ground,normal);if (status<0) return status;
                if (status) for (i=0;i<3u;i++) pm_write32(&m,o+0x143u+i*4u,normal[i]);
                pm_write32(&m,o+0x14fu,ground);
            }
            angle=pm_sar(dt,1);pm_write32(&m,o+0x13fu,pm_add(pm_i32(&m,o+0x13fu),pm_i32(&m,o+36u)>0 ? pm_neg(angle) : angle));
            pm_write32(&m,o+0x137u,pm_add(pm_i32(&m,o+0x137u),pm_i32(&m,o+44u)>0 ? angle : pm_neg(angle)));TRY(pivot_rotate(&m,o,trig));
        } else {
            for (i=0;i<3u;i++) normal[i]=pm_i32(&m,o+0x143u+i*4u);TRY(transform(&m,matrix,normal,local,0));
            TRY(physics_angle_lookup(local[1],local[2],trig,&pitch));pitch=pm_sub(pitch,16384);
            TRY(physics_angle_lookup(pm_neg(local[0]),local[1],trig,&roll));roll=pm_neg(roll);cap=pm_mul(dt,16);
            if (pitch) { if (pm_abs(pitch)>cap) pitch=pitch<0 ? pm_neg(cap) : cap;TRY(frame_matrix_rotate(read,write,ctx,matrix,0,(uint32_t)pitch,trig)); }
            if (roll) { if (pm_abs(roll)>cap) roll=roll<0 ? pm_neg(cap) : cap;TRY(frame_matrix_rotate(read,write,ctx,matrix,2,(uint32_t)roll,trig)); }
            pm_write32(&m,o+0xffu,pm_add(pm_i32(&m,o+0xffu),pm_sar(pm_mul(pm_i32(&m,0x530cb0u),dt),10)));
            TRY(frame_matrix_rotate(read,write,ctx,o+0x107u,0,(uint32_t)(pitch>0 ? pm_neg(dt) : dt),trig));
            TRY(frame_matrix_rotate(read,write,ctx,o+0x107u,2,(uint32_t)(roll>0 ? pm_neg(dt) : dt),trig));
            TRY(frame_matrix_rotate(read,write,ctx,matrix,1,(uint32_t)pm_sar(pm_mul(pm_i32(&m,0x530cb4u),dt),8),trig));
        }
    } else {
        cap=(int32_t)(pm_load(&m,o+76u,2)*128u);
        if (pm_abs(pm_sub(pm_sar(pm_i32(&m,o),3),pm_i32(&m,0x74aee0u)))<cap && pm_abs(pm_sub(pm_sar(pm_i32(&m,o+8u),3),pm_i32(&m,0x74aee8u)))<cap) {
            matrix=pm_load(&m,o+20u,4);TRY(frame_matrix_rotate(read,write,ctx,matrix,1,(uint32_t)pm_sar(pm_mul(pm_i32(&m,0x530cb4u),dt),8),trig));
            if (!((entity^pm_load(&m,0x590564u,4))&255u) && matrix) {
                TRY(frame_matrix_normalize(read,write,ctx,matrix));
            }
        }
    }
    flags=pm_load(&m,o+0xaau,4);
    if (!(flags&8u) && (flags&4u)) {
        angle=pm_add(pm_i32(&m,o+0x153u),pm_i32(&m,0x5904fcu));pm_write32(&m,o+0x153u,angle);
        if (angle>30) { pm_store(&m,o+0x153u,0,4);previous=pm_load(&m,o+0x157u,4);flags^=2u;pm_store(&m,o+0xaau,flags,4);pm_store(&m,o+0x157u,previous+1u,4);if (previous==10u) pm_store(&m,o+0xaau,flags|8u,4); }
    }
    return m.error;
}
static int angle_from_float(physics_memory *m,uint32_t parser,uint32_t limit,uint32_t *bits,uint32_t *defined,uint32_t *angle)
{
    uint32_t more;int status;float value;double product;
    status=frame_text_read_f32_value(m->read,m->write,m->ctx,parser,limit,bits,&more);if (status) return status;
    /* An incomplete first float would use undefined caller stack. Reject it.
     * binary32*182 is exact in binary64, matching this x87 multiplication. */
    if (!more && !*defined) return -4;
    if (more) *defined=1;
    memcpy(&value,bits,4);product=(double)value*182.0;
    *angle=(product>=-9223372036854775808.0 && product<9223372036854775808.0) ? (uint32_t)(int64_t)product : 0;return 0;
}
int frame_powerup_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *e,const physics_trig *trig,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,parent,unused,found,more,mode,a,shape,i,head,d,value,angles[3],bits=0,defined=0;int32_t height;int status;
    static const unsigned destination[]={0xba,0xbe,0xc6,0xc2,0xca,0xce,0xd2,0xd6,0xda,0xde,0xe2,0xe6};
    static const unsigned source[]={0,4,24,8,28,32,36,40,44,48,52,56};
    if (!read || !write || !e || !e->work_limit || !out) return -1;
    if (pm_load(&m,0x58e7acu,4)) return m.error ? m.error : -5;
    o=object_of(&m,entity);parent=pm_load(&m,0x544f80u,4);TRY(frame_class_dispatch_payload(read,ctx,parent,entity,parser,e->method,e->method_ctx,&unused));
    TRY(frame_powerup_setup(read,write,ctx,entity,e,trig));TRY(frame_text_find(read,write,ctx,parser,0x530d2cu,e->work_limit,&found));
    if (found) {
        TRY(frame_text_read_i32(read,write,ctx,parser,o+0xf6u,e->work_limit,&more));value=pm_load(&m,o+0xf6u,4)*1024u;pm_store(&m,o+0xf6u,value,4);
        pm_store(&m,o+0xf6u,value+pm_load(&m,0x58fb00u+pm_load(&m,0x53d18cu,4)*4u,4)*1024u,4);
    }
    mode=pm_load(&m,0x52ee9cu,4);
    if (mode==4u || mode==5u) {
        value=pm_load(&m,0x58fb20u+pm_load(&m,o+0xaeu,4)*4u+pm_load(&m,0x53d18cu,4)*44u,4);pm_store(&m,o+0xb6u,value,4);
        a=pm_load(&m,o+0x88u,4);pm_store(&m,a+32u,1,1);for (i=0;i<12u;i++) pm_store(&m,o+destination[i],pm_load(&m,o+source[i],4),4);
    } else pm_store(&m,o+0xb6u,UINT32_MAX,4);
    if (!pm_load(&m,0x54509cu,4)) {
        a=pm_load(&m,0x5450a8u,4);shape=pm_load(&m,o+72u,4);height=pm_sub(pm_add(pm_sub(pm_i16(&m,a+36u),pm_i16(&m,a+30u)),pm_i16(&m,shape+36u)),pm_i16(&m,shape+30u));pm_write32(&m,0x54509cu,pm_mul(height,8));
    }
    if (m.error) return m.error;
    status=physics_terrain_height(read,ctx,pm_load(&m,0x55ae60u,4),pm_i32(&m,o),pm_i32(&m,o+8u),&height);if (status<0) return status;pm_write32(&m,o+0x14fu,height);
    if (!pm_load(&m,0x5450a4u,4)) { shape=pm_load(&m,o+72u,4);pm_store(&m,0x5450a4u,pm_load(&m,shape+12u,2)*2u,4); }
    head=pm_load(&m,0x53e4b4u,4);TRY(frame_list_append(read,write,ctx,entity,head,e->allocate,e->release,e->system_ctx,e->work_limit,&unused));
    value=pm_load(&m,0x530ce0u,1);
    if (value==2u || value==3u) { pm_store(&m,o+120u,value-1u,2);pm_store(&m,o+122u,pm_load(&m,0x530ce0u,1),2);d=pm_load(&m,0x545f48u,4);TRY(frame_entity_dispatch(read,write,ctx,entity,d,0,0,e->method,e->method_ctx,&unused)); }
    else { d=pm_load(&m,0x545f44u,4);TRY(frame_entity_dispatch(read,write,ctx,entity,d,0,0,e->method,e->method_ctx,&unused));pm_store(&m,o+122u,pm_load(&m,0x530ce0u,1),2); }
    pm_store(&m,o+0x84u,0,4); /*40F170 disabled audio-resource service. */
    TRY(frame_text_find(read,write,ctx,parser,0x530d10u,e->work_limit,&found));if (found) pm_store(&m,0x530ce0u,pm_load(&m,0x530ce0u,1)+1u,1);
    TRY(frame_text_find(read,write,ctx,parser,0x530d08u,e->work_limit,&found));
    if (found) { for (i=0;i<3u;i++) TRY(angle_from_float(&m,parser,e->work_limit,&bits,&defined,&angles[i]));TRY(frame_matrix_from_angles(read,write,ctx,pm_load(&m,o+20u,4),angles,trig)); }
    value=pm_load(&m,o+0xaeu,4);
    if ((value==9u && !pm_load(&m,0x5450b8u,4)) || (value==4u && pm_load(&m,0x52ee9cu,4)==5u)) {
        TRY(frame_entity_queue(read,write,ctx,entity,0,e->allocate,e->release,e->system_ctx,e->work_limit));head=pm_load(&m,0x53e4b4u,4);TRY(frame_list_remove_value(read,write,ctx,entity,head,e->release,e->system_ctx,e->work_limit));
        d=pm_load(&m,0x545ed0u,4);TRY(frame_entity_dispatch(read,write,ctx,entity,d,0,0,e->method,e->method_ctx,&unused));
    }
    pm_store(&m,o+76u,150,2);shape=pm_load(&m,o+72u,4);pm_store(&m,shape+14u,150,2);*out=0;return m.error;
}
int frame_powerup_collect(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t vehicle,const frame_entity_factory_services *e,frame_effect_sink feedback,void *feedback_ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t o,tank,type,flags,a,d,unused,i;int status;
    static const unsigned destination[]={0xfb,0xff,0x107,0x103,0x10b,0x10f,0x113,0x117,0x11b,0x11f,0x123,0x127};
    static const unsigned source[]={0,4,24,8,28,32,36,40,44,48,52,56};
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,entity);tank=object_of(&m,vehicle);type=pm_load(&m,o+0xaeu,4);
    if (pm_load(&m,tank+0xd0fu,4)==type) {
        if (type==9u) pm_store(&m,tank+0xd13u,pm_load(&m,0x58fbb9u,1),1);
        else { pm_store(&m,tank+0xd13u,pm_load(&m,tank+0xd13u,1)+pm_load(&m,0x58fbb0u+type,1),1);if (pm_i8(&m,tank+0xd13u)>3) pm_store(&m,tank+0xd13u,3,1); }
    } else { pm_store(&m,tank+0xd0fu,type,4);pm_store(&m,tank+0xd13u,pm_load(&m,0x58fbb0u+pm_load(&m,o+0xaeu,4),1),1); }
    a=pm_load(&m,o+0x90u,4);TRY(frame_spatial_remove(read,write,ctx,entity,a,e->release,e->system_ctx,e->work_limit));
    d=pm_load(&m,0x545ee4u,4);TRY(frame_entity_dispatch(read,write,ctx,entity,d,1,vehicle,e->method,e->method_ctx,&unused));
    flags=pm_load(&m,o+0xaau,4);pm_store(&m,o+0xaau,flags&~1u,4);
    if (!(flags&4u)) { pm_store(&m,o+0xaau,(flags&~1u)|4u,4);for (i=0;i<12u;i++) pm_store(&m,o+destination[i],pm_load(&m,o+source[i],4),4); }
    frame_entity_sound(e,0x40f6a0u,tank,0,0);
    if (pm_load(&m,tank+0xde5u,1)) { a=pm_load(&m,o+72u,4);TRY(frame_feedback_impact(read,write,ctx,pm_load(&m,a+12u,2),feedback,feedback_ctx)); }
    TRY(frame_pickup_effect_start(read,write,ctx,o,tank));type=pm_load(&m,tank+0xd0fu,4);
    if ((type==1u || type==2u || type==7u || type==8u) && pm_i32(&m,tank+0xd14u)>300) pm_store(&m,tank+0xd14u,300,4);
    return m.error;
}
int frame_powerup_capture(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o,a,shape,triangles,i,bytes;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,entity);a=pm_load(&m,o+0x88u,4);shape=pm_load(&m,a,4);triangles=pm_load(&m,shape+2u,2);bytes=triangles*2u;
    TRY(frame_object_auxiliary_backup_allocate(read,write,ctx,entity,e));a=pm_load(&m,o+0x88u,4);shape=pm_load(&m,a,4);
    copy(&m,pm_load(&m,a+72u,4),pm_load(&m,shape+4u,4),pm_load(&m,shape,2)*8u);
    for (i=0;i<3u;i++) { a=pm_load(&m,o+0x88u,4);shape=pm_load(&m,a,4);copy(&m,pm_load(&m,a+80u,4)+i*bytes,pm_load(&m,shape+16u+i*4u,4),bytes); }
    a=pm_load(&m,o+0x88u,4);pm_store(&m,a+88u,pm_load(&m,o+72u,4),4);return m.error;
}
int frame_powerup_remove(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o,head,packed;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    /*422B27: leave the lap-event master list intact; remove only the draw entry. */
    o=object_of(&m,entity);head=pm_load(&m,0x54b488u,4);TRY(frame_list_remove_value(read,write,ctx,entity,head,e->release,e->system_ctx,e->work_limit));packed=pm_load(&m,o+0x90u,4);
    TRY(frame_spatial_remove(read,write,ctx,entity,packed,e->release,e->system_ctx,e->work_limit));TRY(frame_entity_queue(read,write,ctx,entity,0,e->allocate,e->release,e->system_ctx,e->work_limit));return m.error;
}
int frame_powerup_disable(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o,packed,d,unused;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,entity);if (o && (pm_load(&m,o+0xaau,1)&1u)) {
        packed=pm_load(&m,o+0x90u,4);TRY(frame_spatial_remove(read,write,ctx,entity,packed,e->release,e->system_ctx,e->work_limit));d=pm_load(&m,0x545ee4u,4);
        TRY(frame_entity_dispatch(read,write,ctx,entity,d,1,0,e->method,e->method_ctx,&unused));pm_store(&m,o+0xaau,(pm_load(&m,o+0xaau,4)&~1u)|8u,4);
    }
    return m.error;
}
int frame_powerup_restore(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o,a,shape,triangles,bytes,i,at,head,packed,mode;int32_t value,divisor;int status;
    static const unsigned restore_offsets[]={0,4,28,8,40,52,24,32,36,44,48,56};
    static const unsigned source_offsets[]={0xba,0xbe,0xca,0xc2,0xd6,0xe2,0xc6,0xce,0xd2,0xda,0xde,0xe6};
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,entity);a=pm_load(&m,o+0x88u,4);if (!a || !pm_load(&m,a+32u,1)) return m.error;
    shape=pm_load(&m,a,4);triangles=pm_load(&m,shape+2u,2);copy(&m,pm_load(&m,shape+4u,4),pm_load(&m,a+72u,4),pm_load(&m,shape,2)*8u);a=pm_load(&m,o+0x88u,4);
    pm_store(&m,o+72u,pm_load(&m,a+88u,4),4);
    for (i=0;i<pm_load(&m,pm_load(&m,a,4)+2u,2);i++) { if (i>=e->work_limit) return -3;shape=pm_load(&m,a,4);at=pm_load(&m,shape+8u,4)+i*24u+18u;pm_store(&m,at,pm_load(&m,at,2)&~64u,2);a=pm_load(&m,o+0x88u,4); }
    bytes=triangles*2u;
    for (i=0;i<3u;i++) { a=pm_load(&m,o+0x88u,4);shape=pm_load(&m,a,4);copy(&m,pm_load(&m,shape+16u+i*4u,4),pm_load(&m,a+80u,4)+i*bytes,bytes); }
    TRY(frame_object_auxiliary_clear(read,write,ctx,entity,e));head=pm_load(&m,0x54b488u,4);TRY(frame_list_remove_value(read,write,ctx,entity,head,e->release,e->system_ctx,e->work_limit));packed=pm_load(&m,o+0x90u,4);
    TRY(frame_spatial_remove(read,write,ctx,entity,packed,e->release,e->system_ctx,e->work_limit));pm_store(&m,o+0xb2u,pm_load(&m,o+0xb6u,4),4);
    mode=pm_load(&m,0x52ee9cu,4);
    if (mode==4u && pm_i32(&m,0x543d60u)>2) { divisor=pm_sub(pm_i32(&m,0x543d60u),1);if (divisor>4) divisor=4;value=pm_i32(&m,o+0xb6u)/divisor;pm_write32(&m,o+0xb2u,value);if (value<3000) pm_store(&m,o+0xb2u,3000,4); }
    for (i=0;i<12u;i++) {
        if (i==10u) pm_store(&m,o+0xaau,pm_load(&m,o+0xaau,4)&~12u,4);
        pm_store(&m,o+restore_offsets[i],pm_load(&m,o+source_offsets[i],4),4);
    }
    pm_store(&m,o+0x153u,0,4);pm_store(&m,o+0x157u,0,4);pm_store(&m,o+0x13fu,0,4);pm_store(&m,o+0x13bu,0,4);pm_store(&m,o+0x137u,0,4);return m.error;
}
#undef TRY
