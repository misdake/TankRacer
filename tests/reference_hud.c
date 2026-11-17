/* Read-only original HUD submission oracle. Resources remain presentation IDs. */
#include "reference_game.h"
#include "frame_hud.h"
static unsigned char commands[192*512], vertices[32*2048];
static void set(uint32_t at,uint32_t value,unsigned n){oracle_set((unsigned char *)(uintptr_t)tank_data()+at,value,n);}
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n){(void)ctx;if(at>TANKRACER_DATA_BYTES||n>TANKRACER_DATA_BYTES-at)return 0;memcpy(out,tank_data()+at,n);return 1;}
int main(int argc,char **argv)
{
    uint32_t count,i,j,p,at,map;frame_commands c={0};int status;
    if(argc!=3)return 2;
    map=(uint32_t)strtoul(argv[2],0,10);
    status=reference_game_start(argv[1],map,1,0);if(status)return status;
    p=reference_game_word(0x53e8b8,4);at=reference_game_word(reference_game_word(0x55c694,4)+p*8,4);p=reference_game_word(at+4,4);
    oracle_binary();count=oracle_require();
    for(i=0;i<count;i++){
        set(0x53e4f0,oracle_require(),4);set(0x53e4f4,oracle_require(),4);set(p+0xcba,oracle_require(),4);
        set(p,oracle_require(),4);set(p+4,oracle_require(),4);set(p+8,oracle_require(),4);
        set(p+0xec,oracle_require(),4);j=oracle_require();set(p+0xb0,j,4);set(p+0xa8,j,4);
        set(p+0xdec,oracle_require(),1);set(p+0x762,oracle_require(),2);
        status=frame_commands_begin(&c,commands,sizeof(commands),vertices,sizeof(vertices),640,480,i);if(status)return 8;
        status=frame_hud_draw(read_data,0,&c);if(status)return 8;
        oracle_put(c.command_count);
        for(j=0;j<c.command_count;j++){
            const unsigned char *d=commands+j*192,*v=vertices+oracle_get(d+8,4)*32;unsigned k;
            static const unsigned states[]={7,14,27,19,20,41};
            oracle_put(oracle_get(d+4,4)-FRAME_HUD_TEXTURE_BASE-1);
            for(k=0;k<6;k++)oracle_put(oracle_get(d+24+(states[k]-1)*4,4));
            oracle_put(oracle_get(d+12,4));
            for(k=0;k<4;k++,v+=32){oracle_put(oracle_get(v,4));oracle_put(oracle_get(v+4,4));oracle_put(oracle_get(v+16,4));oracle_put(oracle_get(v+24,4));oracle_put(oracle_get(v+28,4));}
        }
    }
    tank_destroy();
    return 0;
}
