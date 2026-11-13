/* Native stdin/stdout adapter for the same public API used by WebAssembly. */
#include "game_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

static const char *maps[]={"ENGLAND","MEXICO","SWAMP","EAST","THEME","BIO","MONACO","CIRCUIT"};
static const char cups[]="BSG";
static void word(unsigned char *p,uint32_t v)
{ unsigned i;for (i=0;i<4u;i++) p[i]=(unsigned char)(v>>(i*8u)); }

static int mount_file(uint32_t slot,const char *path)
{
    FILE *f=fopen(path,"rb");long length;unsigned char *bytes;int status;
    if (!f) { fprintf(stderr,"Cannot open %s\n",path);return -1; }
    if (fseek(f,0,SEEK_END) || (length=ftell(f))<28 || length>128L*1024L*1024L || fseek(f,0,SEEK_SET)) { fclose(f);return -1; }
    bytes=(unsigned char *)malloc((size_t)length);if (!bytes) { fclose(f);return -3; }
    if (fread(bytes,1,(size_t)length,f)!=(size_t)length) { free(bytes);fclose(f);return -1; }
    fclose(f);status=tank_mount(slot,bytes,(uint32_t)length);free(bytes);return status;
}
static void help(void)
{
    fputs("Tank Racer CUP native host\n"
          "Usage: tankracer [--assets assets/game] [--map ENGLAND] [--cup B|S|G]\n"
          "                 [--vehicle 0..15] [--mode 0|1] [--no-opponents] [--snapshot]\n"
          "stdin: one 'frame_count hexadecimal_buttons focused' line per update.\n"
          "stdout: binary NWF1 packets. --snapshot prefixes each response with\n"
          "two little-endian lengths and appends the full 8 MiB game data.\n",stderr);
}
int main(int argc,char **argv)
{
    const char *assets="assets/game",*map="ENGLAND",*cup="B";
    const char *packs[]={"DATA","TANKS/BRONZE","TANKS/SILVER","TANKS/GOLD","INGAME",0,"COMMON"};
    uint32_t vehicle=0,mode=0,opponents=1,map_index=8,cup_index=3,i,count,bits,focused;
    char file[4096],level[64],line[128],extra;int arg,status,snapshot=0,result=0;
    for (arg=1;arg<argc;arg++) {
        const char *key=argv[arg];
        if (!strcmp(key,"--help") || !strcmp(key,"-h")) { help();return 0; }
        if (!strcmp(key,"--snapshot")) { snapshot=1;continue; }
        if (!strcmp(key,"--no-opponents")) { opponents=0;continue; }
        if (++arg>=argc) { help();return 2; }
        if (!strcmp(key,"--assets")) assets=argv[arg];
        else if (!strcmp(key,"--map")) map=argv[arg];
        else if (!strcmp(key,"--cup")) cup=argv[arg];
        else if (!strcmp(key,"--vehicle") || !strcmp(key,"--mode")) {
            char *end;unsigned long value=strtoul(argv[arg],&end,10);
            if (!*argv[arg] || *end || value>(!strcmp(key,"--vehicle")?15u:1u)) return 2;
            if (!strcmp(key,"--vehicle")) vehicle=(uint32_t)value;else mode=(uint32_t)value;
        } else { help();return 2; }
    }
    for (i=0;i<8u;i++) if (!strcmp(map,maps[i])) map_index=i;
    if (strlen(cup)==1u) { const char *p=strchr(cups,*cup);if (p) cup_index=(uint32_t)(p-cups); }
    if (map_index==8u || cup_index==3u) { help();return 2; }
#ifdef _WIN32
    _setmode(_fileno(stdout),_O_BINARY);
#endif
    status=tank_create(map_index,cup_index,vehicle,mode,opponents);if (status) return 3;
    snprintf(level,sizeof(level),"LEVELS/%s",map);packs[5]=level;
    for (i=0;i<7u;i++) {
        int n=snprintf(file,sizeof(file),"%s/%s/FASTFILE.DAT",assets,packs[i]);
        if (n<0 || (size_t)n>=sizeof(file) || mount_file(i,file)) { result=4;goto done; }
    }
    status=tank_start();if (status) { fprintf(stderr,"Track loading failed: %d\n",status);result=5;goto done; }
    while (fgets(line,sizeof(line),stdin)) {
        if (sscanf(line,"%u %x %u %c",&count,&bits,&focused,&extra)!=3) { result=6;goto done; }
        status=tank_step(count,bits,focused);
        if (status) { fprintf(stderr,"Update failed: %d\n",status);result=7;goto done; }
        if (snapshot) {
            unsigned char header[8];word(header,tank_packet_size());word(header+4,TANKRACER_DATA_BYTES);
            if (fwrite(header,1,8,stdout)!=8) { result=8;goto done; }
        }
        if (fwrite(tank_packet(),1,tank_packet_size(),stdout)!=tank_packet_size() ||
            (snapshot && fwrite(tank_data(),1,TANKRACER_DATA_BYTES,stdout)!=TANKRACER_DATA_BYTES) || fflush(stdout)) { result=8;goto done; }
    }
    if (ferror(stdin)) result=6;
done:
    tank_destroy();return result;
}
