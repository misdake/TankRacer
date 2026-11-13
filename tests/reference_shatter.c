/* Unchanged production debris methods, real bootstrap/factory/lifecycle.
 * Typed fixtures replace only resource fields and the input vehicle velocity. */
#include "frame_bootstrap.h"
#include "frame_native.h"
#include "frame_math.h"
#include "reference_io.h"
static unsigned char ram[0x800000];
static uint32_t cursor,o,f,pieces,count,peer,blast_calls,blast_actions;
static int blast_status;
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *in,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(ram+at,in,n);return 1;}
static uint32_t word(uint32_t at){return oracle_get(ram+at,4);}
static void set(uint32_t at,uint32_t v){oracle_set(ram+at,v,4);}
static int allocate(void *ctx,uint32_t n,uint32_t *out)
{(void)ctx;if(n>0x400000-cursor)return -4;*out=cursor;cursor+=(n+3)&~3u;memset(ram+*out,0,n);return 0;}
static int zero_allocate(void *ctx,uint32_t a,uint32_t b,uint32_t *out)
{if(b&&a>UINT32_MAX/b)return -4;return allocate(ctx,a*b,out);}
static int release(void *ctx,uint32_t at){(void)ctx;(void)at;return 0;}
static void check(int status){if(status){fprintf(stderr,"setup %d\n",status);exit(6);}}
static void load(uint32_t at,uint32_t n,uint32_t width){uint32_t i;for(i=0;i<n;i++)oracle_set(ram+at+i*width,oracle_require(),width);}
static void emit(uint32_t at,uint32_t n){uint32_t i;for(i=0;i<n;i++)oracle_put(word(at+i*4));}
static void list(uint32_t head){uint32_t n=0,node=word(head);while(node!=head){if(n++==65536)exit(6);node=word(node);}
    oracle_put(n);node=word(head);while(node!=head){oracle_put(word(node+8));node=word(node);}}
static void load_list(uint32_t head){uint32_t n=oracle_require(),i,unused;set(head,head);set(head+4,head);set(head+8,head);
    for(i=0;i<n;i++)check(frame_list_append(read_data,write_data,0,oracle_require(),head,allocate,release,0,100000,&unused));}
static void animation(int input,uint32_t at)
{
    static const uint32_t offsets[]={10,14,18,22,30,34,46,48,50,54,4,52,0,2,38,39,40,41,42,43,44};
    static const uint32_t widths[] ={4, 4, 4, 4, 4, 4, 2, 2, 2, 4,2,2,2,2,1,1,1,1,1,1,1};
    uint32_t i;
    for(i=0;i<21;i++){
        if(input)oracle_set(ram+at+offsets[i],oracle_require(),widths[i]);
        else {uint32_t v=oracle_get(ram+at+offsets[i],widths[i]);if(i==11||i==12)v=(uint32_t)(int32_t)(int16_t)v;oracle_put(v);}
    }
}
static int blast_probe(void *ctx,uint32_t target,uint32_t entity,uint32_t source,uint32_t mode,uint32_t *out)
{
    (void)ctx;if(target!=0x7fabc0 || entity!=2 || source!=o || mode!=4)return -5;
    blast_calls++;set(0x55b6ac,word(0x55b6ac)*1664525u+1013904223u);set(peer+80,0u-word(peer+80));
    if(blast_actions&1u)set(peer,word(o)+20480u);
    set(0x55c698,word(0x55c698)+7u);*out=1234;return blast_status;
}
static void snapshot(int status,uint32_t result)
{
    uint32_t i,at,a;oracle_put((uint32_t)status);oracle_put(result);emit(0x55b6ac,1);
    {uint32_t slot=word(0x55c698);if(slot==word(word(0x545ed0)+28))slot=8;else if(slot==word(word(0x545ef0)+28))slot=23;else if(slot==word(word(0x545eec)+28))slot=26;else if(slot==word(word(0x545f3c)+28))slot=4;else if(slot==word(word(0x545eb8)+28))slot=12;else if(slot==word(word(0x545f14)+28))slot=27;else if(slot==word(word(0x545f14)+28)+7u)slot=34;oracle_put(slot);}
    oracle_put(blast_calls);emit(peer,1);emit(peer+80,1);
    emit(f+24,1);oracle_put(oracle_get(ram+f+22,2));emit(o+16,1);{uint32_t node=word(0x55b6c0);oracle_put(node && node!=word(0x55b6a8)?word(node+8):0);}emit(o+0x80,1);emit(o+0xea,1);emit(o+0xee,2);emit(o+0xe2,1);emit(o+0xda,1);emit(o+24,9);
    for(i=0;i<count;i++){
        at=pieces+i*104;emit(at+12,3);emit(at,3);emit(at+28,12);emit(at+88,3);emit(at+76,1);
        oracle_put((uint32_t)(int32_t)(int16_t)oracle_get(ram+at+80,2));oracle_put((uint32_t)(int32_t)(int16_t)oracle_get(ram+at+82,2));emit(at+84,1);emit(at+100,1);
    }
    a=word(f+40);if(a)animation(0,a);
    list(word(0x54b48c));list(word(0x54b488));list(word(0x55b6a8));list(word(0x55b6bc));list(word(0x55b6b8));
    for(i=0;i<64;i++)list(word(0x544e70+i*4));
}
int main(void)
{
    uint32_t steps;static const unsigned char script[]="FSTPC 5 Fall First 0 0 0 0 0 0 nil () Obstacle Peer 0 0 0 0 0 0 nil () Paths ()";
    oracle_binary();fwrite("SH01",1,4,stdout);
    while(oracle_word(&steps)){
        frame_native_context native={0};frame_entity_native_context *e=&native.base.base.base.base;
        uint32_t i,r,result,consumed,flags,a;int status;
        memset(ram,0,sizeof(ram));cursor=0x100000;
        e->read=read_data;e->write=write_data;e->services.allocate=allocate;e->services.zero_allocate=zero_allocate;e->services.release=release;e->services.work_limit=100000;
        native.base.base.base.parameters.trig=frame_math_trig();native.base.base.shatter.trig=frame_math_trig();check(frame_native_bind(&native));set(0x54b3d8,1);
        check(frame_bootstrap_static_data(write_data,0));check(frame_bootstrap_world_constants(write_data,0));check(frame_bootstrap_c_locale(write_data,0,0x710000));
        check(frame_bootstrap_classes(read_data,write_data,0,&e->services,&result));check(frame_entity_table_initialize(read_data,write_data,0,4,&e->services));
        check(frame_list_create(read_data,write_data,0,allocate,release,0,100000,&result));set(0x54b48c,result);
        check(frame_list_create(read_data,write_data,0,allocate,release,0,100000,&result));set(0x54b488,result);
        set(0x55c688,1);set(0x55ae50,0x620000);set(0x5366dc,150u<<10);set(0x5367a8,0x700000);
        set(0x544f70,(uint32_t)-5000);set(0x544f74,(uint32_t)-5000);set(0x544f78,12);set(0x544f7c,12);
        for(i=0;i<64;i++){check(frame_list_create(read_data,write_data,0,allocate,release,0,100000,&result));set(0x544e70+i*4,result);}
        check(frame_world_load_span(read_data,write_data,0,0,0,script,sizeof(script)-1,&e->services,&result,&consumed));
        check(frame_entities_drain_queues(read_data,write_data,0,frame_native_dispatch,&native,release,0,100000));
        r=word(word(0x55c694)+8);o=word(r+4);peer=word(word(word(0x55c694)+16)+4);
        check(allocate(0,64,&f));set(o+0x8c,f);
        load(0x55b6ac,1,4);load(f+24,1,4);load(f+22,1,2);load(f,3,4);load(f+28,3,4);load(f+44,1,4);load(o,3,4);load(peer+80,3,4);set(peer+0xec,0);
        flags=oracle_require();set(o+16,flags&~4u);if(flags&4u)check(frame_entity_enable_tick(read_data,write_data,0,1,allocate,release,0,100000,&result));set(o+16,flags);
        load(0x55c698,1,4);load_list(word(0x54b48c));load_list(word(0x54b488));
        blast_calls=0;flags=oracle_require();blast_status=(int32_t)oracle_require();blast_actions=oracle_require();
        if(flags){uint32_t record,copy,klass,table,slot=word(word(0x545f14)+28);
            record=word(word(0x55c694)+16);check(allocate(0,0x1100,&copy));memcpy(ram+copy,ram+peer,0x100);peer=copy;set(record+4,peer);
            check(allocate(0,64,&klass));check(allocate(0,512,&table));set(klass+28,127);set(klass+36,table);set(table+slot*4,0x7fabc0);set(record,klass);
            set(0x53e8e0,2);set(0x53e8ec,2);native.base.base.shatter.pair_method=blast_probe;
        }
        count=oracle_require();if(count>32)return 2;oracle_set(ram+f+20,count,2);check(allocate(0,count*104,&pieces));set(f+12,pieces);
        for(i=0;i<count;i++){
            uint32_t at=pieces+i*104,radius;load(at+12,3,4);load(at,3,4);load(at+28,12,4);load(at+88,3,4);load(at+76,1,4);
            load(at+80,2,2);load(at+84,1,4);load(at+100,1,4);radius=oracle_require();
            if(radius!=UINT32_MAX){check(allocate(0,84,&a));set(at+24,a);oracle_set(ram+a+12,radius,2);}
        }
        if(oracle_require()){check(allocate(0,64,&a));set(f+40,a);animation(1,a);}
        if(oracle_require()){check(allocate(0,40,&a));set(f+48,a);load(a+24,2,4);}
        set(0x52ec94,0);set(0x52ec98,0);set(0x52ec9c,0);set(0x58e7cc,0);
        for(i=0;i<steps;i++){
            uint32_t op=oracle_require();load(0x5904fc,1,4);load(0x590558,1,4);e->services.work_limit=oracle_require();result=0x7182;
            if(op==5)status=frame_entities_tick(read_data,write_data,0,frame_native_dispatch,&native,e->services.work_limit);
            else status=frame_native_dispatch(&native,op==1?0x41cf80u:op==2?0x40a5f0u:op==4?0x40a3e0u:0x41c980u,1,1,2,&result);
            snapshot(status,result);
        }
    }
    return ferror(stdin)?3:0;
}
