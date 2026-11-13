/* Production track spray and smoke, including real impact generation/pool. */
#include "reference_io.h"
#include "frame_bootstrap.h"
#include "frame_math.h"
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
{(void)ctx;(void)n;(void)mode;*out=!strcmp((const char *)name,"filth")?101:!strcmp((const char *)name,"crap")?102:!strcmp((const char *)name,"splash2")?103:!strcmp((const char *)name,"splash3")?104:7;return 0;}
static void metadata(void)
{
    unsigned i,j;for(i=0;i<16;i++)field(0x5321a0+i,1);
    for(i=0;i<6;i++){
        uint32_t p=0x5321b0+i*23u;for(j=0;j<5;j++)field(p+j,1);
        field(p+5,2);field(p+7,2);field(p+9,2);field(p+11,2);field(p+13,1);field(p+15,4);field(p+19,4);
    }
    words(0x532240,36);
}
static void snapshot(void)
{
    unsigned i,j;uint32_t p;
    for(i=0;i<8;i++){p=OBJECT+0x990+i*34u;words(p,6);field(p+24,2);field(p+26,2);field(p+28,2);field(p+30,2);field(p+32,1);field(p+33,1);}
    words(OBJECT+0xaa0,3);
    for(i=0;i<15;i++){p=OBJECT+0x798+i*32u;words(p,6);field(p+24,2);field(p+26,2);field(p+28,2);field(p+30,1);field(p+31,1);}
    words(OBJECT+0x978,4);field(OBJECT+0xdf1,1);field(OBJECT+0xdf2,1);field(OBJECT+0xdf3,1);field(OBJECT+0xdf4,1);
    field(OBJECT+0x988,4);field(OBJECT+0x98c,4);field(0x55b6ac,4);field(OBJECT+0x1082,4);
    for(i=0;i<4;i++){
        uint32_t b=0x58f400+i*404u;
        for(j=0;j<10;j++){p=b+j*38u;field(p,2);field(p+2,2);words(p+4,2);field(p+12,4);field(p+20,4);field(p+16,4);field(p+24,4);field(p+28,4);
            {unsigned k;for(k=0;k<6;k++)field(p+32+k,1);}}
        words(b+0x17c,2);
        if(loading)set(b+0x184,oracle_require()?OBJECT:0,4);else oracle_put(get(b+0x184,4)!=0);
        words(b+0x188,3);
    }
    field(0x545e1c,4);field(0x55ae20,4);field(0x55ae1c,4);words(0x54f490,800);words(0x550110,8);words(0x54f448,16);words(0x54f488,2);
}
static void input(void)
{
    unsigned i,j;
    set(0x5904fc,oracle_require(),4);set(0x590564,oracle_require(),4);set(OBJECT+0xde5,oracle_require(),1);set(0x53e414,oracle_require(),4);
    for(i=0;i<3;i++)set(OBJECT+0x50+i*4u,oracle_require(),4);
    set(OBJECT+0xc0,oracle_require(),4);set(OBJECT+0xa0,oracle_require(),4);set(OBJECT+0xf0,oracle_require(),4);
    set(OBJECT+0xa4,oracle_require(),4);set(OBJECT+0xec,oracle_require(),4);
    for(i=0;i<2;i++)set(OBJECT+0x716+i*4u,oracle_require(),4);
    for(i=0;i<4;i++)set(OBJECT+0x134+i*70u,oracle_require(),2);
    for(i=0;i<4;i++)for(j=0;j<3;j++)set(OBJECT+0x128+i*70u+j*4u,oracle_require(),4);
    for(i=0;i<4;i++)set(OBJECT+0x144+i*70u,oracle_require(),4);
    for(i=0;i<9;i++)set(OBJECT+24+i*4u,oracle_require(),4);
    for(i=0;i<2;i++)for(j=0;j<12;j++)set(OBJECT+(i?0x2a6:0x30e)+j*4u,oracle_require(),4);
    set(OBJECT+20,oracle_require()?0x7000:0,4);for(i=0;i<9;i++)set(0x7000+i*4u,oracle_require(),4);
    set(0x54b4b0,oracle_require(),4);
}
int main(void)
{
    uint32_t c,cases,n,k,op,swamp,hardware;int status;
    oracle_binary();cases=oracle_require();fwrite("VP03",1,4,stdout);oracle_put(cases);
    for(c=0;c<cases;c++){
        swamp=oracle_require();hardware=oracle_require();memset(ram,0,sizeof(ram));
        if(frame_math_initialize(write_data,0)||frame_bootstrap_vehicle_constants(write_data,0))return 2;
        /* Confirmed immutable PE offsets, as in reference_vehicle.c. The
         * preserved C bootstrap does not currently install these six words. */
        set(0x531870,2000,4);set(0x531880,(uint32_t)-2000,4);
        set(0x5964d4,hardware?32768:0,4);memcpy(ram+0x590540,swamp?"swamp":"england",swamp?6:8);
        if(frame_bootstrap_environment(read_data,write_data,0,0x438d60,material,0))return 2;
        loading=0;metadata();loading=1;snapshot();loading=0;n=oracle_require();oracle_put(n);
        for(k=0;k<n;k++){
            op=oracle_require();input();rng_writes=0;
            status=op==0?frame_vehicle_track_spray(read_data,write_data,0,OBJECT):frame_vehicle_damage_smoke(read_data,write_data,0,OBJECT,frame_math_trig());
            oracle_put((uint32_t)status);oracle_put(rng_writes);snapshot();
        }
    }
    return 0;
}
