/* Test oracle only: call the qualified C decoder on immutable source archives.
 * Output: 13 little-endian words, resource name, then normalized RGBA bytes. */
#include "frame_surface.h"
#include "frame_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

static uint32_t word(const unsigned char *p)
{ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static int write_words(const uint32_t *values,uint32_t count)
{
    uint32_t i,j;unsigned char bytes[52];
    for (i=0;i<count;i++) for (j=0;j<4;j++) bytes[i*4+j]=(unsigned char)(values[i]>>(j*8));
    return fwrite(bytes,4,count,stdout)==count ? 0 : -1;
}
static int emit(uint32_t pack,const char *name,const unsigned char *bytes,uint32_t length)
{
    frame_surface_info info={0};frame_surface_plan plan={0};uint32_t h[13],mip,consumed,alpha,n,size,flags;int opened,status;
    unsigned char palette[1024],*pixels=0,*rgba=0;
    opened=frame_surface_open(bytes,length,4,3,3,&info);
    for (mip=0;mip<=(opened ? 0u : info.mip_levels) && mip<=31u;mip++) {
        consumed=0;size=0;flags=0;status=opened;
        if (!status) status=frame_surface_prepare(&info,4,1,0,0,100000,&plan);
        if (!status) {
            flags=plan.flags;
            if (!((info.kind==0 && info.bits==8) || (info.kind==2 && info.bits==32)) || info.compression>2 || info.format==2) status=-5;
            else if (!(n=info.width*info.height) || n>16777216u) status=-3;
            else {
                pixels=(unsigned char *)malloc(n*4u);rgba=(unsigned char *)malloc(n*4u);
                if (!pixels || !rgba) { free(pixels);free(rgba);return -1; }
                status=frame_surface_decode(bytes,length,&info,mip,pixels,n*4u,palette,1024,&consumed);
                if (!status) {
                    n=(info.width>>mip)*(info.height>>mip);size=n*4u;
                    status=frame_surface_upload_rgba(&info,flags,n,pixels,info.width*info.height*4u,palette,1024,rgba,size,&alpha);
                }
            }
        }
        if (status) size=0;
        h[0]=pack;h[1]=(uint32_t)strlen(name);h[2]=mip;h[3]=(uint32_t)status;
        h[4]=info.width>>mip;h[5]=info.height>>mip;h[6]=flags;h[7]=info.format;
        h[8]=info.kind;h[9]=info.bits;h[10]=info.compression;h[11]=consumed;h[12]=size;
        if (write_words(h,13) || fwrite(name,1,h[1],stdout)!=h[1] || (size && fwrite(rgba,1,size,stdout)!=size)) { free(pixels);free(rgba);return -1; }
        free(pixels);free(rgba);pixels=0;rgba=0;
        if (status) break;
    }
    return 0;
}
int main(int argc,char **argv)
{
    int arg;
#ifdef _WIN32
    _setmode(_fileno(stdout),_O_BINARY);
#endif
    for (arg=1;arg<argc;arg++) {
        FILE *file=fopen(argv[arg],"rb");long length;unsigned char *bytes;uint32_t count,i;
        if (!file) return 2;
        if (fseek(file,0,SEEK_END) || (length=ftell(file))<28 || (unsigned long)length>UINT32_MAX || fseek(file,0,SEEK_SET)) { fclose(file);return 2; }
        bytes=(unsigned char *)malloc((size_t)length);if (!bytes) { fclose(file);return 3; }
        if (fread(bytes,1,(size_t)length,file)!=(size_t)length) { free(bytes);fclose(file);return 2; }fclose(file);
        if (memcmp(bytes,"FASTF101",8)) { free(bytes);return 4; }
        count=word(bytes+24);if (count>((uint32_t)length-28u)/24u) { free(bytes);return 4; }
        for (i=0;i<count;i++) {
            const unsigned char *record=bytes+28u+i*24u;char name[13]={0};uint32_t n=0,offset,size;
            while (n<12u && record[n]) {name[n]=(char)record[n];n++;}
            if (n<4u || strcmp(name+n-4u,".SUR")) continue;
            if (frame_host_archive_find(bytes,(uint32_t)length,name,n,&offset,&size)) { free(bytes);return 4; }
            if (emit((uint32_t)arg-1u,name,bytes+offset,size)) { free(bytes);return 5; }
        }
        free(bytes);
    }
    return fflush(stdout) ? 5 : 0;
}
