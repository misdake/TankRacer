#include "frame_ai.h"
#include "frame_player_lifecycle.h"
#include "physics_memory.h"
#include "physics_vector.h"
#include "frame_progress.h"
#include "frame_shatter.h"
#include "frame_mine.h"
#include "frame_lap_events.h"
#include "frame_scene_effects.h"
#include "physics_pose.h"
#include "frame_file.h"
#include "frame_tank_model.h"
#include <string.h>
static uint32_t random_next(physics_memory *m);

static uint32_t object_of(physics_memory *m,uint32_t id)
{ uint32_t r=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);return r ? pm_load(m,r+4u,4) : 0; }
static uint32_t route_next(physics_memory *m,uint32_t o,uint32_t route,uint32_t branch)
{ return object_of(m,pm_load(m,object_of(m,pm_load(m,o+route,4))+0x78u+pm_load(m,o+branch,4)*4u,4)); }
static int divide(physics_memory *m,int32_t a,int32_t b,int32_t *out)
{ if (m->error) return m->error;if (!b || (a==INT32_MIN && b==-1)) return -2;*out=a/b;return 0; }
static int xz(physics_memory *m,uint32_t matrix,int32_t *x,int32_t *z,int inverse)
{
    int32_t a=*x,b=*z;if (!matrix) return m->error ? m->error : -4;
    *x=pm_sar(pm_add(pm_mul(a,pm_i32(m,matrix)),pm_mul(b,pm_i32(m,matrix+(inverse ? 8u : 24u)))),12);
    *z=pm_sar(pm_add(pm_mul(a,pm_i32(m,matrix+(inverse ? 24u : 8u))),pm_mul(b,pm_i32(m,matrix+32u))),12);return m->error;
}
static int angle(int32_t x,int32_t z,const physics_trig *trig,int32_t *out)
{ uint32_t i=0;while (pm_abs(x)>=32768 || pm_abs(z)>=32768) { if (i++>=32u) return -3;x=pm_sar(x,1);z=pm_sar(z,1); }return physics_angle_lookup(x,z,trig,out); }
static int32_t scaled(physics_memory *m,int32_t v,uint32_t at)
{ return pm_sar(pm_mul(v,pm_i32(m,at)),pm_load(m,at+4u,1)); }
static int32_t damp(int32_t value,uint32_t packed,int32_t dt)
{
    int32_t force=(int32_t)(packed&65535u),step;if (force>32767) force-=65536;step=pm_mul(force,dt);
    if (step<value) value=pm_sub(value,step);else if (value<pm_neg(step)) value=pm_add(value,step);else value=0;
    if (value) { int negative=value<0;value=pm_sub(value,pm_sar(pm_mul(value,dt),packed>>24));if ((negative && value>0) || (!negative && value<0)) value=0; }return value;
}
struct ai_config_field { const char *name;uint32_t count,offset[27],kind[27]; };
#include "frame_ai_config_schema.h"
int frame_ai_constants(frame_data_write write,void *ctx)
{
    physics_memory m={0,write,ctx,0};uint32_t i;
    static const uint32_t ice[8]={0xfffbb41fu,0,0x15eccu,0,0xfffcc0afu,0,0x57c8u,0};
    static const unsigned char selection[88]={7,5,4,0,85,90,95,100,80,90,95,100,70,90,95,100,60,80,90,100,50,70,80,100,40,70,80,100,20,60,80,100,1,2,5,100,3,6,10,100,10,20,30,100,10,20,40,100,10,30,50,100,10,30,60,100,40,70,90,100,1,96,98,100,2,92,95,100,10,80,95,100,10,70,80,100,10,60,70,100,20,60,90,100,30,50,90,100};
    if (!write) return -1;
    if (!write(ctx,0x52e0ccu,selection,sizeof(selection))) return -1;
    for (i=0;i<8u;i++) pm_store(&m,0x531c28u+i*4u,ice[i],4);
    return m.error;
}
static int same_key(const unsigned char *a,const char *b)
{
    uint32_t i,x,y;for (i=0;i<32u;i++) { x=a[i];y=(unsigned char)b[i];if (x>='A' && x<='Z') x+=32;if (y>='A' && y<='Z') y+=32;if (x!=y) return 0;if (!x) return 1; }return 0;
}
int frame_ai_configure_span(frame_data_read read,frame_data_write write,void *ctx,uint32_t index,const unsigned char *bytes,uint32_t length,uint32_t limit,uint32_t *consumed)
{
    physics_memory m={read,write,ctx,0};frame_file f={bytes,length,0,limit};unsigned char key[32];uint32_t i,j,count,at,value,present,used=0;const struct ai_config_field *q;int status;float scalar;double product;
    if (!read || !write || index>=15u || (!bytes && length) || !limit || !consumed) return -1;
    for (;;) {
        if (used++>=limit) return -3;
        status=frame_file_name(&f,key,25,1,&present);if (status) return status;if (!present) break;q=0;
        for (i=0;i<33u;i++) if (same_key(key,ai_config_fields[i].name)) q=&ai_config_fields[i];
        if (!q) continue;
        count=q->count;if (q==&ai_config_fields[0] || q==&ai_config_fields[1]) count+=index/5u;
        for (j=0;j<count;j++) {
            uint32_t kind=q->kind[j<q->count ? j : q->count-1u],offset=j<q->count ? q->offset[j] : q->offset[q->count-1u]+(j-q->count+1u)*4u;
            at=0x53b6a0u+index*364u+offset;
            if (kind==5u) {
                value=0;status=frame_file_real32(&f,&value,&present);if (status) return status;if (!present) return -4;memcpy(&scalar,&value,4);
                product=(double)scalar*(q==&ai_config_fields[5] || q==&ai_config_fields[6] ? 1024.0 : 1000.0);
                if (product!=product || product>=9223372036854775808.0 || product< -9223372036854775808.0) value=0;else value=(uint32_t)(int64_t)product;
                pm_store(&m,at,value,4);
            } else {
                value=pm_load(&m,at,kind);status=frame_file_integer(&f,&value,&present);if (status) return status;pm_store(&m,at,value,kind);
                if (offset==0x11cu || offset==0x130u) pm_store(&m,at,value<<10,4);
                if (offset==0x144u || offset==0x158u) pm_write32(&m,at,pm_mul(pm_signed(value),182));
            }
            if (m.error) return m.error;
        }
    }*consumed=f.cursor;return m.error;
}
static uint32_t crt_random(physics_memory *m)
{ uint32_t value=pm_load(m,0x538820u,4)*214013u+2531011u;pm_store(m,0x538820u,value,4);return (value>>16)&32767u; }
int frame_ai_selection_reset(frame_data_read read,frame_data_write write,void *ctx,uint32_t seed)
{
    physics_memory m={read,write,ctx,0};uint32_t i;int32_t category;if (!read || !write) return -1;
    for (i=0;i<8u;i++) pm_store(&m,0x53b678u+i*4u,0,4);
    pm_store(&m,0x53b670u,0,4);category=pm_sub(pm_i32(&m,0x530440u),1)%5;
    if (category==4) category=pm_i32(&m,0x52f2d8u+pm_load(&m,0x53d18cu,4)*4u);
    pm_store(&m,0x53b678u+(uint32_t)category*8u,1,4);pm_store(&m,0x538820u,seed,4);return m.error;
}
int frame_ai_select_vehicle(frame_data_read read,frame_data_write write,void *ctx,uint32_t limit,uint32_t *vehicle,uint32_t *colour)
{
    physics_memory m={read,write,ctx,0};uint32_t row,tries,i,j,value,at;if (!read || !write || !limit || !vehicle || !colour) return -1;
    row=pm_load(&m,0x53d18cu,4)*7u+pm_load(&m,0x53b670u,4);
    for (tries=0;tries<limit;tries++) {
        value=crt_random(&m)%100u;
        for (i=0;i<4u;i++) if (value<pm_load(&m,0x52e0d0u+row*4u+i,1) || tries>100u) {
            for (j=0;j<2u;j++) { at=0x53b678u+(i*2u+j)*4u;if (!pm_load(&m,at,4)) { pm_store(&m,at,1,4);if (m.error) return m.error;*vehicle=pm_load(&m,0x53d18cu,4)*5u+1u+i;*colour=j;pm_store(&m,0x53b670u,pm_load(&m,0x53b670u,4)+1u,4);return m.error; } }
            break;
        }
    }return m.error ? m.error : -3;
}
/* Native filename replaces the original constructor's local stack string.
 * Resolve the actual virtual method and retain its current-slot save/restore;
 * only the registered4350A0 implementation is accepted for this span ABI. */
static int vehicle_model(physics_memory *m,uint32_t entity,uint32_t vehicle,const frame_entity_factory_services *e,const frame_shape_resources *r)
{
    unsigned char name[16]={'t','a','n','k'};const unsigned char *bytes;uint32_t n=4,i,found,length,descriptor,slot,saved,record,klass,target,unused;int status;
    if (vehicle>=10u) name[n++]='1';
    name[n++]=(unsigned char)('0'+vehicle%10u);for (i=0;i<5u;i++) name[n++]=(unsigned char)".dat"[i];
    descriptor=pm_load(m,0x545ed8u,4);slot=pm_load(m,descriptor+28u,4);saved=pm_load(m,0x55c698u,4);pm_store(m,0x55c698u,slot,4);
    record=pm_load(m,pm_load(m,0x55c694u,4)+entity*8u,4);klass=pm_load(m,record,4);
    if (pm_signed(slot)>pm_i32(m,klass+28u)) return m->error;
    target=pm_load(m,pm_load(m,klass+36u,4)+slot*4u,4);
    if (m->error) return m->error;
    if (target) {
        if (target!=0x4350a0u || !r || !r->fetch) return -5;
        status=r->fetch(r->file_ctx,pm_load(m,0x53d188u,4),name,n,&bytes,&length,&found);if (status) return status;if (!found) return -4;
        status=frame_tank_model_load_span(m->read,m->write,m->ctx,entity,bytes,length,e,r,0,&unused);if (status) return status;
    }
    pm_store(m,0x55c698u,saved,4);return m->error;
}
int frame_ai_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *e,const frame_shape_resources *resources)
{
    physics_memory m={read,write,ctx,0};uint32_t o,vehicle,colour,config,i,seed,unused,count;int32_t span;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,entity);if (m.error) return m.error;
    status=frame_class_dispatch_payload(read,ctx,pm_load(&m,0x545fdcu,4),entity,parser,e->method,e->method_ctx,&unused);if (status) return status;
    if (!o) return -4;
    status=frame_ai_select_vehicle(read,write,ctx,e->work_limit,&vehicle,&colour);if (status) return status;
    pm_store(&m,o+0xddfu,vehicle,1);pm_store(&m,o+0xde0u,colour,1);if (m.error) return m.error;
    status=vehicle_model(&m,entity,vehicle,e,resources);if (status) return status;
    config=0x53b534u+vehicle*364u;pm_store(&m,o+0x10f7u,config,4);count=pm_load(&m,0x53e8dcu,4);pm_store(&m,0x53e8c0u+count*4u,entity,4);pm_store(&m,0x53e8dcu,count+1u,4);
    if (pm_load(&m,config+0x108u,2) || pm_load(&m,config+0x10au,2)) { pm_store(&m,o+0x6a6u,pm_load(&m,config+0x108u,2),2);pm_store(&m,o+0x6a8u,pm_load(&m,config+0x10au,2),2); }
    if (pm_load(&m,config+0x10cu,4)) pm_store(&m,o+0x69au,pm_load(&m,config+0x10cu,4),4);
    if (pm_load(&m,config+0x110u,4)) pm_store(&m,o+0x69eu,pm_load(&m,config+0x110u,4),4);
    if (pm_load(&m,config+0x108u,2) || pm_load(&m,config+0x10au,2)) { pm_store(&m,o+0x6a2u,pm_load(&m,config+0x114u,2),2);pm_store(&m,o+0x6a4u,pm_load(&m,config+0x116u,2),2); }
    if (!pm_load(&m,o+0x1153u,4)) pm_store(&m,o+0x1153u,pm_load(&m,o+0x296u,4),4);
    pm_store(&m,o+0x110fu,pm_load(&m,o+0x790u,4),4);pm_store(&m,o+0x110bu,pm_load(&m,o+0x790u,4),4);
    for (i=0;i<3u;i++) pm_store(&m,o+0x10ffu+i*4u,pm_load(&m,o+i*4u,4),4);
    span=pm_sub(pm_i32(&m,config+0x14cu),pm_i32(&m,config+0x148u));pm_write32(&m,o+0x1143u,span);pm_write32(&m,o+0x1147u,pm_sub(pm_i32(&m,config+0x154u),pm_i32(&m,config+0x150u)));
    seed=random_next(&m);if (m.error) return m.error;if (!span) return -2;
    pm_write32(&m,o+0x113fu,pm_add((int32_t)((int64_t)((seed>>4)&65535u)%(int64_t)span),pm_i32(&m,config+0x148u)));
    seed=random_next(&m);pm_write32(&m,o+0x114fu,pm_i16(&m,0x52e030u+(((seed>>4)&7u)+pm_load(&m,0x53d18cu,4)*8u)*2u));return m.error;
}
int frame_ai_parent(frame_data_read read,void *ctx,uint32_t entity,const frame_entity_factory_services *e,uint32_t *out)
{
    physics_memory m={read,0,ctx,0};uint32_t o;if (!read || !e || !out) return -1;o=object_of(&m,entity);if (pm_load(&m,o+0x10f3u,1)&1u) { *out=0;return m.error; }if (m.error) return m.error;
    return frame_class_dispatch(read,ctx,pm_load(&m,0x545fdcu,4),entity,e->method,e->method_ctx,out);
}
int frame_ai_hit(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t payload,uint32_t shell,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o,unused;int32_t duration;int status;if (!read || !write || !e) return -1;o=object_of(&m,entity);
    if (!shell) { if (m.error) return m.error;status=frame_class_dispatch_payload(read,ctx,pm_load(&m,0x545fdcu,4),entity,payload,e->method,e->method_ctx,&unused);if (status) return status; }
    duration=pm_i32(&m,pm_load(&m,o+0x10f7u,4)+(shell ? 0x168u : 0x160u));pm_write32(&m,o+0x115bu,duration);pm_write32(&m,o+0x10c6u,pm_add(pm_i32(&m,o+0x10c6u),pm_sar(duration,1)));
    if (m.error) return m.error;
    return shell ? frame_entity_dispatch(read,write,ctx,entity,pm_load(&m,0x545f08u,4),1,payload,e->method,e->method_ctx,&unused) : 0;
}
int frame_ai_hit_pair(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t payload,uint32_t type,frame_native_method_pair method,void *method_ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t o,klass,slot,target,unused;int32_t duration;int status;if (!read || !write) return -1;
    o=object_of(&m,entity);klass=pm_load(&m,0x545fdcu,4);slot=pm_load(&m,0x55c698u,4);
    if (pm_signed(slot)<=pm_i32(&m,klass+28u)) { target=pm_load(&m,pm_load(&m,klass+36u,4)+slot*4u,4);if (m.error) return m.error;if (target) { if (!method) return -5;status=method(method_ctx,target,entity,payload,type,&unused);if (status) return status; } }
    duration=pm_i32(&m,pm_load(&m,o+0x10f7u,4)+0x164u);if (type==4u) duration=pm_sar(duration,1);
    pm_write32(&m,o+0x115bu,duration);pm_write32(&m,o+0x112bu,-3000);pm_write32(&m,o+0x10c6u,pm_add(pm_i32(&m,o+0x10c6u),pm_sar(duration,1)));return m.error;
}
int frame_ai_throttle(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity)
{
    physics_memory m={read,write,ctx,0};uint32_t o,player,next,config;int32_t speed,rank,i,value,dt;int status=0;
    if (!read || !write) return -1;
    o=object_of(&m,entity);player=object_of(&m,pm_load(&m,0x53e8b8u,4));next=route_next(&m,o,0x110fu,0x1117u);config=pm_load(&m,o+0x10f7u,4);
    if (!pm_load(&m,0x53e928u,4) || entity!=pm_load(&m,0x590410u,4)) {
        if (pm_i32(&m,o+0x111fu)<pm_i16(&m,next+0x82u) && pm_i32(&m,next+0x8cu)<pm_i32(&m,o+0xecu)) { pm_store(&m,o+0x94u,0,4);pm_store(&m,o+0x98u,pm_load(&m,config+0x1cu,4),4); }
        else {
            speed=pm_i32(&m,config+(pm_load(&m,0x52ed94u,4)+1u)*4u);if (pm_load(&m,0x53e928u,4)) speed=pm_sub(speed,50000);
            rank=(int32_t)pm_load(&m,o+0xddcu,1);
            if (pm_load(&m,player+0xddcu,1)<5u && (uint32_t)rank==pm_load(&m,player+0xddcu,1)-1u) {
                uint32_t at=0x52e060u+(pm_load(&m,0x52ed94u,4)+pm_load(&m,0x53d18cu,4)*6u)*4u;
                for (i=4-rank;i>0;i--) speed=pm_sar(pm_mul(speed,(int32_t)pm_load(&m,at,2)),pm_load(&m,at+2u,1));
            }
            if (speed<pm_i32(&m,o+0xecu) && !pm_load(&m,o+0xdd6u,2) && entity!=pm_load(&m,0x53b69cu,4)) pm_store(&m,o+0x94u,0,4);
            else { value=pm_i32(&m,config);pm_write32(&m,o+0x94u,value);if (!pm_load(&m,o+0xdedu,1)) pm_write32(&m,o+0x94u,pm_sar(pm_mul(pm_i32(&m,0x52e0b8u+pm_load(&m,0x53d18cu,4)*4u),value),pm_load(&m,0x53cc08u+pm_load(&m,0x53d18cu,4)*4u,1))); }
            if (pm_load(&m,o+0x98u,4)) pm_store(&m,o+0x98u,0,4);
        }
    } else { pm_store(&m,o+0x98u,0,4);pm_store(&m,o+0x94u,pm_load(&m,config,4),4); }
    dt=pm_i32(&m,0x5904fcu);value=pm_add(pm_i32(&m,o+0x112fu),pm_sar(pm_mul(pm_i32(&m,o+0xecu),dt),14));pm_write32(&m,o+0x112fu,value);speed=pm_add(pm_i32(&m,o+0x112bu),dt);pm_write32(&m,o+0x112bu,speed);
    if (speed>1500) { if (pm_abs(value)<3072) pm_store(&m,o+0x10f3u,pm_load(&m,o+0x10f3u,4)|4u,4);pm_store(&m,o+0x112fu,0,4);pm_store(&m,o+0x112bu,0,4); }
    return m.error ? m.error : status;
}
int frame_ai_steer(frame_data_read read,frame_data_write write,void *ctx,uint32_t o,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t point,point_id,config,branch,at;int32_t x,z,local_x,local_z,cross=0,quotient,yaw,distance,width;int status,inner=0;
    if (!read || !write || !trig) return -1;
    point_id=pm_load(&m,object_of(&m,pm_load(&m,o+0x110bu,4))+0x78u+pm_load(&m,o+0x111bu,4)*4u,4);point=object_of(&m,point_id);config=pm_load(&m,o+0x10f7u,4);
    pm_write32(&m,o+0x10ffu,pm_add(pm_i32(&m,o),pm_sar(pm_mul(pm_i32(&m,config+0x38u),pm_i32(&m,o+80u)),14)));
    pm_write32(&m,o+0x1103u,pm_add(pm_i32(&m,o+4u),pm_sar(pm_mul(pm_i32(&m,o+84u),pm_i32(&m,config+0x38u)),14)));
    pm_write32(&m,o+0x1107u,pm_add(pm_i32(&m,o+8u),pm_sar(pm_mul(pm_i32(&m,config+0x38u),pm_i32(&m,o+88u)),14)));
    if (pm_load(&m,o+0x10f3u,1)&4u) return m.error;
    x=pm_sub(pm_i32(&m,o+0x10ffu),pm_i32(&m,point));z=pm_sub(pm_i32(&m,o+0x1107u),pm_i32(&m,point+8u));local_x=x;local_z=z;
    status=xz(&m,pm_load(&m,point+20u,4),&local_x,&local_z,1);if (status) return status;
    distance=physics_length2_approx(local_x,local_z);pm_write32(&m,o+0x111fu,distance);
    /* Intersect predicted local position with the checkpoint plane along the
     * local velocity, not along the position vector. Ghidra elides that vector
     * setup;40219F..4021DE explicitly loads velocity>>6 for the second transform. */
    { int32_t vx=pm_sar(pm_i32(&m,o+80u),6),vz=pm_sar(pm_i32(&m,o+88u),6);status=xz(&m,pm_load(&m,point+20u,4),&vx,&vz,1);if (status) return status;
      if (vz) { status=divide(&m,pm_mul(vx,local_z),vz,&quotient);if (status) return status;cross=pm_sub(local_x,quotient); }
    }
    if (pm_load(&m,o+0xdedu,1) && pm_abs(cross)<=pm_i32(&m,point+0x84u)) {
        status=frame_ai_route_branch(read,write,ctx,o,point_id,&branch);
        if (status) return status;
        inner=1;
    }
    if (inner) {
        uint32_t checkpoint=pm_load(&m,point+0x78u+branch*4u,4);uint32_t record=pm_load(&m,pm_load(&m,0x55c694u,4)+checkpoint*8u,4);
        if (!record) point=0;
        else { /*4020D0 deliberately evaluates the branch selector twice. */
            uint32_t current=point;
            status=frame_ai_route_branch(read,write,ctx,o,point_id,&branch);if (status) return status;point=object_of(&m,pm_load(&m,current+0x78u+branch*4u,4));
        }
        x=pm_sub(pm_i32(&m,point),pm_i32(&m,o+0x10ffu));z=pm_sub(pm_i32(&m,point+8u),pm_i32(&m,o+0x1107u));
    } else {
        width=pm_i32(&m,point+0x84u);if (cross<1) width=pm_neg(width);if (pm_load(&m,o+0xdedu,1)) width=pm_sar(width,1);
        local_x=width;local_z=0;status=xz(&m,pm_load(&m,point+20u,4),&local_x,&local_z,0);if (status) return status;
        x=pm_sub(pm_add(local_x,pm_i32(&m,point)),pm_i32(&m,o+0x10ffu));z=pm_sub(pm_add(local_z,pm_i32(&m,point+8u)),pm_i32(&m,o+0x1107u));
    }
    status=xz(&m,pm_load(&m,o+20u,4),&x,&z,1);if (status) return status;status=angle(x,z,trig,&yaw);if (status) return status;
    at=config+(inner ? 0x90u : 0x60u);
    for (;;) { if (pm_i32(&m,at)<pm_abs(distance)) { pm_write32(&m,o+0x9cu,scaled(&m,yaw,at+4u));break; }if (at<=config+(inner ? 0x6cu : 0x3cu)) break;at-=12u; }
    if (!inner) { at=config+0xfcu;for (;;) { if (pm_i32(&m,at)<pm_abs(pm_i32(&m,o+0xecu))) { pm_write32(&m,o+0x9cu,scaled(&m,pm_i32(&m,o+0x9cu),at+4u));break; }if (at<=config+0x9cu) break;at-=12u; } }
    return m.error;
}
int frame_ai_reverse(frame_data_read read,frame_data_write write,void *ctx,uint32_t o,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t previous,next;int32_t timer,period,x,z,yaw;int status;
    if (!read || !write || !trig) return -1;
    previous=object_of(&m,pm_load(&m,o+0x110fu,4));next=object_of(&m,pm_load(&m,previous+0x78u+pm_load(&m,o+0x1117u,4)*4u,4));timer=pm_add(pm_i32(&m,o+0x1127u),pm_i32(&m,0x5904fcu));pm_write32(&m,o+0x1127u,timer);period=pm_i32(&m,0x592e84u);
    if (timer<pm_sar(pm_mul(period,5),3)) { pm_write32(&m,o+0x94u,-65535);x=pm_sub(pm_i32(&m,o),pm_i32(&m,previous));z=pm_sub(pm_i32(&m,o+8u),pm_i32(&m,previous+8u)); }
    else { pm_write32(&m,o+0x94u,timer<period ? -65536 : 65536);x=pm_sub(pm_i32(&m,next),pm_i32(&m,o));z=pm_sub(pm_i32(&m,next+8u),pm_i32(&m,o+8u)); }
    status=xz(&m,pm_load(&m,o+20u,4),&x,&z,1);if (status) return status;status=angle(x,z,trig,&yaw);if (status) return status;pm_write32(&m,o+0x9cu,pm_mul(yaw,16));
    if (timer>=pm_sar(pm_mul(period,5),3) && pm_mul(period,2)<pm_i32(&m,o+0x1127u)) { pm_store(&m,o+0x1127u,0,4);pm_store(&m,o+0x112fu,0,4);pm_store(&m,o+0x10f3u,pm_load(&m,o+0x10f3u,4)&~4u,4);pm_write32(&m,o+0x112bu,-3500); }return m.error;
}
int frame_ai_turn_flag(frame_data_read read,frame_data_write write,void *ctx,uint32_t o)
{
    physics_memory m={read,write,ctx,0};uint32_t flags;if (!read || !write) return -1;flags=pm_load(&m,o+0x10f3u,4);if (pm_i32(&m,pm_load(&m,o+0x10f7u,4)+0x13cu)<pm_abs(pm_i32(&m,o+0x9cu))) flags|=16u;else flags&=~16u;pm_store(&m,o+0x10f3u,flags,4);return m.error;
}
int frame_ai_anger_near(frame_data_read read,frame_data_write write,void *ctx,uint32_t o,uint32_t other,int32_t amount,int32_t range)
{
    physics_memory m={read,write,ctx,0};uint32_t config;int32_t x,z,distance,fraction,value;int status;if (!read || !write) return -1;
    x=pm_sub(pm_i32(&m,o),pm_i32(&m,other));z=pm_sub(pm_i32(&m,o+8u),pm_i32(&m,other+8u));distance=physics_length2_approx(x,z);if (range<distance) distance=range;config=pm_load(&m,o+0x10f7u,4);
    status=divide(&m,pm_mul(pm_sub(range,distance),pm_i32(&m,0x5904fcu)),range,&fraction);if (status) return status;
    if (pm_add(pm_mul(pm_i32(&m,o+0x30u),x),pm_mul(pm_i32(&m,o+0x38u),z))<0) amount=scaled(&m,amount,config+0x124u);
    value=pm_add(pm_i32(&m,o+0x1137u),pm_mul(amount,fraction));pm_write32(&m,o+0x1137u,value);if (pm_i32(&m,config+0x118u)<value) pm_store(&m,o+0x1137u,pm_load(&m,config+0x118u,4),4);return m.error;
}
static int32_t wrap_angle(int32_t value)
{
    if (value>32760) { uint32_t n=((uint32_t)value+32759u)/65520u;value=pm_sub(value,pm_mul((int32_t)n,65520)); }
    if (value<-32760) value=pm_add(value,pm_mul((int32_t)((32759u-(uint32_t)value)/65520u),65520));
    return value;
}
int frame_ai_angle_approach(frame_data_read read,frame_data_write write,void *ctx,int32_t target,uint32_t at,int32_t rate,int32_t *out)
{
    physics_memory m={read,write,ctx,0};int32_t value,delta,step;if (!read || !write || !out) return -1;value=pm_i16(&m,at);delta=wrap_angle(pm_sub(value,target));
    if (delta) { step=pm_mul(rate,pm_i16(&m,0x5904fcu));value=delta<0 ? pm_add(value,step) : pm_sub(value,step);value=pm_signed((uint32_t)(value&65535));if (value>32767) value-=65536;pm_store(&m,at,(uint32_t)value,2);step=wrap_angle(pm_sub(value,target));if ((delta<0 && step>0) || (delta>0 && step<0)) pm_store(&m,at,(uint32_t)target,2);delta=step; }
    if (m.error) return m.error;
    *out=delta;return 0;
}
static int inside(physics_memory *m,uint32_t o,uint32_t at,int reversed_z)
{
    int32_t x=pm_i32(m,o),z=pm_i32(m,o+8u);
    return pm_i32(m,at)<=x && x<=pm_i32(m,at+16u) && (reversed_z ? z<=pm_i32(m,at+8u) && pm_i32(m,at+24u)<=z : pm_i32(m,at+8u)<=z && z<=pm_i32(m,at+24u));
}
int frame_ai_far_drive(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t o,player,next,config,i,j,matrix,at;int32_t direction[3],local[3],speed,rank,yaw;int status;
    if (!read || !write || !trig) return -1;
    o=object_of(&m,entity);player=object_of(&m,pm_load(&m,0x53e8b8u,4));next=route_next(&m,o,0x110fu,0x1117u);config=pm_load(&m,o+0x10f7u,4);
    for (i=0;i<3u;i++) direction[i]=pm_sub(pm_i32(&m,next+i*4u),pm_i32(&m,o+i*4u));
    physics_vector_normalize(direction);
    at=config+0x20u+pm_load(&m,0x52ed94u,4)*4u;speed=pm_i32(&m,at);if (pm_load(&m,0x53e928u,4) && pm_load(&m,0x590564u,4)>199u) speed=pm_sub(speed,30000);
    rank=(int32_t)pm_load(&m,o+0xddcu,1);
    if (pm_load(&m,player+0xddcu,1)<5u && (uint32_t)rank==pm_load(&m,player+0xddcu,1)-1u) {
        at=0x52e060u+(pm_load(&m,0x52ed94u,4)+pm_load(&m,0x53d18cu,4)*6u)*4u;
        for (i=(uint32_t)(4-rank);i;i--) speed=pm_sar(pm_mul(speed,(int32_t)pm_load(&m,at,2)),pm_load(&m,at+2u,1));
    }
    if (pm_load(&m,0x52ed90u,4)==5u && inside(&m,o,0x531be8u,0)) speed=pm_sar(pm_mul(speed,pm_i32(&m,0x52e0c4u)),pm_load(&m,0x53cc14u,1));
    if (pm_load(&m,0x52ed90u,4)==2u && inside(&m,o,0x531c28u,1)) speed=pm_sar(pm_mul(speed,pm_i32(&m,0x52e0c8u)),pm_load(&m,0x53cc18u,1));
    if (pm_load(&m,0x546000u,4)) speed=pm_sar(pm_mul(speed,6),3);
    speed=pm_sar(speed,12);for (i=0;i<3u;i++) pm_write32(&m,o+80u+i*4u,pm_mul(direction[i],speed));matrix=o+0x2b2u;
    for (i=0;i<3u;i++) { int32_t sum=0;for (j=0;j<3u;j++) sum=pm_add(sum,pm_mul(pm_sar(pm_i32(&m,o+80u+j*4u),8),pm_i32(&m,matrix+(i*3u+j)*4u)));local[i]=pm_sar(sum,4); }
    for (i=0;pm_abs(local[0])>32768 || pm_abs(local[1])>32768 || pm_abs(local[2])>32768;i++) { if (i>=32u) return -3;for (j=0;j<3u;j++) local[j]=pm_sar(local[j],1); }
    if (m.error) return m.error;
    status=physics_angle_lookup(local[0],local[2],trig,&yaw);if (status) return status;return frame_matrix_rotate(read,write,ctx,matrix,1,(uint32_t)pm_neg(yaw),trig);
}
int frame_ai_far_refresh(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t o,at,kind;int32_t saved;int status;
    if (!read || !write || !trig) return -1;
    o=object_of(&m,entity);kind=pm_load(&m,o+0x10fbu,4);if (!kind) return frame_ai_far_drive(read,write,ctx,entity,trig);
    at=pm_load(&m,o+0x10f7u,4)+0x20u+pm_load(&m,0x52ed94u,4)*4u;saved=pm_i32(&m,at);
    if (kind==1u || kind==2u) pm_write32(&m,at,pm_sar(pm_mul(saved,pm_i32(&m,kind==1u ? 0x592e8cu : 0x592e90u)),pm_load(&m,kind==1u ? 0x592e88u : 0x592e94u,1)));
    if (m.error) return m.error;
    status=frame_ai_far_drive(read,write,ctx,entity,trig);if (status) return status;pm_write32(&m,at,saved);return m.error;
}
int frame_ai_realign(frame_data_read read,frame_data_write write,void *ctx,uint32_t o,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t next;int32_t x,z,yaw;int status;if (!read || !write || !trig) return -1;
    next=route_next(&m,o,0x110bu,0x111bu);x=pm_sub(pm_i32(&m,next),pm_i32(&m,o+0x78u));z=pm_sub(pm_i32(&m,next+8u),pm_i32(&m,o+0x80u));status=xz(&m,o+0x2b2u,&x,&z,1);if (status) return status;status=angle(x,z,trig,&yaw);if (status) return status;return frame_matrix_rotate(read,write,ctx,o+0x2b2u,1,(uint32_t)pm_neg(yaw),trig);
}
int frame_ai_rejoin(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity)
{
    physics_memory m={read,write,ctx,0};uint32_t o,i,j;if (!read || !write) return -1;o=object_of(&m,entity);
    pm_store(&m,o+0x111bu,0,4);pm_store(&m,o+0x10f3u,pm_load(&m,o+0x10f3u,4)&~3u,4);pm_store(&m,o+0x1117u,0,4);pm_store(&m,o+0x762u,0,2);
    for (i=0;i<3u;i++) pm_store(&m,o+0x78u+i*4u,pm_load(&m,o+i*4u,4),4);
    for (i=0;i<3u;i++) pm_store(&m,o+0x2a6u+i*4u,pm_load(&m,o+i*4u,4),4);
    for (i=0;i<3u;i++) pm_store(&m,o+0x10ffu+i*4u,pm_load(&m,o+i*4u,4),4);
    pm_store(&m,o+0x110bu,pm_load(&m,o+0x110fu,4),4);
    for (j=0;j<9u;j++) pm_store(&m,o+24u+j*4u,pm_load(&m,o+0x2b2u+j*4u,4),4);
    return m.error;
}
int frame_ai_waypoints(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const physics_trig *trig,uint32_t limit)
{
    physics_memory m={read,write,ctx,0};uint32_t o,branch;frame_checkpoint_result r;int32_t lap,score;int status;
    if (!read || !write || !trig || !limit) return -1;
    o=object_of(&m,entity);
    if (!(pm_load(&m,o+0x10f3u,1)&1u)) {
        status=frame_checkpoint_at(read,ctx,o+0x10ffu,pm_load(&m,o+0x110bu,4),pm_load(&m,o+0x111bu,4),&r);if (status) return status;
        if (r.next_entity) { pm_store(&m,o+0x110bu,r.next_entity,4);status=frame_ai_route_branch(read,write,ctx,o,r.next_entity,&branch);if (status) return status;pm_store(&m,o+0x111bu,branch,4); }
    }
    status=frame_checkpoint_query(read,ctx,entity,pm_load(&m,o+0x790u,4),0,&r);if (status) return status;
    if (r.next_entity) {
        pm_store(&m,o+0x790u,r.next_entity,4);pm_write32(&m,o+0x794u,pm_add(pm_i32(&m,o+0x794u),1));
        if (r.finishes_lap) { lap=pm_i16(&m,o+0xddau);if (lap==pm_i32(&m,o+0xcb6u)) { if (lap<3) pm_store(&m,o+0xddau,(uint32_t)lap+1u,2);status=frame_lap_record(read,write,ctx,entity);if (status) return status;if (pm_load(&m,0x53e928u,4) && entity==pm_load(&m,0x590410u,4)) return m.error ? m.error : -5; }else if (lap<3) pm_store(&m,o+0xddau,(uint32_t)lap+1u,2); }
    }
    status=frame_checkpoint_query(read,ctx,entity,pm_load(&m,o+0x110fu,4),pm_load(&m,o+0x1117u,4),&r);if (status) return status;
    if (r.next_entity) { pm_store(&m,o+0x110fu,r.next_entity,4);status=frame_ai_route_branch(read,write,ctx,o,r.next_entity,&branch);if (status) return status;pm_store(&m,o+0x1117u,branch,4);if (pm_load(&m,o+0x10f3u,1)&1u) { status=frame_ai_far_refresh(read,write,ctx,entity,trig);if (status) return status; } }
    if (m.error) return m.error;
    status=frame_route_progress(read,ctx,pm_i32(&m,o),pm_i32(&m,o+8u),pm_load(&m,o+0x790u,4),pm_i16(&m,o+0xddau),limit,&score);if (status) return status;pm_write32(&m,o+0xd18u,score);return m.error;
}
static int near(physics_memory *m,uint32_t a,uint32_t b,int32_t range)
{ return pm_abs(pm_sub(pm_i32(m,a),pm_i32(m,b)))<range && pm_abs(pm_sub(pm_i32(m,a+8u),pm_i32(m,b+8u)))<range; }
static int32_t attack_range(uint32_t weapon)
{ return weapon==3u ? 71680 : weapon==5u ? 61440 : weapon==0u || weapon==4u || weapon==10u ? 40960 : 51200; }
static uint32_t random_next(physics_memory *m)
{ uint32_t n=pm_load(m,0x55b6acu,4)*1664525u+1013904223u;pm_store(m,0x55b6acu,n,4);return n; }
int frame_ai_choose_target(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity)
{
    physics_memory m={read,write,ctx,0};uint32_t o,i,id,other,selected=0;int32_t range,best=1024000,distance;
    if (!read || !write) return -1;
    o=object_of(&m,entity);range=attack_range(pm_load(&m,o+0xd0fu,4));
    for (i=0;i<9u;i++) {
        id=pm_load(&m,i<7u ? 0x53e8c0u+i*4u : 0x53e8b8u+(i-7u)*4u,4);if (!id || id==entity) continue;other=object_of(&m,id);
        if (i<7u && (pm_load(&m,other+0x10f3u,1)&1u)) continue;
        if (near(&m,o,other,range)) { distance=physics_length2_approx(pm_sub(pm_i32(&m,o),pm_i32(&m,other)),pm_sub(pm_i32(&m,o+8u),pm_i32(&m,other+8u)));if (distance<best) { selected=id;best=distance; } }
    }
    pm_store(&m,o+0x1133u,selected,4);return m.error;
}
int frame_ai_target_yaw(frame_data_read read,void *ctx,uint32_t o,const physics_trig *trig,int32_t *out)
{
    physics_memory m={read,0,ctx,0};uint32_t other,i=0;int32_t x,z;int status;if (!read || !trig || !out) return -1;
    other=object_of(&m,pm_load(&m,o+0x1133u,4));x=pm_sub(pm_i32(&m,other),pm_i32(&m,o));z=pm_sub(pm_i32(&m,other+8u),pm_i32(&m,o+8u));status=xz(&m,pm_load(&m,o+20u,4),&x,&z,1);if (status) return status;
    /* Original reduces positive large components only, not their magnitudes. */
    while (x>32767 || z>32767) { if (i++>=32u) return -3;x=pm_sar(x,1);z=pm_sar(z,1); }return physics_angle_lookup(pm_neg(x),z,trig,out);
}
int frame_ai_target_corridor(frame_data_read read,void *ctx,uint32_t o,uint32_t *out)
{
    physics_memory m={read,0,ctx,0};uint32_t other;int32_t x,z;int status;if (!read || !out) return -1;other=object_of(&m,pm_load(&m,o+0x1133u,4));x=pm_sub(pm_i32(&m,other),pm_i32(&m,o));z=pm_sub(pm_i32(&m,other+8u),pm_i32(&m,o+8u));status=xz(&m,o+0x31au,&x,&z,1);if (status) return status;*out=pm_abs(x)<2048 && pm_abs(z)<40960;return 0;
}
int frame_ai_can_warn(frame_data_read read,void *ctx,uint32_t o,uint32_t *out)
{
    physics_memory m={read,0,ctx,0};uint32_t weapon,allowed=0;int status;if (!read || !out) return -1;
    if (pm_load(&m,o+0x1133u,4)==pm_load(&m,0x53e8b8u,4)) { weapon=pm_load(&m,o+0xd0fu,4);if (!weapon) { status=frame_ai_target_corridor(read,ctx,o,&allowed);if (status) return status; }if (weapon==3u || weapon==5u) allowed=1; }
    if (m.error) return m.error;
    *out=allowed;return 0;
}
int frame_ai_warn_player(frame_data_read read,frame_data_write write,void *ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t player,o;if (!read || !write) return -1;player=pm_load(&m,0x53e8b8u,4);o=object_of(&m,player);if (!pm_load(&m,0x53e928u,4) && player!=pm_load(&m,0x53b69cu,4)) pm_store(&m,o+0x1104u,1,1);return m.error;
}
int frame_ai_fire(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o,config,seed,span,player,unused;int32_t v,error;int64_t divisor;int status;
    if (!read || !write) return -1;
    o=object_of(&m,entity);seed=random_next(&m);pm_store(&m,o+0x113bu,0,4);pm_store(&m,o+0x114bu,0,4);error=pm_i16(&m,0x52e030u+(((seed>>4)&7u)+pm_load(&m,0x53d18cu,4)*8u)*2u);pm_write32(&m,o+0x114fu,error);if ((seed>>4)&1u) pm_write32(&m,o+0x114fu,pm_neg(error));
    config=pm_load(&m,o+0x10f7u,4);span=o+0x1143u;player=object_of(&m,pm_load(&m,0x53e8b8u,4));
    if (entity!=pm_load(&m,0x590410u,4) && pm_i32(&m,o+0xd18u)<pm_i32(&m,player+0xd18u)) span=o+0x1147u;
    seed=random_next(&m);divisor=pm_i32(&m,span);if (m.error) return m.error;if (!divisor) return -2;
    v=pm_add((int32_t)((int64_t)((seed>>4)&65535u)%divisor),pm_i32(&m,config+(span==o+0x1143u ? 0x148u : 0x150u)));pm_write32(&m,o+0x113fu,v);
    if (entity!=pm_load(&m,0x590410u,4) && pm_load(&m,0x52ed90u,4)==6u) pm_write32(&m,o+0x113fu,pm_add(v,1000));
    if (m.error) return m.error;
    if (!e || !e->method) return -5;
    status=e->method(e->method_ctx,0x42f8c0u,entity,0,0,&unused);return status;
}
int frame_ai_aim_fire(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,int32_t offset,const physics_trig *trig,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o,config,allowed;int32_t target,delta,timer,unused;int status;
    if (!read || !write || !trig) return -1;
    o=object_of(&m,entity);config=pm_load(&m,o+0x10f7u,4);status=frame_ai_target_yaw(read,ctx,o,trig,&target);if (status) return status;status=frame_ai_angle_approach(read,write,ctx,pm_add(target,offset),o+0x762u,pm_i32(&m,config+0x140u),&unused);if (status) return status;
    delta=wrap_angle(pm_sub(pm_i16(&m,o+0x762u),target));
    if (pm_abs(delta)<pm_i32(&m,config+0x144u)) {
        timer=pm_add(pm_i32(&m,o+0x113bu),pm_i32(&m,0x5904fcu));pm_write32(&m,o+0x113bu,timer);if (timer<=pm_i32(&m,o+0x113fu)) return m.error;
        if (pm_abs(delta)>=pm_add(pm_i32(&m,config+0x158u),offset)) { if (pm_load(&m,o+0x114bu,4)) pm_store(&m,o+0x114bu,0,4);return m.error; }
        if (pm_i32(&m,o+0xd14u)<1) {
            status=frame_ai_can_warn(read,ctx,o,&allowed);if (status) return status;
            if (!pm_load(&m,o+0x114bu,4)) { if (!allowed) return frame_ai_fire(read,write,ctx,entity,e); }
            else if (!allowed) return m.error;
            timer=pm_add(pm_i32(&m,o+0x114bu),pm_i32(&m,0x5904fcu));pm_write32(&m,o+0x114bu,timer);if (timer<=pm_i32(&m,config+0x15cu)) return frame_ai_warn_player(read,write,ctx);return frame_ai_fire(read,write,ctx,entity,e);
        }
    } else { timer=pm_sub(pm_i32(&m,o+0x113bu),pm_i32(&m,0x5904fcu));pm_write32(&m,o+0x113bu,timer);if (timer<0) pm_store(&m,o+0x113bu,0,4);if (!pm_load(&m,o+0x114bu,4)) return m.error; }
    pm_store(&m,o+0x114bu,0,4);return m.error;
}
int frame_ai_attack_target(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const physics_trig *trig,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o,config,other,weapon;int32_t residual,range,offset;int status;if (!read || !write || !trig) return -1;
    o=object_of(&m,entity);config=pm_load(&m,o+0x10f7u,4);
    if (pm_i32(&m,config+0x118u)<=pm_i32(&m,o+0x1137u)) {
        if (!pm_load(&m,o+0x1133u,4)) { status=frame_ai_choose_target(read,write,ctx,entity);if (status) return status;return frame_ai_angle_approach(read,write,ctx,0,o+0x762u,pm_i32(&m,config+0x140u),&residual); }
        weapon=pm_load(&m,o+0xd0fu,4);if (weapon!=0u && weapon!=4u && weapon!=10u && weapon!=3u && weapon!=5u) return m.error;
        other=object_of(&m,pm_load(&m,o+0x1133u,4));range=attack_range(weapon);
        if (near(&m,o,other,range)) { offset=weapon==3u ? 3640 : weapon==5u ? 5460 : pm_i32(&m,o+0x114fu);return frame_ai_aim_fire(read,write,ctx,entity,offset,trig,e); }
    }
    pm_store(&m,o+0x1133u,0,4);return m.error;
}
int frame_ai_mine_warning(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o;int32_t timer,value;int status;if (!read || !write) return -1;o=object_of(&m,entity);timer=pm_i32(&m,o+0x114bu);
    if (timer<1) {
        if (pm_load(&m,o+0x10f3u,1)&16u) pm_write32(&m,o+0x114bu,pm_add(timer,pm_i32(&m,0x5904fcu)));
        else { value=pm_add(pm_i32(&m,o+0x1157u),pm_i32(&m,0x5904fcu));pm_write32(&m,o+0x1157u,value);if (value>5000) pm_write32(&m,o+0x114bu,pm_add(timer,pm_i32(&m,0x5904fcu))); }
    } else { timer=pm_add(timer,pm_i32(&m,0x5904fcu));pm_write32(&m,o+0x114bu,timer);status=frame_ai_warn_player(read,write,ctx);if (status) return status;if (pm_i32(&m,pm_load(&m,o+0x10f7u,4)+0x15cu)<pm_i32(&m,o+0x114bu)) return frame_ai_fire(read,write,ctx,entity,e); }return m.error;
}
int frame_ai_anger_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity)
{
    physics_memory m={read,write,ctx,0};uint32_t o,config,i,id,other;int observed=0,status;int32_t value;if (!read || !write) return -1;o=object_of(&m,entity);
    if (((entity^pm_load(&m,0x590564u,4))&4u)!=0u) return m.error;
    config=pm_load(&m,o+0x10f7u,4);
    for (i=0;i<9u;i++) { id=pm_load(&m,i<7u ? 0x53e8c0u+i*4u : 0x53e8b8u+(i-7u)*4u,4);if (!id || (i<7u && id==entity)) continue;other=object_of(&m,id);if (i<7u && (pm_load(&m,other+0x10f3u,1)&1u)) continue;
        if (near(&m,o,other,pm_i32(&m,config+0x11cu))) { status=frame_ai_anger_near(read,write,ctx,o,other,pm_mul(pm_i32(&m,config+0x120u),4),pm_i32(&m,config+0x11cu));if (status) return status;observed=1; }
    }
    if (!observed) { value=pm_add(pm_i32(&m,o+0x1137u),pm_mul(pm_mul(pm_i32(&m,config+0x12cu),pm_i32(&m,0x5904fcu)),-4));pm_write32(&m,o+0x1137u,value);if (value<0) pm_store(&m,o+0x1137u,0,4); }return m.error;
}
static int fire_if_angry(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o=object_of(&m,entity);if (pm_sar(pm_mul(pm_i32(&m,pm_load(&m,o+0x10f7u,4)+0x118u),7),3)<=pm_i32(&m,o+0x1137u)) return frame_ai_fire(read,write,ctx,entity,e);return m.error;
}
int frame_ai_weapon_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const physics_trig *trig,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o,weapon;int status;if (!read || !write || !trig) return -1;o=object_of(&m,entity);status=frame_ai_turn_flag(read,write,ctx,o);if (status) return status;
    if (pm_load(&m,0x53e928u,4) && entity==pm_load(&m,0x590410u,4)) pm_store(&m,o+0x10f3u,pm_load(&m,o+0x10f3u,4)&~16u,4);
    status=frame_ai_anger_tick(read,write,ctx,entity);if (status) return status;weapon=pm_load(&m,o+0xd0fu,4);
    if (weapon==1u || weapon==2u) return frame_ai_mine_warning(read,write,ctx,entity,e);
    if (weapon==7u) return frame_ai_fire(read,write,ctx,entity,e);
    if (weapon==8u) return fire_if_angry(read,write,ctx,entity,e);
    return frame_ai_attack_target(read,write,ctx,entity,trig,e);
}
int frame_ai_on_shot(frame_data_read read,frame_data_write write,void *ctx,uint32_t owner,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t i,id,o,source;int status;if (!read || !write) return -1;source=object_of(&m,owner);
    for (i=0;i<7u;i++) { id=pm_load(&m,0x53e8c0u+i*4u,4);if (!id || id==owner) continue;o=object_of(&m,id);if (!(pm_load(&m,o+0x10f3u,1)&1u) && pm_load(&m,o+0xd0fu,4)==8u && near(&m,o,source,76800)) { status=fire_if_angry(read,write,ctx,id,e);if (status) return status; } }return m.error;
}
int frame_ai_on_blast(frame_data_read read,frame_data_write write,void *ctx,const int32_t origin[3],uint32_t owner)
{
    physics_memory m={read,write,ctx,0};uint32_t i,id,o,config;int32_t dx,dz,range,distance,amount,value,part;int status;if (!read || !write || !origin) return -1;
    for (i=0;i<7u;i++) { id=pm_load(&m,0x53e8c0u+i*4u,4);if (!id || id==owner) continue;o=object_of(&m,id);if (pm_load(&m,o+0x10f3u,1)&1u) continue;config=pm_load(&m,o+0x10f7u,4);range=pm_i32(&m,config+0x130u);dx=pm_sub(origin[0],pm_i32(&m,o));dz=pm_sub(origin[2],pm_i32(&m,o+8u));
        if (pm_abs(dx)>=range || pm_abs(dz)>=range) continue;
        distance=physics_length2_approx(dx,dz);if (range<distance) distance=range;amount=pm_i32(&m,config+0x134u);status=divide(&m,pm_mul(pm_sar(pm_sub(range,distance),12),amount),range,&part);if (status) return status;
        value=pm_add(pm_i32(&m,o+0x1137u),pm_mul(part,4096));pm_write32(&m,o+0x1137u,value);if (pm_i32(&m,config+0x118u)<value) pm_store(&m,o+0x1137u,pm_load(&m,config+0x118u,4),4);
    }return m.error;
}
int frame_ai_global_tick(frame_data_read read,frame_data_write write,void *ctx,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t frame,i,id,o,at;int32_t saved;int status;
    if (!read || !write || !trig) return -1;
    frame=pm_load(&m,0x590564u,4);
    if (frame<200u) for (i=0;i<7u;i++) {
        id=pm_load(&m,0x53e8c0u+i*4u,4);if (!id) continue;o=object_of(&m,id);if (!(pm_load(&m,o+0x10f3u,1)&1u)) continue;
        at=pm_load(&m,o+0x10f7u,4)+0x20u+pm_load(&m,0x52ed94u,4)*4u;saved=pm_i32(&m,at);pm_write32(&m,at,pm_mul((int32_t)frame,saved)/200);
        if (m.error) return m.error;
        status=frame_ai_far_drive(read,write,ctx,id,trig);if (status) return status;pm_write32(&m,at,saved);
    }
    if (frame>200u && !(frame&31u)) for (i=0;i<7u;i++) { id=pm_load(&m,0x53e8c0u+i*4u,4);if (!id) continue;status=frame_ai_neighbor(read,write,ctx,id);if (status) return status; }
    return m.error;
}
int frame_ai_wake(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_ai_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t o,seed,period,unused,type=0;int status;
    if (!read || !write || !s || !s->entities || !s->trig) return -1;
    o=object_of(&m,entity);seed=random_next(&m);
    if (pm_load(&m,0x590564u,4)>499u) {
        period=pm_load(&m,0x52e0ccu+pm_load(&m,0x53d18cu,4),1);if (m.error) return m.error;if (!period) return -2;
        if (!(((seed>>4)&65535u)%period) && pm_load(&m,o+0xe13u,4)!=1u) {
            if (pm_load(&m,o+0x764u,4)) return m.error;
            if (pm_load(&m,0x52ed90u,4)!=6u || !(random_next(&m)&16u)) {
                if (!((seed>>4)&7u) && pm_load(&m,0x53d18cu,4)) type=1;
                if (m.error) return m.error;
                status=frame_mine_spawn(read,write,ctx,entity,1,type,s->entities,s->trig,s->feedback,s->feedback_ctx,&unused);if (status) return status;
                pm_store(&m,o+0xe13u,1,4);return m.error;
            }
        } else if (pm_load(&m,o+0x764u,4)) return m.error;
        pm_store(&m,o+0xe13u,1,4);
    }return m.error;
}
int frame_ai_visibility(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_ai_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t o,close,flags,i;int status;
    static const uint32_t audio[]={0xce7,0xcdb,0xce3,0xcff};
    if (!read || !write || !s || !s->entities || !s->trig) return -1;
    o=object_of(&m,entity);
#define AI_TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
    AI_TRY(frame_entity_near_camera(read,ctx,o,0,0x2080,&close));
    if (close) {
        if (!pm_load(&m,o+0xdedu,1)) {
            if (pm_load(&m,0x53e924u,4) && (pm_load(&m,o+0x10f3u,1)&1u)) AI_TRY(frame_ai_rejoin(read,write,ctx,entity));
            pm_store(&m,o+0xdedu,1,1);AI_TRY(frame_ai_wake(read,write,ctx,entity,s));AI_TRY(physics_pose_update(read,write,ctx,o,s->trig));pm_store(&m,o+0x10f3u,pm_load(&m,o+0x10f3u,4)|32u,4);
        }
        pm_store(&m,o+0x10f3u,pm_load(&m,o+0x10f3u,4)&~1u,4);return m.error;
    }
    pm_store(&m,o+0xdedu,0,1);AI_TRY(frame_entity_near_camera(read,ctx,o,0,0x4b00,&close));flags=pm_load(&m,o+0x10f3u,4);
    if (!close) {
        pm_store(&m,o+0x10f3u,flags|1u,4);
        if (!(flags&2u)) {
            pm_store(&m,o+0x110fu,pm_load(&m,o+0x110bu,4),4);pm_store(&m,o+0x10f3u,flags|3u,4);pm_store(&m,o+0x1117u,0,4);pm_store(&m,o+0x111bu,0,4);AI_TRY(frame_ai_far_refresh(read,write,ctx,entity,s->trig));pm_store(&m,o+0xdeeu,0,1);
            if (pm_load(&m,o+0xdd0u,2)) { uint32_t mode=(uint32_t)pm_i16(&m,o+0xdceu);pm_store(&m,o+0xdd0u,0,2);pm_store(&m,o+0x6a6u,pm_load(&m,o+0xdd2u,4),4);AI_TRY(frame_temp_effects_expire(read,write,ctx,mode)); }
            if (pm_i32(&m,o+0xdafu)>0) { pm_store(&m,o+0xde4u,0,1);pm_store(&m,o+0xdafu,0,4); }
            if (pm_load(&m,o+0xdcbu,1)) { pm_store(&m,o+0xc9eu,0,4);pm_store(&m,o+0xdd6u,0,2);pm_store(&m,o+0xcaau,0,4);pm_store(&m,o+0xdcbu,0,1); }
        }
    } else if (flags&2u) { AI_TRY(frame_ai_rejoin(read,write,ctx,entity));AI_TRY(frame_ai_wake(read,write,ctx,entity,s)); }
    else if (flags&32u) {
        pm_store(&m,o+0x10f3u,flags&~32u,4);
        /* Audio device is disabled. The four owned handles still clear in the
         * same order; no gameplay/RNG/timing function is skipped with them. */
        for (i=0;i<4u;i++) if (pm_load(&m,o+audio[i],4)) pm_store(&m,o+audio[i],0,4);
        if (!pm_load(&m,0x53e928u,4)) pm_store(&m,o+0xd0fu,0,4);
    }return m.error;
#undef AI_TRY
}
int frame_ai_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_ai_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t o,flags,i,unused;int32_t value,dt;int status;
    if (!read || !write || !s || !s->entities || !s->trig) return -1;
    o=object_of(&m,entity);if (pm_load(&m,0x53e928u,4)) return m.error ? m.error : -5;
#define AI_TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
    if (!pm_load(&m,o+0xdeau,1)) { AI_TRY(frame_ai_visibility(read,write,ctx,entity,s));if (!(pm_load(&m,o+0x10f3u,1)&1u)) AI_TRY(frame_class_dispatch(read,ctx,pm_load(&m,0x545fdcu,4),entity,s->entities->method,s->entities->method_ctx,&unused)); }
    else {
        AI_TRY(frame_ai_rank(read,write,ctx,entity));AI_TRY(frame_ai_visibility(read,write,ctx,entity,s));dt=pm_i32(&m,0x5904fcu);flags=pm_load(&m,o+0x10f3u,4);
        if (!(flags&1u)) {
            AI_TRY(frame_class_dispatch(read,ctx,pm_load(&m,0x545fdcu,4),entity,s->entities->method,s->entities->method_ctx,&unused));pm_store(&m,o+0xd03u,0,1);
            if (!(pm_load(&m,o+0x10f3u,1)&4u)) {
                if (!pm_load(&m,o+0x115bu,4)) {
                    AI_TRY(frame_ai_throttle(read,write,ctx,entity));AI_TRY(frame_ai_steer(read,write,ctx,o,s->trig));
                    if (pm_abs(pm_i32(&m,o+0x9cu))<180001 || (pm_load(&m,o+0x10f3u,1)&4u)) pm_store(&m,o+0x1123u,0,4);
                    else { value=pm_add(pm_i32(&m,o+0x1123u),dt);pm_write32(&m,o+0x1123u,value);if (value>50) { pm_store(&m,o+0x94u,0,4);pm_store(&m,o+0x98u,10000,4); } }
                } else { pm_store(&m,o+0x94u,0,4);value=pm_sub(pm_i32(&m,o+0x115bu),dt);pm_write32(&m,o+0x115bu,value);if (value<0) pm_store(&m,o+0x115bu,0,4); }
            } else AI_TRY(frame_ai_reverse(read,write,ctx,o,s->trig));
            if (entity!=pm_load(&m,0x53b69cu,4) && pm_load(&m,o+0xdedu,1)) AI_TRY(frame_ai_weapon_tick(read,write,ctx,entity,s->trig,s->entities));
            for (i=0;i<3u;i++) { uint32_t at=o+0x94u+i*4u;value=pm_i32(&m,at);if (value<(i==1u ? 0 : -65535)) pm_write32(&m,at,i==1u ? 0 : -65535);if (pm_i32(&m,at)>65535) pm_store(&m,at,65535,4); }
            value=pm_i32(&m,o+0x10c6u);
            if (value<5001) pm_write32(&m,o+0x10c6u,pm_sub(value,pm_sar(dt,2)));
            else { pm_store(&m,o+0xd03u,1,1);pm_write32(&m,o+0x10c6u,pm_sub(value,(int32_t)((random_next(&m)>>4)&4095u))); }
            if (pm_i32(&m,o+0x10c6u)<0) pm_store(&m,o+0x10c6u,0,4);
        } else for (i=0;i<3u;i++) pm_write32(&m,o+i*4u,pm_add(pm_i32(&m,o+i*4u),pm_sar(pm_mul(pm_i32(&m,o+80u+i*4u),dt),14)));
        if (!(pm_load(&m,o+0x10f3u,4)&1u)) AI_TRY(frame_ai_spacing_throttle(read,write,ctx,entity));
        else if (pm_load(&m,o+0x10f3u,4)&8u) { AI_TRY(frame_ai_far_refresh(read,write,ctx,entity,s->trig));pm_store(&m,o+0x10f3u,pm_load(&m,o+0x10f3u,4)&~8u,4); }
        AI_TRY(frame_ai_waypoints(read,write,ctx,entity,s->trig,s->entities->work_limit));
    }
    if (pm_i16(&m,o+0xe0bu)>0) { pm_store(&m,o+0x762u,(uint32_t)pm_add(pm_i16(&m,o+0x762u),pm_sar(pm_mul(pm_i32(&m,0x5904fcu),pm_i32(&m,o+0xe0fu)),6)),2);pm_write32(&m,o+0xe0fu,damp(pm_i32(&m,o+0xe0fu),pm_load(&m,0x531ba0u,4),pm_i32(&m,0x5904fcu))); }
    AI_TRY(frame_player_horn(read,write,ctx,entity));frame_entity_sound(s->entities,0x40f8f0u,o,entity,0);return m.error;
#undef AI_TRY
}
