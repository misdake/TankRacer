#include "frame_deformable.h"
#include "reference_io.h"
static unsigned char ram[0x600000];
static uint32_t vertex_count,flag_count;
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{
    (void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;
    if(at>=0x9000u&&at<0x9000u+vertex_count&&at+n>0x9000u+flag_count)return 0;
    memcpy(out,ram+at,n);return 1;
}
static int write_data(void *ctx,uint32_t at,const void *data,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(ram+at,data,n);return 1;}
static void load(uint32_t at,unsigned n,unsigned width){unsigned i;for(i=0;i<n;i++)oracle_set(ram+at+i*width,oracle_require(),width);}
int main(void)
{
    uint32_t op;oracle_binary();fwrite("DH01",1,4,stdout);
    oracle_set(ram+0x55c694,0x1000,4);oracle_set(ram+0x1008,0x2000,4);oracle_set(ram+0x1010,0x2010,4);
    oracle_set(ram+0x2004,0x3000,4);oracle_set(ram+0x2014,0x5000,4);
    oracle_set(ram+0x3048,0x7000,4);oracle_set(ram+0x30dd,0x9000,4);oracle_set(ram+0x7004,0x8000,4);
    while(oracle_word(&op)) {
        unsigned i,j;uint32_t external,result=0x7182;int status;if(op!=1)return 2;
        load(0x30ca,1,1);load(0x30aa,1,4);load(0x30e9,1,4);load(0x30ae,1,4);load(0x30b2,1,4);load(0x30e5,1,4);load(0x304e,1,2);load(0x3084,1,4);
        load(0x3000,3,4);external=oracle_require();load(0xa000,9,4);oracle_set(ram+0x3014,external?0xa000:0,4);
        load(0x5000,3,4);load(0x5904fc,1,4);load(0x5d95,1,4);load(0x55b6ac,1,4);
        vertex_count=oracle_require();flag_count=oracle_require();if(vertex_count>256||flag_count>vertex_count)return 2;oracle_set(ram+0x7000,vertex_count,2);
        for(i=0;i<vertex_count;i++)load(0x8000+i*8,3,2);
        load(0x9000,flag_count,1);
        status=frame_deformable_hit(read_data,write_data,0,1,2,&result);oracle_put((uint32_t)status);oracle_put(result);
        oracle_put(oracle_get(ram+0x30ca,1));oracle_put(oracle_get(ram+0x30aa,4));oracle_put(oracle_get(ram+0x30e9,4));oracle_put(oracle_get(ram+0x5d95,4));oracle_put(oracle_get(ram+0x55b6ac,4));
        for(i=0;i<vertex_count;i++)for(j=0;j<3;j++){uint32_t v=oracle_get(ram+0x8000+i*8+j*2,2);oracle_put(v>32767?v|0xffff0000u:v);}
    }
    return ferror(stdin)?3:0;
}
