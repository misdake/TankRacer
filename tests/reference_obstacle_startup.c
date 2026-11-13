/* Real parent construction with unchanged production formulas. The wrapper
 * captures class defaults and resource requests, not a precomputed world. */
#define frame_object_construct oracle_object_construct
#define frame_object_native oracle_object_native
#include "../src/frame_object_parameters.c"
#undef frame_object_construct
#undef frame_object_native
#include "reference_game.h"
typedef struct material_log { frame_object_material_name run;void *ctx;uint32_t count,id[32],mode[32];unsigned char name[32][64]; } material_log;
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
    emit_components(o);oracle_put(reference_game_word(0x55b6ac,4));return 0;
}
int main(int argc,char **argv)
{
    int status;if(argc!=4)return 2;oracle_binary();fwrite("OS02",1,4,stdout);
    status=reference_game_start_cup(argv[1],(uint32_t)strtoul(argv[2],0,10),(uint32_t)strtoul(argv[3],0,10),1,0,0);
    if(status)return status;
    oracle_put(0);tank_destroy();return 0;
}
