/* Unchanged motion body; injectable terrain height isolates the branch/error
 * boundary. Grid removal, notifications and matrices use real production C. */
#define physics_terrain_height oracle_height
#include "../src/frame_fall.c"
#undef physics_terrain_height
#include "frame_math.h"
#include "reference_io.h"
static unsigned char ram[0x800000];
static uint32_t allocation,queries,query_x,query_z;
static int height_status;static int32_t height_value;
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *in,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(ram+at,in,n);return 1;}
int oracle_height(physics_world_read read,void *ctx,uint32_t root,int32_t x,int32_t z,int32_t *out)
{(void)read;(void)ctx;(void)root;queries++;query_x=(uint32_t)x;query_z=(uint32_t)z;if(height_status>=0)*out=height_value;return height_status;}
static uint32_t word(uint32_t at){return oracle_get(ram+at,4);}
static void set(uint32_t at,uint32_t value){oracle_set(ram+at,value,4);}
static int allocate(void *ctx,uint32_t n,uint32_t *out)
{(void)ctx;if(n>0x400000-allocation)return -4;*out=allocation;allocation+=(n+3)&~3u;memset(ram+*out,0,n);return 0;}
static int release(void *ctx,uint32_t at){(void)ctx;(void)at;return 0;}
static void load(uint32_t at,uint32_t n){uint32_t i;for(i=0;i<n;i++)set(at+i*4,oracle_require());}
static void load_list(uint32_t at){uint32_t n=oracle_require(),i,out;set(at,at);set(at+4,at);set(at+8,at);
    for(i=0;i<n;i++)if(frame_list_append(read_data,write_data,0,oracle_require(),at,allocate,release,0,10000,&out))exit(2);}
static void emit_list(uint32_t at){uint32_t n=0,node=word(at);while(node!=at){if(n++==65536)exit(2);node=word(node);}
    oracle_put(n);node=word(at);while(node!=at){oracle_put(word(node+8));node=word(node);}}
int main(void)
{
    uint32_t steps,i,j,effect;oracle_binary();fwrite("FT01",1,4,stdout);
    while(oracle_word(&steps)){
        frame_entity_factory_services e={0};memset(ram,0,sizeof(ram));allocation=0x100000;queries=0;query_x=query_z=0;
        set(0x54b3d8,1);set(0x55c694,0x7000);
        for(i=1;i<=2;i++){set(0x7000+i*8,0x8000+i*8);set(0x8000+i*8,0x9000);set(0x8004+i*8,0x10000+(i-1)*0x1000);}
        set(0x9024,0x9100);set(0x545ef0,0xa000);set(0x10014,0x10018);set(0x10048,0x20000);set(0x100da,2);
        load(0x10000,3);load(0x10018,9);load(0x11018,9);oracle_set(ram+0x20024,oracle_require(),2);
        load(0x100ee,2);load(0x100e2,1);load(0x100f6,1);load(0x100ea,1);load(0x10080,1);
        load(0x52ec9c,1);load(0x530acc,1);load(0x530ad0,2);load(0x10090,1);load(0x10010,1);
        effect=oracle_require();if(effect){set(0x1008c,0x30000);set(0x30018,1);}
        load_list(0x50000);set(0x55b6bc,0x50000);
        for(i=0;i<64;i++){load_list(0x60000+i*16);set(0x544e70+i*4,0x60000+i*16);}
        e.allocate=allocate;e.release=release;
        for(i=0;i<steps;i++){
            int status;set(0x5904fc,oracle_require());height_status=(int32_t)oracle_require();height_value=oracle_signed(oracle_require());e.work_limit=oracle_require();
            status=frame_fall_tick(read_data,write_data,0,1,&e,frame_math_trig());oracle_put((uint32_t)status);
            for(j=0;j<9;j++)oracle_put(word(0x10018+j*4));
            oracle_put(word(0x100ee));oracle_put(word(0x100f2));oracle_put(word(0x100e2));oracle_put(word(0x100f6));
            oracle_put(word(0x100ea));oracle_put(word(0x10080));oracle_put(word(0x10010));
            oracle_put(queries);oracle_put(query_x);oracle_put(query_z);emit_list(0x50000);
            for(j=0;j<64;j++)emit_list(0x60000+j*16);
        }
    }
    return ferror(stdin)?3:0;
}
