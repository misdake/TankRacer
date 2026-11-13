#include "physics_obstacle.h"
#include "frame_math.h"
#include "reference_io.h"
static unsigned char ram[0x600000];
static const uint32_t vehicle=0x10000,objects[2]={0x20000,0x30000};
static uint32_t calls,trace,fail_at,actions;static int fail_status;static int32_t delta;
static uint32_t word(uint32_t at){return oracle_get(ram+at,4);}
static void set(uint32_t at,uint32_t value){oracle_set(ram+at,value,4);}
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *data,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(ram+at,data,n);return 1;}
static void load(uint32_t at,unsigned n,unsigned width){unsigned i;for(i=0;i<n;i++)oracle_set(ram+at+i*width,oracle_require(),width);}
static void emit(uint32_t at,unsigned n,unsigned width){unsigned i;for(i=0;i<n;i++){uint32_t v=oracle_get(ram+at+i*width,width);oracle_put(width==2&&v>32767?v|0xffff0000u:v);}}
static int probe(uint32_t tag)
{set(0x55b6ac,word(0x55b6ac)*UINT32_C(0x19660d)+UINT32_C(0x3c6ef35f));calls++;trace=trace*37u+tag;return calls==fail_at?fail_status:0;}
static int method(void *ctx,uint32_t fn,uint32_t entity,uint32_t argument,uint32_t *result)
{
    uint32_t o=objects[entity-2],id=fn-1000u;int status;(void)ctx;if(entity<2||entity>3)return -6;
    status=probe(id*256u+entity*16u+argument);set(0x55c698,word(0x55c698)+5u);*result=entity+argument+(word(0x55b6ac)&255u);
    set(vehicle+0x50,word(vehicle+0x50)+(uint32_t)delta);set(vehicle+0x54,word(vehicle+0x54)-(uint32_t)delta);set(vehicle+0xec,0u-word(vehicle+0xec));
    if(actions&1u)set(o,word(o)+(uint32_t)delta);
    if(actions&2u)ram[vehicle+0xde5]=0;
    if(actions&4u){set(0x80000,0x80000);set(0x80100,0x80100);}
    return status;
}
static int feedback(void *ctx,int32_t intensity)
{int status;(void)ctx;status=probe(1000u+(uint32_t)intensity);set(vehicle+0x54,word(vehicle+0x54)+(uint32_t)delta);return status;}
static int impact(void *ctx,uint32_t o,uint32_t record,int32_t kind)
{int status;unsigned index=(record-o-0x11cu)/70u;(void)ctx;status=probe(2000u+index*8u+(uint32_t)kind);set(o+0x50,word(o+0x50)+(uint32_t)delta);set(o+0xec,0u-word(o+0xec));return status;}
static void list(uint32_t sentinel,uint32_t node)
{
    uint32_t n=oracle_require(),last=sentinel,i;if(n>2)exit(2);
    for(i=0;i<n;i++){uint32_t id=oracle_require();if(id>1)exit(2);set(last,node);set(node+8,id+2);last=node;node+=16;}
    set(last,sentinel);
}
static void collider(unsigned i)
{
    uint32_t o=objects[i],external,class_id;unsigned j;
    memset(ram+o,0,0x4000);class_id=oracle_require();if(class_id>3)exit(2);set(0x8100u+(i+2u)*8u,0x9000u+class_id*0x100u);
    load(o,3,4);load(o+0x18,9,4);external=oracle_require();load(o+0x1000,9,4);set(o+0x14,external?o+0x1000:0);set(o+0x48,o+0x2000);load(o+0x200c,1,2);load(o+0x60,6,4);
    load(o+0x5c,1,4);load(o+0x94,1,4);load(o+0x80,1,4);load(o+0xea,1,4);
    for(j=0;j<6;j++)load(o+0x201c+j*2,1,2);
}
static void class_table(unsigned i)
{
    unsigned j;uint32_t at=0x9000u+i*0x100u;load(at+0x1c,1,4);set(at+0x24,at+0x40);
    for(j=0;j<3;j++){uint32_t id=oracle_require();set(at+0x40+j*4,id==UINT32_MAX?0:1000u+id);}
}
int main(void)
{
    uint32_t op;unsigned i;oracle_binary();fwrite("OB01",1,4,stdout);
    set(0x55c694,0x7000);for(i=1;i<=3;i++){set(0x7000u+i*8u,0x8100u+i*8u);set(0x8104u+i*8u,i==1?vehicle:objects[i-2]);}
    set(0x544f98,0x9000);set(0x53d13c,0x9100);set(0x545ec4,0xa000);set(0x545f04,0xa100);set(0x53cd18,0x80100);
    for(i=0;i<64;i++)set(0x544e70u+i*4u,0x80000);
    while(oracle_word(&op)) {
        physics_collision_services collision={impact,0,0};physics_obstacle_services services={&collision,method,feedback,0};int status;uint32_t class_id,result=0x7182;
        memset(ram+vehicle,0,0x2000);load(0x52ee9c,1,4);load(0x5904fc,1,4);load(0x58e7ec,1,4);set(0x58e7cc,1);set(0x53e928,0);
        load(0xa01c,1,4);load(0xa11c,1,4);load(0x531b98,1,4);load(0x531b94,1,4);load(0x55b6ac,1,4);load(0x55c698,1,4);
        fail_at=oracle_require();fail_status=oracle_signed(oracle_require());delta=oracle_signed(oracle_require());actions=oracle_require();calls=trace=0;
        class_id=oracle_require();if(class_id>3)exit(2);set(0x8108,0x9000u+class_id*0x100u);
        load(vehicle,3,4);load(vehicle+0x78,3,4);load(vehicle+0x84,3,4);load(vehicle+0x50,3,4);load(vehicle+0xec,1,4);load(vehicle+0xde5,1,1);
        for(i=0;i<4;i++)load(vehicle+0x128+i*70u,3,4);
        load(vehicle+0x118,1,4);load(vehicle+0x107e,1,4);load(vehicle+0xd95,1,4);load(vehicle+0xd8c,1,4);
        collider(0);collider(1);for(i=0;i<4;i++)class_table(i);load(0x544f70,4,4);list(0x80000,0x80200);list(0x80100,0x80300);
        if(op==1)status=physics_obstacle_collision_step(read_data,write_data,0,vehicle,1,frame_math_trig(),&services);
        else if(op==2)status=physics_deformable_collision_step(read_data,write_data,0,vehicle,1,frame_math_trig(),&services);
        else if(op==3)status=physics_entity_collision_dispatch(read_data,write_data,0,2,0xa000,1,&services,&result);
        else return 2;
        oracle_put((uint32_t)status);oracle_put(result);emit(vehicle+0x50,3,4);emit(vehicle+0xec,1,4);emit(vehicle+0xde5,1,1);emit(vehicle+0x118,1,4);emit(vehicle+0x107e,1,4);emit(vehicle+0xd95,1,4);emit(vehicle+0xd8c,1,4);
        emit(0x55b6ac,1,4);emit(0x55c698,1,4);oracle_put(calls);oracle_put(trace);
        for(i=0;i<2;i++)emit(objects[i],3,4);
        {uint32_t node=word(0x80000),n=0;while(node!=0x80000){n++;node=word(node);}oracle_put(n);node=word(0x80100);n=0;while(node!=0x80100){n++;node=word(node);}oracle_put(n);}
    }
    return ferror(stdin)?3:0;
}
