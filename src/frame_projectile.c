#include "frame_projectile.h"
#include "frame_ai.h"
#include "physics_memory.h"
#include "frame_overlay.h"
#include "frame_shatter.h"
#include "frame_mine.h"
#include "frame_ability_boost.h"
#include "frame_weapon_spin.h"
#include "frame_tank_hit.h"
#include "frame_camera.h"
#include "physics_terrain.h"
#include "frame_ufo.h"
#include "physics_objects.h"
#include "physics_vector.h"
#include "frame_lap_events.h"
#include "frame_text.h"
static uint32_t object_of(physics_memory *m,uint32_t id)
{ uint32_t r=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);return r ? pm_load(m,r+4u,4) : 0; }
#define TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
static uint32_t random_next(physics_memory *m)
{ uint32_t v=pm_load(m,0x55b6acu,4)*1664525u+1013904223u;pm_store(m,0x55b6acu,v,4);return v; }
static void transform(physics_memory *m,uint32_t matrix,const int32_t in[3],int32_t out[3])
{
    uint32_t i,j;
    for (i=0;i<3u;i++) { out[i]=in[i];if (matrix) { out[i]=0;for (j=0;j<3u;j++) out[i]=pm_add(out[i],pm_mul(in[j],pm_i32(m,matrix+(j*3u+i)*4u)));out[i]=pm_sar(out[i],12); } }
}
int frame_projectile_initialize(frame_data_read read,frame_data_write write,void *ctx,frame_object_material_name material,void *material_ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t token,size,mode;int status;const char *name;
    if (!read || !write || !material) return -1;
    status=material(material_ctx,(const unsigned char *)"glowy5",7,1,&token);if (status) return status;
    pm_store(&m,0x545d58u,token,4);pm_store(&m,0x545d7au,31,1);pm_store(&m,0x545d7bu,31,1);mode=pm_load(&m,0x52ee9cu,4);
    pm_store(&m,0x545d68u,1,4);pm_store(&m,0x545d6cu,UINT32_MAX,4);pm_store(&m,0x545d78u,0,1);pm_store(&m,0x545d79u,0,1);
    size=(mode==2u || mode==3u) ? 410u : 510u;pm_store(&m,0x545d70u,size,4);pm_store(&m,0x545d74u,size,4);
    pm_store(&m,0x545d7cu,128,1);pm_store(&m,0x545d7du,128,1);pm_store(&m,0x545d7eu,128,1);
    name=(pm_load(&m,0x5964d4u,4)&0x8000u) ? "glow" : "pcglow";if (m.error) return m.error;
    status=material(material_ctx,(const unsigned char *)name,name[0]=='g' ? 5u : 7u,1,&token);if (status) return status;
    pm_store(&m,0x545d88u,token,4);pm_store(&m,0x545daau,63,1);pm_store(&m,0x545dabu,63,1);
    pm_store(&m,0x545da8u,0,1);pm_store(&m,0x545da9u,0,1);pm_store(&m,0x545dadu,0,1);pm_store(&m,0x545daeu,0,1);
    pm_store(&m,0x545d98u,1,4);pm_store(&m,0x545d9cu,UINT32_MAX,4);pm_store(&m,0x545da0u,300,4);pm_store(&m,0x545da4u,300,4);pm_store(&m,0x545dacu,128,1);return m.error;
}
int frame_projectile_materials(frame_data_read read,frame_data_write write,void *ctx,frame_object_material_name material,void *material_ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t i,token,flags,length;int status;const char *name;
    static const struct { uint32_t address;const char *name;uint32_t length; } items[]={
        {0x5905fc,"a_exp02",8},{0x5905f8,0,0},{0x546734,"ufo",4},{0x58fa60,"fireball",9},{0x58fa64,"ball1",6},{0x58fa68,"Flare3",7},{0x58fa6c,"ufo",4},{0x58fa70,0,0},{0x58fa74,"Flare3",7},
        {0x58fa80,0,0},{0x58fa84,0,0},{0x58fa88,0,0},{0x58fa90,0,0},{0x58fa8c,0,0},{0x58fa94,0,0}};
    if (!read || !write || !material) return -1;
    for (i=0;i<15u;i++) {
        name=items[i].name;flags=pm_load(&m,0x5964d4u,4);
        if (!name) {
            if (i==7u || i>=13u) name=(flags&0x8000u) ? "glow" : "pcglow";
            else if (!(flags&0x8000u)) name="pcsmoke";
            else name=(i==1u || i==9u) ? "smoke2" : "smke2";
        }
        if (m.error) return m.error;
        length=items[i].length;if (!items[i].name) { for (length=0;name[length];length++) {}length++; }
        status=material(material_ctx,(const unsigned char *)name,length,1,&token);if (status) return status;pm_store(&m,items[i].address,token,4);
    }
    TRY(frame_projectile_initialize(read,write,ctx,material,material_ctx));return m.error;
}
int frame_projectile_model(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *e,const frame_shape_resources *r)
{
    physics_memory m={read,write,ctx,0};uint32_t shape,space;int status;
    if (!read || !write || !e || !r) return -1;
    space=pm_load(&m,0x55ae48u,4);TRY(frame_shape_load_name(read,write,ctx,(const unsigned char *)"homing.sm",10,space,e,r,&shape));pm_store(&m,0x545dd4u,shape,4);
    pm_store(&m,0x545d48u,0,4);pm_store(&m,0x545d4cu,0,4);pm_write32(&m,0x545d50u,pm_mul(pm_i16(&m,shape+32u),8));return m.error;
}
int frame_projectile_sprite(frame_data_read read,frame_data_write write,void *ctx,uint32_t o)
{
    physics_memory m={read,write,ctx,0};uint32_t type,size=0,difficulty,mode;
    if (!read || !write) return -1;
    type=pm_load(&m,o+0x7cu,4);pm_store(&m,o+0x98u,1,4);pm_store(&m,o+0x88u,pm_load(&m,0x58fa60u+type*4u,4),4);
    switch (type) {
    case 0:
        pm_store(&m,o+0xa8u,0,1);pm_store(&m,o+0xa9u,0,1);pm_store(&m,o+0xaau,63,1);pm_store(&m,o+0xabu,63,1);
        pm_store(&m,o+0xa0u,400,4);pm_store(&m,o+0xa4u,400,4);difficulty=pm_load(&m,0x53d18cu,4);
        if (difficulty==1u) { pm_store(&m,o+0xa0u,430,4);pm_store(&m,o+0xa4u,430,4); }
        if (difficulty==2u) { pm_store(&m,o+0xa0u,pm_load(&m,o+0xa0u,4)+60u,4);pm_store(&m,o+0xa4u,pm_load(&m,o+0xa4u,4)+60u,4); }
        break;
    case 1:
        pm_store(&m,o+0x98u,UINT32_MAX,4);pm_store(&m,o+0xa8u,0,1);pm_store(&m,o+0xa9u,0,1);pm_store(&m,o+0xaau,31,1);pm_store(&m,o+0xabu,31,1);
        mode=pm_load(&m,0x52ee9cu,4);size=(mode==2u || mode==3u) ? 350u : 450u;break;
    case 2:pm_store(&m,0x5317c8u,1,4);pm_store(&m,0x545da0u,150,4);pm_store(&m,0x545da4u,150,4);return m.error;
    case 3:
        pm_store(&m,o+0xa8u,0,1);pm_store(&m,o+0xa9u,0,1);pm_store(&m,o+0x98u,UINT32_MAX,4);pm_store(&m,o+0xaau,63,1);pm_store(&m,o+0xabu,63,1);size=650;break;
    case 5:
        pm_store(&m,o+0xaau,63,1);pm_store(&m,o+0xabu,63,1);pm_store(&m,o+0xa8u,0,1);pm_store(&m,o+0xa9u,0,1);pm_store(&m,o+0xa0u,1400,4);pm_store(&m,o+0xa4u,1400,4);
        pm_store(&m,o+0xacu,0,1);pm_store(&m,o+0xadu,0,1);pm_store(&m,o+0xaeu,128,1);pm_store(&m,o+0xb0u,random_next(&m)>>4,2);return m.error;
    default:return m.error;
    }
    if (size) { pm_store(&m,o+0xa0u,size,4);pm_store(&m,o+0xa4u,size,4); }
    pm_store(&m,o+0xacu,128,1);pm_store(&m,o+0xadu,128,1);pm_store(&m,o+0xaeu,128,1);return m.error;
}
int frame_projectile_uv_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t o)
{
    physics_memory m={read,write,ctx,0};uint32_t t,x,y,z;
    if (!read || !write) return -1;
    t=pm_load(&m,o+0x4ddu,4)+pm_load(&m,0x5904fcu,4);pm_store(&m,o+0x4ddu,t,4);if (pm_signed(t)<=60) return m.error;
    x=pm_load(&m,o+0xa8u,1);pm_store(&m,o+0x4ddu,0,4);
    if (x<31u) {
        pm_store(&m,o+0xa8u,x+32u,1);pm_store(&m,o+0xaau,pm_load(&m,o+0xaau,1)+32u,1);pm_store(&m,0x545d78u,x+32u,1);pm_store(&m,0x545d7au,pm_load(&m,o+0xaau,1),1);return m.error;
    }
    y=pm_load(&m,o+0xa9u,1);pm_store(&m,o+0xa8u,0,1);
    if (y<31u) { pm_store(&m,o+0xaau,31,1);pm_store(&m,o+0xa9u,y+32u,1);z=pm_load(&m,o+0xabu,1)+32u;pm_store(&m,o+0xabu,z,1); }
    else { pm_store(&m,o+0xa9u,0,1);pm_store(&m,o+0xaau,31,1);pm_store(&m,o+0xabu,31,1); }
    pm_store(&m,0x545d78u,0,1);pm_store(&m,0x545d79u,pm_load(&m,o+0xa9u,1),1);pm_store(&m,0x545d7au,pm_load(&m,o+0xaau,1),1);pm_store(&m,0x545d7bu,pm_load(&m,o+0xabu,1),1);return m.error;
}
static void fade_clamped(physics_memory *m,uint32_t at,uint32_t fade)
{ uint32_t old=pm_load(m,at,1),v=(old-fade)&255u;if (old) { pm_store(m,at,v,1);if (v>old) pm_store(m,at,0,1); } }
int frame_projectile_trail_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t o)
{
    physics_memory m={read,write,ctx,0};uint32_t i,r,type,channel,fade,selected=0,dt,j;int32_t timer,omega;
    if (!read || !write) return -1;
    type=pm_load(&m,o+0x7cu,4);
    for (i=0;i<15u;i++) {
        r=o+0xb2u+i*65u;if (!pm_load(&m,r,1)) continue;
        dt=pm_load(&m,0x5904fcu,4);pm_store(&m,r+1u,pm_load(&m,r+1u,2)+dt,2);timer=pm_i16(&m,r+1u);if (timer<=30) continue;
        channel=type==3u ? pm_load(&m,r+61u,4) : 0u;
        if (channel<=2u) selected=r+(channel==0u ? 55u : channel==1u ? 57u : 56u);else if (!selected) return m.error ? m.error : -4;fade=pm_load(&m,0x531718u+pm_load(&m,o+0x7cu,4)*4u,1);
        pm_store(&m,selected,pm_load(&m,selected,1)-fade,1);fade=pm_load(&m,0x531718u+pm_load(&m,o+0x7cu,4)*4u,4);
        if ((int32_t)pm_load(&m,selected,1)<pm_signed(fade)) { pm_store(&m,r,0,1);pm_store(&m,o+0x481u,pm_load(&m,o+0x481u,2)-1u,2);continue; }
        if (pm_load(&m,o+0x7cu,4)==5u) pm_write32(&m,r+7u,pm_sub(pm_i32(&m,r+7u),pm_sar(pm_i32(&m,0x5904fcu),2)));
        if (type!=3u) { for (j=56u;j<=57u;j++) pm_store(&m,r+j,pm_load(&m,r+j,1)-pm_load(&m,0x531718u+pm_load(&m,o+0x7cu,4)*4u,1),1); }
        else if (channel<=2u) {
            if (channel!=2u) fade_clamped(&m,r+56u,pm_load(&m,0x531718u+pm_load(&m,o+0x7cu,4)*4u,1));
            if (channel==1u) fade_clamped(&m,r+55u,pm_load(&m,0x531718u+pm_load(&m,o+0x7cu,4)*4u,1));
            else { if (channel==2u) fade_clamped(&m,r+55u,pm_load(&m,0x531718u+pm_load(&m,o+0x7cu,4)*4u,1));fade_clamped(&m,r+57u,pm_load(&m,0x531718u+pm_load(&m,o+0x7cu,4)*4u,1)); }
        }
        pm_store(&m,r+1u,0,2);pm_store(&m,r+59u,pm_load(&m,r+59u,2)+(uint32_t)pm_sar(pm_mul(pm_i32(&m,0x5904fcu),pm_i32(&m,r+15u)),8),2);
        for (j=0;j<3u;j++) pm_write32(&m,r+23u+j*4u,pm_add(pm_i32(&m,r+23u+j*4u),pm_sar(pm_mul(pm_i32(&m,r+3u+j*4u),pm_i32(&m,0x5904fcu)),8)));
        if (pm_load(&m,o+0x7cu,4)!=5u) for (j=0;j<2u;j++) pm_write32(&m,r+43u+j*4u,pm_add(pm_i32(&m,r+43u+j*4u),pm_mul(pm_i32(&m,0x5904fcu),2)));
        omega=pm_i32(&m,r+15u);dt=pm_load(&m,0x5904fcu,4);if (omega<=0) dt=0u-dt;pm_store(&m,r+15u,(uint32_t)omega-(dt<<3),4);
    }
    return m.error;
}
static int signed_remainder(int32_t a,int32_t b,int32_t *out)
{ if (!b || (a==INT32_MIN && b==-1)) return -2;*out=a%b;return 0; }
int frame_projectile_trail_emit(frame_data_read read,frame_data_write write,void *ctx,uint32_t o)
{
    physics_memory m={read,write,ctx,0};uint32_t time,index,type,r,s,i,channel,size;int32_t first,second,vertical,rem1,rem2,a,b,input[3],offset[3];int status;
    if (!read || !write) return -1;
    time=pm_load(&m,o+0x485u,4)+pm_load(&m,0x5904fcu,4);pm_store(&m,o+0x485u,time,4);if (pm_signed(time)<=32) return m.error;
    pm_store(&m,o+0x485u,0,4);index=pm_load(&m,0x545dd0u,4);first=pm_i32(&m,0x531790u+index*4u);index++;if (index==11u) index=0;
    second=pm_i32(&m,0x531790u+index*4u);index++;pm_store(&m,0x545dd0u,index,4);if (index==11u) pm_store(&m,0x545dd0u,0,4);
    index=(uint32_t)pm_i16(&m,o+0x483u);r=o+0xb2u+index*65u;s=r+19u;type=pm_load(&m,o+0x7cu,4);pm_store(&m,s,pm_load(&m,0x58fa80u+type*4u,4),4);
    if (type==2u) { if (!pm_load(&m,o+20u,4)) return m.error ? m.error : -4;for (i=0;i<3u;i++) input[i]=pm_i32(&m,0x545d48u+i*4u);transform(&m,pm_load(&m,o+20u,4),input,offset); }
    if (type==5u) { pm_store(&m,r+55u,100,1);pm_store(&m,r+56u,100,1);pm_store(&m,r+57u,128,1); }
    else {
        vertical=pm_signed(((uint32_t)pm_i32(&m,0x531778u+type*4u)<<10)<<((8u-(uint32_t)(first%2))&31u))/1000;pm_write32(&m,r+7u,vertical);
        if (type==3u) {
            channel=(uint32_t)(first%3);pm_store(&m,r+61u,channel,4);
            if (channel<=2u) {
                a=pm_i32(&m,0x531730u+type*4u);status=signed_remainder(first,a,&rem1);if (status) return m.error ? m.error : status;
                status=signed_remainder(second,a,&rem2);if (status) return m.error ? m.error : status;
                pm_store(&m,r+55u,channel==0u ? (uint32_t)a : channel==1u ? (uint32_t)rem2 : (uint32_t)rem1,1);
                pm_store(&m,r+56u,channel==2u ? (uint32_t)a : (uint32_t)rem1,1);pm_store(&m,r+57u,channel==1u ? (uint32_t)a : (uint32_t)rem2,1);
            }
        } else for (i=0;i<3u;i++) pm_store(&m,r+55u+i,pm_load(&m,0x531730u+type*4u,1),1);
    }
    pm_store(&m,s+16u,1,4);pm_store(&m,s+34u,63,1);pm_store(&m,s+35u,63,1);
    if (type==4u) { if (!pm_load(&m,o+20u,4)) return m.error ? m.error : -4;for (i=0;i<3u;i++) input[i]=pm_i32(&m,o+0x4e5u+i*4u);transform(&m,pm_load(&m,o+20u,4),input,offset); }
    for (i=0;i<3u;i++) pm_write32(&m,s+4u+i*4u,pm_add(pm_i32(&m,o+i*4u),(type==2u || type==4u) ? offset[i] : 0));
    a=pm_i32(&m,0x531760u+type*4u);status=signed_remainder(second,a,&rem1);if (status) return m.error ? m.error : status;
    b=pm_mul(pm_mul(rem1,131072),1-first%3);pm_write32(&m,r+3u,b/1000);
    status=signed_remainder(first,a,&rem2);if (status) return m.error ? m.error : status;b=pm_mul(pm_mul(rem2,131072),1-second%3);pm_write32(&m,r+11u,b/1000);
    pm_store(&m,s+32u,0,1);pm_store(&m,s+33u,0,1);size=pm_load(&m,0x531748u+type*4u,4);pm_store(&m,s+24u,size,4);pm_store(&m,s+28u,pm_load(&m,0x531748u+type*4u,4),4);
    if (pm_load(&m,0x54b480u,4)) { pm_store(&m,s+24u,(pm_load(&m,s+24u,4)*3u)>>2,4);pm_store(&m,s+28u,(pm_load(&m,s+28u,4)*3u)>>2,4); }
    pm_store(&m,s+40u,0,2);pm_write32(&m,r+15u,pm_sar(pm_mul(first%720-360,182),2));pm_store(&m,r,1,1);
    pm_store(&m,o+0x483u,(uint32_t)((pm_i16(&m,o+0x483u)+1)%15),2);if (pm_i16(&m,o+0x481u)<15) pm_store(&m,o+0x481u,pm_load(&m,o+0x481u,2)+1u,2);return m.error;
}
int frame_projectile_flash_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t o)
{
    physics_memory m={read,write,ctx,0};uint32_t i;int32_t amplitude;int status;frame_overlay_params p={1,0,0,0,0};
    if (!read || !write) return -1;
    for (i=0;i<2u;i++) if (pm_load(&m,o+0x489u+i,1)) {
        amplitude=pm_i16(&m,o+0x48bu);
        if (!pm_load(&m,0x53e944u,4)) {
            if (m.error) return m.error;
            p.alpha=(uint32_t)amplitude;p.red=255;p.green=pm_load(&m,o+0x7cu,4)==4u ? 128u : 200u;p.blue=128;
            status=frame_overlay_set(read,write,ctx,amplitude>=0,i,&p);if (status) return status;
        }
        if (amplitude<0) pm_store(&m,o+0x84u,pm_load(&m,o+0x84u,4)&~2u,4);
    }
    pm_store(&m,o+0x48bu,pm_load(&m,o+0x48bu,2)-(uint32_t)pm_sar(pm_i32(&m,0x5904fcu),1),2);return m.error;
}
int frame_projectile_stop(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t parent,unused;
    if (!read || !write || !e) return -1;
    if (pm_load(&m,0x58e7acu,4)) return m.error ? m.error : -5;
    parent=pm_load(&m,0x5450bcu,4);if (m.error) return m.error;return frame_class_dispatch(read,ctx,parent,entity,e->method,e->method_ctx,&unused);
}
static void velocity_rotate(physics_memory *m,uint32_t at,uint32_t axis,uint32_t angle,const physics_trig *t)
{
    int32_t v[3],rotated[3],c=t->cosine[(angle&65532u)>>2],s=t->sine[(angle&65532u)>>2];uint32_t i;
    for (i=0;i<3u;i++) v[i]=pm_i32(m,at+i*4u);
    if (axis==1u) { physics_vector_yaw(v,rotated,angle,t);pm_write32(m,at,rotated[0]);pm_write32(m,at+8u,rotated[2]);pm_write32(m,at+4u,v[1]); }
    else { pm_write32(m,at+4u,pm_trunc(pm_add(pm_mul(v[1],c),pm_mul(v[2],s)),12));pm_write32(m,at+8u,pm_trunc(pm_sub(pm_mul(v[2],c),pm_mul(v[1],s)),12));pm_write32(m,at,v[0]); }
}
static void transform_local(physics_memory *m,uint32_t matrix,const int32_t in[3],int32_t out[3])
{
    uint32_t i,j;for (i=0;i<3u;i++) { out[i]=in[i];if (matrix) { out[i]=0;for (j=0;j<3u;j++) out[i]=pm_add(out[i],pm_mul(in[j],pm_i32(m,matrix+(i*3u+j)*4u)));out[i]=pm_sar(out[i],12); } }
}
int frame_projectile_spawn(frame_data_read read,frame_data_write write,void *ctx,uint32_t owner,uint32_t type,const frame_entity_factory_services *e,const physics_trig *trig,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t tank,klass,entity,o,i,j,id,target,best=0,head,unused;int32_t pitch,yaw,length,elevation,speed,v[3],local[3],drift,best_z=512000;int status;
    static const uint32_t order[]={1,0,4,3,2,7,6,5,8};
    if (!read || !write || !e || !e->work_limit || !trig || !out) return -1;
    if (pm_load(&m,0x58e7acu,4)) return m.error ? m.error : -5;
    tank=object_of(&m,owner);klass=pm_load(&m,0x545db4u,4);TRY(frame_entity_create(read,write,ctx,klass,e,&entity));*out=entity;o=object_of(&m,entity);if (m.error) return m.error;if (!o) return -4;
    pm_store(&m,o+0x78u,owner,4);pm_store(&m,o+0x7cu,type,4);
    length=physics_length2_approx(pm_i32(&m,tank+0x332u),pm_i32(&m,tank+0x33au));TRY(physics_angle_lookup(pm_i32(&m,tank+0x336u),length,trig,&pitch));
    if (type==2u) {
        pm_write32(&m,o+0x4d1u,pitch);pm_store(&m,o+0x58u,pm_load(&m,0x531710u,4),4);
        for (i=0;i<8u;i++) pm_store(&m,o+24u+i*4u,pm_load(&m,tank+0x31au+i*4u,4),4);
        for (i=0;i<9u;i++) { j=order[i];pm_store(&m,o+0x4a1u+j*4u,j==8u ? pm_load(&m,tank+0x33au,4) : pm_load(&m,o+24u+j*4u,4),4);if (i==1u) pm_store(&m,o+56u,pm_load(&m,tank+0x33au,4),4); }
        for (i=0;i<3u;i++) { v[i]=pm_i32(&m,o+0x50u+i*4u); }transform(&m,pm_load(&m,o+20u,4),v,local);for (i=0;i<3u;i++) pm_write32(&m,o+0x50u+i*4u,local[i]);
        drift=(int32_t)(((random_next(&m)>>4)&65535u)%2000u)-1000;pm_write32(&m,0x545ddcu,drift);
        if (drift<0 && drift>-500) pm_write32(&m,0x545ddcu,-500);else if (drift>0 && drift<500) pm_write32(&m,0x545ddcu,500);
    } else {
        elevation=pm_i32(&m,type==1u ? 0x5316f4u : 0x5316f0u);speed=pm_i32(&m,type==1u ? 0x5316fcu : 0x5316f8u);TRY(physics_angle_lookup(pm_i32(&m,tank+0x332u),pm_i32(&m,tank+0x33au),trig,&yaw));yaw=pm_neg(yaw);
        if (pm_abs(pitch)<pm_i32(&m,0x545db8u)) pitch=0;
        else for (i=1;i<6u;i++) if (pm_abs(pitch)<pm_i32(&m,0x545db8u+i*4u)) { pitch=pitch>0 ? pm_i32(&m,0x545db4u+i*4u) : pm_neg(pm_i32(&m,0x545db4u+i*4u));break; }
        pm_write32(&m,o+0x58u,speed);pm_store(&m,o+0x54u,0,4);pm_store(&m,o+0x50u,0,4);velocity_rotate(&m,o+0x50u,0,(uint32_t)pm_add(elevation,pitch),trig);velocity_rotate(&m,o+0x50u,1,(uint32_t)yaw,trig);
        for (i=0;i<3u;i++) pm_write32(&m,o+0x50u+i*4u,pm_add(pm_i32(&m,o+0x50u+i*4u),pm_sar(pm_i32(&m,tank+0x50u+i*4u),6)));
    }
    for (i=0;i<3u;i++) { pm_store(&m,o+i*4u,pm_load(&m,tank+0x376u+i*4u,4),4); }pm_write32(&m,o+4u,pm_sub(pm_i32(&m,o+4u),150));
    TRY(frame_entity_activate(read,write,ctx,entity,e->method,e->method_ctx,&unused));head=pm_load(&m,0x54b48cu,4);TRY(frame_list_remove_value(read,write,ctx,entity,head,e->release,e->system_ctx,e->work_limit));
    head=pm_load(&m,0x54b488u,4);TRY(frame_list_append(read,write,ctx,entity,head,e->allocate,e->release,e->system_ctx,e->work_limit,&unused));TRY(frame_projectile_sprite(read,write,ctx,o));
    type=pm_load(&m,o+0x7cu,4);
    if (type==2u || type==5u) {
        if (type==2u) { pm_store(&m,o+72u,pm_load(&m,0x545dd4u,4),4);pm_store(&m,o+0x491u,pm_load(&m,o+4u,4),4);for (i=0;i<3u;i++) pm_store(&m,o+0x495u+i*4u,pm_load(&m,o+i*4u,4),4); }
        else for (i=0;i<9u;i++) pm_store(&m,o+24u+i*4u,pm_load(&m,tank+0x31au+i*4u,4),4);
        for (i=0;i<9u;i++) {
            id=pm_load(&m,0x53e8e0u+i*4u,4);if (!id || id==owner) continue;target=object_of(&m,id);if (!target || (pm_load(&m,target+0x10f3u,1)&1u)) continue;
            for (j=0;j<3u;j++) { v[j]=pm_sub(pm_i32(&m,target+j*4u),pm_i32(&m,tank+j*4u)); }transform_local(&m,tank+0x31au,v,local);
            if (local[2]<=0 || local[2]>=102400) continue;
            while (pm_abs(local[0])>32768 || pm_abs(local[1])>32768 || pm_abs(local[2])>32768) for (j=0;j<3u;j++) local[j]=pm_sar(local[j],1);
            TRY(physics_angle_lookup(local[0],local[2],trig,&yaw));if (pm_abs(yaw)<5460 && local[2]<best_z) { best=id;best_z=local[2]; }
        }
        pm_store(&m,o+0x48du,best,4);
    }
    return m.error;
}
static int sweep(physics_memory *m,uint32_t entity,const int32_t old[3],const uint32_t *addresses,frame_projectile_hit *out)
{
    uint32_t o=object_of(m,entity),id,target,i,j,matrix;int32_t current[3],delta[3],origin[3],v[3],a[3],b[3],low[3],high[3],hit[3],normal[3],world[3],direction[3],radius,length,dot;int code;
    out->entity=0;out->local_normal_valid=0;for (i=0;i<3u;i++) { current[i]=pm_i32(m,o+i*4u);delta[i]=pm_sub(current[i],old[i]); }length=physics_approximate_distance(delta);
    for (i=0;i<9u;i++) {
        id=pm_load(m,0x53e8e0u+i*4u,4);if (!id || id==pm_load(m,o+0x78u,4)) continue;target=object_of(m,id);if (pm_load(m,target+0xd90u,1)&1u) continue;
        radius=(int32_t)pm_load(m,pm_load(m,target+72u,4)+12u,2)*16;
        for (j=0;j<3u;j++) origin[j]=pm_i32(m,target+j*4u);
        if (pm_sub(pm_abs(pm_sub(origin[0],old[0])),length)>=radius || pm_sub(pm_abs(pm_sub(origin[2],old[2])),length)>=radius || pm_sub(pm_abs(pm_sub(origin[1],old[1])),length)>=radius) continue;
        matrix=pm_load(m,target+20u,4);for (j=0;j<3u;j++) v[j]=pm_sub(old[j],origin[j]);transform_local(m,matrix,v,a);
        for (j=0;j<3u;j++) { v[j]=pm_sub(pm_sub(current[j],pm_sar(pm_mul(pm_i32(m,target+0x50u+j*4u),pm_i32(m,0x5904fcu)),14)),origin[j]); }transform_local(m,matrix,v,b);
        for (j=0;j<3u;j++) { low[j]=pm_i32(m,target+0x1086u+j*4u);high[j]=pm_i32(m,target+0x1092u+j*4u); }if (m->error) return m->error;
        code=physics_segment_box(a,b,low,high,hit,normal);if (code<0) return code;
        if (code==2) {
            for (j=0;j<3u;j++) direction[j]=pm_sub(b[j],a[j]);
            if (physics_approximate_distance(direction)>2) { physics_vector_normalize(direction);for (j=0;j<3u;j++) b[j]=pm_sub(a[j],pm_mul(direction[j],pm_sar(radius,11)));code=physics_segment_box(b,a,low,high,hit,normal);if (code<0) return code; }
        }
        if (code!=1) continue;
        out->local_normal_valid=1;for (j=0;j<3u;j++) { out->local_normal[j]=normal[j];if (addresses) pm_write32(m,addresses[2]+j*4u,normal[j]); }
        transform(m,matrix,normal,world);for (j=0;j<3u;j++) direction[j]=delta[j];physics_vector_normalize(direction);dot=0;for (j=0;j<3u;j++) dot=pm_add(dot,pm_trunc(pm_mul(direction[j],world[j]),12));if (dot>=0) continue;
        for (j=0;j<3u;j++) { out->normal[j]=world[j];if (addresses) pm_write32(m,addresses[1]+j*4u,world[j]); }
        for (j=0;j<3u;j++) { out->local_point[j]=hit[j];if (addresses) pm_write32(m,addresses[3]+j*4u,hit[j]); }
        transform(m,matrix,hit,world);for (j=0;j<3u;j++) { out->point[j]=pm_add(world[j],origin[j]);if (addresses) pm_write32(m,addresses[0]+j*4u,out->point[j]); }out->entity=pm_load(m,0x53e8e0u+i*4u,4);return m->error;
    }
    return m->error;
}
int frame_projectile_sweep(frame_data_read read,void *ctx,uint32_t entity,const int32_t old[3],frame_projectile_hit *out)
{ physics_memory m={read,0,ctx,0};if (!read || !old || !out) return -1;return sweep(&m,entity,old,0,out); }
int frame_projectile_sweep_data(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t old,uint32_t point,uint32_t normal,uint32_t local_normal,uint32_t local_point,uint32_t *hit_entity)
{
    physics_memory m={read,write,ctx,0};int32_t v[3];uint32_t i,addresses[4]={point,normal,local_normal,local_point};frame_projectile_hit hit;int status;
    if (!read || !write || !hit_entity) return -1;
    for (i=0;i<3u;i++) { v[i]=pm_i32(&m,old+i*4u); }if (m.error) return m.error;status=sweep(&m,entity,v,addresses,&hit);if (!status) *hit_entity=hit.entity;return status;
}
int frame_projectile_ricochet(frame_data_read read,frame_data_write write,void *ctx,uint32_t o,uint32_t vehicle,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t target,owner,mode,i,id,candidate,chosen=0;int32_t a,b,closest=5120000,v[3],local[3],angle;int status;
    if (!read || !write || !trig) return -1;
    target=object_of(&m,vehicle);a=(int32_t)(((random_next(&m)>>4)&65535u)%10u);mode=pm_load(&m,0x52ee9cu,4);owner=pm_load(&m,o+0x78u,4);
    if (!a && (mode==0u || mode==1u) && owner && physics_length2_approx(pm_sub(pm_i32(&m,object_of(&m,owner)),pm_i32(&m,target)),pm_sub(pm_i32(&m,object_of(&m,owner)+8u),pm_i32(&m,target+8u)))<35840) {
        for (i=0;i<7u;i++) {
            id=pm_load(&m,0x53e8c0u+i*4u,4);if (id==vehicle) continue;candidate=object_of(&m,id);if (!pm_load(&m,candidate+0xdedu,1)) continue;
            a=pm_sub(pm_i32(&m,target),pm_i32(&m,candidate));if (pm_abs(a)>=closest) continue;b=pm_sub(pm_i32(&m,target+8u),pm_i32(&m,candidate+8u));if (pm_abs(b)>=closest) continue;
            closest=pm_sar(pm_add(a,b),1);chosen=candidate;
        }
        if (chosen) {
            v[0]=pm_sub(pm_i32(&m,chosen),pm_i32(&m,target));v[1]=0;v[2]=pm_sub(pm_i32(&m,chosen+8u),pm_i32(&m,target+8u));
            for (i=0;i<3u;i++) local[i]=pm_i32(&m,o+0x50u+i*4u);TRY(frame_matrix_direction(read,write,ctx,pm_load(&m,o+20u,4),local,trig));transform_local(&m,pm_load(&m,o+20u,4),v,local);
            TRY(physics_angle_lookup(local[0],local[2],trig,&angle));velocity_rotate(&m,o+0x50u,1,(uint32_t)pm_neg(angle),trig);pm_write32(&m,o+0x54u,pm_add(pm_i32(&m,o+0x54u),1310));
        }
    } else {
        angle=(int32_t)(int16_t)((random_next(&m)>>4)&65535u);
        if (angle<-24570 || angle>24570) { angle=angle<1 ? -24570 : 24570;a=pm_add(pm_i32(&m,o+0x54u),9175); }
        else a=pm_add(pm_i32(&m,o+0x54u),(int32_t)(((random_next(&m)>>4)&65535u)%5242u)+2621);
        pm_write32(&m,o+0x54u,a);velocity_rotate(&m,o+0x50u,1,(uint32_t)angle,trig);
    }
    mode=pm_load(&m,0x52ee9cu,4);if (mode==4u || mode==5u || pm_load(&m,o+0x7cu,4)!=1u || pm_load(&m,o+0x78u,4)!=pm_load(&m,0x53e8b8u,4)) pm_store(&m,o+0x78u,vehicle,4);
    for (i=0;i<3u;i++) { pm_write32(&m,o+0x50u+i*4u,pm_trunc(pm_mul(pm_i32(&m,o+0x50u+i*4u),700),10)); }return m.error;
}
int frame_projectile_fire_select(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t o,const frame_entity_factory_services *e,const physics_trig *trig,frame_effect_sink feedback,void *feedback_ctx,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t weapon,mode,result=0,unused,type;int status;
    if (!read || !write || !e || !out) return -1;
    weapon=pm_load(&m,o+0xd0fu,4);mode=pm_load(&m,0x52ee9cu,4);
    if (!((mode==4u||mode==5u)&&!weapon&&pm_load(&m,0x5450b8u,4))) frame_entity_sound(e,0x40f5b0u,o,weapon,0);
    switch (weapon) {
    case 0:
        if (mode!=0u && mode!=1u && pm_load(&m,0x5450b8u,4)) break;
        TRY(frame_projectile_spawn(read,write,ctx,entity,0,e,trig,&unused));pm_store(&m,o+0xe13u,0,4);result=1;break;
    case 1:case 2:
        TRY(frame_mine_spawn(read,write,ctx,entity,1,weapon-1u,e,trig,feedback,feedback_ctx,&unused));pm_store(&m,o+0xd13u,pm_load(&m,o+0xd13u,1)-1u,1);break;
    case 3:case 4:case 5:case 9:case 10:
        type=weapon==3u ? 2u : weapon==4u ? 3u : weapon==5u ? 1u : weapon==9u ? 0u : 5u;
        TRY(frame_projectile_spawn(read,write,ctx,entity,type,e,trig,&unused));pm_store(&m,o+0xe13u,weapon,4);pm_store(&m,o+0xd13u,pm_load(&m,o+0xd13u,1)-1u,1);result=1;break;
    case 7:TRY(frame_ability_boost_start(read,write,ctx,entity));pm_store(&m,o+0xd13u,pm_load(&m,o+0xd13u,1)-1u,1);break;
    case 8:TRY(frame_weapon_spin_start(read,write,ctx,entity,&unused));pm_store(&m,o+0xd13u,pm_load(&m,o+0xd13u,1)-1u,1);break;
    default:if (m.error) return m.error;*out=0;return 0;
    }
    if (pm_load(&m,o+0xde5u,1) && result) TRY(frame_feedback_fire(read,write,ctx,pm_load(&m,o+0xd0fu,4),feedback,feedback_ctx));
    if (!pm_load(&m,o+0xd13u,1)) { pm_store(&m,o+0xd0fu,0,4); }if (m.error) return m.error;*out=result;return 0;
}
int frame_projectile_tank_fire(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e,const physics_trig *trig,frame_effect_sink feedback,void *feedback_ctx,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,fired;int status;
    if (!read || !write || !e || !out) return -1;
    o=object_of(&m,entity);
    if (pm_i32(&m,o+0xd14u)<1) {
        TRY(frame_projectile_fire_select(read,write,ctx,entity,o,e,trig,feedback,feedback_ctx,&fired));
        if (fired) { pm_write32(&m,o+0x768u,pm_sub(pm_i32(&m,o+0x768u),600));pm_write32(&m,o+0x770u,pm_sub(pm_i32(&m,o+0x770u),900));pm_store(&m,o+0xd14u,1000,4); }
        else pm_store(&m,o+0xd14u,300,4);
        if (m.error) return m.error;
        TRY(frame_ai_on_shot(read,write,ctx,entity,e));
    }
    if (m.error) return m.error;
    *out=entity;return 0;
}
int frame_projectile_player_fire(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,parent;
    if (!read || !write || !e || !out) return -1;
    o=object_of(&m,entity);if (pm_i32(&m,o+0xd14u)<1) { pm_write32(&m,o+0x118u,pm_add(pm_i32(&m,o+0x118u),75));pm_write32(&m,o+0x107eu,pm_add(pm_i32(&m,o+0x107eu),50)); }
    parent=pm_load(&m,0x545fdcu,4);if (m.error) return m.error;return frame_class_dispatch(read,ctx,parent,entity,e->method,e->method_ctx,out);
}
typedef struct impact_values {
    frame_data_read read;frame_data_write write;void *ctx;const uint32_t *contact;const int32_t *origin;const frame_shatter_services *shatter;
    const frame_entity_factory_services *entities;
} impact_values;
static int impact_contact(void *ctx,uint32_t target,uint32_t entity,uint32_t has_payload,uint32_t payload,uint32_t *out)
{
    impact_values *v=(impact_values *)ctx;int status;(void)has_payload;(void)payload;
    if (target==0x403d90u || target==0x403e50u) { frame_entity_factory_services e=*v->entities;e.method=impact_contact;e.method_ctx=v;status=frame_ai_hit(v->read,v->write,v->ctx,entity,payload,target==0x403d90u,&e); }
    else if (target==0x438440u) status=frame_tank_shell_hit_values(v->read,v->write,v->ctx,entity,v->contact,v->shatter->feedback,v->shatter->feedback_ctx);
    else if (target==0x437c70u || target==0x438970u) status=frame_tank_hit_values(v->read,v->write,v->ctx,entity,v->contact,v->shatter->feedback,v->shatter->feedback_ctx);
    else return -5;
    if (!status) { *out=target==0x438970u ? entity : 0u; }return status;
}
static int impact_pair(void *ctx,uint32_t target,uint32_t entity,uint32_t payload,uint32_t kind,uint32_t *out)
{
    impact_values *v=(impact_values *)ctx;int status;(void)payload;
    if (target==0x403de0u) { status=frame_ai_hit_pair(v->read,v->write,v->ctx,entity,payload,kind,impact_pair,v);if (!status) *out=0;return status; }
    if (target!=0x438540u) return -5;
    status=frame_shatter_player_impulse_values(v->read,v->write,v->ctx,entity,v->origin,kind,v->shatter->feedback,v->shatter->feedback_ctx);if (!status) *out=0;return status;
}
static int near_origin(physics_memory *m,uint32_t o,const int32_t origin[3],int32_t radius)
{ return pm_abs(pm_sub(pm_i32(m,o),origin[0]))<radius && pm_abs(pm_sub(pm_i32(m,o+8u),origin[2]))<radius; }
static int shake_player(frame_data_read read,frame_data_write write,void *ctx,physics_memory *m,uint32_t tank,uint32_t amplitude,uint32_t duration)
{
    uint32_t slot=pm_load(m,tank+0xdf0u,1),camera;if (slot&128u) slot|=0xffffff00u;camera=pm_load(m,0x53cc38u+slot*4u,4);if (m->error) return m->error;return frame_camera_shake(read,write,ctx,camera,amplitude,duration);
}
int frame_projectile_impact(frame_data_read read,frame_data_write write,void *ctx,uint32_t o,const int32_t origin[3],const uint32_t *contact,uint32_t kind,const frame_entity_factory_services *e,const frame_shatter_services *shatter,const frame_explosion_services *explosion)
{
    physics_memory m={read,write,ctx,0};impact_values values={read,write,ctx,contact,origin,shatter,e};uint32_t i,id,tank,owner,type,mode,unused,d,slot;int32_t distance,radius,amplitude;int status;
    if (!read || !write || !origin || !e || !shatter) return -1;
    if (pm_load(&m,0x58e7acu,4)) return m.error ? m.error : -5;
    kind=kind==1u ? 1u : kind==2u ? 6u : kind==4u ? 2u : kind==5u ? 5u : 0u;
    TRY(frame_explosion_spawn_native(read,write,ctx,origin,contact,kind,e,explosion,&unused));pm_store(&m,o+0x84u,pm_load(&m,o+0x84u,4)|1u,4);owner=pm_load(&m,o+0x78u,4);
    frame_entity_sound(e,0x40f720u,0,0,origin);
    TRY(frame_ai_on_blast(read,write,ctx,origin,owner));
    type=pm_load(&m,o+0x7cu,4);
    if (type==1u || type==4u) {
        for (i=0;i<2u;i++) {
            id=pm_load(&m,0x53e8b8u+i*4u,4);if (!id) continue;tank=object_of(&m,id);mode=pm_load(&m,0x52ee9cu,4);radius=type==4u || mode==2u || mode==3u ? 81920 : 163840;
            distance=physics_length2_approx(pm_sub(pm_i32(&m,tank),origin[0]),pm_sub(pm_i32(&m,tank+8u),origin[2]));if (distance>=radius) continue;
            slot=pm_load(&m,tank+0xdf0u,1);if (slot&128u) slot|=0xffffff00u;pm_store(&m,o+0x489u+slot,1,1);pm_store(&m,o+0x84u,pm_load(&m,o+0x84u,4)|2u,4);
            amplitude=pm_mul(pm_sub(type==4u ? 81920 : 163840,distance),type==4u ? 128 : 255)/(type==4u ? 81920 : 163840);pm_store(&m,o+0x48bu,(uint32_t)amplitude,2);
        }
        for (i=0;i<9u;i++) {
            id=pm_load(&m,0x53e8e0u+i*4u,4);if (!id || id==pm_load(&m,o+0x78u,4)) continue;tank=object_of(&m,id);mode=pm_load(&m,0x52ee9cu,4);
            if (type==4u) { if (!near_origin(&m,tank,origin,20480) || pm_i32(&m,tank+0xdafu)>0) continue; }
            else if (!((mode==2u || mode==3u) && near_origin(&m,tank,origin,20480) && pm_i32(&m,tank+0xdafu)<1)) { if (!near_origin(&m,tank,origin,30720) || pm_i32(&m,tank+0xdafu)>0) continue; }
            d=pm_load(&m,0x545f14u,4);TRY(frame_entity_dispatch_pair(read,write,ctx,id,d,0,pm_load(&m,o+0x7cu,4),impact_pair,&values,&unused));
            if (pm_load(&m,tank+0xde5u,1)) TRY(shake_player(read,write,ctx,&m,tank,450,type==4u ? 300u : 350u));
        }
    }
    if (contact) {
        id=contact[18];tank=object_of(&m,id);if (pm_i32(&m,tank+0xdafu)>0) return m.error;type=pm_load(&m,o+0x7cu,4);
        if (type!=0u && type!=2u && type!=5u) return m.error;
        d=pm_load(&m,type==0u ? 0x545f08u : type==2u ? 0x545f1cu : 0x545f20u,4);TRY(frame_entity_dispatch(read,write,ctx,id,d,1,0,impact_contact,&values,&unused));
        if (pm_load(&m,tank+0xde5u,1)) TRY(shake_player(read,write,ctx,&m,tank,type==5u ? 300u : 400u,type==0u ? 100u : type==2u ? 500u : 50u));
    }
    return m.error;
}
int frame_projectile_ground_mark(frame_data_read read,frame_data_write write,void *ctx,const int32_t origin[3])
{
    physics_memory m={read,write,ctx,0};physics_terrain_hit first={0},last={0};int32_t vertices[12],average,x0,x1,z0,z1;uint32_t root,head,id,index;int status;
    static const unsigned char style[12]={255,128,128,128,1,1,63,1,63,63,1,63};
    if (!read || !write || !origin) return -1;
    root=pm_load(&m,0x55ae60u,4);x0=pm_sub(origin[0],1024);x1=pm_add(origin[0],1024);z0=pm_sub(origin[2],1024);z1=pm_add(origin[2],1024);if (m.error) return m.error;
    status=physics_terrain_query(read,ctx,root,x0,z0,&first);if (status<0) return status;if (!status || (first.flags_word&16u)) return 0;
    status=physics_terrain_query(read,ctx,root,x1,z1,&last);if (status<0) return status;if (!status || (last.flags_word&16u)) return 0;
    average=pm_trunc(pm_add(first.height,last.height),1);
    vertices[0]=x0;vertices[1]=first.height;vertices[2]=z0;vertices[3]=x1;vertices[4]=average;vertices[5]=z0;vertices[6]=x1;vertices[7]=last.height;vertices[8]=z1;vertices[9]=x0;vertices[10]=average;vertices[11]=z1;
    if (pm_load(&m,0x58f3c4u,4)==24u) {
        head=pm_load(&m,0x58f3c0u,4);id=pm_load(&m,0x58f3e0u+head,1);TRY(frame_light_release(read,write,ctx,id));head=pm_load(&m,0x58f3c0u,4)+1u;pm_store(&m,0x58f3c0u,head,4);if (head==24u) pm_store(&m,0x58f3c0u,0,4);pm_store(&m,0x58f3c4u,pm_load(&m,0x58f3c4u,4)-1u,4);
    }
    TRY(frame_light_allocate_values(read,write,ctx,vertices,pm_load(&m,0x545ea0u,4),style,&id));if (id!=UINT32_MAX) {
        index=pm_load(&m,0x58f3f8u,4);pm_store(&m,0x58f3e0u+index,id,1);index++;pm_store(&m,0x58f3f8u,index,4);if (index==24u) pm_store(&m,0x58f3f8u,0,4);pm_store(&m,0x58f3c4u,pm_load(&m,0x58f3c4u,4)+1u,4);
    }
    return m.error;
}
static int steer(physics_memory *m,uint32_t o,uint32_t target,uint32_t matrix,int32_t bias,const physics_trig *trig)
{
    int32_t x,z,a,b,angle,limit;int status;
    x=pm_sub(pm_i32(m,target),pm_i32(m,o));z=pm_sub(pm_i32(m,target+8u),pm_i32(m,o+8u));
    a=pm_add(pm_sar(pm_add(pm_mul(x,pm_i32(m,matrix)),pm_mul(z,pm_i32(m,matrix+8u))),12),bias);b=pm_sar(pm_add(pm_mul(x,pm_i32(m,matrix+24u)),pm_mul(z,pm_i32(m,matrix+32u))),12);
    if (pm_abs(a)>32768 || pm_abs(b)>32768) { a=pm_sar(a,1);b=pm_sar(b,1); }if (m->error) return m->error;
    status=physics_angle_lookup(a,b,trig,&angle);if (status) return status;angle=pm_neg(angle);limit=pm_i32(m,0x53170cu);if (angle<pm_neg(limit)) angle=pm_neg(limit);if (angle>limit) angle=limit;
    if (m->error) { return m->error; }status=frame_matrix_rotate(m->read,m->write,m->ctx,matrix,1,(uint32_t)angle,trig);if (status) return status;velocity_rotate(m,o+0x50u,1,(uint32_t)angle,trig);return m->error;
}
int frame_projectile_ground_initialize(frame_data_read read,frame_data_write write,void *ctx,frame_object_material_name material,void *material_ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t token;int status;if (!read || !write || !material) return -1;
    status=material(material_ctx,(const unsigned char *)"crater",7,1,&token);if (status) return status;pm_store(&m,0x545ea0u,token,4);pm_store(&m,0x58f3c0u,0,4);pm_store(&m,0x58f3f8u,0,4);pm_store(&m,0x58f3c4u,0,4);return m.error;
}
static uint32_t word(physics_memory *m,uint32_t at) { return pm_load(m,at,2); }
static void hit_coefficients(physics_memory *m,uint32_t kind,uint32_t tank,uint32_t out[25])
{
    uint32_t c[8],player=pm_load(m,tank+0xde5u,1),mode=pm_load(m,0x52ee9cu,4),i;
    if (kind==2u) {
        c[0]=player ? 1u : 2u;c[1]=0;
        if (!player) { c[2]=word(m,0x5319b0u);c[3]=word(m,0x545ff4u);c[4]=word(m,0x53198cu);c[5]=word(m,0x531990u);c[6]=word(m,0x5319a4u);c[7]=word(m,0x5319a8u); }
        else { c[4]=word(m,0x531984u);c[5]=word(m,0x531988u);c[2]=word(m,mode==4u || mode==5u ? 0x5319bcu : 0x5319acu);c[3]=word(m,mode==4u || mode==5u ? 0x545ff8u : 0x545ff0u);c[6]=word(m,mode==4u || mode==5u ? 0x5319b4u : 0x53199cu);c[7]=word(m,mode==4u || mode==5u ? 0x5319b8u : 0x5319a0u); }
    } else {
        c[2]=kind==0u ? word(m,player ? 0x531968u : 0x53196cu+pm_load(m,0x53d18cu,4)*2u) : 1u;c[3]=kind==0u ? word(m,player ? 0x545fe0u : 0x545fe8u+pm_load(m,0x53d18cu,4)*2u) : 0u;
        c[0]=c[2];c[1]=c[3];c[4]=word(m,!player ? 0x53198cu : kind==0u && (mode==4u || mode==5u) ? 0x531994u : 0x531984u);c[5]=word(m,!player ? 0x531990u : kind==0u && (mode==4u || mode==5u) ? 0x531998u : 0x531988u);
        c[6]=word(m,player ? 0x531974u : 0x53197cu);c[7]=word(m,player ? 0x531978u : 0x531980u);
    }
    for (i=0;i<4u;i++) out[19u+i]=c[i*2u]|(c[i*2u+1u]<<16);
}
int frame_projectile_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e,const physics_trig *trig,const frame_shatter_services *shatter,const frame_explosion_services *explosion)
{
    physics_memory m={read,write,ctx,0};uint32_t o,type,flags,matrix,i,angles[3],frame,dt,owner,target,ufo,unused,head,record[25],have_height=0;int32_t gravity,height=0,pitch,yaw,a,b,v[3],position[3],old[3],value;frame_projectile_hit hit;int status;
    if (!read || !write || !e || !e->work_limit || !trig || !shatter) return -1;
    if (pm_load(&m,0x58e7acu,4)) return m.error ? m.error : -5;
    o=object_of(&m,entity);type=pm_load(&m,o+0x7cu,4);gravity=pm_i32(&m,0x5316e8u);matrix=pm_load(&m,o+20u,4);
    if (type==1u) { TRY(frame_projectile_uv_tick(read,write,ctx,o));gravity=pm_i32(&m,0x5316ecu); }
    else if (type==4u) {
        if (pm_load(&m,o+0x84u,1)&16u) {
            TRY(frame_matrix_rotate(read,write,ctx,matrix,0,pm_load(&m,0x5904fcu,4)<<((entity&3u)+2u),trig));TRY(frame_matrix_rotate(read,write,ctx,matrix,2,pm_load(&m,0x5904fcu,4)<<(((entity>>2)&3u)+1u),trig));
            if ((entity^pm_load(&m,0x590564u,4))&7u) TRY(frame_matrix_normalize(read,write,ctx,matrix));
        } else {
            TRY(physics_angle_lookup(pm_i32(&m,o+0x54u),pm_i32(&m,o+0x50u),trig,&pitch));TRY(physics_angle_lookup(pm_i32(&m,o+0x50u),pm_i32(&m,o+0x58u),trig,&yaw));angles[0]=(uint32_t)pitch;angles[1]=(uint32_t)pm_neg(yaw);angles[2]=0;TRY(frame_matrix_from_angles(read,write,ctx,matrix,angles,trig));
        }
    }
    if (type==0u || type==4u || type==5u) pm_store(&m,o+0xb0u,pm_load(&m,o+0xb0u,2)+(uint32_t)pm_sar(pm_mul(pm_i32(&m,0x531700u),pm_i32(&m,0x5904fcu)),8),2);
    if (type==4u && (pm_load(&m,o+0x84u,1)&16u)) gravity=pm_sar(pm_i32(&m,0x5316e8u),2);
    if (type==5u) { target=object_of(&m,pm_load(&m,o+0x48du,4));if (target) { if (m.error) return m.error;status=steer(&m,o,target,matrix,0,trig);if (status) return status; } }
    flags=pm_load(&m,o+0x84u,4);
    if (flags&3u) {
        if (flags&2u) TRY(frame_projectile_flash_tick(read,write,ctx,o));
        if (!pm_load(&m,o+0x481u,2) && ((pm_load(&m,o+0x7cu,4)!=1u && pm_load(&m,o+0x7cu,4)!=4u) || !(pm_load(&m,o+0x84u,1)&2u))) {
            TRY(frame_entity_dispatch(read,write,ctx,entity,pm_load(&m,0x545ed0u,4),0,0,e->method,e->method_ctx,&unused));head=pm_load(&m,0x54b488u,4);TRY(frame_list_remove_value(read,write,ctx,entity,head,e->release,e->system_ctx,e->work_limit));
        }
        TRY(frame_projectile_trail_tick(read,write,ctx,o));return m.error;
    }
    for (i=0;i<3u;i++) old[i]=pm_i32(&m,o+i*4u);
    if (!(flags&4u)) {
        pm_write32(&m,o,pm_add(pm_i32(&m,o),pm_sar(pm_mul(pm_i32(&m,0x5904fcu),pm_i32(&m,o+0x50u)),8)));pm_write32(&m,o+8u,pm_add(pm_i32(&m,o+8u),pm_sar(pm_mul(pm_i32(&m,o+0x58u),pm_i32(&m,0x5904fcu)),8)));
        if (pm_load(&m,o+0x7cu,4)==2u) {
            frame=pm_load(&m,0x590564u,4);
            if (!((entity^frame)&1u) || !pm_load(&m,o+0x80u,4)) {
                if (m.error) { return m.error; }status=physics_terrain_height(read,ctx,pm_load(&m,0x55ae60u,4),pm_i32(&m,o),pm_i32(&m,o+8u),&height);if (status<0) return status;have_height=1;
                if (height!=-2147483647) { pm_write32(&m,o+0x491u,pm_add(height,2048));pm_write32(&m,o+4u,pm_add(pm_sar(pm_sub(pm_add(height,2048),pm_i32(&m,o+4u)),1),pm_i32(&m,o+4u))); }
            } else { height=pm_i32(&m,o+0x491u);have_height=1;pm_write32(&m,o+4u,height); }
            if (pm_i32(&m,o+4u)!=pm_i32(&m,o+0x499u)) {
                a=physics_length2_approx(pm_sub(pm_i32(&m,o),pm_i32(&m,o+0x495u)),pm_sub(pm_i32(&m,o+8u),pm_i32(&m,o+0x49du)));TRY(physics_angle_lookup(pm_sub(pm_i32(&m,o+0x499u),pm_i32(&m,o+4u)),a,trig,&pitch));pitch=pm_neg(pitch);
                TRY(frame_matrix_rotate(read,write,ctx,o+0x4a1u,0,(uint32_t)pm_sub(pitch,pm_i32(&m,o+0x4d1u)),trig));pm_write32(&m,o+0x4d1u,pitch);
            }
            for (i=0;i<3u;i++) pm_store(&m,o+0x495u+i*4u,pm_load(&m,o+i*4u,4),4);
            if (pm_load(&m,0x545dd8u,4)!=pm_load(&m,0x590564u,4)) {
                dt=pm_load(&m,0x5904fcu,4);a=pm_i32(&m,0x545da0u);
                if (pm_load(&m,0x5317c8u,4)) { a=pm_add(a,pm_signed(dt<<4));if (a>=500) { a=500;pm_store(&m,0x5317c8u,0,4); } }
                else { a=pm_sub(a,pm_signed(dt<<4));if (a<=300) { a=300;pm_store(&m,0x5317c8u,1,4); } }
                if (a<0) { a=0; }pm_write32(&m,0x545da0u,a);pm_write32(&m,0x545da4u,a);a=(int32_t)pm_load(&m,0x545dadu,1);b=pm_sar(pm_signed(dt),1);
                if (pm_load(&m,0x5317ccu,4)) { a=pm_add(a,b);if (a>80) { pm_store(&m,0x545dadu,80,1);pm_store(&m,0x5317ccu,0,4); }else pm_store(&m,0x545dadu,(uint32_t)a,1); }
                else { a=pm_sub(a,b);if (a<0) { pm_store(&m,0x545dadu,0,1);pm_store(&m,0x5317ccu,1,4); }else pm_store(&m,0x545dadu,(uint32_t)a,1); }
            }
            pm_store(&m,0x545dd8u,pm_load(&m,0x590564u,4),4);a=pm_sar(pm_sub(pm_i32(&m,0x597380u+((pm_load(&m,o+0x80u,2)*48u)&65535u)/4u*4u),pm_i16(&m,o+0x4d7u)),5);pm_store(&m,o+0x4d7u,(uint32_t)a,2);
            b=pm_sar(pm_mul(a,pm_i32(&m,o+0x4a1u)),12);a=pm_sar(pm_mul(a,pm_i32(&m,o+0x4a9u)),12);pm_write32(&m,o,pm_add(pm_i32(&m,o),b));pm_write32(&m,o+8u,pm_add(pm_i32(&m,o+8u),a));
            if (pm_i32(&m,o+0x80u)<pm_i32(&m,0x531708u)) { target=object_of(&m,pm_load(&m,o+0x48du,4));if (target) { if (m.error) return m.error;status=steer(&m,o,target,o+0x4a1u,pm_i32(&m,0x545ddcu),trig);if (status) return status; } }
            for (i=0;i<9u;i++) pm_store(&m,o+24u+i*4u,pm_load(&m,o+0x4a1u+i*4u,4),4);
            pm_store(&m,o+0x4d5u,pm_load(&m,o+0x4d5u,2)+(uint32_t)pm_sar(pm_mul(pm_i32(&m,0x5904fcu),pm_i32(&m,0x5317c4u)),8),2);TRY(frame_matrix_rotate(read,write,ctx,pm_load(&m,o+20u,4),2,(uint32_t)pm_i16(&m,o+0x4d5u),trig));
        } else {
            a=pm_sub(pm_i32(&m,o+0x54u),pm_sar(pm_mul(gravity,pm_i32(&m,0x5904fcu)),8));pm_write32(&m,o+0x54u,a);pm_write32(&m,o+4u,pm_add(pm_i32(&m,o+4u),pm_sar(pm_mul(pm_i32(&m,0x5904fcu),a),8)));
        }
    } else {
        ufo=object_of(&m,pm_load(&m,o+0x4e1u,4));target=object_of(&m,pm_load(&m,ufo+0xc6u,4));matrix=pm_load(&m,target+20u,4);
        if (matrix) { for (i=0;i<3u;i++) v[i]=pm_i32(&m,o+0x4e5u+i*4u);transform(&m,matrix,v,position);for (i=0;i<3u;i++) pm_write32(&m,o+i*4u,position[i]); }
        for (i=0;i<3u;i++) { pm_write32(&m,o+i*4u,pm_add(pm_i32(&m,o+i*4u),pm_i32(&m,target+i*4u))); }if (pm_load(&m,ufo+0x78u,1)&2u) pm_store(&m,o+0x84u,pm_load(&m,o+0x84u,4)|1u,4);
    }
    TRY(frame_projectile_sweep(read,ctx,entity,old,&hit));
    if (!hit.entity) {
        if (!((entity^pm_load(&m,0x590564u,4))&3u)) {
            if (pm_load(&m,o+0x7cu,4)!=2u) { if (m.error) { return m.error; }status=physics_terrain_height(read,ctx,pm_load(&m,0x55ae60u,4),pm_i32(&m,o),pm_i32(&m,o+8u),&height);if (status<0) return status; }
            else if (!have_height) return m.error ? m.error : -4;
            if (height==-2147483647 && pm_load(&m,o+0x7cu,4)!=4u) { TRY(frame_projectile_impact(read,write,ctx,o,old,0,pm_load(&m,o+0x7cu,4),e,shatter,explosion));return m.error; }
            if (pm_i32(&m,o+4u)<height) { position[0]=old[0];position[1]=height;position[2]=old[2];if (pm_load(&m,o+0x7cu,4)!=4u) TRY(frame_projectile_ground_mark(read,write,ctx,position));position[1]=pm_add(height,1024);TRY(frame_projectile_impact(read,write,ctx,o,position,0,pm_load(&m,o+0x7cu,4),e,shatter,explosion));return m.error; }
        }
        value=pm_add(pm_i32(&m,o+0x80u),pm_i32(&m,0x5904fcu));pm_write32(&m,o+0x80u,value);if (value>6000) { pm_store(&m,o+0x84u,pm_load(&m,o+0x84u,4)|1u,4);return m.error; }
        TRY(frame_projectile_trail_emit(read,write,ctx,o));TRY(frame_projectile_trail_tick(read,write,ctx,o));return m.error;
    }
    target=object_of(&m,hit.entity);
    for (i=0;i<3u;i++) { record[i]=pm_load(&m,o+i*4u,4);record[3u+i]=pm_load(&m,o+0x50u+i*4u,4);record[6u+i]=(uint32_t)hit.normal[i];record[9u+i]=(uint32_t)hit.local_normal[i];record[12u+i]=(uint32_t)hit.point[i];record[15u+i]=(uint32_t)hit.local_point[i]; }
    record[18]=hit.entity;type=pm_load(&m,o+0x7cu,4);record[23]=type;record[24]=pm_load(&m,o+0x80u,4);hit_coefficients(&m,type,target,record);
    if (type==3u) {
        if (pm_load(&m,o+0x84u,1)&4u) return m.error;
        if (pm_i32(&m,target+0xdafu)<1 && !pm_load(&m,o+0x4e1u,4)) { TRY(frame_ufo_spawn_values(read,write,ctx,hit.entity,0,e,&ufo));pm_store(&m,o+0x4e1u,ufo,4);for (i=0;i<3u;i++) pm_store(&m,o+0x4e5u+i*4u,(uint32_t)hit.local_point[i],4);pm_store(&m,o+0x84u,pm_load(&m,o+0x84u,4)|4u,4);return m.error; }
    } else if (pm_i32(&m,target+0xdafu)>0 && type!=2u) { TRY(frame_projectile_ricochet(read,write,ctx,o,hit.entity,trig));return m.error; }
    owner=type==3u ? 3u : type;TRY(frame_projectile_impact(read,write,ctx,o,old,record,owner,e,shatter,explosion));return m.error;
}

int frame_projectile_emitter_spawn(frame_data_read read,frame_data_write write,void *ctx,uint32_t source,const frame_entity_factory_services *e,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t entity,o,value,i,unused;int status;
    if (!read || !write || !e || !e->work_limit || !out) return -1;
    if (pm_load(&m,0x58e7acu,4)) return m.error ? m.error : -5;
    value=pm_load(&m,0x545db4u,4);TRY(frame_entity_create(read,write,ctx,value,e,&entity));o=object_of(&m,entity);
    pm_store(&m,o+0x78u,0,4);pm_store(&m,o+0x7cu,4,4);pm_store(&m,o+0x80u,0,4);
    value=pm_load(&m,source+0xb4u,4);if (value) pm_store(&m,o+0x48u,value,4);
    pm_write32(&m,o+0x98u,pm_i8(&m,source+0xb3u));value=pm_load(&m,source+0xacu,4);pm_store(&m,o+0x88u,value ? value : pm_load(&m,0x58fa70u,4),4);
    pm_store(&m,o+0xa8u,0,1);pm_store(&m,o+0xa9u,0,1);pm_store(&m,o+0xaau,63,1);pm_store(&m,o+0xabu,63,1);
    pm_store(&m,o+0xa0u,pm_load(&m,source+0xb8u,4),4);pm_store(&m,o+0xa4u,pm_load(&m,source+0xb8u,4),4);
    for (i=0;i<3u;i++) pm_store(&m,o+0xacu+i,pm_load(&m,source+0xb0u+i,1),1);
    for (i=0;i<3u;i++) pm_store(&m,o+0x4e5u+i*4u,pm_load(&m,source+0xbcu+i*4u,4),4);
    TRY(frame_entity_activate(read,write,ctx,entity,e->method,e->method_ctx,&unused));
    value=pm_load(&m,0x54b48cu,4);TRY(frame_list_remove_value(read,write,ctx,entity,value,e->release,e->system_ctx,e->work_limit));
    value=pm_load(&m,0x54b488u,4);TRY(frame_list_append(read,write,ctx,entity,value,e->allocate,e->release,e->system_ctx,e->work_limit,&unused));
    *out=entity;return m.error;
}
int frame_projectile_emitter_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t o,active=0,kind,i,subject,p,id,matrix,angles[3]={0xffffc004u,0,0};int status;int32_t v[3],r[3];
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,entity);kind=pm_load(&m,o+0x94u,4);
    if (!kind) { TRY(frame_entity_near_camera(read,ctx,o,0,19200,&active));if (!active) pm_store(&m,o+0x7cu,0,4); }
    else if (kind==1u) {
        for (i=0;i<2u;i++) {
            subject=object_of(&m,pm_load(&m,0x53e8b8u+i*4u,4));if (!subject) continue;
            active=pm_i32(&m,o+0x98u)<pm_i32(&m,subject) && pm_i32(&m,subject)<pm_i32(&m,o+0xa0u) && pm_i32(&m,o+0x9cu)<pm_i32(&m,subject+8u) && pm_i32(&m,subject+8u)<pm_i32(&m,o+0xa4u);
            if (!active) pm_store(&m,o+0x7cu,0,4);
        }
    }
    if (!active) return m.error;
    kind=pm_load(&m,o+0x80u,4);if (kind>1u) return m.error;
    if (pm_i32(&m,o+0x7cu)>=pm_i32(&m,0x5904fcu)) { pm_write32(&m,o+0x7cu,pm_sub(pm_i32(&m,o+0x7cu),pm_i32(&m,0x5904fcu)));return m.error; }
    pm_store(&m,o+0x7cu,((random_next(&m)>>4)&2047u)+pm_load(&m,o+0xa8u,4),4);
    TRY(frame_projectile_emitter_spawn(read,write,ctx,o,e,&id));p=object_of(&m,id);
    if (!kind) {
        if (pm_load(&m,o+0xc8u,4)) { pm_store(&m,p+0x50u,2097,4);pm_store(&m,p+0x84u,pm_load(&m,p+0x84u,4)|16u,4); }
        else pm_store(&m,p+0x50u,4194,4);
        matrix=pm_load(&m,p+20u,4);pm_store(&m,p+0x58u,0,4);pm_store(&m,p+0x54u,0,4);TRY(frame_matrix_from_angles(read,write,ctx,matrix,angles,trig));
    } else {
        pm_store(&m,p+0x50u,0,4);pm_store(&m,p+0x54u,0,4);pm_store(&m,p+0x58u,5242,4);
        for (i=0;i<12u;i++) pm_store(&m,p+24u+i*4u,pm_load(&m,o+24u+i*4u,4),4);
        matrix=pm_load(&m,p+20u,4);TRY(frame_matrix_rotate(read,write,ctx,matrix,0,pm_load(&m,o+0x90u,4),trig));
        for (i=0;i<3u;i++) v[i]=pm_i32(&m,p+0x50u+i*4u);
        transform(&m,matrix,v,r);for (i=0;i<3u;i++) pm_write32(&m,p+0x50u+i*4u,r[i]);
    }
    for (i=0;i<3u;i++) pm_write32(&m,p+i*4u,pm_add(pm_i32(&m,o+0x84u+i*4u),pm_i32(&m,o+i*4u)));
    frame_entity_sound(e,0x40f5b0u,o,9u,0);
    return m.error;
}

static int emitter_name_equal(const unsigned char *name,const char *literal)
{
    uint32_t i,a,b;for (i=0;i<64u;i++) { a=name[i];b=(unsigned char)literal[i];if (a>=65u && a<=90u) a+=32u;if (b>=65u && b<=90u) b+=32u;if (a!=b) return 0;if (!a) return 1; }return 0;
}
/* Same x87 binary32 * exact1024 -> signed64 FISTP -> low32 convention as
 * world positions. Integer decomposition avoids host rounding-mode coupling. */
int frame_projectile_emitter_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *e,const frame_object_parameters_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t o,parent,unused,found,more,value,i,length,bits,box[4];int32_t v[3],r[3];int status;unsigned char name[64];
#define FIND_EMITTER(name) frame_text_find_name(read,write,ctx,parser,(const unsigned char *)(name),(uint32_t)sizeof(name),e->work_limit,&found)
#define TOKEN_EMITTER() frame_text_next(read,write,ctx,parser,name,sizeof(name),e->work_limit,&more)
#define INTEGER_EMITTER(at) frame_text_read_i32(read,write,ctx,parser,(at),e->work_limit,&more)
    if (!read || !write || !e || !e->work_limit || !s) return -1;
    o=object_of(&m,entity);parent=pm_load(&m,0x53e4b0u,4);TRY(frame_class_dispatch_payload(read,ctx,parent,entity,parser,e->method,e->method_ctx,&unused));
    value=pm_load(&m,0x54b48cu,4);TRY(frame_list_remove_value(read,write,ctx,entity,value,e->release,e->system_ctx,e->work_limit));
    if (pm_load(&m,0x58e7acu,4) && !pm_load(&m,0x58e790u,4)) { value=pm_load(&m,0x545ed0u,4);TRY(frame_entity_dispatch(read,write,ctx,entity,value,0,0,e->method,e->method_ctx,&unused));return m.error; }
    pm_store(&m,o+0x7cu,(random_next(&m)>>4)&1023u,4);
    TRY(FIND_EMITTER(":bomber_type"));
    if (!found) { pm_store(&m,o+0x80u,0,4);pm_store(&m,o+0xb8u,4000,4); }
    else { TRY(TOKEN_EMITTER());if (emitter_name_equal(name,"GUN")) { pm_store(&m,o+0x80u,1,4);pm_store(&m,o+0xb8u,2000,4); }else if (emitter_name_equal(name,"SKY")) { pm_store(&m,o+0x80u,0,4);pm_store(&m,o+0xb8u,3000,4); } }
    TRY(FIND_EMITTER(":size"));if (found) { bits=0;TRY(frame_text_read_f32_value(read,write,ctx,parser,e->work_limit,&bits,&more));if (!more) return -4;pm_store(&m,o+0xb8u,frame_text_fixed1024(bits),4); }
    TRY(FIND_EMITTER(":offset"));
    if (found) {
        for (i=0;i<3u;i++) { TRY(INTEGER_EMITTER(o+0x84u+i*4u)); }
        if (pm_load(&m,o+0x80u,4)==1u) { for (i=0;i<3u;i++) v[i]=pm_i32(&m,o+0x84u+i*4u);transform(&m,pm_load(&m,o+20u,4),v,r);for (i=0;i<3u;i++) pm_write32(&m,o+0x84u+i*4u,r[i]); }
    } else { pm_store(&m,o+0x88u,76800,4);pm_store(&m,o+0x8cu,0,4);pm_store(&m,o+0x84u,(uint32_t)-59392,4); }
    TRY(FIND_EMITTER(":x_angle"));if (found) { TRY(INTEGER_EMITTER(o+0x90u));pm_store(&m,o+0x90u,pm_load(&m,o+0x90u,4)*182u,4); }
    TRY(FIND_EMITTER(":trigger_type"));
    if (!found) pm_store(&m,o+0x94u,0,4);
    else {
        TRY(TOKEN_EMITTER());if (emitter_name_equal(name,"VIEW")) pm_store(&m,o+0x94u,0,4);
        else if (emitter_name_equal(name,"BOX")) {
            pm_store(&m,o+0x94u,1,4);
            for (i=0;i<4u;i++) { bits=0;TRY(frame_text_read_f32_value(read,write,ctx,parser,e->work_limit,&bits,&more));if (!more) return -4;box[i]=frame_text_fixed1024(bits); }
            pm_write32(&m,o+0xa0u,pm_add(pm_signed(box[0]),pm_sar(pm_signed(box[2]),1)));pm_write32(&m,o+0x98u,pm_sub(pm_signed(box[0]),pm_sar(pm_signed(box[2]),1)));
            pm_write32(&m,o+0x9cu,pm_sub(pm_signed(box[1]),pm_sar(pm_signed(box[3]),1)));pm_write32(&m,o+0xa4u,pm_add(pm_signed(box[1]),pm_sar(pm_signed(box[3]),1)));
        }
    }
    TRY(FIND_EMITTER(":bomb_times"));if (found) { TRY(INTEGER_EMITTER(o+0xa8u));pm_store(&m,o+0xa8u,pm_load(&m,o+0xa8u,4)*1000u,4); }else pm_store(&m,o+0xa8u,2500,4);
    TRY(FIND_EMITTER(":bomb_shape"));if (found) { TRY(TOKEN_EMITTER());for (length=0;length<sizeof(name) && name[length];length++) { }if (length==sizeof(name)) return -3;TRY(frame_shape_load_name(read,write,ctx,name,length+1u,pm_load(&m,0x55ae40u,4),e,s->shapes,&value));pm_store(&m,o+0xb4u,value,4); }
    TRY(FIND_EMITTER(":bomb_tex"));if (found) {
        TRY(TOKEN_EMITTER());if (!s->material) return -5;for (length=0;length<sizeof(name) && name[length];length++) { }if (length==sizeof(name)) return -3;
        TRY(s->material(s->material_ctx,name,length+1u,2,&value));pm_store(&m,o+0xacu,value,4);
    }
    TRY(FIND_EMITTER(":bomb_col"));if (found) {
        value=o+0x84u;for (i=0;i<3u;i++) { TRY(frame_text_read_value(read,write,ctx,parser,e->work_limit,&value,&more));pm_store(&m,o+0xb0u+i,value,1); }
    } else { pm_store(&m,o+0xb0u,128,1);pm_store(&m,o+0xb2u,50,1);pm_store(&m,o+0xb1u,50,1); }
    TRY(FIND_EMITTER(":bomb_alpha"));if (!found) pm_store(&m,o+0xb3u,1,1);
    else { TRY(TOKEN_EMITTER());if (emitter_name_equal(name,"NONE")) pm_store(&m,o+0xb3u,255,1);else if (emitter_name_equal(name,"SUB")) pm_store(&m,o+0xb3u,0,1);else if (emitter_name_equal(name,"ADD")) pm_store(&m,o+0xb3u,1,1); }
    TRY(FIND_EMITTER(":bomb_glow_offset"));if (found) { for (i=0;i<3u;i++) { TRY(INTEGER_EMITTER(o+0xbcu+i*4u)); } }else { pm_store(&m,o+0xc4u,0,4);pm_store(&m,o+0xc0u,0,4);pm_store(&m,o+0xbcu,0,4); }
    TRY(FIND_EMITTER(":asteroid"));if (found) { pm_store(&m,o+0xc8u,1,4);pm_store(&m,o+0x84u,(uint32_t)-37888,4);pm_store(&m,o+0x88u,30720,4);pm_store(&m,o+0x8cu,0,4); }
    return m.error;
#undef FIND_EMITTER
#undef TOKEN_EMITTER
#undef INTEGER_EMITTER
}
