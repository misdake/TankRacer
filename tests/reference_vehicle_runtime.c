/* Fresh resource-built car -> real prefix/physics/after-pose, without race rules.
 * Save C's own just-created actor before later race/session initialization.
 * Rust independently constructs its actor from files, never from this state. */
#define physics_pose_update original_pose_update
#include "../src/physics_pose.c"
#undef physics_pose_update
#include "reference_game.h"
#include "frame_bootstrap.h"
#include "frame_results.h"
#include "frame_math.h"
#include "frame_vehicle_scene.h"
#include "frame_scene_effects.h"
#include "physics_step.h"
#include "physics_effects.h"
#include "physics_entities.h"
#include "frame_player_input.h"
#include "frame_player_state.h"
#include "frame_player_collision.h"
#include "frame_race.h"
#include "frame_time.h"
#include "frame_turret.h"
static unsigned char *ram,fresh[0x1128];
static uint32_t object,entity,poses,actors[1],count=1,after_calls;
static int controls;
static uint32_t word(uint32_t a){return reference_game_word(a,4);}
static void set(uint32_t a,uint32_t v,unsigned n){oracle_set(ram+a,v,n);}
static void emit(uint32_t a,unsigned n){unsigned i;for(i=0;i<n;i++)oracle_put(word(a+i*4u));}
static void narrow(uint32_t a,unsigned n){uint32_t v=reference_game_word(a,n);oracle_put(n==2&&v>32767?v|0xffff0000u:v);}
static void raw(uint32_t a,unsigned n){oracle_put(reference_game_word(a,n));}
static uint32_t owner_entity(uint32_t o){if(!o)return 0;if(o!=object)exit(8);return 1;}
#include "reference_motion_io.h"
#include "reference_player_io.h"
#include "reference_vehicle_scene_io.h"
int physics_pose_update(physics_world_read read,physics_world_write write,void *ctx,uint32_t o,const physics_trig *trig)
{
    uint32_t id=word(0x53e8b8),record=word(word(0x55c694)+id*8u);int status;
    int player=record&&word(record)==word(0x544f98)&&word(record+4u)==o;
    status=original_pose_update(read,write,ctx,o,trig);if(status)return status;
    if(player&&++poses==2){object=o;entity=id;memcpy(fresh,tank_data()+o,sizeof(fresh));}
    return 0;
}
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if(at>TANKRACER_DATA_BYTES||n>TANKRACER_DATA_BYTES-at)return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *data,uint32_t n)
{(void)ctx;if(at>TANKRACER_DATA_BYTES||n>TANKRACER_DATA_BYTES-at)return 0;memcpy(ram+at,data,n);return 1;}
static int material(void *ctx,const unsigned char *name,uint32_t n,uint32_t mode,uint32_t *out)
{
    (void)ctx;(void)n;(void)mode;
    *out=!strcmp((const char *)name,"filth")?1u:!strcmp((const char *)name,"crap")?2u:
         !strcmp((const char *)name,"splash2")?3u:4u;return 0;
}
static int scene(void *ctx,int phase,uint32_t o,uint32_t id,int32_t arg)
{
    (void)ctx;(void)arg;
    if(phase==PHYSICS_SCENE_AFTER_POSE){after_calls++;return frame_vehicle_scene_active(read_data,write_data,0,o,id,frame_math_trig());}
    if(phase==PHYSICS_SCENE_CONTACT_FEEDBACK)return 0;
    return -8;
}
static int feedback(void *ctx,int32_t strength){(void)ctx;(void)strength;return 0;}
static void empty_list(uint32_t p){set(p,p,4);set(p+4,p,4);}
static void snapshot(void)
{
    unsigned i;player_state();emit(0x55b6ac,1);for(i=0;i<16;i++)raw(0x546748+i*2u,2);
    emit(0x58eebc,1);emit(0x58ef5c,1);
    emit(0x55ae20,1);emit(0x55ae1c,1);emit(0x54f490,800);emit(0x54f448,16);emit(0x54f488,2);emit(0x550110,8);
    scene_state();
    if(controls){emit(object+0x94,3);raw(object+0x1106,1);raw(object+0x1107,1);emit(0x544fb8,1);}
    if(controls>=3){emit(0x53e4f0,2);emit(0x5904e0,1);emit(object+0xcba,1);raw(object+0xdda,2);raw(object+0xdea,1);}
    if(controls==7){narrow(object+0x762,2);emit(object+0xe0f,1);emit(0x544fa0,1);raw(0x544fa8,1);raw(object+0xd0a,1);narrow(object+0xe0b,2);raw(0x544fac,1);raw(0x544fb0,1);}
}
int main(int argc,char **argv)
{
    uint32_t map,car,mode,steps,i,dt,active;int status;
    physics_effect_environment effect;physics_collision_services collision;physics_obstacle_services obstacle;physics_vehicle_services services;physics_entity_environment entities;
    if(argc!=6&&argc!=7)return 2;
    controls=argc==7?atoi(argv[6]):0;
    map=(uint32_t)strtoul(argv[2],0,10);car=(uint32_t)strtoul(argv[3],0,10);mode=(uint32_t)strtoul(argv[4],0,10);steps=(uint32_t)strtoul(argv[5],0,10);
    if(steps>128)return 2;
    status=reference_game_start(argv[1],map,car,mode);if(status)return status;
    if(!object||poses<2)return 6;
    ram=(unsigned char *)(uintptr_t)tank_data();memcpy(ram+object,fresh,sizeof(fresh));actors[0]=object;
    /* World/race/session are outside this vehicle slice. Exclude their actors,
     * scenery collision lists and RNG; initialize the real shared effect rules. */
    for(i=0;i<64;i++)empty_list(word(0x544e70+i*4u));
    empty_list(word(0x53cd18));for(i=0;i<9;i++)set(0x53e8e0+i*4u,i?0u:entity,4);
    set(0x545ffc,0,4);set(0x58e7ac,0,4);set(0x53e8b4,0,1);set(0x53e8b0,0,1);
    set(0x54b4b0,1,4);set(0x58e7cc,0,4);set(0x54b480,0,4);set(0x53e414,0,4);
    for(i=0;i<3;i++){set(0x74aee0+i*4u,0,4);set(0x74af18+i*4u,i==2?4096u:0,4);}
    set(0x55b6ac,0,4);
    if(controls){set(0x53ccdc,0,4);set(0x53cca8,0,4);set(0x544fb8,0,4);for(i=0;i<16;i++)set(0x592000+i*4u,i,4);}
    if(controls==2){
        set(object+0x50,20000,4);set(object+0x58,500000,4);set(object+0xe4,20000,4);set(object+0xec,500000,4);
        set(object+0x107e,300,4);set(object+0x118,80,4);
        set(object+0xfc,64000,4);set(object+0x100,(uint32_t)-32000,4);set(object+0x104,2000,4);
        set(object+0x108,50000,4);set(object+0x114,(uint32_t)-50000,4);
    }
    status=frame_results_route_random(read_data,write_data,0);if(status)return 7;
    status=frame_bootstrap_environment(read_data,write_data,0,0x438d60,material,0);if(status)return 7;
    status=frame_bootstrap_environment(read_data,write_data,0,0x4427c0,material,0);if(status)return 7;
    status=frame_scene_pools_initialize(write_data,0);if(status)return 7;
    effect=(physics_effect_environment){read_data,write_data,0,frame_math_trig()};
    collision=(physics_collision_services){physics_impact_particles_callback,&effect,0};
    obstacle=(physics_obstacle_services){&collision,physics_known_entity_method,feedback,&entities};
    services=(physics_vehicle_services){&obstacle,scene,0};
    entities=(physics_entity_environment){read_data,write_data,0,frame_math_trig(),&obstacle,scene,0};
    if(controls>=3){status=frame_race_initialize(write_data,0);if(status)return 8;status=frame_race_reset_laps(read_data,write_data,0);if(status)return 8;}
    oracle_binary();fwrite("VR01",1,4,stdout);snapshot();
    for(i=0;i<steps;i++){
        if(controls>=3){
            uint32_t continuation;
            dt=controls==6 ? (uint32_t[]){1000,1000,990,17,16,1000,16,17,20,1000,16,17}[i%12u]
                          : (uint32_t[]){1000,500,490,10,990,10,16,1000,17,20,1000,16}[i%12u];
            set(0x590550,dt,4);status=frame_time_physics_dt(read_data,write_data,0,&continuation);if(status)return 8;
            status=frame_race_tick(read_data,write_data,0);if(status)return 8;
            dt=word(0x5904fc);
        }else{
            dt=(uint32_t[]){16,17,20,12}[i%4u];active=controls==2||i>=3u;
            set(0x5904fc,dt,4);set(object+0xdea,active,1);
        }
        set(0x590564,i+1u,4);
        if(controls){
            frame_player_input_rates rates={(uint16_t)word(0x530c60),(uint16_t)word(0x530c64)};frame_player_input_result result;
            if(controls<3)set(0x5904e0,i==0?1u:2u,4);
            set(0x74b605,i%12u<8u&&i!=1u,1);set(0x74b606,i%12u==9u,1);
            set(0x74b601,i%12u==10u,1);set(0x74b602,(i%12u>=4u&&i%12u<8u)||i%12u==10u,1);
            set(0x74b60f,controls==2&&i%12u>=4u&&i%12u<8u,1);
            if(controls>=3){
                set(0x74b605,controls==3 ? i>=1u : controls==4 ? 1u : i>=3u,1);
                set(0x74b606,i==8u,1);set(0x74b601,0,1);set(0x74b602,i>=7u&&i<=9u,1);set(0x74b60f,i==8u,1);
                set(0x74b60a,controls==7&&i>=6u&&i<=8u,1);set(0x74b60b,controls==7&&i>=8u&&i<=10u,1);
            }
            status=frame_player_input_step(read_data,write_data,0,object,&rates,&result);if(status)return 8;
            set(0x53e3fc,controls!=2||i%12u!=7u,4);
            if(result.driving){
                status=frame_player_collision_state(read_data,write_data,0,object);if(status)return 8;
                if(controls==7){status=frame_turret_input(read_data,write_data,0,object);if(status)return 8;}
                status=frame_player_aux_brake(read_data,write_data,0,object,result.auxiliary_down);if(status)return 8;
            }
        }else{
            set(object+0x94,i%12u<8u?65536u:0,4);set(object+0x98,i%12u==9u?65536u:0,4);
            set(object+0x9c,i%12u>=4u&&i%12u<8u?12000u:0,4);
        }
        after_calls=0;status=physics_vehicle_step(read_data,write_data,0,entity,frame_math_trig(),&services);
        oracle_put((uint32_t)status);oracle_put(after_calls);snapshot();if(status)break;
    }
    tank_destroy();return 0;
}
