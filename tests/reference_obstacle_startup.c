/* Real parent construction with unchanged production formulas. The wrapper
 * captures class defaults and resource requests, not a precomputed world. */
#define frame_object_construct oracle_object_construct
#define frame_object_native oracle_object_native
#include "../src/frame_object_parameters.c"
#undef frame_object_construct
#undef frame_object_native
#define frame_fall_construct oracle_fall_construct
#include "../src/frame_fall.c"
#undef frame_fall_construct
#include "reference_game.h"
#include "frame_math.h"
typedef struct material_log { frame_object_material_name run;void *ctx;uint32_t count,id[32],mode[32];unsigned char name[32][64]; } material_log;
static uint32_t ordinary_ids[4096],ordinary_count;
static int ordinary_id(uint32_t entity)
{uint32_t i;for(i=0;i<ordinary_count;i++)if(ordinary_ids[i]==entity)return 1;return 0;}
static void emit_ordinary_list(uint32_t head)
{
    uint32_t node=reference_game_word(head,4),n=0,used=0;
    while(node!=head){if(used++==100000)exit(5);if(ordinary_id(reference_game_word(node+8,4)))n++;node=reference_game_word(node,4);}
    oracle_put(n);node=reference_game_word(head,4);
    while(node!=head){n=reference_game_word(node+8,4);if(ordinary_id(n))oracle_put(n);node=reference_game_word(node,4);}
}
static void emit_ordinary_membership(uint32_t entity,uint32_t o,uint32_t klass)
{
    uint32_t i,is_ordinary=strcmp((const char *)tank_data()+klass,"Obstacle")==0;
    static const uint32_t lists[]={0x54b48c,0x54b488,0x53e4b4,0x55b6a8,0x55b6bc,0x55b6b8,0x53d148};
    if(strcmp((const char *)tank_data()+klass,"Fall")==0)return; /* tail emits this marker */
    oracle_put(is_ordinary);if(!is_ordinary)return;
    if(ordinary_count==4096)exit(5);
    ordinary_ids[ordinary_count++]=entity;
    oracle_put(reference_game_word(o+16,4));
    for(i=0;i<7;i++)emit_ordinary_list(reference_game_word(lists[i],4));
    for(i=0;i<64;i++)emit_ordinary_list(reference_game_word(0x544e70+i*4,4));
}
static int log_material(void *ctx,const unsigned char *name,uint32_t n,uint32_t mode,uint32_t *out)
{
    material_log *log=(material_log *)ctx;uint32_t i=log->count++;int status;
    if(i>=32||n>64)return -3;
    memcpy(log->name[i],name,n);log->mode[i]=mode;
    status=log->run(log->ctx,name,n,mode,out);log->id[i]=*out;return status;
}
static void emit_words(uint32_t at,unsigned n)
{unsigned i;for(i=0;i<n;i++)oracle_put(reference_game_word(at+i*4,4));}
static void emit_short(uint32_t at)
{uint32_t v=reference_game_word(at,2);oracle_put(v>32767?v|0xffff0000u:v);}
static void emit_text(const unsigned char *s,uint32_t n)
{oracle_put(n);if(fwrite(s,1,n,stdout)!=n)exit(4);}
static void emit_name(uint32_t at,uint32_t limit)
{uint32_t n=0;while(n<limit&&reference_game_word(at+n,1))n++;emit_text(tank_data()+at,n);}
static void emit_material(uint32_t at)
{uint32_t p=reference_game_word(at,4);oracle_put(p);if(p)emit_name(p,16);}
static unsigned char fall_shadow[TANKRACER_DATA_BYTES*2u];
static uint32_t fall_cursor,fall_cases;
static int fall_read(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if(at>sizeof(fall_shadow)||n>sizeof(fall_shadow)-at)return 0;memcpy(out,fall_shadow+at,n);return 1;}
static int fall_write(void *ctx,uint32_t at,const void *in,uint32_t n)
{(void)ctx;if(at>sizeof(fall_shadow)||n>sizeof(fall_shadow)-at)return 0;memcpy(fall_shadow+at,in,n);return 1;}
static uint32_t fw(uint32_t at){return oracle_get(fall_shadow+at,4);}
static void fsw(uint32_t at,uint32_t v){oracle_set(fall_shadow+at,v,4);}
static int fall_allocate(void *ctx,uint32_t n,uint32_t *out)
{(void)ctx;if(n>sizeof(fall_shadow)-fall_cursor)return -4;*out=fall_cursor;fall_cursor+=(n+3u)&~3u;memset(fall_shadow+*out,0,n);return 0;}
static int fall_release(void *ctx,uint32_t at){(void)ctx;(void)at;return 0;}
static void fall_list(uint32_t head)
{
    uint32_t n=0,node=fw(head),used=0;while(node!=head){if(used++==65536)exit(5);if(ordinary_id(fw(node+8)))n++;node=fw(node);}
    oracle_put(n);node=fw(head);while(node!=head){n=fw(node+8);if(ordinary_id(n))oracle_put(n);node=fw(node);}
}
static void fall_sequence(uint32_t entity,uint32_t o,const frame_entity_factory_services *source)
{
    frame_entity_factory_services e=*source;uint32_t i;int status;
    oracle_put(fall_cases==0);if(fall_cases++)return;
    memcpy(fall_shadow,tank_data(),TANKRACER_DATA_BYTES);fall_cursor=TANKRACER_DATA_BYTES;
    e.allocate=fall_allocate;e.release=fall_release;e.work_limit=100000;
    fsw(o+0x80,1);fsw(o+0xea,fw(o+0xea)&32u);fsw(o+0xee,fw(0x530acc)*16u);fsw(o+0xf2,1000000);fsw(o+0xf6,0);fsw(0x5904fc,16);
    status=frame_fall_tick(fall_read,fall_write,0,entity,&e,frame_math_trig());oracle_put((uint32_t)status);
    for(i=0;i<9;i++)oracle_put(fw(o+24+i*4));
    oracle_put(fw(o+0xee));oracle_put(fw(o+0xf2));oracle_put(fw(o+0xf6));oracle_put(fw(o+0x80));oracle_put(fw(o+0xea));oracle_put(fw(o+16));
    fall_list(fw(0x55b6bc));for(i=0;i<64;i++)fall_list(fw(0x544e70+i*4));
}
int frame_fall_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *e,uint32_t *out)
{
    uint32_t o,r,head,node,n=0,i,flags;int status;
    static const uint32_t lists[]={0x54b48c,0x54b488,0x53e4b4,0x55b6a8,0x55b6bc,0x55b6b8,0x53d148};
    status=oracle_fall_construct(read,write,ctx,entity,parser,e,out);if(status)return status;
    if(!reference_game_word(0x55c688,4))return 0;
    r=reference_game_word(reference_game_word(0x55c694,4)+entity*8,4);o=reference_game_word(r+4,4);
    if(ordinary_count==4096)exit(5);
    ordinary_ids[ordinary_count++]=entity;
    oracle_put(2);flags=reference_game_word(o+0xea,4);oracle_put(flags);
    oracle_put((flags&32u)!=0);if(flags&32u)emit_words(o+0xaa,9);
    head=reference_game_word(o+0xfa,4);oracle_put(head!=0);
    if(head){node=reference_game_word(head,4);while(node!=head){if(n++==65536)exit(5);node=reference_game_word(node,4);}
        oracle_put(n);node=reference_game_word(head,4);while(node!=head){uint32_t symbol=reference_game_word(node+8,4);emit_name(reference_game_word(symbol+32,4),1024);node=reference_game_word(node,4);}}
    oracle_put(reference_game_word(o+16,4));
    for(i=0;i<7;i++)emit_ordinary_list(reference_game_word(lists[i],4));
    for(i=0;i<64;i++)emit_ordinary_list(reference_game_word(0x544e70+i*4,4));
    fall_sequence(entity,o,e);
    return 0;
}
static void emit_shape(uint32_t shape)
{
    uint32_t n,i,j;oracle_put(shape!=0);if(!shape)return;
    n=reference_game_word(shape,2);oracle_put(n);
    for(i=0;i<n;i++)for(j=0;j<3;j++)emit_short(reference_game_word(shape+4,4)+i*8+j*2);
}
static void emit_components(uint32_t o)
{
    uint32_t a,f,n,i,at;
    emit_words(o+0x60,6);emit_words(o+0x5c,1);emit_words(o+0x7c,2);
    oracle_put(reference_game_word(o+0x4c,2));emit_short(o+0x4e);emit_short(o+0x78);emit_short(o+0x7a);
    oracle_put(reference_game_word(o+0xa8,2));emit_words(o+0x94,1);emit_words(o+0x90,1);
    a=reference_game_word(o+0x88,4);oracle_put(a!=0);
    if(a){emit_name(a+4,40);emit_words(a+44,6);}
    f=reference_game_word(o+0x8c,4);oracle_put(f!=0);if(!f)return;
    emit_words(f,3);oracle_put(reference_game_word(f+22,2));emit_words(f+24,4);emit_words(f+44,1);
    n=reference_game_word(f+20,2);oracle_put(n);
    for(i=0;i<n;i++){
        at=reference_game_word(f+12,4)+i*104;emit_shape(reference_game_word(at+24,4));
        emit_words(at+12,3);emit_words(at,3);emit_words(at+28,12);emit_words(at+88,3);
        emit_words(at+76,1);emit_short(at+80);emit_short(at+82);emit_words(at+84,1);emit_words(at+100,1);
    }
    a=reference_game_word(f+48,4);oracle_put(a!=0);
    if(a){emit_material(a);emit_words(a+4,7);for(i=0;i<7;i++)oracle_put(reference_game_word(a+32+i,1));}
    a=reference_game_word(f+40,4);oracle_put(a!=0);
    if(a){emit_material(a+6);emit_words(a+10,3);emit_words(a+22,4);
        oracle_put(reference_game_word(a+48,2));oracle_put(reference_game_word(a+50,2));emit_words(a+54,1);
        oracle_put(reference_game_word(a+4,2));emit_short(a+52);emit_short(a);oracle_put(reference_game_word(a+2,2));
        for(i=0;i<7;i++)oracle_put(reference_game_word(a+38+i,1));}
}
int frame_object_native(void *ctx,uint32_t target,uint32_t entity,uint32_t has_payload,uint32_t payload,uint32_t *out)
{
    frame_object_native_context *c=(frame_object_native_context *)ctx;uint32_t record,o,klass,n,i;int status;
    frame_object_parameters_services resources;material_log log={0};
    if(target!=0x41e9a0u||!reference_game_word(0x55c688,4))return oracle_object_native(ctx,target,entity,has_payload,payload,out);
    record=reference_game_word(reference_game_word(0x55c694,4)+entity*8,4);o=reference_game_word(record+4,4);klass=reference_game_word(record,4);
    oracle_put(1);oracle_put(entity);oracle_put(reference_game_word(0x55b6ac,4));emit_name(klass,16);
    n=reference_game_word(payload,4);emit_text(tank_data()+payload+8,n);emit_words(o,3);emit_words(o+0x18,9);
    emit_words(o+0x5c,1);emit_words(o+0x7c,2);emit_short(o+0x4e);emit_shape(reference_game_word(o+0x48,4));
    /* Original immutable constants confirmed in tankrace.exe SHA256 e8c4f12d...
     * at52E1C8/4/0. They are absent from the current C bootstrap. */
    if(!c->base.write(c->base.data_ctx,0x52e1c0,"ADD\0SUB\0NONE\0",13))return -1;
    resources=c->parameters;log.run=resources.material;log.ctx=resources.material_ctx;
    resources.material=log_material;resources.material_ctx=&log;
    status=oracle_object_construct(c->base.read,c->base.write,c->base.data_ctx,entity,payload,&c->base.services,&resources,out);
    oracle_put((uint32_t)status);if(status)return status;
    oracle_put(log.count);for(i=0;i<log.count;i++){oracle_put(log.id[i]);oracle_put(log.mode[i]);emit_text(log.name[i],(uint32_t)strlen((char *)log.name[i]));}
    emit_components(o);oracle_put(reference_game_word(0x55b6ac,4));emit_ordinary_membership(entity,o,klass);
    if(strcmp((const char *)tank_data()+klass,"Obstacle")==0)oracle_put(0);
    return 0;
}
int main(int argc,char **argv)
{
    int status;if(argc!=4)return 2;oracle_binary();fwrite("OS05",1,4,stdout);
    status=reference_game_start_cup(argv[1],(uint32_t)strtoul(argv[2],0,10),(uint32_t)strtoul(argv[3],0,10),1,0,0);
    if(status)return status;
    oracle_put(0);tank_destroy();return 0;
}
