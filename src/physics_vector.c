#include "physics_vector.h"
#include "physics_memory.h"

void physics_vector_normalize(int32_t v[3])
{
    int32_t a=pm_abs(v[0]),b=pm_abs(v[1]),c=pm_abs(v[2]),length;unsigned i;
    if (b>a && b>c) length=pm_add(pm_add(b,pm_sar(a,2)),pm_sar(c,2));
    else if (a>=b && a>c) length=pm_add(pm_add(a,pm_sar(b,2)),pm_sar(c,2));
    else length=pm_add(pm_add(c,pm_sar(a,2)),pm_sar(b,2));
    if (length<=0) return;
    if (length>262144) for (i=0;i<3;++i) v[i]=v[i]/pm_sar(length,12);
    else for (i=0;i<3;++i) v[i]=pm_mul(v[i],4096)/length;
}
void physics_vector_cross(const int32_t a[3],const int32_t b[3],int32_t out[3])
{
    int32_t temp[3];unsigned i;
    for (i=0;i<3;++i) temp[i]=pm_sar(pm_sub(pm_mul(a[(i+1)%3],b[(i+2)%3]),pm_mul(b[(i+1)%3],a[(i+2)%3])),12);
    for (i=0;i<3;++i) out[i]=temp[i];
}
void physics_vector_yaw(const int32_t v[3],int32_t out[3],uint32_t angle,const physics_trig *t)
{
    uint32_t i=(angle&65532u)>>2;int32_t x=v[0],y=v[1],z=v[2],sine=t->sine[i],cosine=t->cosine[i];
    out[0]=pm_trunc(pm_sub(pm_mul(x,cosine),pm_mul(z,sine)),12);
    out[2]=pm_trunc(pm_add(pm_mul(x,sine),pm_mul(z,cosine)),12);out[1]=y;
}
int physics_yaw_correction(physics_world_read read,physics_world_write write,void *ctx,uint32_t o)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;int32_t v[3],axis[3],cross[3],delta=pm_i32(m,o+0xcc),change;uint32_t config;
    v[0]=pm_i32(m,o+0xe4);v[1]=0;v[2]=pm_i32(m,o+0xec);physics_vector_normalize(v);
    axis[0]=pm_i32(m,0x531c98);axis[1]=pm_i32(m,0x531c9c);axis[2]=pm_i32(m,0x531ca0);physics_vector_cross(axis,v,cross);
    change=pm_trunc(pm_mul(pm_abs(delta),cross[1]),12);config=o+(delta<0 ? 0x686u : 0x68au);
    change=pm_trunc(pm_mul(change,(int32_t)pm_load(m,config,2)),pm_load(m,config+2,1));
    pm_write32(m,o+0xb8,delta<0 ? pm_sub(pm_i32(m,o+0xb8),change) : pm_add(pm_i32(m,o+0xb8),change));
    return m->error;
}
static int32_t scale(int32_t value,const physics_collision_scale *s)
{ return pm_trunc(pm_mul(value,(int32_t)s->multiplier),s->shift); }
int physics_collision_respond(const int32_t input_normal[3],const int32_t velocity[3],
                               const physics_collision_scale *local_z,const physics_collision_scale *final,
                               int32_t mode,int32_t player,const physics_trig *t,physics_collision_response *out)
{
    int32_t normal[3]={input_normal[0],0,input_normal[2]},toward[3]={pm_neg(velocity[0]),0,pm_neg(velocity[2])};
    int32_t speed=pm_trunc(physics_approximate_distance(velocity),12),d,reflected[3],yaw,v[3];unsigned i;
    for (i=0;i<3;++i) v[i]=velocity[i];
    physics_vector_normalize(normal);physics_vector_normalize(toward);
    d=pm_add(pm_trunc(pm_mul(toward[0],normal[0]),12),pm_trunc(pm_mul(toward[2],normal[2]),12));
    if (d>0) {
        int32_t damping=(mode==4 || mode==5) ? 870 : player ? 930 : 900;
        if (d>4096) d=4096;
        reflected[0]=pm_sub(pm_trunc(pm_mul(d,pm_add(normal[0],normal[0])),12),toward[0]);
        reflected[1]=0;reflected[2]=pm_sub(pm_trunc(pm_mul(d,pm_add(normal[2],normal[2])),12),toward[2]);
        if (physics_angle_lookup(pm_neg(normal[0]),normal[2],t,&yaw)<0) return -2;
        physics_vector_yaw(reflected,reflected,(uint32_t)pm_neg(yaw),t);
        reflected[2]=local_z ? scale(reflected[2],local_z) : pm_trunc(reflected[2],3);
        physics_vector_yaw(reflected,reflected,(uint32_t)yaw,t);
        for (i=0;i<3;i+=2) v[i]=final ? scale(pm_mul(speed,reflected[i]),final) : pm_trunc(pm_mul(pm_mul(speed,reflected[i]),damping),10);
    }
    v[0]=pm_add(v[0],pm_mul(normal[0],4));v[2]=pm_add(v[2],pm_mul(normal[2],4));
    for (i=0;i<3;++i) { out->normal[i]=normal[i];out->velocity[i]=v[i]; }
    out->approach=d;out->speed_units=speed;return 0;
}
int32_t physics_binary_one_point_seven(int32_t value)
{
    int64_t magnitude=value<0 ? -(int64_t)value : value;
    int64_t scaled=(magnitude*17)/10;
    if (magnitude && !(magnitude%10)) --scaled;
    return pm_signed((uint32_t)(value<0 ? -scaled : scaled));
}
static void transform(const int32_t *v,const int32_t *matrix,int32_t *out,int inverse)
{
    int32_t tmp[3];unsigned i,j;
    for (i=0;i<3;++i) { int32_t sum=0;for (j=0;j<3;++j) sum=pm_add(sum,pm_mul(v[j],matrix[inverse ? i+j*3u : i*3u+j]));tmp[i]=pm_sar(sum,12); }
    for (i=0;i<3;++i) out[i]=tmp[i];
}
int physics_tank_pair_response(physics_world_read read,physics_world_write write,void *ctx,
                                uint32_t self,uint32_t other,const int32_t input_normal[3],
                                const int32_t other_velocity[3],int32_t *approach)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;int32_t matrix[9],normal[3],their[3],mine[3],impulse[3],world[3],unit_a[3],unit_b[3];
    int32_t speed,d,dot,old_impulse_x,old_impulse_z;int capped=0;unsigned i;
    for (i=0;i<9;++i) matrix[i]=pm_i32(m,self+0x18+i*4u);
    transform(input_normal,matrix,normal,0);normal[1]=0;physics_vector_normalize(normal);
    mine[0]=pm_trunc(pm_i32(m,self+0xe4),4);mine[1]=0;mine[2]=pm_trunc(pm_i32(m,self+0xec),4);
    for (i=0;i<3;++i) their[i]=pm_trunc(other_velocity[i],4);
    transform(their,matrix,their,0);
    their[0]=pm_sub(their[0],mine[0]);their[1]=0;their[2]=pm_sub(their[2],mine[2]);
    speed=pm_trunc(physics_approximate_distance(their),12);physics_vector_normalize(their);
    d=pm_add(pm_trunc(pm_mul(normal[0],their[0]),12),pm_trunc(pm_mul(normal[2],their[2]),12));
    if (d<=2048) {
        for (i=0;i<3;++i) impulse[i]=i==1 ? 0 : pm_trunc(normal[i],2);
        transform(impulse,matrix,world,1);
        /*42D495..42D4D7: local Y is zero, but a tilted body transforms
         *that impulse into nonzero world Y. Exchange all three components. */
        for (i=0;i<3;++i) {
            pm_write32(m,self+0x50+i*4u,pm_add(pm_i32(m,self+0x50+i*4u),pm_mul(world[i],4)));
        }
        pm_write32(m,other+0x50,pm_sub(pm_i32(m,other+0x50),pm_mul(world[0],4)));
        pm_write32(m,other+0x58,pm_sub(pm_i32(m,other+0x58),pm_mul(world[2],4)));
        pm_write32(m,other+0x54,pm_sub(pm_i32(m,other+0x54),pm_mul(world[1],4)));
        *approach=d;return m->error;
    }
    if (d>4096) d=4096;
    for (i=0;i<3;++i) impulse[i]=i==1 ? 0 : pm_sub(pm_trunc(pm_mul(pm_mul(d,normal[i]),2),12),their[i]);
    for (i=0;i<3;++i) { unit_a[i]=i==1 ? 0 : pm_i32(m,self+0x50+i*4u);unit_b[i]=i==1 ? 0 : pm_i32(m,other+0x50+i*4u); }
    physics_vector_normalize(unit_a);physics_vector_normalize(unit_b);
    dot=pm_add(pm_mul(unit_a[0],unit_b[0]),pm_mul(unit_a[2],unit_b[2]));
    if (dot>=-12146421 && speed>3) speed=3;
    for (i=0;i<3;++i) impulse[i]=pm_trunc(pm_mul(pm_mul(speed,impulse[i]),600),10);
    old_impulse_x=impulse[0];old_impulse_z=impulse[2];
    if (dot>=-12146421 && physics_approximate_distance(impulse)>7000) {
        capped=1;physics_vector_normalize(impulse);
        for (i=0;i<3;++i) impulse[i]=physics_binary_one_point_seven(impulse[i]);
    }
    transform(impulse,matrix,world,1);
    for (i=0;i<3;++i) pm_write32(m,self+0x50+i*4u,pm_add(pm_i32(m,self+0x50+i*4u),pm_mul(world[i],16)));
    pm_write32(m,other+0x50,pm_sub(pm_i32(m,other+0x50),old_impulse_x));
    pm_write32(m,other+0x58,pm_sub(pm_i32(m,other+0x58),old_impulse_z));
    if (capped) {
        pm_write32(m,self+0x50,pm_rate_step(pm_i32(m,self+0x50),1000,1));
        pm_write32(m,self+0x58,pm_rate_step(pm_i32(m,self+0x58),1000,1));
        pm_write32(m,other+0x50,pm_rate_step(pm_i32(m,other+0x50),1000,1));
        pm_write32(m,other+0x50,pm_rate_step(pm_i32(m,other+0x50),1000,1));
    }
    *approach=d;return m->error;
}
