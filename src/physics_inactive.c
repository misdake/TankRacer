#include "physics_inactive.h"
#include "frame_scene_effects.h"
#include "frame_weapon_spin.h"
#include "physics_memory.h"
#include "physics_velocity.h"

static void vr(physics_memory *m,uint32_t a,int32_t *v,unsigned n)
{ unsigned i;for (i=0;i<n;++i) v[i]=pm_i32(m,a+i*4u); }
static void vw(physics_memory *m,uint32_t a,const int32_t *v,unsigned n)
{ unsigned i;for (i=0;i<n;++i) pm_write32(m,a+i*4u,v[i]); }
static void transform(int32_t v[3],const int32_t matrix[9],int inverse)
{
    int32_t temp[3];unsigned i,j;
    for (i=0;i<3;++i) { int32_t sum=0;for (j=0;j<3;++j) sum=pm_add(sum,pm_mul(v[j],matrix[inverse ? j*3u+i : i*3u+j]));temp[i]=pm_sar(sum,12); }
    for (i=0;i<3;++i) v[i]=temp[i];
}
static int32_t clamp_angle(int32_t a) { return a<-800 ? -800 : a>800 ? 800 : a; }
int physics_inactive_contact_ambient(physics_world_read read,physics_world_write write,void *ctx,uint32_t o,const physics_trig *trig,const int32_t *miss_words)
{
    physics_memory memory={read,write,ctx,0},*m=&memory;int32_t v[3],matrix[9],normal[3],height,ax,az;physics_terrain_hit hit={0};unsigned i;int status;
    vr(m,o+0x11c,v,3);vr(m,o+0x3ea,matrix,9);transform(v,matrix,1);v[1]=pm_add(v[1],pm_i32(m,o+0x3e2));
    v[0]=pm_i32(m,o+0x2a6);v[2]=pm_i32(m,o+0x2ae);vw(m,o+0x128,v,3);
    pm_write32(m,o+0xd8,0);pm_write32(m,o+0xdc,0);pm_write32(m,o+0xe0,0);if (m->error) return m->error;
    status=physics_terrain_query(read,ctx,pm_load(m,0x55ae60,4),v[0],v[2],&hit);if (status<0) return status;
    if (!status) {
        /* 43349E transforms the UNWRITTEN hit.normal stack slots even after
         * initializing a different temporary to (0,4096,0). Do not invent an
         * up normal: this branch requires an explicit caller-stack input. */
        int32_t boundary[3];if (!miss_words) return -4;
        height=pm_i32(m,o+0xca2);vr(m,o+0x154,boundary,3);if (m->error) return m->error;
        status=physics_fallback_query(read,ctx,pm_load(m,0x55ae60,4),v,0,boundary);if (status<0) return status;
        if (status) vw(m,o+0x154,boundary,3);
        pm_store(m,o+0x134,status ? 4u : 3u,2);for (i=0;i<3;i++) normal[i]=miss_words[i];
    } else {
        height=hit.height;for (i=0;i<3;++i) normal[i]=hit.normal[i];
        pm_write32(m,o+0xca2,height);pm_store(m,o+0x134,1,2);pm_store(m,o+0xde2,(hit.special&1) ? 2u : 0u,1);
    }
    pm_write32(m,o+0x138,height);pm_write32(m,o+0x7c,pm_add(height,1400));
    vr(m,o+0x2b2,matrix,9);transform(normal,matrix,0);if (m->error) return m->error;
    status=physics_angle_lookup(normal[1],normal[2],trig,&ax);if (status) return status;ax=clamp_angle(pm_sub(ax,16384));
    status=physics_angle_lookup(pm_neg(normal[0]),normal[1],trig,&az);if (status) return status;az=clamp_angle(pm_neg(az));
    physics_matrix_rotate(matrix,0,(uint32_t)ax,trig);physics_matrix_rotate(matrix,2,(uint32_t)az,trig);vw(m,o+0x2b2,matrix,9);
    pm_write32(m,o+0xf0,0);pm_store(m,o+0xdee,0,1);pm_store(m,o+0xde1,0,1);pm_write32(m,o+0xf4,14500);
    pm_store(m,o+0xde3,0,1);pm_write32(m,o+0x54,0);pm_write32(m,o+0x716,1);pm_write32(m,o+0x71a,1);return m->error;
}
int physics_inactive_contact(physics_world_read read,physics_world_write write,void *ctx,uint32_t o,const physics_trig *trig)
{ return physics_inactive_contact_ambient(read,write,ctx,o,trig,0); }
int physics_inactive_attitude(physics_world_read read,physics_world_write write,void *ctx,uint32_t o,const physics_trig *trig)
{
    physics_memory memory={read,write,ctx,0},*m=&memory;int32_t old[9],matrix[9],position[3],pivot[3],angles[3]={0},angle,b4,bc,dt=pm_i32(m,0x5904fc);int status;
    vr(m,o+0x18,old,9);vr(m,o+0x2b2,matrix,9);vr(m,o+0x2a6,position,3);vr(m,o+0xd8,pivot,3);b4=pm_i32(m,o+0xb4);bc=pm_i32(m,o+0xbc);
    physics_matrix_rotate(matrix,1,(uint32_t)pm_trunc(pm_mul(pm_i32(m,o+0xb8),dt),14),trig);
    status=physics_angle_lookup(pm_abs(old[1]),physics_length2_approx(pm_abs(old[0]),pm_abs(old[2])),trig,&angle);if (status) return status;
    if (angle>12740) {
        if (old[1]<0) { if (bc>0) bc=pm_neg(pm_sar(bc,2));bc=pm_sub(bc,5000); }
        else { if (bc<0) bc=pm_sar(pm_neg(bc),2);bc=pm_add(bc,5000); }
    }
    status=physics_angle_lookup(pm_abs(old[7]),physics_length2_approx(pm_abs(old[6]),pm_abs(old[8])),trig,&angle);if (status) return status;
    if (angle>12740) {
        if (old[7]>0) { if (b4>0) b4=pm_neg(pm_trunc(bc,2));b4=pm_sub(b4,5000); }
        else { if (b4<0) b4=pm_neg(pm_trunc(bc,2));b4=pm_add(b4,5000); }
    }
    angles[0]=pm_trunc(pm_mul(b4,dt),14);angles[2]=pm_trunc(pm_mul(bc,dt),14);
    physics_pivot_rotate(matrix,position,pivot,angles,trig);
    if (pm_load(m,0x590564,4)%10u==0) { status=physics_matrix_normalize(matrix);if (status) return status; }
    vw(m,o+0x2b2,matrix,9);vw(m,o+0x2a6,position,3);pm_write32(m,o+0xb4,b4);pm_write32(m,o+0xbc,bc);return m->error;
}
int physics_inactive_pose(physics_world_read read,physics_world_write write,void *ctx,uint32_t o)
{
    physics_memory memory={read,write,ctx,0},*m=&memory;int32_t position[3],matrix[12],sum=0;unsigned i;
    vr(m,o+0x78,position,3);vw(m,o+0x2a6,position,3);vr(m,o+0x2b2,matrix,12);vw(m,o+0x18,matrix,12);vw(m,o,position,3);
    for (i=0;i<4;++i) sum=pm_add(sum,pm_trunc(pm_mul(pm_i32(m,o+0x148+i*70u),(int32_t)pm_load(m,o+0x6fe,2)),pm_load(m,o+0x700,1)));
    pm_write32(m,o+4,pm_add(position[1],pm_sar(sum,3)));vw(m,o+0x78,position,3);return m->error;
}
int physics_inactive_hull(physics_world_read read,physics_world_write write,void *ctx,uint32_t o)
{
    physics_memory memory={read,write,ctx,0},*m=&memory;int32_t half=pm_i16(m,pm_load(m,o+0x3ce,4)+0x26),x[2],z[2],rx,rz,matrix[12];uint32_t mesh=pm_load(m,o+0x71e,4),vertices=pm_load(m,mesh+4,4);unsigned i;
    pm_write32(m,o+0x722,pm_i32(m,o+0x78));pm_write32(m,o+0x726,pm_i32(m,o+0x138));pm_write32(m,o+0x72a,pm_i32(m,o+0x80));
    vr(m,o+0x18,matrix,12);vw(m,o+0x72e,matrix,12);x[0]=pm_i32(m,o+0x632);x[1]=pm_i32(m,o+0x63e);z[0]=pm_i32(m,o+0x63a);z[1]=pm_i32(m,o+0x646);
    for (i=0;i<4;++i) {
        pm_store(m,vertices+i*8u,(uint32_t)pm_sar(x[i==1 || i==2],3),2);pm_store(m,vertices+i*8u+2,0,2);
        pm_store(m,vertices+i*8u+4,(uint32_t)pm_sar(pm_mul(half,i<2 ? -8 : 8),3),2);
    }
    rx=pm_abs(x[0])<pm_abs(x[1]) ? pm_abs(x[1]) : pm_abs(x[0]);rz=pm_abs(z[0])<pm_abs(z[1]) ? pm_abs(z[1]) : pm_abs(z[0]);
    pm_store(m,mesh+0xc,(uint32_t)pm_sar(rx<=rz ? pm_add(pm_sar(rx,1),rz) : pm_add(pm_sar(rz,1),rx),1),2);return m->error;
}
static int32_t coef(physics_memory *m,int32_t value,uint32_t a)
{ return pm_trunc(pm_mul(value,(int32_t)pm_load(m,a,2)),pm_load(m,a+2,1)); }
static int32_t surface(physics_memory *m,uint32_t o,int32_t v,unsigned offset)
{
    return pm_sar(pm_add(coef(m,v,UINT32_C(0x546018)+(uint32_t)pm_i32(m,o+0x716)*40u+offset),coef(m,v,UINT32_C(0x546018)+(uint32_t)pm_i32(m,o+0x71a)*40u+offset)),1);
}
static uint32_t packed(physics_memory *m,int32_t force,uint32_t config)
{ return ((uint32_t)force&65535u)|(pm_load(m,config,2)<<16); }
static void inactive_drive(physics_memory *m,uint32_t o,int32_t dt)
{
    int32_t oldx=pm_i32(m,o+0xe4),oldz=pm_i32(m,o+0xec),f,drive,target,budget,cap,force,air=pm_i8(m,o+0xde1),delta,applied,term;uint32_t descriptor;
    f=surface(m,o,pm_i16(m,o+0x6ca),8);f=pm_trunc(pm_mul(pm_mul(f,pm_i32(m,o+0xf4)),(int32_t)pm_load(m,o+0x6a6,2)),pm_load(m,o+0x6a8,1));
    f=pm_mul(f,3);pm_write32(m,o+0xe4,pm_damp(oldx,packed(m,f,o+0x6cc),dt));pm_write32(m,o+0xe8,pm_rate_step(pm_i32(m,o+0xe8),pm_i16(m,o+0x6be),dt));
    f=surface(m,o,pm_i16(m,o+0x6ae),0);pm_write32(m,o+0xec,pm_damp(oldz,packed(m,f,o+0x6b0),dt));
    pm_write32(m,o+0xb4,pm_damp(pm_i32(m,o+0xb4),pm_load(m,o+0x6c6,4),dt));pm_write32(m,o+0xbc,pm_damp(pm_i32(m,o+0xbc),pm_load(m,o+0x6c6,4),dt));
    pm_write32(m,o+0xb8,air==4 ? pm_decay(pm_i32(m,o+0xb8),pm_load(m,o+0x6c6,4),dt) : pm_damp(pm_i32(m,o+0xb8),pm_load(m,o+0x6c2,4),dt));
    pm_write32(m,o+0xc0,pm_i32(m,o+0xe4));
    descriptor=air==4 ? pm_load(m,o+0x6b6,4) : pm_i32(m,o+0xa0) ? pm_load(m,o+0x6ba,4) : packed(m,surface(m,o,pm_i16(m,o+0x6b2),0),o+0x6b4);
    drive=pm_damp(pm_i32(m,o+0xa4),descriptor,dt);target=pm_i32(m,o+0xec);budget=surface(m,o,pm_mul(dt,pm_i32(m,o+0x696)),4);
    if (target>0 && target>drive) { delta=pm_sub(target,drive);drive=pm_add(drive,delta<budget ? delta : budget); }
    else if (target<0 && pm_neg(drive)<pm_neg(target)) { delta=pm_sub(drive,target);drive=pm_sub(drive,delta<budget ? delta : budget); }
    term=pm_sar(drive,12);term=pm_mul(term,term);force=pm_sub(pm_i32(m,o+0x66e),pm_mul(pm_i32(m,o+(pm_i16(m,o+0xdd6)<2501 ? 0x672u : 0x712u)),term));
    if (pm_i16(m,o+0xdd6)>=2501) force=pm_add(force,pm_i32(m,o+0xc9e));
    if (force<0) force=0;
    if (force>pm_i32(m,o+0x666)) force=pm_i32(m,o+0x666);
    term=pm_mul(pm_mul(pm_sar(pm_i32(m,o+0x94),8),pm_sar(force,8)),dt);if (air==4) term=pm_mul(term,4);
    drive=pm_add(drive,pm_sar(term,15));term=pm_trunc(pm_mul(pm_mul(pm_mul(pm_sub(4,air),pm_sar(pm_i32(m,o+0x676),23)),pm_sar(pm_i32(m,o+0x98),8)),dt),2);
    f=drive;drive=drive<1 ? pm_add(drive,term) : pm_sub(drive,term);if ((f<1 && drive>0) || (f>0 && drive<0)) drive=0;
    cap=pm_i32(m,o+0x662);if (drive<pm_neg(cap)) drive=pm_neg(cap);if (drive>cap) drive=cap;pm_write32(m,o+0xa4,drive);
    delta=air==4 ? 0 : pm_sub(drive,target);budget=surface(m,o,pm_mul(dt,pm_i32(m,o+0x68e)),4);
    if (delta>budget) { applied=budget;pm_write32(m,o+0xa0,pm_sub(delta,budget)); }
    else if (delta<pm_neg(budget)) { applied=pm_neg(budget);pm_write32(m,o+0xa0,pm_add(delta,budget)); }
    else { applied=delta;pm_write32(m,o+0xa0,0); }
    target=pm_add(target,applied);pm_write32(m,o+0xec,target);pm_write32(m,o+0xcc,pm_sub(target,oldz));pm_write32(m,o+0xc4,pm_sub(pm_i32(m,o+0xe4),oldx));
    f=pm_trunc(pm_i32(m,o+0xf4),8);pm_write32(m,o+0xf4,f);term=pm_trunc(pm_mul(pm_i32(m,o+0x66a),pm_i32(m,o+0x9c)),8);
    term=coef(m,pm_mul(term,f),o+0x67a);term=pm_sub(pm_i32(m,o+0xb8),pm_mul(dt,term));
    f=pm_mul((int32_t)pm_load(m,o+0x682,2),pm_i32(m,o+0xe4));f=pm_neg(f)>=0 ? pm_sar(pm_neg(f),pm_load(m,o+0x684,1)) : pm_neg(pm_sar(f,pm_load(m,o+0x684,1)));
    pm_write32(m,o+0xb8,pm_add(term,f));
}
static uint32_t entity_object(physics_memory *m,uint32_t id)
{ uint32_t link=pm_load(m,pm_load(m,0x55c694,4)+id*8u,4);return link ? pm_load(m,link+4,4) : 0; }
static int inactive_collision(physics_memory *m,uint32_t o,const physics_trig *trig)
{
    int32_t kind=pm_i16(m,o+0x134);unsigned i;int status;
    if (kind==3) { pm_write32(m,o+0x50,pm_neg(pm_i32(m,o+0x50)));pm_write32(m,o+0x58,pm_neg(pm_i32(m,o+0x58))); }
    if (kind==4) {
        int32_t normal[3],v[3],matrix[9],x,z,angle;physics_collision_response response;uint32_t route,target,ma;
        vr(m,o+0x154,normal,3);vr(m,o+0x50,v,3);
        status=physics_collision_respond(normal,v,0,0,pm_i32(m,0x52ee9c),0,trig,&response);if (status) return status;
        vw(m,o+0x154,response.normal,3);vw(m,o+0x50,response.velocity,3);
        route=entity_object(m,pm_load(m,o+0x110b,4));target=entity_object(m,pm_load(m,route+0x78+pm_load(m,o+0x111b,4)*4u,4));
        x=pm_sub(pm_i32(m,target),pm_i32(m,o+0x78));z=pm_sub(pm_i32(m,target+8),pm_i32(m,o+0x80));vr(m,o+0x2b2,matrix,9);
        v[0]=pm_sar(pm_add(pm_mul(x,matrix[0]),pm_mul(z,matrix[2])),12);v[2]=pm_sar(pm_add(pm_mul(z,matrix[8]),pm_mul(x,matrix[6])),12);
        for (i=0;(pm_abs(v[0])>=32768 || pm_abs(v[2])>=32768) && i<32;++i) { v[0]=pm_sar(v[0],1);v[2]=pm_sar(v[2],1); }
        status=physics_angle_lookup(v[0],v[2],trig,&angle);if (status) return status;physics_matrix_rotate(matrix,1,(uint32_t)pm_neg(angle),trig);vw(m,o+0x2b2,matrix,9);
        ma=pm_load(m,o+0x14,4);for (i=0;i<3;++i) v[i]=response.normal[i];
        if (ma) { vr(m,ma,matrix,9);transform(v,matrix,0); } else return -4; /* native consumes undefined local output */
        v[2]=0;transform(v,matrix,1);pm_write32(m,o+0x50,pm_add(pm_i32(m,o+0x50),pm_mul(v[0],32)));pm_write32(m,o+0x58,pm_add(pm_i32(m,o+0x58),pm_mul(v[2],32)));
    }
    return m->error;
}
int physics_inactive_step_ambient(physics_world_read read,physics_world_write write,void *ctx,uint32_t o,uint32_t id,const physics_trig *trig,const physics_vehicle_services *services,const int32_t *miss_words)
{
    physics_memory memory={read,write,ctx,0},*m=&memory;int32_t dt=pm_i32(m,0x5904fc),timer=pm_i16(m,o+0xdd6),v,world[3],matrix[9],local[3];unsigned i;int large,status;
    if (timer>0) {
        pm_store(m,o+0xdd6,(uint32_t)pm_sub(timer,dt),2);v=pm_sub(pm_i32(m,o+0xcaa),pm_mul(pm_i32(m,o+0xcb2),dt));pm_write32(m,o+0xcaa,v<0 ? 0 : v);
        if (pm_i16(m,o+0xdd6)<1 && (!pm_load(m,o+0xde5,1) || pm_load(m,o+0xdcd,1)==8)) { pm_write32(m,o+0xc9e,0);pm_store(m,o+0xdd6,0,2);pm_write32(m,o+0xcaa,0);pm_store(m,o+0xdcb,0,1); }
    }
    if (pm_i32(m,o+0xdaf)>0) {
        if (m->error) return m->error;
        status=frame_weapon_spin_update(read,write,ctx,o,65536);if (status) return status;
    }
    timer=pm_i16(m,o+0xdd0);
    if (timer) {
        if (dt<timer) pm_store(m,o+0xdd0,(uint32_t)pm_sub(timer,dt),2);
        else {
            pm_store(m,o+0xdd0,0,2);pm_write32(m,o+0x6a6,pm_i32(m,o+0xdd2));
            status=frame_temp_effects_expire(read,write,ctx,(uint32_t)pm_i16(m,o+0xdce));if (status) return status;
        }
    }
    if (m->error) return m->error;
    status=physics_inactive_contact_ambient(read,write,ctx,o,trig,miss_words);if (status) return status;
    vr(m,o+0x50,world,3);vr(m,o+0x18,matrix,9);large=physics_velocity_large(world);physics_velocity_transform(world,matrix,local,0,large);vw(m,o+0xe4,local,3);
    inactive_drive(m,o,dt);vr(m,o+0xe4,local,3);physics_velocity_transform(local,matrix,world,1,large);vw(m,o+0x50,world,3);pm_write32(m,o+0x54,pm_add(pm_i32(m,o+0x54),pm_i32(m,o+0xc8)));
    if (m->error) return m->error;
    status=inactive_collision(m,o,trig);if (status) return status;
    for (i=0;i<3;++i) pm_write32(m,o+0x84+i*4u,pm_i32(m,o+0x78+i*4u));
    for (i=0;i<3;++i) pm_write32(m,o+0x78+i*4u,pm_add(pm_i32(m,o+0x78+i*4u),pm_trunc(pm_mul(pm_i32(m,o+0x50+i*4u),dt),14)));
    if (m->error) return m->error;
    status=physics_inactive_attitude(read,write,ctx,o,trig);if (status) return status;
    status=physics_inactive_pose(read,write,ctx,o);if (status) return status;
    if (!services || !services->scene) return -8;
    status=services->scene(services->ctx,PHYSICS_SCENE_INACTIVE_EFFECTS,o,id,0);if (status) return status;
    return physics_inactive_hull(read,write,ctx,o);
}
int physics_inactive_step(physics_world_read read,physics_world_write write,void *ctx,uint32_t o,uint32_t id,const physics_trig *trig,const physics_vehicle_services *services)
{ return physics_inactive_step_ambient(read,write,ctx,o,id,trig,services,0); }
