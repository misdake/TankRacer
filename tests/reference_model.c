/* Qualified C SS lookup + SM relocation. Material service resolves validated
 * immutable names as identities; this test creates no GPU resources. */
#include "frame_host.h"
#include "frame_shape.h"
#include "reference_io.h"
static unsigned char ram[0x600000];
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if (at>sizeof(ram)||n>sizeof(ram)-at) return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *data,uint32_t n)
{(void)ctx;if (at>sizeof(ram)||n>sizeof(ram)-at) return 0;memcpy(ram+at,data,n);return 1;}
static uint32_t word(uint32_t at,unsigned n)
{if (at>sizeof(ram)||n>sizeof(ram)-at) exit(5);return oracle_get(ram+at,n);}
static void short_value(uint32_t at)
{uint32_t v=word(at,2);oracle_put(v>32767 ? v|0xffff0000u : v);}
static int material(void *ctx,uint32_t name,uint32_t mode,uint32_t *out)
{
    unsigned i;(void)ctx;if (mode!=1 && mode!=2) return -5;
    for (i=0;i<16;i++) {uint32_t c=word(name+i,1);if (c>=128) return -5;if (!c) {if (!i) return -5;*out=name;return 0;}}
    return -5;
}
int main(int argc,char **argv)
{
    FILE *file;long length;unsigned char *bytes;uint32_t count,i,base=0x10000;
    if (argc!=2) return 2;
    file=fopen(argv[1],"rb");if (!file) return 3;
    if (fseek(file,0,SEEK_END)||(length=ftell(file))<8||fseek(file,0,SEEK_SET)) {fclose(file);return 3;}
    bytes=(unsigned char *)malloc((size_t)length);if (!bytes) {fclose(file);return 3;}
    if (fread(bytes,1,(size_t)length,file)!=(size_t)length) {free(bytes);fclose(file);return 3;}fclose(file);
    count=oracle_get(bytes+4,4);if (count>((uint32_t)length-8)/12) {free(bytes);return 2;}
    oracle_binary();fwrite("SM01",1,4,stdout);oracle_put(count);
    for (i=0;i<count;i++) {
        char name[12]={0};uint32_t offset,size,n=0,vertices,faces,j,k,normal[3],has_normals;int status;
        while (n<8 && bytes[8+i*12+n]) {name[n]=(char)bytes[8+i*12+n];n++;}
        memcpy(name+n,".SM",3);
        status=frame_host_shape_find(bytes,(uint32_t)length,name,n+3,&offset,&size);if (status) {free(bytes);return 4;}
        oracle_put(offset);oracle_put(size);if (!size) continue;
        if (size>0x200000) return 4;
        memcpy(ram+base,bytes+offset,size);
        status=frame_shape_relocate(read_data,write_data,0,base,material,0,1000000);if (status) {fprintf(stderr,"shape %s status %d\n",name,status);free(bytes);return 4;}
        vertices=word(base,2);faces=word(base+2,2);oracle_put(vertices);oracle_put(faces);oracle_put(word(base+12,2));oracle_put(word(base+14,2));
        for (j=0;j<6;j++) short_value(base+28+j*2);
        for (j=0;j<vertices;j++) for (k=0;k<3;k++) short_value(word(base+4,4)+j*8+k*2);
        for (k=0;k<3;k++) normal[k]=word(base+72+k*4,4);
        has_normals=normal[0] && normal[1] && normal[2];oracle_put(has_normals);
        if (has_normals) for (j=0;j<vertices;j++) for (k=0;k<3;k++) short_value(normal[k]+j*2);
        for (j=0;j<faces;j++) {
            uint32_t tri=word(base+8,4)+j*24,mat=word(tri+20,4),flags=word(tri+18,2);
            for (k=0;k<3;k++) short_value(tri+k*2);
            oracle_put(word(tri+6,2));for (k=0;k<10;k++) oracle_put(word(tri+8+k,1));oracle_put(flags);oracle_put(mat ? mat-base : 0);
            for (k=0;k<4;k++) oracle_put(mat ? word(mat+k*4,4) : 0);
            if (flags&30) {short_value(word(base+16,4)+j*2);short_value(word(base+24,4)+j*2);}
        }
    }
    free(bytes);return 0;
}
