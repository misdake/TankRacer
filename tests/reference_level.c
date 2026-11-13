/* Common authored-object reader, not a substitute for native class startup. */
#include "reference_io.h"
#include "frame_world.h"
#include "frame_world_text.h"
#include "frame_file.h"
#include "frame_math.h"
static unsigned char ram[0x600000];
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *in,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(ram+at,in,n);return 1;}
static void set(uint32_t at,uint32_t value){oracle_set(ram+at,value,4);}
static void string(const unsigned char *value)
{size_t n=strlen((const char *)value);oracle_put((uint32_t)n);if(fwrite(value,1,n,stdout)!=n)exit(4);}
int main(int argc,char **argv)
{
    FILE *input;long length;unsigned char *bytes,magic[80],klass[32],name[64],shape[16],discard[32];
    frame_file file;frame_entity_factory_services entities={0};frame_object_parameters_services resources={0};
    uint32_t present,version,i,out,cursor,used=0;int status;
    if(argc!=2)return 2;
    input=fopen(argv[1],"rb");if(!input)return 3;
    if(fseek(input,0,SEEK_END)||(length=ftell(input))<0||length>40000||fseek(input,0,SEEK_SET))return 3;
    bytes=(unsigned char *)malloc((size_t)length);if(!bytes)return 3;
    if(fread(bytes,1,(size_t)length,input)!=(size_t)length)return 3;
    fclose(input);
    file=(frame_file){bytes,(uint32_t)length,0,1000000};
    status=frame_file_token(&file,magic,80,&present);if(status||!present||strcmp((char *)magic,"FSTPC"))return 5;
    status=frame_file_integer(&file,&version,&present);if(status||!present||version!=5)return 5;
    memcpy(ram+0x10000,bytes+file.cursor,(size_t)length-file.cursor);
    free(bytes);
    set(0x55c6a0,0x10000);set(0x536d64,version);set(0x55c694,0x1000);
    set(0x1008,0x1100);set(0x1100,0x1200);set(0x1104,0x2000);set(0x1224,0x1300);
    /* Parent/final class slots intentionally empty: this oracle isolates the
     * actual common record reader, not class-specific construction. */
    set(0x545f0c,0x1400);set(0x545ecc,0x1400);set(0x545f10,0x1400);
    set(0x538838,0x1500);for(i=9;i<=13;i++)ram[0x1500+i*2]=8;ram[0x1500+32*2]=8;
    for(i=0;i<9;i++)set(0x532888+i*4,i%4==0?4096u:0u);
    entities.work_limit=1000000;resources.trig=frame_math_trig();
    oracle_binary();fwrite("LV01",1,4,stdout);
    for(;;){
        if(used++>=65536)return 6;
        status=frame_world_token(read_data,write_data,0,klass,32,1000000);if(status)return 7;
        if(!strcmp((char *)klass,"Paths")){oracle_put(0);break;}
        status=frame_world_token(read_data,write_data,0,name,64,1000000);if(status)return 7;
        cursor=oracle_get(ram+0x55c6a4,4);
        for(i=0;i<6;i++){status=frame_world_token(read_data,write_data,0,discard,32,1000000);if(status)return 7;}
        status=frame_world_token(read_data,write_data,0,shape,16,1000000);if(status)return 7;
        /* Compare the parsed shape name, while bypassing resource loading only.
         * No expected coordinate/matrix/group formula is copied here. */
        memcpy(ram+0x534d00,shape,16);set(0x55c6a4,cursor);
        status=frame_world_object_read(read_data,write_data,0,1,0,&entities,&resources,&out);if(status)return 8;
        oracle_put(1);string(klass);string(name);string(shape);
        for(i=0;i<3;i++)oracle_put(oracle_get(ram+0x2000+i*4,4));
        for(i=0;i<9;i++)oracle_put(oracle_get(ram+0x2018+i*4,4));
        string(ram+0x55b280);
    }
    return 0;
}
