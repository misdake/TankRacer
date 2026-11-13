/* Link this translation unit instead of the archived deformable object file.
 * The production implementation is included unchanged; the wrapper captures
 * its actual inputs/outputs while the real host constructs an entire map. */
#define frame_deformable_construct_resources oracle_crush_construct
#include "../src/frame_deformable.c"
#undef frame_deformable_construct_resources
#include "reference_game.h"
#include "physics_geometry.h"
/* Only the oracle gets a copied C arena. Its allocator uses an extra address
 * range above ALL original DATA, so startup and its host allocator stay intact.
 * Rust still constructs from archives and receives semantic fields only. */
static unsigned char shadow[TANKRACER_DATA_BYTES*2u];
static uint32_t shadow_cursor;
static int shadow_read(void *ctx,uint32_t at,void *out,uint32_t n)
{ (void)ctx;if(at>sizeof(shadow)||n>sizeof(shadow)-at)return 0;memcpy(out,shadow+at,n);return 1; }
static int shadow_write(void *ctx,uint32_t at,const void *in,uint32_t n)
{ (void)ctx;if(at>sizeof(shadow)||n>sizeof(shadow)-at)return 0;memcpy(shadow+at,in,n);return 1; }
static int shadow_allocate(void *ctx,uint32_t n,uint32_t *out)
{ (void)ctx;if(n>sizeof(shadow)-shadow_cursor)return -4;*out=shadow_cursor;shadow_cursor+=(n+3u)&~3u;memset(shadow+*out,0,n);return 0; }
static int shadow_release(void *ctx,uint32_t at) { (void)ctx;(void)at;return 0; }
static uint32_t shadow_word(uint32_t at) { if(at>sizeof(shadow)-4)exit(7);return oracle_get(shadow+at,4); }
static void shadow_state(uint32_t o,uint32_t records,uint32_t count)
{
    uint32_t i,j,head=shadow_word(0x55b6bc),node,n=0;
    oracle_put(shadow_word(o+0x10));oracle_put(shadow[o+0xcc]);oracle_put(shadow_word(o+0xd5)!=0);oracle_put(shadow_word(o+0xf9));oracle_put(shadow_word(o+0xf1));
    oracle_put(shadow_word(o+0xed)!=0);oracle_put(shadow_word(o+0xdd)!=0);
    for(i=0;i<count;i++)for(j=0;j<8;j++)oracle_put(shadow_word(records+i*32+j*4));
    node=shadow_word(head);while(node!=head) { if(n++==65536)exit(7);node=shadow_word(node); }
    oracle_put(n);node=shadow_word(head);while(node!=head) { oracle_put(shadow_word(node+8));node=shadow_word(node); }
}
static void shadow_sequence(uint32_t entity,uint32_t o)
{
    frame_entity_factory_services e={0};uint32_t count,records,i,j,result=0;physics_deform_result sample={0};int status;
    memcpy(shadow,tank_data(),TANKRACER_DATA_BYTES);shadow_cursor=TANKRACER_DATA_BYTES;
    e.allocate=shadow_allocate;e.release=shadow_release;e.work_limit=100000;
    count=shadow_word(o+0xcd);records=shadow_word(o+0xd5);
    shadow_state(o,records,count);
    for(i=0;i<count;i++) {
        int32_t p[3];for(j=0;j<3;j++)p[j]=oracle_signed(shadow_word(records+i*32+12+j*4));
        status=physics_deform_query(shadow_read,shadow_write,0,o,p,&sample);if(status)exit(7);
    }
    shadow_state(o,records,count);
    for(i=0;i<2;i++) {
        status=-1;
        if(!count||shadow_word(o+0xd5)) {
            oracle_set(shadow+0x5904fc,1500,4);
            status=frame_deformable_tick(shadow_read,shadow_write,0,entity,&e,&result);
        }
        oracle_put((uint32_t)status);shadow_state(o,records,count);
    }
    if(count) {
        int32_t p[3];for(j=0;j<3;j++)p[j]=oracle_signed(shadow_word(records+12+j*4));
        status=physics_deform_query(shadow_read,shadow_write,0,o,p,&sample);if(status)exit(7);
        oracle_put(sample.activated_records);
    }else oracle_put(0);
    shadow_state(o,records,count);
}
static void dump_words(uint32_t at,unsigned n)
{unsigned i;for(i=0;i<n;i++)oracle_put(reference_game_word(at+i*4,4));}
static void dump_short(uint32_t at)
{uint32_t v=reference_game_word(at,2);oracle_put(v>32767?v|0xffff0000u:v);}
int frame_deformable_construct_resources(frame_data_read read,frame_data_write write,void *ctx,
    uint32_t entity,uint32_t parser,const frame_entity_factory_services *s,
    const frame_shape_resources *resources,uint32_t *out)
{
    uint32_t o,mesh,n,i,j,at,head,node,entered=0;int status;
    oracle_put(1);oracle_put(entity);oracle_put(reference_game_word(0x55b6ac,4));
    n=reference_game_word(parser,4);oracle_put(n);
    if(fwrite(tank_data()+parser+8,1,n,stdout)!=n)exit(4);
    status=oracle_crush_construct(read,write,ctx,entity,parser,s,resources,out);
    oracle_put((uint32_t)status);if(status)return status;
    o=reference_game_word(reference_game_word(0x55c694,4)+entity*8,4);o=reference_game_word(o+4,4);
    mesh=reference_game_word(o+0x48,4);oracle_put(mesh);
    dump_words(o,3);dump_words(o+0x18,9);oracle_put(reference_game_word(o+0x14,4)==o+0x18);
    oracle_put(reference_game_word(mesh+12,2));dump_words(o+0x60,6);dump_words(o+0x5c,1);
    oracle_put(reference_game_word(o+0x4c,2));dump_short(o+0x4e);dump_short(o+0x78);dump_short(o+0x7a);
    head=reference_game_word(0x53cd18,4);node=reference_game_word(head,4);i=0;
    while(node!=head){if(i++==65536)exit(8);if(reference_game_word(node+8,4)==entity)entered=1;node=reference_game_word(node,4);}
    oracle_put(entered);dump_words(o+0xd9,1);dump_words(o+0xf9,1);
    dump_words(o+0xae,1);dump_words(o+0xb2,1);dump_words(o+0xe5,1);dump_words(o+0xb6,1);dump_words(o+0xba,4);
    for(i=0;i<3;i++)oracle_put(reference_game_word(o+0xca+i,1));
    dump_words(o+0xaa,1);dump_words(o+0xe9,1);oracle_put(reference_game_word(o+0x84,4)!=0);
    n=reference_game_word(o+0xcd,4);oracle_put(n);
    for(i=0;i<n;i++){at=reference_game_word(o+0xd5,4)+i*32;dump_words(at,8);}
    n=reference_game_word(mesh,2);oracle_put(n);
    for(i=0;i<n;i++)for(j=0;j<3;j++)dump_short(reference_game_word(mesh+4,4)+i*8+j*2);
    oracle_put(reference_game_word(mesh+14,2));
    for(i=0;i<n;i++)oracle_put(reference_game_word(reference_game_word(o+0xdd,4)+i,1));
    oracle_put(reference_game_word(0x55b6ac,4));shadow_sequence(entity,o);return 0;
}
int main(int argc,char **argv)
{
    uint32_t map;int status;
    if(argc!=4)return 2;
    map=(uint32_t)strtoul(argv[2],0,10);
    oracle_binary();fwrite("SC02",1,4,stdout);
    status=reference_game_start_cup(argv[1],map,(uint32_t)strtoul(argv[3],0,10),1,0,0);if(status)return status;
    oracle_put(0);tank_destroy();return 0;
}
