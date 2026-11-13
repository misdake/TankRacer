/* Link this translation unit instead of the archived deformable object file.
 * The production implementation is included unchanged; the wrapper captures
 * its actual inputs/outputs while the real host constructs an entire map. */
#define frame_deformable_construct_resources oracle_crush_construct
#include "../src/frame_deformable.c"
#undef frame_deformable_construct_resources
#include "reference_game.h"
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
    oracle_put(reference_game_word(0x55b6ac,4));return 0;
}
int main(int argc,char **argv)
{
    uint32_t map;int status;
    if(argc!=4)return 2;
    map=(uint32_t)strtoul(argv[2],0,10);
    oracle_binary();fwrite("SC01",1,4,stdout);
    status=reference_game_start_cup(argv[1],map,(uint32_t)strtoul(argv[3],0,10),1,0,0);if(status)return status;
    oracle_put(0);tank_destroy();return 0;
}
