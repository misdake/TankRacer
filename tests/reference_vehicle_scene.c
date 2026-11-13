/* Full production active/inactive chain. The test supplies typed field values,
 * not a captured RAM image; only the C boundary uses original data addresses. */
#include "reference_io.h"
#include "frame_bootstrap.h"
#include "frame_math.h"
#include "frame_vehicle_scene.h"
static unsigned char ram[0x800000];
static uint32_t addresses[12000],sizes[12000],fields,draws;
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *in,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;if(at==0x55b6ac&&n==4)draws++;memcpy(ram+at,in,n);return 1;}
static void set(uint32_t at,uint32_t value,unsigned n){oracle_set(ram+at,value,n);}
static int material(void *ctx,const unsigned char *name,uint32_t n,uint32_t mode,uint32_t *out)
{(void)ctx;(void)n;(void)mode;*out=!strcmp((const char *)name,"filth")?101:!strcmp((const char *)name,"crap")?102:103;return 0;}
static void load_fields(int remember)
{
    uint32_t n=oracle_require(),i,a,s,v;if(n>12000)exit(2);if(remember)fields=n;
    for(i=0;i<n;i++){
        a=oracle_require();s=oracle_require();v=oracle_require();
        if(a>sizeof(ram)||s>sizeof(ram)-a||s<1||s>4)exit(2);
        set(a,v,s);if(remember){addresses[i]=a;sizes[i]=s;}
    }
}
int main(void)
{
    uint32_t cases,c,n,k,i,op,own,o;int status;
    oracle_binary();cases=oracle_require();fwrite("VA01",1,4,stdout);oracle_put(cases);
    for(c=0;c<cases;c++){
        memset(ram,0,sizeof(ram));set(0x5964d4,32768,4);memcpy(ram+0x590540,"england",8);
        if(frame_math_initialize(write_data,0)||frame_bootstrap_vehicle_constants(write_data,0)||
           frame_bootstrap_environment(read_data,write_data,0,0x438d60,material,0))return 2;
        set(0x531870,2000,4);set(0x531880,(uint32_t)-2000,4);set(0x55c694,0x18000,4);
        for(i=0;i<2;i++){set(0x18008+i*8u,0x19000+i*8u,4);set(0x19004+i*8u,0x2000+i*0x2000u,4);}
        load_fields(1);n=oracle_require();oracle_put(n);
        for(k=0;k<n;k++){
            own=oracle_require();op=oracle_require();if(own>1||op>1)return 2;o=0x2000+own*0x2000u;
            load_fields(0);draws=0;
            status=op?frame_vehicle_scene_inactive(read_data,write_data,0,o,frame_math_trig()):
                frame_vehicle_scene_active(read_data,write_data,0,o,own+1,frame_math_trig());
            oracle_put((uint32_t)status);oracle_put(draws);
            for(i=0;i<fields;i++)oracle_put(oracle_get(ram+addresses[i],sizes[i]));
        }
    }
    return 0;
}
