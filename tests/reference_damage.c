#include "physics_body.h"
#include "physics_collision.h"
#include "frame_math.h"
#include "reference_io.h"
static unsigned char ram[0x600000];
static const uint32_t object=0x1000;
static uint32_t seed,counter,calls,trace,fail_at;static int fail_status;
/* Contract probe only: ordered shared-state writes and injected service errors.
 * The real particle/impact game operation remains a separate migration stage. */
static int impact(void *ctx,uint32_t o,uint32_t record,int32_t kind)
{
    unsigned index=(record-o-0x11cu)/70u;(void)ctx;
    seed=seed*UINT32_C(0x19660d)+UINT32_C(0x3c6ef35f);counter++;calls++;
    trace=trace*16u+index*2u+(kind==4)+1u;
    return calls==fail_at ? fail_status : 0;
}
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *data,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(ram+at,data,n);return 1;}
static void load(uint32_t at,unsigned n,unsigned width)
{unsigned i;for(i=0;i<n;i++)oracle_set(ram+at+i*width,oracle_require(),width);}
static void shape(uint32_t mesh,int output)
{
    uint32_t nv,nf,vertices=mesh+0x1000,faces=mesh+0x2000;unsigned i,j;
    if(output) {nv=oracle_get(ram+mesh,2);nf=oracle_get(ram+mesh+2,2);}
    else {
        nv=oracle_require();nf=oracle_require();if(nv>256||nf>256)exit(2);
        oracle_set(ram+mesh,nv,2);oracle_set(ram+mesh+2,nf,2);
        oracle_set(ram+mesh+4,vertices,4);oracle_set(ram+mesh+8,faces,4);
    }
    for(i=0;i<nv;i++)for(j=0;j<3;j++) {
        uint32_t at=vertices+i*8+j*2;
        if(output) {uint32_t v=oracle_get(ram+at,2);oracle_put(v>32767 ? v|0xffff0000u : v);}
        else oracle_set(ram+at,oracle_require(),2);
    }
    for(i=0;i<nf*6;i++) {
        if(output)oracle_put(oracle_get(ram+faces+i*4,4));
        else oracle_set(ram+faces+i*4,oracle_require(),4);
    }
}
int main(void)
{
    uint32_t op;oracle_binary();fwrite("DM01",1,4,stdout);
    while(oracle_word(&op)) {
        unsigned i;int status;int32_t world[3];uint32_t player,kind;
        memset(ram+object,0,0x2000);
        load(object+0xdf7,4,1);load(object+0xdfb,4,1);load(object+0x292,2,1);
        player=oracle_require();kind=oracle_require();oracle_set(ram+object+0xde5,player,1);
        load(object+0x2a6,12,4);load(object+0x30e,12,4);
        load(object+0xdff,4,1);load(object+0xe03,4,1);
        for(i=0;i<3;i++)world[i]=oracle_signed(oracle_require());
        shape(0x4000,0);shape(0x8000,0);
        oracle_set(ram+object+0x296,0x4000,4);oracle_set(ram+object+0x2fe,0x8000,4);
        if(op==0)status=physics_mesh_vertex_normals(read_data,write_data,0,0x4000,kind);
        else if(op==1)status=physics_body_damage(read_data,write_data,0,object,world);
        else if(op==2)status=physics_turret_damage(read_data,write_data,0,object,world);
        else if(op==3)status=physics_track_damage(read_data,write_data,0,object,oracle_signed(kind),world);
        else if(op==4) {
            uint32_t external;physics_collision_services services={impact,0,0};
            load(object+0x50,3,4);load(object+0xb8,1,4);load(object+0xec,1,4);load(0x52ee9c,1,4);
            external=oracle_require();load(0x18000,9,4);oracle_set(ram+object+0x14,external?0x18000:0,4);
            for(i=0;i<3;i++)oracle_set(ram+object+i*4,(uint32_t)world[i],4);
            for(i=0;i<4;i++) {uint32_t r=object+0x11cu+i*70u;load(r+24,1,2);load(r+40,1,4);load(r+56,3,4);}
            fail_at=oracle_require();fail_status=oracle_signed(oracle_require());seed=oracle_require();counter=oracle_require();calls=trace=0;
            status=physics_ground_collision_step(read_data,write_data,0,object,frame_math_trig(),&services);
        }
        else return 2;
        oracle_put((uint32_t)status);
        for(i=0;i<4;i++)oracle_put(ram[object+0xdf7+i]);
        for(i=0;i<4;i++)oracle_put(ram[object+0xdfb+i]);
        for(i=0;i<2;i++)oracle_put(ram[object+0x292+i]);
        shape(0x4000,1);shape(0x8000,1);
        if(op==4) {
            for(i=0;i<3;i++)oracle_put(oracle_get(ram+object+0x50+i*4,4));
            oracle_put(oracle_get(ram+object+0xb8,4));
            for(i=0;i<4;i++) {unsigned j;for(j=0;j<3;j++)oracle_put(oracle_get(ram+object+0x11cu+i*70u+56u+j*4u,4));}
            oracle_put(seed);oracle_put(counter);oracle_put(calls);oracle_put(trace);
        }
    }
    return ferror(stdin) ? 3 : 0;
}
