/* Fresh bootstrap, real World reader and real native dispatch. No dump or
 * captured scene state. Nil-shape cases isolate lifecycle/RNG from resource I/O. */
#include "frame_bootstrap.h"
#include "frame_native.h"
#include "frame_math.h"
#include "reference_io.h"
static unsigned char ram[0x800000];
static uint32_t cursor;
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *in,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(ram+at,in,n);return 1;}
static uint32_t word(uint32_t at){return oracle_get(ram+at,4);}
static void set(uint32_t at,uint32_t value){oracle_set(ram+at,value,4);}
static int allocate(void *ctx,uint32_t n,uint32_t *out)
{(void)ctx;if(n>0x400000-cursor)return -4;*out=cursor;cursor+=(n+3)&~3u;memset(ram+*out,0,n);return 0;}
static int zero_allocate(void *ctx,uint32_t a,uint32_t b,uint32_t *out)
{if(b&&a>0xffffffffu/b)return -4;return allocate(ctx,a*b,out);}
static int release(void *ctx,uint32_t at){(void)ctx;(void)at;return 0;}
static void emit_list(uint32_t head)
{
    uint32_t n=0,node=word(head);while(node!=head){if(n++==65536)exit(5);node=word(node);}
    oracle_put(n);node=word(head);while(node!=head){oracle_put(word(node+8));node=word(node);}
}
static void emit(int status,uint32_t result,uint32_t highest)
{
    uint32_t i,j,r,o;static const uint32_t lists[]={0x54b48c,0x54b488,0x53e4b4,0x55b6a8,0x55b6bc,0x55b6b8};
    oracle_put((uint32_t)status);oracle_put(result);oracle_put(word(0x55c68c));oracle_put(word(0x55b6ac));
    for(i=1;i<=highest;i++){
        r=word(word(0x55c694)+i*8);oracle_put(r!=0);if(!r)continue;o=word(r+4);
        oracle_put(word(o+16));for(j=0;j<3;j++)oracle_put(word(o+j*4));for(j=0;j<9;j++)oracle_put(word(o+24+j*4));
        oracle_put(word(o+20)!=0);oracle_put(word(o+0x90));oracle_put(oracle_get(ram+o+0x4c,2));
        oracle_put((uint32_t)(int32_t)(int16_t)oracle_get(ram+o+0x4e,2));
        oracle_put(oracle_get(ram+o+0x78,2));oracle_put(oracle_get(ram+o+0x7a,2));
        oracle_put(word(o+0x5c));oracle_put(word(o+0x94));oracle_put(oracle_get(ram+o+0xa8,2));
        oracle_put(word(o+72)!=0);oracle_put(word(o+0x88)!=0);oracle_put(word(o+0x8c)!=0);
    }
    for(i=0;i<6;i++)emit_list(word(lists[i]));
    for(i=0;i<64;i++)emit_list(word(0x544e70+i*4));
}
static void check(int status){if(status){fprintf(stderr,"bootstrap status %d\n",status);exit(6);}}
int main(void)
{
    uint32_t n,seed,is_static,distance,result,consumed,i,highest;unsigned char script[39000];int status;
    oracle_binary();fwrite("SL01",1,4,stdout);
    while(oracle_word(&n)){
        frame_native_context native={0};frame_entity_native_context *e=&native.base.base.base.base;
        if(n>=sizeof(script))return 2;
        seed=oracle_require();is_static=oracle_require();distance=oracle_require();
        if(fread(script,1,n,stdin)!=n)return 3;
        memset(ram,0,sizeof(ram));cursor=0x100000;
        e->read=read_data;e->write=write_data;e->services.allocate=allocate;e->services.zero_allocate=zero_allocate;
        e->services.release=release;e->services.work_limit=100000;
        native.base.base.base.parameters.trig=frame_math_trig();
        check(frame_native_bind(&native));set(0x54b3d8,1);
        check(frame_bootstrap_static_data(write_data,0));check(frame_bootstrap_world_constants(write_data,0));
        check(frame_bootstrap_c_locale(write_data,0,0x710000));check(frame_bootstrap_classes(read_data,write_data,0,&e->services,&result));
        check(frame_entity_table_initialize(read_data,write_data,0,4,&e->services));
        check(frame_list_create(read_data,write_data,0,allocate,release,0,100000,&result));set(0x54b48c,result);
        check(frame_list_create(read_data,write_data,0,allocate,release,0,100000,&result));set(0x54b488,result);
        set(0x55c688,is_static);set(0x55ae50,0x620000);set(0x55b6ac,seed);set(0x5366dc,distance<<10);set(0x5367a8,0x700000);
        set(0x544f70,(uint32_t)-5000);set(0x544f74,(uint32_t)-5000);set(0x544f78,12);set(0x544f7c,12);
        for(i=0;i<64;i++){
            check(frame_list_create(read_data,write_data,0,allocate,release,0,100000,&result));set(0x544e70+i*4,result);
        }
        result=0x7182;status=frame_world_load_span(read_data,write_data,0,0,0,script,n,&e->services,&result,&consumed);
        highest=word(0x536cc0);emit(status,result,highest);
        status=frame_entities_drain_queues(read_data,write_data,0,frame_native_dispatch,&native,release,0,100000);emit(status,0x7182,highest);
        status=0;for(i=1;i<=highest;i++){
            int s=frame_entity_dispatch(read_data,write_data,0,i,word(0x545ed0),0,0,frame_native_dispatch,&native,&result);if(s&&!status)status=s;
        }
        emit(status,0x7182,highest);
        status=frame_entities_drain_queues(read_data,write_data,0,frame_native_dispatch,&native,release,0,100000);emit(status,0x7182,highest);
    }
    return ferror(stdin)?3:0;
}
