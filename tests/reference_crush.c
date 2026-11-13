/* Concrete production Crush methods and shared scheduler. Parent/device
 * callbacks are explicit boundaries; records persist after their pointer clears. */
#include "frame_deformable.h"
#include "physics_geometry.h"
#include "reference_io.h"
static unsigned char ram[0x800000];
static uint32_t allocation,record_count,calls,trace[1024][3],shape_releases;
static int activate_status,parent_status;
static physics_deform_result sample;
static frame_entity_factory_services services;
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{ (void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1; }
static int write_data(void *ctx,uint32_t at,const void *in,uint32_t n)
{ (void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(ram+at,in,n);return 1; }
static int allocate(void *ctx,uint32_t bytes,uint32_t *out)
{ (void)ctx;if(bytes>0x400000-allocation)return -4;*out=allocation;allocation+=(bytes+3u)&~3u;memset(ram+*out,0,bytes);return 0; }
static int release(void *ctx,uint32_t at) { (void)ctx;if(at==0x80100)shape_releases++;return 0; }
static void set(uint32_t at,uint32_t v) { oracle_set(ram+at,v,4); }
static uint32_t word(uint32_t at) { return oracle_get(ram+at,4); }
static void load(uint32_t at,uint32_t count) { uint32_t i;for(i=0;i<count;i++)set(at+i*4,oracle_require()); }
static void head(uint32_t at) { set(at,at);set(at+4,at);set(at+8,at); }
static void list(uint32_t at)
{
    uint32_t count=oracle_require(),i,result;head(at);
    for(i=0;i<count;i++)if(frame_list_append(read_data,write_data,0,oracle_require(),at,allocate,release,0,1000,&result))exit(2);
}
static void emit_list(uint32_t at)
{
    uint32_t node=word(at),count=0;while(node!=at) { if(count++==1024)exit(2);node=word(node); }
    oracle_put(count);node=word(at);while(node!=at) { oracle_put(word(node+8));node=word(node); }
}
static int method(void *ctx,uint32_t target,uint32_t id,uint32_t has,uint32_t payload,uint32_t *out)
{
    int status;(void)ctx;(void)has;(void)payload;
    if(calls==1024)return -5;
    trace[calls][0]=target-0x6000;trace[calls][1]=id;trace[calls++][2]=word(0x55c698);
    if(target==0x6000) { if(id==1)return frame_deformable_tick(read_data,write_data,0,id,&services,out);*out=id;return 0; }
    if(target==0x6001) { status=frame_entity_enable_tick(read_data,write_data,0,id,allocate,release,0,services.work_limit,out);return status?status:activate_status; }
    if(target==0x6002)return frame_entity_disable_tick(read_data,write_data,0,id,release,0,services.work_limit,out);
    if(target==0x6003) { set(0x55c698,word(0x55c698)+7);*out=123;return parent_status; }
    return -5;
}
static int operation(uint32_t op,const uint32_t a[3],uint32_t limit,uint32_t *out)
{
    int status;uint32_t mode=a[0];services.work_limit=limit;set(0x5904fc,a[0]);
    switch(op) {
    case 1:return frame_deformable_tick(read_data,write_data,0,1,&services,out);
    case 2:return frame_deformable_enter(read_data,write_data,0,1,&services,out);
    case 3:status=frame_deformable_leave(read_data,write_data,0,1,&services,out);if(!status)*out=*out!=0;return status;
    case 4:status=frame_deformable_set_flag(read_data,write_data,0,1,mode,out);if(!status)*out=*out==0x10000;return status;
    case 5:return frame_deformable_destroy(read_data,write_data,0,1,&services,out);
    case 6:return frame_entities_drain_queues(read_data,write_data,0,method,0,release,0,limit);
    case 7:return frame_entities_tick(read_data,write_data,0,method,0,limit);
    case 8: { int32_t p[3]={oracle_signed(a[0]),oracle_signed(a[1]),oracle_signed(a[2])};return physics_deform_query(read_data,write_data,0,0x10000,p,&sample); }
    default:return -1;
    }
}
static void emit(int status,uint32_t result)
{
    uint32_t i,j,cursor=word(0x55b6c0);
    oracle_put((uint32_t)status);oracle_put(result);oracle_put(word(0x55c698));oracle_put(word(0x100f9));oracle_put(word(0x100f1));
    oracle_put(ram[0x100cb]);oracle_put(ram[0x100cc]);oracle_put(word(0x100d5)!=0);oracle_put(word(0x100dd)!=0);oracle_put(word(0x10048)!=0);oracle_put(shape_releases);oracle_put(word(0x55b6ac));
    for(i=1;i<=3;i++)oracle_put(word(0x10010+(i-1)*0x1000));
    for(i=0;i<record_count;i++)for(j=0;j<8;j++)oracle_put(word(0x90000+i*32+j*4));
    oracle_put(!cursor||cursor==0x50020?0:word(cursor+8));
    for(i=0;i<5;i++)emit_list(0x50000+i*16);
    oracle_put(calls);for(i=0;i<calls;i++)for(j=0;j<3;j++)oracle_put(trace[i][j]);
    for(i=0;i<3;i++)oracle_put((uint32_t)sample.local_raw[i]);
    for(i=0;i<3;i++)oracle_put((uint32_t)sample.local_clamped[i]);
    oracle_put((uint32_t)sample.vertex_height);oracle_put((uint32_t)sample.edge_weight);oracle_put((uint32_t)sample.offset);oracle_put(sample.activated_records);
}
int main(void)
{
    uint32_t steps,i,j,attached,private_mesh;oracle_binary();fwrite("CR01",1,4,stdout);
    services.allocate=allocate;services.release=release;services.method=method;
    while(oracle_word(&steps)) {
        memset(ram,0,sizeof(ram));memset(&sample,0,sizeof(sample));allocation=0x100000;calls=shape_releases=0;set(0x54b3d8,1);set(0x55c694,0x7000);set(0x55c698,4);
        for(i=1;i<=3;i++) { set(0x7000+i*8,0x8000+i*8);set(0x8000+i*8,0x9100);set(0x8004+i*8,0x10000+(i-1)*0x1000); }
        set(0x911c,3);set(0x9124,0x9140);set(0x9140,0x6000);set(0x9144,0x6001);set(0x9148,0x6002);
        set(0x921c,15);set(0x9224,0x9240);for(i=0;i<16;i++)set(0x9240+i*4,0x6003);set(0x544f80,0x9200);
        for(i=0;i<3;i++)set(0xa000+i*64+28,i);
        set(0x545eb8,0xa000);set(0x545f3c,0xa040);set(0x545f34,0xa080);
        load(0x10000,3);load(0x74aee0,3);load(0x100f9,1);load(0x100f1,1);ram[0x100cb]=(unsigned char)oracle_require();ram[0x100cc]=(unsigned char)oracle_require();
        attached=oracle_require();private_mesh=oracle_require();set(0x100dd,oracle_require()?0x83000:0);activate_status=oracle_signed(oracle_require());parent_status=oracle_signed(oracle_require());load(0x55b6ac,1);
        for(i=1;i<=3;i++)load(0x10010+(i-1)*0x1000,1);
        record_count=oracle_require();if(record_count>16)return 2;set(0x100cd,record_count);set(0x100d5,attached?0x90000:0);load(0x90000,record_count*8);
        set(0x10048,private_mesh?0x80100:0x80000);set(0x100ed,private_mesh?64:0);
        for(i=0;i<2;i++) { uint32_t shape=0x80000+i*0x100;oracle_set(ram+shape,3,2);set(shape+4,0x81000); }
        for(i=0;i<3;i++)for(j=0;j<3;j++)oracle_set(ram+0x81000+i*8+j*2,j==1?200-i*50:i*10,2);
        for(i=0;i<9;i++)set(0x10018+i*4,i%4?0:4096);
        set(0x10014,0x10018);
        set(0x10060,(uint32_t)-16000);set(0x1006c,16000);set(0x10068,(uint32_t)-16000);set(0x10074,16000);
        set(0x100ba,(uint32_t)-2000);set(0x100be,2000);set(0x100c2,(uint32_t)-2000);set(0x100c6,2000);set(0x100b6,45000);
        for(i=0;i<5;i++)list(0x50000+i*16);
        set(0x53cd18,0x50000);set(0x54b488,0x50010);set(0x55b6a8,0x50020);set(0x55b6bc,0x50030);set(0x55b6b8,0x50040);head(0x50050);set(0x55ae54,0x50050);
        for(i=0;i<steps;i++) { uint32_t op=oracle_require(),a[3],limit,out=0x7182;int status;for(j=0;j<3;j++)a[j]=oracle_require();limit=oracle_require();status=operation(op,a,limit,&out);emit(status,out); }
    }
    return ferror(stdin)?3:0;
}
