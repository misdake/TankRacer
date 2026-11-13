#include "physics_collision.h"
#include "physics_memory.h"
#include "physics_objects.h"
#include "physics_body.h"

static void vector_read(physics_memory *m,uint32_t a,int32_t *v,unsigned n)
{ unsigned i;for (i=0;i<n;++i) v[i]=pm_i32(m,a+i*4u); }
static void vector_write(physics_memory *m,uint32_t a,const int32_t *v,unsigned n)
{ unsigned i;for (i=0;i<n;++i) pm_write32(m,a+i*4u,v[i]); }
static void transform(const int32_t v[3],const int32_t matrix[9],int32_t out[3],int inverse)
{
    int32_t t[3];unsigned i,j;
    for (i=0;i<3;++i) { int32_t sum=0;for (j=0;j<3;++j) sum=pm_add(sum,pm_mul(v[j],matrix[inverse ? j*3u+i : i*3u+j]));t[i]=pm_sar(sum,12); }
    for (i=0;i<3;++i) out[i]=t[i];
}
static void transform_at(physics_memory *m,int32_t v[3],uint32_t matrix,int inverse)
{ int32_t data[9];if (!matrix) return;vector_read(m,matrix,data,9);transform(v,data,v,inverse); }
static int impact(const physics_collision_services *s,uint32_t o,uint32_t record,int32_t kind)
{ return s && s->impact ? s->impact(s->ctx,o,record,kind) : -5; }
uint32_t physics_collision_direction(int32_t x,int32_t z)
{
    if (z>0) return x>0 ? (x<=z ? 2u : 1u) : (pm_neg(x)<=z ? 2u : 0u);
    return x>0 ? (x<=pm_neg(z) ? 3u : 1u) : (pm_neg(x)<=pm_neg(z) ? 3u : 0u);
}
static int approaching(physics_memory *m,uint32_t a,uint32_t b)
{
    unsigned i;int32_t now=0,next=0,dt=pm_i32(m,0x5904fc);
    for (i=0;i<3;++i) {
        int32_t p=pm_i32(m,a+i*4u),q=pm_i32(m,b+i*4u),d=pm_sub(p,q);
        int32_t ap=pm_trunc(pm_mul(pm_i32(m,a+0x50+i*4u),dt),14);
        int32_t bp=pm_trunc(pm_mul(pm_i32(m,b+0x50+i*4u),dt),14);
        int32_t predicted=pm_sub(pm_add(p,ap),pm_add(q,bp));
        now=pm_add(now,pm_mul(d,d));next=pm_add(next,pm_mul(predicted,predicted));
    }
    return next<now;
}
int physics_tanks_approaching(physics_world_read read,void *ctx,uint32_t a,uint32_t b)
{ physics_memory m={read,0,ctx,0};int result=approaching(&m,a,b);return m.error ? m.error : result; }
static int corner_inside(physics_memory *m,uint32_t box,uint32_t record)
{
    int32_t v[3];unsigned i;
    for (i=0;i<3;++i) v[i]=pm_sub(pm_i32(m,record+12+i*4u),pm_i32(m,box+i*4u));
    transform_at(m,v,box+0x18,0);
    return v[0]>pm_i32(m,box+0x632) && v[2]>pm_i32(m,box+0x63a) && v[0]<pm_i32(m,box+0x63e) && v[2]<pm_i32(m,box+0x646);
}
static uint32_t facing_normal(physics_memory *m,uint32_t self,uint32_t other,int32_t normal[3])
{
    int32_t direction[3];uint32_t code;unsigned i;
    for (i=0;i<3;++i) direction[i]=pm_sub(pm_i32(m,other+i*4u),pm_i32(m,self+i*4u));
    transform_at(m,direction,self+0x18,0);physics_vector_normalize(direction);
    code=physics_collision_direction(direction[0],direction[2]);
    vector_read(m,UINT32_C(0x531b38)+code*12u,normal,3);transform_at(m,normal,self+0x18,1);return code;
}
static int32_t horizontal_speed(physics_memory *m,uint32_t o)
{
    int32_t x=pm_abs(pm_i32(m,o+0x50)),z=pm_abs(pm_i32(m,o+0x58));
    return pm_add(pm_sub(z,pm_sar(x<z ? x : z,1)),x);
}
static void separate(physics_memory *m,uint32_t o,const int32_t normal[3],int32_t factor)
{
    unsigned i;for (i=0;i<3;i+=2) pm_write32(m,o+0x50+i*4u,pm_sub(pm_i32(m,o+0x50+i*4u),pm_mul(normal[i],factor)));
}
int physics_tank_overlap_response(physics_world_read read,physics_world_write write,void *ctx,
    uint32_t self,uint32_t other,int32_t speed,const physics_collision_services *services)
{
    physics_memory memory={read,write,ctx,0},*m=&memory;unsigned i;int overlap=0,status;
    int32_t normal_a[3],normal_b[3],mode=pm_i32(m,0x52ee9c);uint32_t code_a,code_b;
    for (i=0;i<4;++i) if (corner_inside(m,self,other+0x11c+i*70u)) { overlap=1;break; }
    for (i=0;i<4;++i) if (corner_inside(m,other,self+0x11c+i*70u)) {
        if (m->error) return m->error;
        if (pm_load(m,self+0xde5,1) && mode!=4 && mode!=5) {
            status=impact(services,self,self+0x11c+i*70u,i<2 ? 3 : 4);if (status) return status<0 ? status : -5;
        }
        overlap=1;
    }
    if (m->error) return m->error;
    if (!overlap) return 0;
    code_a=facing_normal(m,self,other,normal_a);code_b=facing_normal(m,other,self,normal_b);
    if (m->error) return m->error;
    if (speed<450000) {
        int32_t threshold=pm_add(pm_add(horizontal_speed(m,self),horizontal_speed(m,other)),20000);
        uint32_t iteration=0,limit=services && services->separation_limit ? services->separation_limit : 65536u;
        do {
            if (iteration++==limit) return -3;
            separate(m,self,normal_a,10);separate(m,other,normal_b,10);
            if (m->error) return m->error;
        } while (pm_add(horizontal_speed(m,self),horizontal_speed(m,other))<threshold && approaching(m,self,other));
        if (speed>250000) {
            int32_t point[3];
            if (code_a>=2) { vector_read(m,other,point,3);if (m->error) return m->error;status=physics_track_damage(read,write,ctx,self,(int32_t)code_a,point);if (status) return status; }
            if (code_b>=2) { vector_read(m,self,point,3);if (m->error) return m->error;status=physics_track_damage(read,write,ctx,other,(int32_t)code_b,point);if (status) return status; }
        }
    } else if (approaching(m,self,other)) {
        if (code_a<2 && code_b<2) { separate(m,self,normal_a,50);separate(m,other,normal_b,50); }
        else {
            int32_t velocity[3],approach;vector_read(m,other+0x50,velocity,3);if (m->error) return m->error;
            status=physics_tank_pair_response(read,write,ctx,self,other,normal_b,velocity,&approach);if (status) return status;
        }
    }
    return m->error ? m->error : 1;
}
int physics_tank_collision_step(physics_world_read read,physics_world_write write,void *ctx,
    uint32_t o,const physics_collision_services *services)
{
    physics_memory memory={read,write,ctx,0},*m=&memory;physics_object_hit hit={0};
    int32_t start[3],end[3],relative[3]={0},mode;unsigned i;int status;
    vector_read(m,o,start,3);for (i=0;i<3;++i) end[i]=pm_sub(pm_mul(start[i],2),pm_i32(m,o+0x84+i*4u));
    if (m->error) return m->error;
    status=physics_tank_query(read,ctx,o,start,end,&hit);if (status<=0) return status;
    relative[0]=pm_sub(pm_i32(m,hit.object+0x50),pm_i32(m,o+0x50));relative[2]=pm_sub(pm_i32(m,hit.object+0x58),pm_i32(m,o+0x58));
    mode=pm_i32(m,0x52ee9c);
    if (mode==4 || mode==5) {
        pm_write32(m,0x546008,pm_i32(m,hit.object+0x50));pm_write32(m,0x546010,pm_i32(m,hit.object+0x58));pm_store(m,0x546014,hit.object,4);
    }
    if (m->error) return m->error;
    status=physics_tank_overlap_response(read,write,ctx,o,hit.object,physics_approximate_distance(relative),services);if (status<0) return status;
    if (!status && approaching(m,o,hit.object)) {
        int32_t normal[3],velocity[3],approach;
        facing_normal(m,hit.object,o,normal);vector_read(m,hit.object+0x50,velocity,3);if (m->error) return m->error;
        status=physics_tank_pair_response(read,write,ctx,o,hit.object,normal,velocity,&approach);if (status) return status;
    }
    pm_write32(m,o+0x10c6,pm_add(pm_i32(m,o+0x10c6),pm_mul(pm_i32(m,0x5904fc),4)));return m->error;
}
int physics_ground_collision_step(physics_world_read read,physics_world_write write,void *ctx,
    uint32_t o,const physics_trig *trig,const physics_collision_services *services)
{
    physics_memory memory={read,write,ctx,0},*m=&memory;int32_t initial[3],last=-1;unsigned i;int flipped=0,status;
    vector_read(m,o+0x50,initial,3);
    for (i=0;i<4;++i) {
        uint32_t record=o+0x11c+i*70u;int32_t kind=pm_i16(m,record+24);
        if (kind==4 || kind==5) {
            int32_t normal[3],velocity[3],corner,yaw,mode=pm_i32(m,0x52ee9c),player=(int32_t)pm_load(m,o+0xde5,1);physics_collision_response response;
            if (kind==4) {
                if (player && pm_abs(pm_i32(m,o+0xec))>150000 && mode!=2 && mode!=3) {
                    if (m->error) return m->error;
                    status=impact(services,o,record,i<2 ? 3 : 4);if (status) return status<0 ? status : -5;
                }
                last=pm_i32(m,record+40);
            }
            vector_read(m,record+56,normal,3);vector_read(m,o+0x50,velocity,3);if (m->error) return m->error;
            status=physics_collision_respond(normal,velocity,0,0,mode,player,trig,&response);if (status) return status;
            vector_write(m,record+56,response.normal,3);vector_write(m,o+0x50,response.velocity,3);
            corner=pm_i32(m,record+40);yaw=pm_i32(m,o+0xb8);
            if (response.approach>0 && (uint32_t)corner<=3u) {
                int negative=corner==0 || corner==3;
                if ((negative && yaw>0) || (!negative && yaw<0)) yaw=pm_neg(pm_trunc(pm_mul(yaw,300),10));
                else {
                    int32_t amount=pm_trunc(pm_mul(physics_approximate_distance(initial),300),10);
                    amount=pm_trunc(pm_mul(amount,response.approach),12);yaw=negative ? pm_sub(yaw,amount) : pm_add(yaw,amount);
                }
                pm_write32(m,o+0xb8,yaw);
            }
        } else if (kind==3 && !flipped) {
            pm_write32(m,o+0x50,pm_neg(pm_i32(m,o+0x50)));pm_write32(m,o+0x58,pm_neg(pm_i32(m,o+0x58)));flipped=1;
        }
        if (m->error) return m->error;
    }
    if (last!=-1 && pm_abs(pm_i32(m,o+0xec))>450000) {
        int32_t point[3]={last==0 || last==1 ? -1024 : 1024,0,last==0 || last==2 ? 1024 : -1024};
        transform_at(m,point,pm_load(m,o+0x14,4),1);
        for (i=0;i<3;++i) point[i]=pm_add(point[i],pm_i32(m,o+i*4u));
        if (m->error) return m->error;
        return physics_track_damage(read,write,ctx,o,pm_i32(m,o+0xec)>0 ? 2 : 3,point);
    }
    return m->error;
}
