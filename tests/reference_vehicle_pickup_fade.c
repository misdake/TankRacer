/* Production shared fade, including complete quad bank and bounded aliases. */
#include "reference_io.h"
#include "frame_bootstrap.h"
#include "frame_vehicle_scene.h"
static unsigned char ram[0x800000];
static int loading;
static void set(uint32_t at,uint32_t v,unsigned n){oracle_set(ram+at,v,n);}
static void field(uint32_t at,unsigned n){if(loading)set(at,oracle_require(),n);else oracle_put(oracle_get(ram+at,n));}
static void words(uint32_t at,unsigned n){unsigned i;for(i=0;i<n;i++)field(at+i*4u,4);}
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *in,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(ram+at,in,n);return 1;}
static void target(uint32_t at)
{
    uint32_t v;if(loading){v=oracle_require();set(at,v?0x2000u+v*0x1000u:0,4);}
    else {v=oracle_get(ram+at,4);oracle_put(v?(v-0x2000u)/0x1000u:0);}
}
static void snapshot(void)
{
    unsigned b,i;uint32_t p;
    for(b=0;b<2;b++){
        p=0x58faa0u+b*44u;for(i=0;i<4;i++)field(p+i,1);target(p+4);words(p+8,9);
        field(0x5450c0u+b*4u,4);field(0x5450c8u+b*4u,4);field(0x5457d8u+b*4u,4);
        for(i=0;i<10;i++){
            p=0x545148u+b*840u+i*84u;field(p,2);field(p+2,2);words(p+4,20);
        }
        for(i=0;i<10;i++){p=0x5457e0u+b*680u+i*68u;words(p,17);}
    }
    field(0x55ae1c,4);field(0x55ae20,4);words(0x54cc88,2560);
    words(0x54cc48,16);words(0x54f488,2);words(0x54f490,16);
}
int main(void)
{
    uint32_t c,cases,n,k,entity,object;unsigned i;int status;
    oracle_binary();cases=oracle_require();fwrite("VF01",1,4,stdout);oracle_put(cases);
    for(c=0;c<cases;c++){
        memset(ram,0,sizeof(ram));if(frame_bootstrap_vehicle_constants(write_data,0))return 2;
        set(0x545d38,oracle_require(),4);loading=0;
        field(0x531690,4);field(0x531694,4);for(i=0;i<30;i++)field(0x5316c0+i,1);
        loading=1;snapshot();n=oracle_require();oracle_put(n);
        for(k=0;k<n;k++){
            entity=oracle_require();object=0x2000u+entity*0x1000u;
            words(object,3);field(0x590564,4);field(0x5904fc,4);loading=0;
            status=frame_vehicle_shared_fade(read_data,write_data,0,object);
            oracle_put((uint32_t)status);snapshot();loading=1;
        }
    }
    return 0;
}
