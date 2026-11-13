/* Complete native reference startup for read-only C integration oracles. */
#ifndef TEST_REFERENCE_GAME_H
#define TEST_REFERENCE_GAME_H
#include "game_host.h"
#include "reference_io.h"
static inline int reference_game_start_with_opponents(const char *assets,uint32_t map,uint32_t vehicle,uint32_t mode,uint32_t opponents)
{
    static const char *names[]={"ENGLAND","MEXICO","SWAMP","EAST","THEME","BIO","MONACO","CIRCUIT"};
    const char *packs[]={"DATA","TANKS/BRONZE","TANKS/SILVER","TANKS/GOLD","INGAME",0,"COMMON"};
    char level[64],path[4096];uint32_t cup,i;int status;
    if (map>=8) return 2;
    cup=map==7 ? 2 : map==3 || map==5 || map==6 ? 1 : 0;
    status=tank_create(map,cup,vehicle,mode,opponents);if (status) return 3;
    snprintf(level,sizeof(level),"LEVELS/%s",names[map]);packs[5]=level;
    for (i=0;i<7;i++) {
        FILE *file;long size;unsigned char *bytes;
        int n=snprintf(path,sizeof(path),"%s/%s/FASTFILE.DAT",assets,packs[i]);if (n<0 || (size_t)n>=sizeof(path)) return 3;
        file=fopen(path,"rb");if (!file) return 3;
        if (fseek(file,0,SEEK_END)||(size=ftell(file))<0||fseek(file,0,SEEK_SET)) {fclose(file);return 3;}
        bytes=(unsigned char *)malloc((size_t)size);if (!bytes) {fclose(file);return 3;}
        if (fread(bytes,1,(size_t)size,file)!=(size_t)size) {free(bytes);fclose(file);return 3;}fclose(file);
        status=tank_mount(i,bytes,(uint32_t)size);free(bytes);if (status) return 3;
    }
    return tank_start() ? 4 : 0;
}
static inline int reference_game_start(const char *assets,uint32_t map,uint32_t vehicle,uint32_t mode)
{return reference_game_start_with_opponents(assets,map,vehicle,mode,0);}
static inline uint32_t reference_game_word(uint32_t address,unsigned width)
{
    if (address>TANKRACER_DATA_BYTES || width>TANKRACER_DATA_BYTES-address) exit(5);
    return oracle_get(tank_data()+address,width);
}
#endif
