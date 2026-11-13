/* Test oracle: original C terrain functions against the fully initialized game.
 * --bsm accepts tiny generated contracts through the qualified shape loader.
 * Input: signed x z lines. Output: TQ01/base, then 20 LE words per point. */
#include "game_host.h"
#include "frame_shape.h"
#include "physics_terrain.h"
#include "physics_geometry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

typedef struct context { const unsigned char *ram;uint64_t reads; } context;
static const char *maps[]={"ENGLAND","MEXICO","SWAMP","EAST","THEME","BIO","MONACO","CIRCUIT"};
static uint32_t get32(const unsigned char *p)
{return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int write_words(const uint32_t *words,uint32_t n)
{
    unsigned char bytes[80];uint32_t i,j;
    for (i=0;i<n;i++) for (j=0;j<4;j++) bytes[i*4+j]=(unsigned char)(words[i]>>(j*8));
    return fwrite(bytes,4,n,stdout)==n ? 0 : -1;
}
static int read_data(void *ctx,uint32_t at,void *out,uint32_t bytes)
{
    context *s=(context *)ctx;s->reads++;
    if (at>TANKRACER_DATA_BYTES || bytes>TANKRACER_DATA_BYTES-at) return 0;
    memcpy(out,s->ram+at,bytes);return 1;
}
static int write_data(void *ctx,uint32_t at,const void *bytes,uint32_t n)
{
    context *s=(context *)ctx;if (at>TANKRACER_DATA_BYTES || n>TANKRACER_DATA_BYTES-at) return 0;
    memcpy((unsigned char *)s->ram+at,bytes,n);return 1;
}
static unsigned char *read_file(const char *path,uint32_t *size)
{
    FILE *file=fopen(path,"rb");long length;unsigned char *bytes;
    if (!file) return 0;
    if (fseek(file,0,SEEK_END) || (length=ftell(file))<1 || (unsigned long)length>UINT32_MAX || fseek(file,0,SEEK_SET)) {fclose(file);return 0;}
    bytes=(unsigned char *)malloc((size_t)length);
    if (!bytes || fread(bytes,1,(size_t)length,file)!=(size_t)length) {free(bytes);fclose(file);return 0;}
    fclose(file);*size=(uint32_t)length;return bytes;
}
static int no_material(void *ctx,uint32_t name,uint32_t mode,uint32_t *out)
{(void)ctx;(void)name;(void)mode;(void)out;return -5; /* fixtures have no material references */}
int main(int argc,char **argv)
{
    context ctx={0};unsigned char *owned=0;uint32_t root,words[20],n,i,map,cup;int32_t x,z;
    uint64_t queries=0;double seconds=0;char line[1024];int status;
    int auxiliary=argc==4 && !strcmp(argv[3],"--aux"),fallback=argc==4 && !strcmp(argv[3],"--fallback");
    if (argc!=3 && !auxiliary && !fallback) return 2;
#ifdef _WIN32
    _setmode(_fileno(stdout),_O_BINARY);
#endif
    if (!strcmp(argv[1],"--bsm")) {
        unsigned char *bytes=read_file(argv[2],&n);
        if (!bytes || n>TANKRACER_DATA_BYTES-0x5dab40u) {free(bytes);return 3;}
        owned=(unsigned char *)calloc(TANKRACER_DATA_BYTES,1);if (!owned) {free(bytes);return 3;}
        ctx.ram=owned;memcpy(owned+0x5dab40u,bytes,n);free(bytes);
        status=frame_shape_prepare_world(read_data,write_data,&ctx,0x53d180u,no_material,0,1000000,&root);
        if (status) {fprintf(stderr,"fixture loader status%d\n",status);free(owned);return 4;}
    } else {
        const char *packs[]={"DATA","TANKS/BRONZE","TANKS/SILVER","TANKS/GOLD","INGAME",0,"COMMON"};char level[64],path[4096];
        for (map=0;map<8u && strcmp(argv[2],maps[map]);map++) { }
        if (map==8u) return 2;
        cup=map==7u ? 2u : map==3u || map==5u || map==6u ? 1u : 0u;
        status=tank_create(map,cup,0,0,0);if (status) return 3;
        snprintf(level,sizeof(level),"LEVELS/%s",maps[map]);packs[5]=level;
        for (i=0;i<7u;i++) {
            unsigned char *bytes;int length=snprintf(path,sizeof(path),"%s/%s/FASTFILE.DAT",argv[1],packs[i]);
            if (length<0 || (size_t)length>=sizeof(path)) return 3;
            bytes=read_file(path,&n);if (!bytes) return 3;
            status=tank_mount(i,bytes,n);free(bytes);if (status) return 3;
        }
        status=tank_start();if (status) return 4;ctx.ram=tank_data();
    }
    root=get32(ctx.ram+0x55ae60u);ctx.reads=0;
    words[0]=auxiliary ? 0x31305141u : fallback ? 0x31305146u : 0x31305154u;
    words[1]=0x5dab54u;if (write_words(words,2)) return 5;
    while (fgets(line,sizeof(line),stdin)) {
        if (fallback) {
            int32_t world[3],mode,normal[3]={0x1234,-6,89};
            if (sscanf(line,"%d %d %d %d",world,world+1,world+2,&mode)!=4) return 6;
            words[0]=(uint32_t)physics_fallback_query(read_data,&ctx,root,world,mode,normal);
            for (i=0;i<3;i++) words[1+i]=(uint32_t)normal[i];
            if (write_words(words,4)) return 5;
            queries++;continue;
        }
        if (auxiliary) {
            int32_t values[16];char *next=line,*end;
            physics_auxiliary_contact out={.world={-1,-2,-3},.kind=17,.cached=23,.surface=19};
            for (i=0;i<16;i++) {
                long value=strtol(next,&end,10);
                if (next==end || value<INT32_MIN || value>INT32_MAX) return 6;
                values[i]=(int32_t)value;next=end;
            }
            words[0]=(uint32_t)physics_auxiliary_query(read_data,&ctx,root,values,values+3,values+12,values[15],&out);
            for (i=0;i<3;i++) words[1+i]=(uint32_t)out.world[i];
            words[4]=(uint32_t)out.kind;words[5]=(uint32_t)out.surface;words[6]=(uint32_t)out.cached;
            if (write_words(words,7)) return 5;
            queries++;continue;
        }
        physics_terrain_hit hit={-55,0x11223344u,{-12,-13,-14},0x8899aabbu,-77,{19,23,-29,31},0xabcd1234u};
        int32_t height=0x12345678,normal_height=0x12345678,normal[3]={0x11111111,0x22222222,0x33333333};clock_t begin;
        if (sscanf(line,"%d %d",&x,&z)!=2) return 6;
        begin=clock();
        words[0]=(uint32_t)physics_terrain_query(read_data,&ctx,root,x,z,&hit);
        words[13]=(uint32_t)physics_terrain_height(read_data,&ctx,root,x,z,&height);
        words[15]=(uint32_t)physics_terrain_normal_height(read_data,&ctx,root,x,z,&normal_height,normal);
        seconds+=(double)(clock()-begin)/CLOCKS_PER_SEC;
        words[1]=(uint32_t)hit.height;words[2]=hit.node;
        for (i=0;i<3u;i++) words[3+i]=(uint32_t)hit.normal[i];
        words[6]=hit.surface;words[7]=(uint32_t)hit.special;
        for (i=0;i<4u;i++) words[8+i]=(uint32_t)hit.extra[i];
        words[12]=hit.flags_word;words[14]=(uint32_t)height;words[16]=(uint32_t)normal_height;
        for (i=0;i<3u;i++) words[17+i]=(uint32_t)normal[i];
        if (write_words(words,20)) return 5;
        queries++;
    }
    fprintf(stderr,"queries=%" PRIu64 " callback_reads=%" PRIu64 " query_cpu_ms=%.3f\n",queries,ctx.reads,seconds*1000.);
    tank_destroy();free(owned);return fflush(stdout) ? 5 : 0;
}
