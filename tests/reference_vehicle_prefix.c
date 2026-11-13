/* Exercise production prefix/helper entries. Stop 430A30 at its first contact
 * read, before contact writes, rather than copying any prefix implementation. */
#include "reference_io.h"
#include "physics_step.h"
#include "frame_weapon_spin.h"
#include "frame_ability_boost.h"
#include "frame_scene_effects.h"
#include "frame_environment.h"
#include "frame_math.h"
static unsigned char ram[0x800000];
enum { OBJECT=0x4000, CAMERA=0x7000, OTHER=0x9000 };
static int loading,stop_contact,reached;
static void set(uint32_t at,uint32_t value,unsigned n){oracle_set(ram+at,value,n);}
static uint32_t get(uint32_t at,unsigned n){return oracle_get(ram+at,n);}
static void field(uint32_t at,unsigned n)
{if(loading)set(at,oracle_require(),n);else oracle_put(get(at,n));}
static void words(uint32_t at,unsigned count){unsigned i;for(i=0;i<count;i++)field(at+i*4u,4);}
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{
    (void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;
    if(stop_contact&&at==OBJECT+0x11c){reached=1;return 0;}
    memcpy(out,ram+at,n);return 1;
}
static int write_data(void *ctx,uint32_t at,const void *in,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(ram+at,in,n);return 1;}
static void snapshot(void)
{
    static const unsigned spin[]={0xd99,0xd9a,0xd9b,0xd9f,0xda3,0xda7,0xdab,0xdaf,0xde4};
    static const unsigned boost[]={0xc9e,0xcaa,0xcb2,0xdd6,0xdbf,0xdc3,0xdc7,0xdcb,0xdcc,0xdcd};
    unsigned i,j;
    field(OBJECT+0xd91,4);field(OBJECT+0x90,4);field(OBJECT+0xa8,4);field(OBJECT+0xc9a,4);
    for(i=0;i<9;i++)field(OBJECT+spin[i],i==0||i==1||i==8?1:4);
    for(i=0;i<10;i++)field(OBJECT+boost[i],i==3?2:i>=7?1:4);
    field(OBJECT+0xdd0,2);field(OBJECT+0xdce,2);field(OBJECT+0xdd2,4);field(OBJECT+0x6a6,4);
    field(OBJECT+0x250,1);field(OBJECT+0x26d,1);
    field(0x53e8b4,1);field(0x53e8b0,1);words(0x5302e8,3);field(0x5302f4,4);words(0x5302f8,9);
    field(CAMERA+0xa8,4);field(CAMERA+0xbc,4);field(CAMERA+0xc0,4);
    field(0x545de0,1);field(0x545de4,1);
    field(0x5466e8,4);field(0x58ee12,2);field(0x58ee10,2);field(0x58ee08,2);
    field(0xa004,2);field(0xa104,2);words(0x546704,3);
    field(0x54669c,4);for(i=0;i<4;i++)field(0x5461b0+i*136u,4);
    for(i=0;i<4;i++)for(j=0;j<9;j++)field(0x5461d4+i*136u+j/3u*44u+j%3u*4u,4);
    field(0x55b6ac,4);field(0x55c698,4);
    field(0x55ae20,4);for(i=0;i<100;i++)field(0x54f494+i*32u,4);
    field(0x550114,4);field(0x54f474,4);
    field(OTHER+0xc9e,4);field(OTHER+0xcaa,4);field(OTHER+0xcb2,4);field(OTHER+0xdd6,2);
}
int main(void)
{
    uint32_t cases,c,k,n,op,dt,frame,player,active,preset,variant,vehicle_mode,limit,dispatch,unused;
    int status;
    oracle_binary();cases=oracle_require();fwrite("VP01",1,4,stdout);oracle_put(cases);
    for(c=0;c<cases;c++){
        memset(ram,0,sizeof(ram));
        set(0x55c694,0x1000,4);set(0x1008,0x2000,4);set(0x2000,0x2200,4);set(0x2004,OBJECT,4);
        set(0x1010,0x2010,4);set(0x2010,0x2200,4);set(0x2014,OTHER,4);
        set(0x1018,0x2020,4);set(0x2024,CAMERA,4);set(0x53cc38,3,4);
        set(0x545f00,0x2100,4);set(0x2100+28,1,4);set(0x2200+36,0x2300,4);
        set(0x58ee00,0xa000,4);set(0x58ee04,0xa100,4);set(OBJECT+0xded,1,1);
        loading=1;snapshot();loading=0;
        /* Immutable/configured inputs, rather than captured executable state. */
        for(k=0;k<6;k++)set(0x5317fc+k*4u,oracle_require(),4);
        set(0x531814,oracle_require(),4);set(OBJECT+0x70e,oracle_require(),4);set(OTHER+0x70e,oracle_require(),4);
        set(0x531958,oracle_require(),4);set(0x53195c,oracle_require(),4);
        set(0x531960,oracle_require(),4);set(0x531964,oracle_require(),4);
        for(k=0;k<3;k++)set(0x74aee0+k*4u,oracle_require(),4);
        set(OBJECT+0x4e,oracle_require(),2);
        n=oracle_require();oracle_put(n);
        for(k=0;k<n;k++){
            op=oracle_require();dt=oracle_require();frame=oracle_require();player=oracle_require();active=oracle_require();
            preset=oracle_require();variant=oracle_require();vehicle_mode=oracle_require();limit=oracle_require();dispatch=oracle_require();
            set(0x5904fc,dt,4);set(0x590564,frame,4);set(OBJECT+0xde5,player,1);set(OBJECT+0xdea,active,1);
            set(OTHER+0xde5,!player,1);
            set(0x53e40c,preset,4);set(0x54671c,variant,4);set(OBJECT+0xdda,vehicle_mode,2);set(0x531b80,oracle_require(),4);
            set(0x52ee9c,oracle_require(),4);set(0x2200+28,dispatch==2?0:1,4);
            set(0x2304,dispatch==1?0:dispatch==3?0x123456:0x437bf0,4);
            reached=0;stop_contact=0;unused=0;
            if(op==0){stop_contact=1;status=physics_vehicle_step(read_data,write_data,0,1,frame_math_trig(),0);stop_contact=0;if(reached)status=0;}
            else if(op==1)status=frame_weapon_spin_start(read_data,write_data,0,1,&unused);
            else if(op==2)status=frame_weapon_spin_update(read_data,write_data,0,OBJECT,limit);
            else if(op==3)status=frame_ability_boost_start(read_data,write_data,0,1);
            else if(op==4)status=frame_ability_boost_apply(read_data,write_data,0,1,&unused);
            else if(op==5)status=frame_environment_player_view(read_data,write_data,0,frame_math_trig());
            else if(op==6)status=frame_ability_boost_tick(read_data,write_data,0,OBJECT,0,0);
            else return 6;
            oracle_put((uint32_t)status);oracle_put(op==1?unused:0);snapshot();
        }
    }
    return 0;
}
