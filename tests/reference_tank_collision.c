#include "physics_collision.h"
#include "reference_io.h"
struct vehicle_default {uint32_t address,value;};
#include "frame_vehicle_defaults.h"
static unsigned char ram[0x600000];
static const uint32_t objects[2]={0x10000,0x20000};
static uint32_t seed,counter,calls,trace,fail_at;static int fail_status;
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *data,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(ram+at,data,n);return 1;}
static void load(uint32_t at,unsigned n,unsigned width)
{unsigned i;for(i=0;i<n;i++)oracle_set(ram+at+i*width,oracle_require(),width);}
static void emit(uint32_t at,unsigned n,unsigned width)
{unsigned i;for(i=0;i<n;i++){uint32_t v=oracle_get(ram+at+i*width,width);oracle_put(width==2&&v>32767?v|0xffff0000u:v);}}
static int impact(void *ctx,uint32_t o,uint32_t record,int32_t kind)
{
    unsigned index=(record-o-0x11cu)/70u,actor=o==objects[0]?0:1;(void)ctx;
    seed=seed*UINT32_C(0x19660d)+UINT32_C(0x3c6ef35f);counter++;calls++;
    trace=trace*32u+actor*8u+index*2u+(kind==4)+1u;
    return calls==fail_at?fail_status:0;
}
static void actor(unsigned index)
{
    uint32_t o=objects[index],entity=oracle_require(),external,mesh=o+0x3000,i,j;
    if(entity!=index+1)exit(2);
    memset(ram+o,0,0x6000);
    oracle_set(ram+0x7000+entity*8,0x8000+entity*8,4);oracle_set(ram+0x8004+entity*8,o,4);
    load(o,3,4);load(o+0x18,9,4);external=oracle_require();load(o+0x1000,9,4);oracle_set(ram+o+0x14,external?o+0x1000:0,4);
    oracle_set(ram+o+0x48,o+0x2000,4);load(o+0x200c,1,2);load(o+0x60,6,4);
    load(o+0xe07,1,1);load(o+0xded,1,1);load(o+0xd90,1,1);load(o+0xde9,1,1);load(o+0x632,6,4);
    for(i=0;i<4;i++)load(o+0x128+i*70u,3,4);
    load(o+0x50,3,4);load(o+0xe4,3,4);load(o+0x84,3,4);load(o+0x10c6,1,4);load(o+0xde5,1,1);
    load(o+0xdf7,4,1);load(o+0xdfb,4,1);load(o+0x292,2,1);load(o+0x2a6,12,4);load(o+0xdff,4,1);
    oracle_set(ram+o+0x296,mesh,4);oracle_set(ram+mesh,4,2);oracle_set(ram+mesh+2,2,2);oracle_set(ram+mesh+4,o+0x4000,4);oracle_set(ram+mesh+8,o+0x5000,4);
    for(i=0;i<4;i++)load(o+0x4000+i*8,3,2);
    for(i=0;i<2;i++){
        static const unsigned indices[2][3]={{0,1,2},{2,1,3}};load(o+0x5000+i*24,3,2);
        for(j=0;j<3;j++)oracle_set(ram+o+0x5000+i*24+8+j,indices[i][j],1);
    }
}
static void emit_actor(uint32_t o)
{
    unsigned i;emit(o+0x50,3,4);emit(o+0x10c6,1,4);emit(o+0xdf7,4,1);emit(o+0xdfb,4,1);emit(o+0x292,2,1);
    for(i=0;i<4;i++)emit(o+0x4000+i*8,3,2);
    for(i=0;i<2;i++)emit(o+0x5000+i*24,3,2);
}
int main(void)
{
    uint32_t op,i;oracle_binary();fwrite("TC01",1,4,stdout);
    for(i=0;i<sizeof(vehicle_defaults)/sizeof(vehicle_defaults[0]);i++){
        uint32_t at=vehicle_defaults[i].address;if(at>=0x531b38u&&at<=0x531b64u)oracle_set(ram+at,vehicle_defaults[i].value,4);
    }
    emit(0x531b38,12,4);
    while(oracle_word(&op)) {
        uint32_t own=oracle_require(),peer,limit;int32_t speed;int status;physics_collision_services services={impact,0,0};
        if(own>1)exit(2);
        speed=oracle_signed(oracle_require());load(0x52ee9c,1,4);load(0x5904fc,1,4);limit=oracle_require();services.separation_limit=limit;
        peer=oracle_require();if(peer!=UINT32_MAX&&peer>1)exit(2);oracle_set(ram+0x546014,peer==UINT32_MAX?0:objects[peer],4);load(0x546008,1,4);load(0x546010,1,4);
        fail_at=oracle_require();fail_status=oracle_signed(oracle_require());seed=oracle_require();counter=oracle_require();calls=trace=0;
        oracle_set(ram+0x55c694,0x7000,4);actor(0);actor(1);
        for(i=0;i<9;i++){uint32_t id=oracle_require();if(id!=UINT32_MAX&&id>1)exit(2);oracle_set(ram+0x53e8e0u+i*4u,id==UINT32_MAX?0:id+1,4);}
        if(op==1)status=physics_tank_overlap_response(read_data,write_data,0,objects[own],objects[1-own],speed,&services);
        else if(op==2)status=physics_tank_collision_step(read_data,write_data,0,objects[own],&services);
        else return 2;
        oracle_put((uint32_t)status);emit_actor(objects[0]);emit_actor(objects[1]);
        peer=oracle_get(ram+0x546014,4);oracle_put(!peer?UINT32_MAX:peer==objects[0]?0:1);emit(0x546008,1,4);emit(0x546010,1,4);
        oracle_put(seed);oracle_put(counter);oracle_put(calls);oracle_put(trace);
    }
    return ferror(stdin)?3:0;
}
