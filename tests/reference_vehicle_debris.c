/* Shared debris ownership/update/clear, with production bootstrap and RNG. */
#include "reference_io.h"
#include "frame_bootstrap.h"
#include "frame_vehicle_scene.h"
static unsigned char ram[0x800000];
static int loading;
static uint32_t draws,first_draw;
static uint32_t object(unsigned i){return 0x2000u+i*0x2000u;}
static void set(uint32_t at,uint32_t v,unsigned n){oracle_set(ram+at,v,n);}
static void field(uint32_t at,unsigned n){if(loading)set(at,oracle_require(),n);else oracle_put(oracle_get(ram+at,n));}
static void words(uint32_t at,unsigned n){unsigned i;for(i=0;i<n;i++)field(at+i*4u,4);}
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *in,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;if(at==0x55b6ac&&n==4){if(!draws)first_draw=oracle_get(in,n);draws++;}memcpy(ram+at,in,n);return 1;}
static int material(void *ctx,const unsigned char *name,uint32_t n,uint32_t mode,uint32_t *out)
{(void)ctx;(void)name;(void)n;(void)mode;*out=1;return 0;}
static void snapshot(void)
{
    unsigned i,b;for(i=0;i<9;i++){
        uint32_t o=object(i);words(o+0xc92,2);field(o+0xdef,1);field(o+0x118,4);
    }
    for(b=0;b<2;b++){
        uint32_t p=0x546460u+b*264u;words(p,66);
    }
    field(0x55b6ac,4);
}
static void input(uint32_t o)
{
    field(0x5904fc,4);field(o+0xde5,1);words(o,3);words(o+0x50,3);
    field(o+0xdee,1);field(o+0x27a,4);words(o+0x716,2);
    words(o+0x128,3);words(o+0x1b4,3);words(0x74aee0,3);words(0x74af18,3);
}
int main(void)
{
    uint32_t c,cases,n,k,own,op;unsigned i;int status;
    oracle_binary();cases=oracle_require();fwrite("VD01",1,4,stdout);oracle_put(cases);
    for(c=0;c<cases;c++){
        memset(ram,0,sizeof(ram));set(0x55c694,0x18000,4);
        for(i=0;i<9;i++){
            set(0x18000u+(i+1)*8u,0x19000u+i*8u,4);set(0x19004u+i*8u,object(i),4);
        }
        if(frame_bootstrap_vehicle_constants(write_data,0) ||
           frame_bootstrap_environment(read_data,write_data,0,0x438d60,material,0))return 2;
        loading=1;snapshot();n=oracle_require();oracle_put(n);
        for(k=0;k<n;k++){
            own=oracle_require();op=oracle_require();if(own>=9)return 2;input(object(own));
            loading=0;draws=0;first_draw=0;
            status=op?frame_vehicle_debris_clear(read_data,write_data,0,object(own)):
                frame_vehicle_debris_tick(read_data,write_data,0,object(own),own+1);
            oracle_put((uint32_t)status);oracle_put(draws);oracle_put(first_draw);snapshot();loading=1;
        }
    }
    return 0;
}
