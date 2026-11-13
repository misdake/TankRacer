/* Capture the actual post-ground boundary inside the complete C vehicle step.
 * No copied integration formula or alternative expected implementation. */
#include "reference_game.h"
#include "physics_step.h"
#include "physics_effects.h"
#include "physics_entities.h"
#include "frame_math.h"
static unsigned char baseline[TANKRACER_DATA_BYTES];
static uint32_t object,variant,captured,after_calls;
static unsigned char *data(void){return (unsigned char *)(uintptr_t)tank_data();}
static uint32_t word(uint32_t a){return reference_game_word(a,4);}
static void set(uint32_t a,uint32_t v,unsigned n){oracle_set(data()+a,v,n);}
static void emit(uint32_t a,unsigned n){unsigned i;for(i=0;i<n;i++)oracle_put(word(a+i*4u));}
static void narrow(uint32_t a,unsigned width){uint32_t v=reference_game_word(a,width);oracle_put(width==2&&v>32767?v|0xffff0000u:v);}
#include "reference_motion_io.h"
static void inputs(void)
{
    uint32_t external=word(object+0x14),tag=external==object+0x18 || external==object+0x2b2 ? 1u:external?2u:0u;
    static const uint32_t offsets[]={0x3d2,0x43a,0x302,0x36a,0x4a2,0x50a,0x5da};unsigned i;
    emit(0x5904fc,1);emit(0x590564,1);narrow(object+0xde5,1);emit(0x52ee9c,1);
    narrow(object+0xdd0,2);emit(0x545ffc,1);emit(0x58e7ac,1);emit(0x59054c,1);emit(object+0xe13,1);narrow(object+0x762,2);oracle_put(tag);
    if(tag==2)emit(external,9);else for(i=0;i<9;i++)oracle_put(i%4==0?4096:0);
    narrow(object+0x6f6,2);narrow(object+0x6f8,1);narrow(object+0x6fa,2);narrow(object+0x6fc,1);
    oracle_put(word(0x5319c8)>>24);narrow(0x5319cc,2);emit(0x5319d0,1);oracle_put(word(0x531ca4)>>24);
    for(i=0;i<7;i++)emit(object+offsets[i],3);
    emit(object+0x62e,1);emit(object+0x6fe,1);emit(0x531bd8,3);
    state();gun(1);
}
static void inject(void)
{
    if(variant==1){set(object+0x50,0x7fffffffu,4);set(object+0x54,0x80000000u,4);set(object+0x58,0x80000001u,4);set(object+0x78,0x7fffffffu,4);}
    if(variant==2)set(object+0x1c,0x80000000u,4);
    if(variant==3){set(object+0x700,0,2);set(object+0x1d4,0x80000000u,4);set(object+0x21a,0,4);}
    if(variant==7)set(object+0x366,0,4);
    if(variant==8)set(object+0xde5,0,1);
    if(variant==9)set(0x52ee9c,2,4);
    if(variant==10){set(object+0xdd0,480,2);set(0x545ffc,1,4);set(0x58e7ac,0,4);set(0x59054c,123,4);set(object+0x14,0,4);}
    if(variant==11){unsigned i;set(object+0x14,object+0x1000,4);for(i=0;i<9;i++)set(object+0x1000+i*4,i%4==0?(i==4?2048u:4096u):0,4);}
    if(variant==12){
        static const uint32_t matrix[]={4000,1,0,65407,339,0,0,0,4096};unsigned i;
        for(i=0;i<9;i++){set(object+0x2b2+i*4,matrix[i],4);set(object+0x18+i*4,i%4==0?4096u:0u,4);}
        for(i=0;i<3;i++)set(object+0xb4+i*4,0,4);
        set(object+0xc4,0,4);set(object+0xcc,0,4);
    }
    if(variant==14){set(object+0xf8,0x7fffffffu,4);set(object+0xf0,0x80000000u,4);set(object+0xc0,0x7ffffff0u,4);set(object+0xa0,0x80000010u,4);}
}
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if(at>TANKRACER_DATA_BYTES||n>TANKRACER_DATA_BYTES-at)return 0;memcpy(out,tank_data()+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *bytes,uint32_t n)
{
    (void)ctx;if(at>TANKRACER_DATA_BYTES||n>TANKRACER_DATA_BYTES-at)return 0;
    memcpy(data()+at,bytes,n);
    /* Last ground-delta word: capture before any previous-position read, so
     * injected wrapping inputs cannot race a pre-evaluated write argument. */
    if(!captured&&at==object+0x104){captured=1;inject();inputs();}
    return 1;
}
static int scene(void *ctx,int phase,uint32_t o,uint32_t id,int32_t arg)
{
    (void)ctx;(void)id;(void)arg;
    if(phase!=PHYSICS_SCENE_AFTER_POSE)return 0;
    if(o!=object)exit(8);
    after_calls++;
    if(variant==6 || variant==15){set(o+0x78,word(o+0x78)+123,4);set(o+0x138,word(o+0x138)+77,4);set(o+0xf8,123456,4);}
    return variant==4?11:variant==5?-19:0;
}
static int feedback(void *ctx,int32_t intensity){(void)ctx;(void)intensity;return 0;}
int main(int argc,char **argv)
{
    uint32_t map,vehicle,id,step;int status;physics_effect_environment effect;
    physics_collision_services collision;physics_obstacle_services obstacle;physics_vehicle_services services;physics_entity_environment entities;
    if(argc!=4)return 2;
    map=(uint32_t)strtoul(argv[2],0,10);vehicle=(uint32_t)strtoul(argv[3],0,10);
    status=reference_game_start(argv[1],map,vehicle,0);if(status)return status;
    id=word(0x53e8b8);object=word(word(word(0x55c694)+id*8u)+4u);
    memcpy(baseline,tank_data(),TANKRACER_DATA_BYTES);
    effect=(physics_effect_environment){read_data,write_data,0,frame_math_trig()};
    collision=(physics_collision_services){physics_impact_particles_callback,&effect,0};
    obstacle=(physics_obstacle_services){&collision,physics_known_entity_method,feedback,&entities};
    entities=(physics_entity_environment){read_data,write_data,0,frame_math_trig(),&obstacle,scene,0};
    services=(physics_vehicle_services){&obstacle,scene,0};
    oracle_binary();fwrite("MO01",1,4,stdout);
    for(variant=0;variant<16;variant++){
        memcpy(data(),baseline,TANKRACER_DATA_BYTES);
        for(step=0;step<4;step++){
            uint32_t dt=(uint32_t[]){1,16,17,40}[variant%4];
            set(0x5904fc,dt,4);set(0x590564,variant==12?10:variant==13?UINT32_MAX-step:step+1,4);
            set(object+0x94,65536,4);set(object+0x9c,step%2?16384u:0u,4);
            set(object+0xded,1,1);set(object+0xdea,1,1);
            captured=after_calls=0;oracle_put(variant);oracle_put(step);
            status=physics_vehicle_step(read_data,write_data,0,id,frame_math_trig(),&services);
            if(!captured){fprintf(stderr,"before motion: map=%u vehicle=%u variant=%u step=%u status=%d\n",map,vehicle,variant,step,status);return 7;}
            oracle_put((uint32_t)status);state();gun(0);oracle_put(after_calls);
            if(status)break;
        }
    }
    tank_destroy();return 0;
}
