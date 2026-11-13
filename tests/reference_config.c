#include "frame_tank_config.h"
#include "frame_surface_config.h"
#include "reference_io.h"
struct config_field {const char *name;uint32_t kind,reference,offset;};
#include "frame_tank_config_schema.h"
static unsigned char ram[0x550000];
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if (at>sizeof(ram)||n>sizeof(ram)-at) return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *data,uint32_t n)
{(void)ctx;if (at>sizeof(ram)||n>sizeof(ram)-at) return 0;memcpy(ram+at,data,n);return 1;}
int main(int argc,char **argv)
{
    FILE *file;long length;unsigned char *bytes;uint32_t first;int surface;
    if (argc!=3) return 2;
    surface=!strcmp(argv[1],"--surface");
    if (!surface && strcmp(argv[1],"--tank")) return 2;
    file=fopen(argv[2],"rb");if (!file) return 3;
    if (fseek(file,0,SEEK_END)||(length=ftell(file))<0||fseek(file,0,SEEK_SET)) {fclose(file);return 3;}
    bytes=(unsigned char *)malloc((size_t)length);if (!bytes) {fclose(file);return 3;}
    if (fread(bytes,1,(size_t)length,file)!=(size_t)length) {free(bytes);fclose(file);return 3;}fclose(file);
    oracle_binary();fwrite("CF01",1,4,stdout);
    while (oracle_word(&first)) {
        uint32_t consumed=0,i,j,object=0x1000;int status;
        memset(ram,0,sizeof(ram));
        if (surface) {
            (void)first;status=frame_surface_configure_span(read_data,write_data,0,bytes,(uint32_t)length,1000000,&consumed);
            oracle_put((uint32_t)status);oracle_put(consumed);
            for (i=0;i<100;i++) oracle_put(oracle_get(ram+0x546018+i*4,4));
        } else {
            frame_tank_config_ambient ambient;uint32_t has_ambient;
            oracle_set(ram+0x53e414,first,4);oracle_set(ram+object+0xde5,oracle_require(),1);
            oracle_set(ram+0x52ee9c,oracle_require(),4);oracle_set(ram+0x52ed90,oracle_require(),4);
            oracle_set(ram+0x5319c4,oracle_require(),4);has_ambient=oracle_require();
            ambient.max_turn_battle=oracle_require();ambient.traction_limit_battle=oracle_require();
            status=frame_tank_configure_span(read_data,write_data,0,object,bytes,(uint32_t)length,1000000,has_ambient ? &ambient : 0,&consumed);
            if (!status) status=frame_tank_configure_derived(read_data,write_data,0,object);
            oracle_put((uint32_t)status);oracle_put(consumed);
            for (i=0;i<57;i++) {
                const struct config_field *q=&config_fields[i];uint32_t at=object+q->offset;
                if (q->reference) continue;
                if (q->kind==3) {
                    for (j=0;j<4;j++) oracle_put(oracle_get(ram+at+j*4,4));
                    for (j=0;j<3;j++) oracle_put(oracle_get(ram+at+28+j*4,4));
                } else if (q->kind==5) for (j=0;j<3;j++) oracle_put(oracle_get(ram+at+j*4,4));
                else oracle_put(oracle_get(ram+at,4));
            }
            oracle_put(oracle_get(ram+object+0x64a,4));oracle_put(oracle_get(ram+object+0x64e,4));
            oracle_put(oracle_get(ram+object+0xdd2,4));oracle_put(oracle_get(ram+object+0x1116,4));oracle_put(oracle_get(ram+object+0x111a,4));
        }
    }
    free(bytes);return ferror(stdin) ? 5 : 0;
}
