/* Real production dispatcher and parent chains; only allocation/free are host
 * boundaries. Slots are scoped test interfaces, not a full bootstrap catalogue. */
#include "frame_native.h"
#include "reference_io.h"
enum { INIT=1,DESTROY=2,RELEASE=3,ACTIVATE=4,STOP=5,ENTER=6,LEAVE=7,DEACTIVATE=8,MODE=9,MOVE=10,COLLIDABLE=11,TICK=12,DESTROY_ALIAS=13,ACTIVATE_ALIAS=14,NOTIFY=15,SLOTS=16 };
static unsigned char ram[0x800000];
static uint32_t allocation,kind,records;
static frame_native_context native;
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{ (void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1; }
static int write_data(void *ctx,uint32_t at,const void *in,uint32_t n)
{ (void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(ram+at,in,n);return 1; }
static int allocate(void *ctx,uint32_t n,uint32_t *out)
{ (void)ctx;if(n>0x400000-allocation)return -4;*out=allocation;allocation+=(n+3)&~3u;memset(ram+*out,0,n);return 0; }
static int release(void *ctx,uint32_t at) { (void)ctx;(void)at;return 0; }
static uint32_t word(uint32_t at) { return oracle_get(ram+at,4); }
static void set(uint32_t at,uint32_t v) { oracle_set(ram+at,v,4); }
static uint32_t object(uint32_t id) { return 0x10000+(id-1)*0x1000; }
static void load(uint32_t at,uint32_t count) { uint32_t i;for(i=0;i<count;i++)set(at+i*4,oracle_require()); }
static void head(uint32_t at) { set(at,at);set(at+4,at);set(at+8,at); }
static void list(uint32_t at)
{
    uint32_t n=oracle_require(),i,result;head(at);
    for(i=0;i<n;i++)if(frame_list_append(read_data,write_data,0,oracle_require(),at,allocate,release,0,10000,&result))exit(2);
}
static void emit_list(uint32_t at)
{
    uint32_t n=0,node=word(at);while(node!=at) { if(n++==65536)exit(2);node=word(node); }
    oracle_put(n);node=word(at);while(node!=at) { oracle_put(word(node+8));node=word(node); }
}
static void classes(uint32_t parent_max)
{
    static const uint32_t common[SLOTS]={0,0x503ea0,0x504eb0,0x504c50,0x503cf0,0x503d30,0,0,0x504000,0,0,0,0,0,0x4041e0,0x4041f0};
    uint32_t c,i,j;
    for(i=0;i<4;i++) {
        c=0x9000+i*0x100;set(c+20,0xfd);set(c+28,i?15:parent_max);set(c+36,c+64);
        for(j=0;j<SLOTS;j++)set(c+64+j*4,common[j]);
    }
    set(0x9144,0x43f670);
    for(i=0;i<SLOTS;i++)set(0x9240+i*4,word(0x9140+i*4));
    set(0x9244,0x41f6e0);set(0x9248,0x41e890);set(0x9258,0x41c230);set(0x925c,0x41c270);set(0x9260,0x41c300);set(0x9264,0x41c7a0);set(0x9268,0x41c620);set(0x926c,0x41c660);set(0x9274,0x41f720);
    for(i=0;i<SLOTS;i++)set(0x9340+i*4,word(0x9240+i*4));
    set(0x9344,0x4079a0);set(0x9348,0x407f00);set(0x9358,0x407790);set(0x935c,0x4077e0);set(0x9364,0x407760);set(0x9370,0x407f70);
    set(0x55b6c4,0x9000);set(0x53e4b0,0x9100);set(0x544f80,0x9200);
    for(i=0;i<SLOTS;i++)set(0xa000+i*64+28,i);
    set(0x545ebc,0xa040);set(0x545ef4,0xa080);set(0x545ed4,0xa0c0);set(0x545f3c,0xa100);set(0x545f34,0xa140);set(0x545ed0,0xa200);set(0x545eb8,0xa300);
}
static int operation(uint32_t op,uint32_t a,uint32_t b,uint32_t c,uint32_t limit,uint32_t *out)
{
    static const uint32_t slots[]={0,ENTER,LEAVE,DEACTIVATE,MODE,MOVE,COLLIDABLE,0,INIT,0,0,NOTIFY,DESTROY_ALIAS,ACTIVATE_ALIAS};
    frame_entity_native_context *e=&native.base.base.base.base;uint32_t payload=0,has=0;int status;
    e->services.work_limit=limit;set(0x5904fc,a);
    if(op==7)return frame_entity_destroy(read_data,write_data,0,1,frame_native_dispatch,&native,release,0);
    if(op==9)return frame_entities_tick(read_data,write_data,0,frame_native_dispatch,&native,limit);
    if(op==10)return frame_entities_drain_queues(read_data,write_data,0,frame_native_dispatch,&native,release,0,limit);
    if(op>=14)return -1;
    if(op==4||op==6) { payload=a;has=1; }
    if(op==5) { set(0x90000,a);set(0x90004,b);set(0x90008,c);payload=0x90000;has=1; }
    status=frame_entity_dispatch(read_data,write_data,0,1,0xa000+slots[op]*64,has,payload,frame_native_dispatch,&native,out);
    if(!status&&op==4&&kind)*out=*out==object(1);
    /* Guest node/object addresses are represented by presence/logical identity. */
    if(!status&&op==2&&kind)*out=*out!=0;
    return status;
}
static void emit(int status,uint32_t out)
{
    uint32_t i,j,r,o,cursor=word(0x55b6c0);oracle_put((uint32_t)status);oracle_put(out);oracle_put(word(0x55c698));oracle_put(word(0x55c68c));
    for(i=1;i<=3;i++) {
        r=word(0x7000+i*8);oracle_put(r!=0);if(!r)continue;o=object(i);
        for(j=0;j<3;j++)oracle_put(word(o+j*4));
        for(j=0;j<9;j++)oracle_put(word(o+24+j*4));
        oracle_put(word(o+20)!=0);oracle_put(word(o+0x5c));oracle_put(word(o+0x90));oracle_put((uint32_t)(int32_t)(int16_t)word(o+0x4e));oracle_put(word(o+16));oracle_put(word(o+0x88)!=0);oracle_put(word(o+0x8c)!=0);oracle_put(word(o+72)!=0);
        if(i==1&&kind) { oracle_put(word(o+0xae));oracle_put(word(o+0xb2));oracle_put(word(o+0xb6));oracle_put(word(o+0xe5));oracle_put(ram[o+0xcb]);oracle_put(ram[o+0xcc]);oracle_put(word(o+0xd5)!=0);oracle_put(word(o+0xf9));oracle_put(word(o+0xdd)!=0);for(j=0;j<records*8;j++)oracle_put(word(0x30000+j*4)); }
    }
    oracle_put(!cursor||cursor==0x50030?0:word(cursor+8));for(i=0;i<6;i++)emit_list(0x50000+i*16);for(i=0;i<64;i++)emit_list(0x60000+i*16);
}
int main(void)
{
    uint32_t steps,i,j,parent_max,o;oracle_binary();fwrite("SO01",1,4,stdout);
    while(oracle_word(&steps)) {
        memset(ram,0,sizeof(ram));memset(&native,0,sizeof(native));allocation=0x100000;set(0x54b3d8,1);set(0x55c694,0x7000);set(0x55c690,16);set(0x55c68c,3);set(0x55c698,19);
        for(i=0;i<3;i++)set(0x532888+i*16,4096);
        kind=oracle_require();parent_max=oracle_require();classes(parent_max);
        for(i=1;i<=3;i++) { set(0x7000+i*8,0x8000+i*8);set(0x8000+i*8,i==1&&kind?0x9300:0x9200);set(0x8004+i*8,object(i)); }
        o=object(1);load(o,3);load(o+24,9);set(o+20,oracle_require()?o+24:0);load(o+0x90,1);oracle_set(ram+o+0x4e,oracle_require(),2);load(o+0x5c,1);load(o+16,1);
        if(oracle_require()) { set(o+72,0x20000);oracle_set(ram+0x2000c,64,2); }
        if(oracle_require()) { set(o+0x88,0x21000);set(0x21000,0x22000);set(0x21044,64); }
        if(oracle_require()) { set(o+0x8c,0x23000);set(0x2300c,0x24000);set(0x23010,104);set(0x23018,128);set(0x23030,0x24100);set(0x23028,0x24200); }
        if(kind) { set(o+0xdd,0x31000);load(o+0xae,2);load(o+0xb6,1);load(o+0xe5,1);ram[o+0xcb]=(unsigned char)oracle_require();ram[o+0xcc]=(unsigned char)oracle_require();load(o+0xf9,1);set(o+0xd5,oracle_require()?0x30000:0);set(o+0xed,oracle_require()?64:0);records=oracle_require();if(records>8)return 2;set(o+0xcd,records);load(0x30000,records*8); }
        for(i=2;i<=3;i++)load(object(i)+16,1);
        for(i=0;i<6;i++)list(0x50000+i*16);
        set(0x54b48c,0x50000);set(0x54b488,0x50010);set(0x53cd18,0x50020);set(0x55b6a8,0x50030);set(0x55b6bc,0x50040);set(0x55b6b8,0x50050);
        set(0x544f70,(uint32_t)-5000);set(0x544f74,(uint32_t)-5000);set(0x544f78,12);set(0x544f7c,12);
        for(i=0;i<64;i++) { set(0x544e70+i*4,0x60000+i*16);list(0x60000+i*16); }head(0x61000);set(0x55ae54,0x61000);
        { frame_entity_native_context *e=&native.base.base.base.base;e->read=read_data;e->write=write_data;e->services.allocate=allocate;e->services.release=release;e->services.work_limit=10000;if(frame_native_bind(&native))return 2; }
        for(i=0;i<steps;i++) { uint32_t op=oracle_require(),a[3],limit,out=0x7182;int status;for(j=0;j<3;j++)a[j]=oracle_require();limit=oracle_require();status=operation(op,a[0],a[1],a[2],limit,&out);emit(status,out); }
    }
    return ferror(stdin)?3:0;
}
