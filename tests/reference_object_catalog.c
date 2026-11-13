#include "reference_game.h"
int main(int argc,char **argv)
{
    uint32_t n,base;int status;if(argc!=2)return 2;
    status=reference_game_start(argv[1],0,1,0);if(status)return status;
    n=reference_game_word(0x544f88,4);base=reference_game_word(0x544f84,4);
    oracle_binary();fwrite("OC02",1,4,stdout);oracle_put(n);
    if(fwrite(tank_data()+base,76,n,stdout)!=n)return 5;
    oracle_put(reference_game_word(0x52ec94,4));oracle_put(reference_game_word(0x52ec98,4));oracle_put(reference_game_word(0x52ec9c,4));
    oracle_put(reference_game_word(0x530acc,4));oracle_put(reference_game_word(0x530ad0,4));oracle_put(reference_game_word(0x530ad4,4));
    tank_destroy();return 0;
}
