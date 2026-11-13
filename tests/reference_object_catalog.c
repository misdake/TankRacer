#include "reference_game.h"
int main(int argc,char **argv)
{
    uint32_t n,base;int status;if(argc!=2)return 2;
    status=reference_game_start(argv[1],0,1,0);if(status)return status;
    n=reference_game_word(0x544f88,4);base=reference_game_word(0x544f84,4);
    oracle_binary();fwrite("OC01",1,4,stdout);oracle_put(n);
    if(fwrite(tank_data()+base,76,n,stdout)!=n)return 5;
    tank_destroy();return 0;
}
