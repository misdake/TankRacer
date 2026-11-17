/* Complete original C startup, observed after Player route initialization. */
#include "reference_game.h"
static uint32_t word(uint32_t a) { return reference_game_word(a,4); }
static uint32_t object_of(uint32_t id)
{ uint32_t r=word(word(0x55c694)+id*8u);return r ? word(r+4u) : 0; }
int main(int argc,char **argv)
{
    uint32_t map,player,target,o,p,i;int status;
    if(argc!=3)return 2;
    map=(uint32_t)strtoul(argv[2],0,10);oracle_binary();
    status=reference_game_start(argv[1],map,1,0);
    if(status){fprintf(stderr,"race startup status %d\n",status);return 7;}
    player=word(0x53e8b8);o=object_of(player);target=word(o+0x790u);p=object_of(target);
    fwrite("RS01",1,4,stdout);
    for(i=0;i<3u;i++)oracle_put(word(o+i*4u));
    oracle_put(target!=0);oracle_put(word(o+0x794u));
    for(i=0;i<3u;i++)oracle_put(p ? word(p+i*4u) : 0);
    oracle_put(p ? word(p+0x90u) : 0);
    /* Diagnostics only: IDs/RNG still belong to full-startup qualification. */
    oracle_put(player);oracle_put(target);oracle_put(word(0x55b6acu));
    tank_destroy();return 0;
}
