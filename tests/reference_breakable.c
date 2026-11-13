#include "physics_entities.h"
#include "frame_entities.h"
#include "frame_spatial.h"
#include "frame_math.h"
#include "reference_io.h"
static unsigned char ram[0x600000];
static const uint32_t vehicle=0x10000,objects[3]={0x20000,0x30000,0x40000};
static uint32_t next_allocation,calls,trace,fail_at,actions;static int fail_status;static int32_t delta;
static physics_entity_environment environment;static physics_obstacle_services services;
static uint32_t word(uint32_t at){return oracle_get(ram+at,4);}
static void set(uint32_t at,uint32_t v){oracle_set(ram+at,v,4);}
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *data,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(ram+at,data,n);return 1;}
static int allocate(void *ctx,uint32_t n,uint32_t *out)
{(void)ctx;if(next_allocation+n>0x400000)return -1;*out=next_allocation;next_allocation+=(n+3u)&~3u;memset(ram+*out,0,n);return 0;}
static int release(void *ctx,uint32_t at){(void)ctx;(void)at;return 0;}
static void load(uint32_t at,unsigned n,unsigned width){unsigned i;for(i=0;i<n;i++)oracle_set(ram+at+i*width,oracle_require(),width);}
static void emit(uint32_t at,unsigned n){unsigned i;for(i=0;i<n;i++)oracle_put(word(at+i*4));}
static int probe(uint32_t tag,uint32_t id)
{
    uint32_t o=objects[id-2],fx=word(o+0x8c);set(0x55b6ac,word(0x55b6ac)*1664525u+1013904223u);calls++;trace=trace*37u+tag;
    set(o+0xee,word(o+0xee)+(uint32_t)delta);set(o+0xf2,word(o+0xf2)+(uint32_t)delta);set(o+0xe2,word(o+0xe2)-(uint32_t)delta);set(o+0x80,word(o+0x80)+(uint32_t)delta);
    set(o+0xea,word(o+0xea)^16u);set(vehicle+0xec,0u-word(vehicle+0xec));
    if(actions&1u&&fx)ram[fx+0x18]^=16u;
    if(actions&2u)ram[vehicle+0xde5]=0;
    return calls==fail_at?fail_status:0;
}
static int custom(uint32_t fn,uint32_t id,uint32_t has_payload,uint32_t argument,uint32_t *out)
{
    int status=probe((fn-1000u)*65536u+id*256u+has_payload*128u+argument,id);set(0x55c698,word(0x55c698)+7u);*out=1234;return status;
}
static int physical_method(void *ctx,uint32_t fn,uint32_t id,uint32_t argument,uint32_t *out)
{(void)ctx;if(fn==0x40a3e0u)return physics_known_entity_method(&environment,fn,id,argument,out);return custom(fn,id,1,argument,out);}
static int native_method(void *ctx,uint32_t fn,uint32_t id,uint32_t payload,uint32_t argument,uint32_t *out)
{(void)ctx;if(fn==0x503cf0u)return frame_entity_enable_tick(read_data,write_data,0,id,allocate,release,0,word(0x7ff0),out);return custom(fn,id,payload,argument,out);}
static int scene(void *ctx,int phase,uint32_t object,uint32_t id,int32_t argument)
{
    uint32_t unused;(void)ctx;(void)object;
    if(phase==PHYSICS_SCENE_OBJECT_SOUND)return probe(0x600000u+id,id);
    if(phase==PHYSICS_SCENE_FORCE_FEEDBACK)return probe(0x900000u+id*65536u+(uint32_t)argument,id);
    if(phase==PHYSICS_SCENE_ENTITY_ACTIVATE)return frame_entity_activate(read_data,write_data,0,id,native_method,0,&unused);
    if(phase==PHYSICS_SCENE_REMOVE_GRID)return frame_spatial_remove(read_data,write_data,0,id,(uint32_t)argument,release,0,word(0x7ff0));
    return -8;
}
static uint32_t function(uint32_t id)
{return id==UINT32_MAX?0:id==1?0x40a3e0u:id==2?0x503cf0u:1000u+id;}
static void list(uint32_t head,int attachments)
{
    uint32_t n=oracle_require(),last=head,i;set(head+8,head);if(n>65537)exit(2);
    for(i=0;i<n;i++){
        uint32_t value=oracle_require(),node=next_allocation;next_allocation+=12;set(last,node);set(node+4,last);
        if(attachments){uint32_t data=next_allocation;next_allocation+=8;set(data+4,value);set(node+8,data);}else{if(value>2)exit(2);set(node+8,value+2);}
        last=node;
    }
    set(last,head);set(head+4,last);
}
static void emit_list(uint32_t head)
{uint32_t n=0,node=word(head);while(node!=head){if(n++==65536)exit(2);node=word(node);}oracle_put(n);node=word(head);while(node!=head){oracle_put(word(node+8)-2);node=word(node);}}
int main(void)
{
    uint32_t op,i,j;oracle_binary();fwrite("BK01",1,4,stdout);environment.read=read_data;environment.write=write_data;environment.trig=frame_math_trig();environment.services=&services;environment.scene=scene;
    services.method=physical_method;
    while(oracle_word(&op)){
        uint32_t result=0x7182,feedback,limit;int status;next_allocation=0x100000;calls=trace=0;set(0x54b3d8,1);set(0x55c694,0x7000);
        load(0x52ec94,2,4);feedback=oracle_require();set(0x58e7cc,1);set(0x58e7ec,feedback);set(0x53e928,0);limit=oracle_require();set(0x7ff0,limit);
        load(0x5904fc,1,4);load(0x55b6ac,1,4);load(0x55c698,1,4);fail_at=oracle_require();fail_status=oracle_signed(oracle_require());delta=oracle_signed(oracle_require());actions=oracle_require();
        set(0x7008,0x8000);set(0x8000,0x6300);set(0x8004,vehicle);load(vehicle,3,4);load(vehicle+0x78,3,4);load(vehicle+0x84,3,4);load(vehicle+0x50,3,4);load(vehicle+0xec,1,4);load(vehicle+0xde5,1,1);load(vehicle+0x118,1,4);load(vehicle+0x107e,1,4);
        for(i=0;i<3;i++){
            uint32_t o=objects[i],alive=oracle_require(),fx,head=o+0x7000;
            set(0x7010+i*8,alive?0x8010+i*8:0);set(0x8010+i*8,0x6000+i*0x100);set(0x8014+i*8,o);
            load(o,3,4);load(o+0x18,9,4);set(o+0x14,o+0x18);set(o+0x48,o+0x6000);load(o+0x600c,1,2);load(o+0x60,6,4);
            load(o+0x5c,1,4);load(o+0x94,1,4);load(o+0x80,1,4);load(o+0xea,1,4);load(o+0xee,1,4);load(o+0xf2,1,4);load(o+0xe2,1,4);load(o+0xda,1,4);
            set(o+0x84,oracle_require());set(o+0x88,oracle_require());fx=oracle_require();set(o+0x8c,fx==UINT32_MAX?0:o+0x6100);ram[o+0x6118]=(unsigned char)fx;
            load(o+0x90,1,4);load(o+0x10,1,4);set(o+0xfa,head);list(head,1);
        }
        for(i=0;i<3;i++){uint32_t c=0x6000+i*0x100;load(c+0x1c,1,4);set(c+0x24,c+0x40);for(j=0;j<4;j++)set(c+0x40+j*4,function(oracle_require()));}
        set(0x631c,3);set(0x6324,0x6340);memset(ram+0x6340,0,16);set(0x544f98,0x6300);set(0x53d13c,0x6000);
        set(0x545f3c,0xb000);set(0x545ee4,0xb100);set(0x545eec,0xb200);set(0x545ec4,0xb300);for(i=0;i<4;i++)set(0xb01c+i*0x100,i);
        load(0x544f70,4,4);for(i=0;i<64;i++){uint32_t head=0x50000+i*16;set(0x544e70+i*4,head);list(head,0);}set(0x55b6a8,0x52000);list(0x52000,0);
        set(0x531b98,0x10001);set(0x531b94,0xa0400);set(0x52ee9c,0);
        if(op==1)status=physics_known_entity_method(&environment,0x40a3e0,2,1,&result);
        else if(op==2)status=physics_obstacle_collision_step(read_data,write_data,0,vehicle,1,frame_math_trig(),&services);
        else return 2;
        oracle_put((uint32_t)status);oracle_put(result);emit(0x55b6ac,1);emit(0x55c698,1);oracle_put(calls);oracle_put(trace);emit(vehicle+0x50,3);emit(vehicle+0xec,1);oracle_put(ram[vehicle+0xde5]);emit(vehicle+0x118,1);emit(vehicle+0x107e,1);
        for(i=0;i<3;i++){uint32_t o=objects[i],fx=word(o+0x8c);emit(o+0x80,1);emit(o+0xea,1);emit(o+0xee,1);emit(o+0xf2,1);emit(o+0xe2,1);emit(o+0xda,1);emit(o+0x10,1);oracle_put(fx?ram[fx+0x18]:UINT32_MAX);}
        emit_list(0x52000);for(i=0;i<64;i++)emit_list(0x50000+i*16);
    }
    return ferror(stdin)?3:0;
}
