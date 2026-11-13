/* Production temporary-line and shield entries, real lookup tables/bootstrap. */
#include "reference_io.h"
#include "frame_bootstrap.h"
#include "frame_math.h"
#include "frame_scene_effects.h"
#include "frame_vehicle_scene.h"
static unsigned char ram[0x800000];
enum { OBJECT=0x2000 };
static int loading;
static uint32_t rng_writes;
static void set(uint32_t at,uint32_t v,unsigned n){oracle_set(ram+at,v,n);}
static uint32_t get(uint32_t at,unsigned n){return oracle_get(ram+at,n);}
static void field(uint32_t at,unsigned n){if(loading)set(at,oracle_require(),n);else oracle_put(get(at,n));}
static void words(uint32_t at,unsigned n){unsigned i;for(i=0;i<n;i++)field(at+i*4u,4);}
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *in,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;if(at==0x55b6ac&&n==4)rng_writes++;memcpy(ram+at,in,n);return 1;}
static int material(void *ctx,const unsigned char *name,uint32_t n,uint32_t mode,uint32_t *out)
{(void)ctx;(void)name;(void)n;(void)mode;*out=1;return 0;}
static void snapshot(void)
{
    unsigned i,j;field(OBJECT+0xdd0,2);field(OBJECT+0xdce,2);field(0x55b6ac,4);field(0x54669c,4);
    words(0x5461b0,136);
    for(i=0;i<9;i++){
        for(j=0;j<4;j++){
            uint32_t p=0x58ee20u+i*160u+j*39u;
            words(p,6);field(p+24,1);field(p+25,2);words(p+27,3);
        }
        field(0x58ee20u+i*160u+156u,4);
    }
    field(0x55ae20,4);field(0x55ae1c,4);words(0x54f490,800);words(0x550110,8);words(0x54f448,16);words(0x54f488,2);
}
int main(void)
{
    uint32_t cases,c,n,k,op,i,slot;int status;
    oracle_binary();cases=oracle_require();fwrite("VE02",1,4,stdout);oracle_put(cases);
    for(c=0;c<cases;c++){
        memset(ram,0,sizeof(ram));if(frame_math_initialize(write_data,0))return 2;
        loading=1;snapshot();loading=0;n=oracle_require();oracle_put(n);
        for(k=0;k<n;k++){
            op=oracle_require();set(OBJECT+0xdd0,oracle_require(),2);set(OBJECT+0xdce,oracle_require(),2);
            for(i=0;i<3;i++)set(OBJECT+i*4u,oracle_require(),4);
            set(0x59055c,oracle_require(),4);set(0x5904fc,oracle_require(),4);set(OBJECT+0xe08,oracle_require(),1);
            set(OBJECT+0xde5,oracle_require(),1);set(0x58e7ac,oracle_require(),4);set(OBJECT+0x1135,oracle_require(),4);
            set(OBJECT+20,oracle_require()?0x7000:0,4);for(i=0;i<9;i++)set(0x7000+i*4u,oracle_require(),4);
            rng_writes=0;
            if(op==0)status=frame_bootstrap_environment(read_data,write_data,0,0x438d60,material,0);
            else if(op==1)status=frame_temp_effects_start(read_data,write_data,0,OBJECT);
            else if(op==2)status=frame_temp_effects_tick(read_data,write_data,0,OBJECT);
            else if(op==3){slot=get(OBJECT+0xdce,2);if(slot>32767)slot|=0xffff0000u;status=frame_temp_effects_expire(read_data,write_data,0,slot);}
            else if(op==4)status=frame_shield_orbit_tick(read_data,write_data,0,OBJECT);
            else return 2;
            oracle_put((uint32_t)status);oracle_put(rng_writes);snapshot();
        }
    }
    return 0;
}
