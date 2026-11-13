#include "reference_game.h"
static void emit(uint32_t address,unsigned words)
{unsigned i;for (i=0;i<words;i++) oracle_put(reference_game_word(address+i*4,4));}
static void short_value(uint32_t address)
{uint32_t v=reference_game_word(address,2);oracle_put(v>32767 ? v|0xffff0000u : v);}
int main(int argc,char **argv)
{
    uint32_t vehicle,map,mode,id,object,table,record,i,j,gun,shadow,faces;int status;
    if (argc!=5) return 2;
    vehicle=(uint32_t)strtoul(argv[2],0,10);map=(uint32_t)strtoul(argv[3],0,10);mode=(uint32_t)strtoul(argv[4],0,10);
    status=reference_game_start(argv[1],map,vehicle,mode);if (status) return status;
    id=reference_game_word(0x53e8b8,4);table=reference_game_word(0x55c694,4);record=reference_game_word(table+id*8,4);object=reference_game_word(record+4,4);
    if (!object) return 4;
    oracle_binary();fwrite("VM01",1,4,stdout);
    oracle_put(reference_game_word(object+0xde5,1));oracle_put(reference_game_word(0x52ee9c,4));oracle_put(reference_game_word(0x52ed90,4));
    oracle_put(reference_game_word(0x53e414,4));oracle_put(reference_game_word(0x545ffc,4));oracle_put(reference_game_word(0x58e7ac,4));oracle_put(reference_game_word(0x5319c4,4));
    for (i=0;i<4;i++) emit(object+0x11c+i*70,3);
    for (i=0;i<2;i++) emit(object+0x234+i*29,3);
    emit(object+0x626,3);emit(object+0x632,6);emit(object+0x60,6);emit(object+0x1086,12);emit(object+0x652,1);
    for (i=0;i<4;i++) oracle_put(reference_game_word(object+0x784+i,1));
    for (i=0;i<4;i++) short_value(object+0x788+i*2);
    for (i=0;i<8;i++) oracle_put(reference_game_word(object+0xdff+i,1));
    gun=reference_game_word(object+0x366,4);oracle_put(gun ? reference_game_word(gun+12,2) : 0);
    shadow=reference_game_word(object+0x71e,4);faces=reference_game_word(shadow+8,4);
    for (i=0;i<4;i++) for (j=0;j<3;j++) short_value(reference_game_word(shadow+4,4)+i*8+j*2);
    for (i=0;i<2;i++) oracle_put(reference_game_word(faces+i*24+6,2));
    short_value(object+0x10b6);short_value(object+0x10b8);
    for (i=0;i<9;i++) emit(object+0x29a+i*104,3);
    emit(object+0x6a6,1);emit(object+0xdd2,1);emit(object+0x66a,1);emit(object+0x6c2,1);
    for (i=0;i<2;i++) {
        uint32_t at=object+0xe22+i*300,shape=reference_game_word(object+0x3ceu+i*104,4),polys=reference_game_word(shape+8,4);
        static const uint32_t bases[]={8,32,56,152},snap[]={0,0,248,272};
        for (j=0;j<4;j++) {
            uint32_t n=reference_game_word(at+296+j,1),k;oracle_put(n);
            for (k=0;k<n;k++) {
                uint32_t address=reference_game_word(at+bases[j]+k*4,4),relative=address-polys-13;
                oracle_put(relative/24);oracle_put((relative%24)/2);oracle_put(j>=2 ? reference_game_word(at+snap[j]+k,1) : 0);
            }
        }
    }
    tank_destroy();return 0;
}
