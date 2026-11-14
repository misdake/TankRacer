/* Real resource/bootstrap/World constructors, stopped before lap linking.
 * A controlled scenery-only script is input, not a captured memory arena.
 * Build a real Player and run the registered scheduler's physics-stage slice. */
#define frame_world_load_span original_world_load_span
#include "../src/frame_world.c"
#undef frame_world_load_span
#include "reference_game.h"
#include "frame_math.h"
#include "frame_player_lifecycle.h"
#include "frame_bootstrap.h"
#include "frame_scene_effects.h"
#include "frame_vehicle_scene.h"
#include "frame_native.h"
static const frame_entity_factory_services *services;
static frame_data_read read_data;
static frame_data_write write_data;
static void *data_ctx;
static unsigned char script[40000],*ram;
static uint32_t script_size,world_loaded,object,entity,scenery_count,fall_class,crush_class,bounce_class,closer_class,spin_class,mover_class,explosion_class;
static uint32_t low_gravity;
static uint32_t word(uint32_t a){return reference_game_word(a,4);}
static void set(uint32_t a,uint32_t v,unsigned n){oracle_set(ram+a,v,n);}
static uint32_t object_of(uint32_t id){uint32_t r=word(word(0x55c694)+id*8u);return r?word(r+4u):0;}
static void emit(uint32_t a,unsigned n){unsigned i;for(i=0;i<n;i++)oracle_put(word(a+i*4u));}
static void narrow(uint32_t a,unsigned n){uint32_t v=reference_game_word(a,n);oracle_put(n==2&&v>32767?v|0xffff0000u:v);}
#include "reference_motion_io.h"
#include "reference_player_io.h"
int frame_world_load_span(frame_data_read r,frame_data_write w,void *c,uint32_t f,uint32_t l,const unsigned char *b,uint32_t n,const frame_entity_factory_services *e,uint32_t *out,uint32_t *used)
{
    int status;(void)b;(void)n;
    services=e;read_data=r;write_data=w;data_ctx=c;
    {unsigned char flag[4];oracle_set(flag,low_gravity,4);if(!w(c,0x546000,flag,4))return -1;}
    status=original_world_load_span(r,w,c,f,l,script,script_size,e,out,used);
    if(status)return status;
    world_loaded=1;return -98; /* Explicit test boundary, before unsupported lap rules. */
}
static void check(int status){if(status){fprintf(stderr,"scenery oracle status %d\n",status);exit(7);}}
static int material(void *ctx,const unsigned char *name,uint32_t n,uint32_t mode,uint32_t *out)
{(void)ctx;(void)name;(void)n;(void)mode;*out=4;return 0;}
static int tick(void *ctx,uint32_t target,uint32_t id,uint32_t has,uint32_t payload,uint32_t *out)
{
    /* M3 replaces the Player controller with direct drive inputs, on both sides. */
    if(id==entity&&target==0x41f930u)target=0x430a30u;
    return services->method(ctx,target,id,has,payload,out);
}
static int scene_id(uint32_t id){uint32_t r=word(word(0x55c694)+id*8);return id<=scenery_count||(r&&word(r)==explosion_class);}
static void list(uint32_t global,int scenery_only)
{
    uint32_t head=word(global),p=word(head),n=0,visited=0;
    while(p!=head){if(visited++==65536)exit(8);if(!scenery_only||scene_id(word(p+8)))n++;p=word(p);}
    oracle_put(n);p=word(head);
    while(p!=head){uint32_t id=word(p+8);if(!scenery_only||scene_id(id))oracle_put(id);p=word(p);}
}
static void scenery(void)
{
    uint32_t id,o,r,mesh,v,n,i,j,klass,basis;
    oracle_put(scenery_count);
    for(id=1;id<=scenery_count;id++){
        o=object_of(id);oracle_put(o!=0);if(!o)continue;
        r=word(word(0x55c694)+id*8u);klass=word(r);
        oracle_put(klass==fall_class?2u:klass==crush_class?1u:klass==bounce_class?3u:klass==closer_class?4u:klass==spin_class?5u:klass==mover_class?6u:0u);
        emit(o+16,1);emit(o,3);emit(o+24,9);basis=word(o+20);oracle_put(basis!=0);
        if(basis)emit(basis,9);else for(i=0;i<9;i++)oracle_put(i%4==0?4096u:0u);
        mesh=word(o+72);oracle_put(mesh!=0);if(mesh)narrow(mesh+12,2);
        emit(o+0x60,6);emit(o+0x5c,1);emit(o+0x94,1);emit(o+0x80,1);
        emit(o+0x90,1);narrow(o+0x4c,2);narrow(o+0x4e,2);narrow(o+0x78,2);narrow(o+0x7a,2);
        if(klass==fall_class){
            emit(o+0xea,1);emit(o+0xee,2);emit(o+0xe2,1);emit(o+0xda,1);emit(o+0xf6,1);
            oracle_put((word(o+0xea)&32u)!=0);if(word(o+0xea)&32u)emit(o+0xaa,9);
        }
        if(klass==crush_class){
            emit(o+0xae,2);emit(o+0xe5,1);narrow(o+0xca,1);emit(o+0xaa,1);emit(o+0xe9,1);
            emit(o+0xba,4);emit(o+0xb6,1);narrow(o+0xcc,1);emit(o+0xcd,1);
            n=word(o+0xcd);v=word(o+0xd5);
            for(i=0;i<n;i++){emit(v+i*32u,6);for(j=0;j<4;j++)narrow(v+i*32u+24+j,1);emit(v+i*32u+28,1);}
        }
        if(klass==bounce_class){
            emit(o+0x50,3);emit(o+0xaa,1);emit(o+0xba,1);
            oracle_put(reference_game_word(o+0xc0,2));narrow(o+0xc2,2);
            narrow(o+0xc4,1);narrow(o+0xc5,1);narrow(o+0xc6,1);narrow(o+0xcc,1);
            narrow(o+0xb8,2);narrow(o+0xbe,1);narrow(o+0xbf,1);
            narrow(o+0xb0,2);narrow(o+0xb2,2);narrow(o+0xb4,2);narrow(o+0xb6,2);narrow(o+0xae,2);
            emit(o+0xc7,1);narrow(o+0xcb,1);
        }
        if(klass==closer_class){uint32_t phase=word(o+0xb2),aux=word(o+0x88);emit(o+0xaa,2);oracle_put(phase==0x406490?1:phase==0x4064e0?2:phase==0x406480?3:0);if(aux)narrow(aux+0x22,2);else oracle_put(0);}
        if(klass==spin_class||klass==mover_class){
            emit(o+0x50,3);emit(o+0xaa,1);narrow(o+0xb2,2);narrow(o+0xb4,1);narrow(o+0xb5,1);
            emit(o+0xb6,9);for(i=0;i<12;i++)narrow(o+0xda+i*2,2);emit(o+0xae,1);
            if(word(o+0x88))narrow(word(o+0x88)+0x22,2);else oracle_put(0);
        }
        if(klass==mover_class)emit(o+0xf2,5);
        v=word(o+0x8c);oracle_put(v!=0);
        if(v){uint32_t pieces=word(v+12),count=reference_game_word(v+20,2);emit(v+24,1);narrow(v+22,2);emit(v,3);emit(v+28,3);emit(v+44,1);oracle_put(count);
            for(i=0;i<count;i++){uint32_t p=pieces+i*104;emit(p+12,3);emit(p,3);emit(p+28,12);emit(p+88,3);emit(p+76,1);narrow(p+80,2);narrow(p+82,2);emit(p+84,1);emit(p+100,1);}
        }
        /* Full current vertex payload catches shared/private mesh mutations. */
        n=mesh?reference_game_word(mesh,2):0;oracle_put(n);v=mesh?word(mesh+4):0;
        for(i=0;i<n;i++)for(j=0;j<3;j++)narrow(v+i*8u+j*2u,2);
    }
    list(0x54b48c,1);list(0x54b488,1);list(0x53e4b4,1);list(0x53d148,1);
    list(0x55b6a8,0);list(0x55b6bc,0);list(0x55b6b8,0);
    for(i=0;i<64;i++)list(0x544e70+i*4u,1);
}
static void sprite(uint32_t p){unsigned i;oracle_put(word(p)!=0);emit(p+4,3);emit(p+16,4);for(i=0;i<7;i++)narrow(p+32+i,1);narrow(p+40,2);}
static void explosions(void){
    uint32_t id,o,n=0,i,p;emit(0x53d12c,1);narrow(0x53d134,1);emit(0x54b4c8,1);
    for(i=0;i<15;i++)emit(0x590580+i*8,2);
    for(id=1;id<word(0x55c690);id++){p=word(word(0x55c694)+id*8);if(p&&word(p)==explosion_class&&word(p+4))n++;}
    oracle_put(n);
    for(id=1;id<word(0x55c690);id++){
        p=word(word(0x55c694)+id*8);if(!p||word(p)!=explosion_class||!word(p+4))continue;o=word(p+4);
        oracle_put(id);emit(o+16,1);emit(o,3);emit(o+24,9);narrow(o+120,2);sprite(o+0x7a);emit(o+80,3);emit(o+0xa4,2);narrow(o+0xad,1);
        for(i=0;i<10;i++){p=o+0xaeu+i*66;sprite(p);emit(p+42,5);narrow(p+62,2);}
        emit(o+0x35a,1);narrow(o+0x362,1);emit(o+0x363,2);
    }
}
static void snapshot(void){player_state();emit(object+0x10c2,1);emit(object+0xdaf,1);narrow(object+0xded,1);emit(0x55b6ac,1);emit(0x546000,1);scenery();explosions();}
int main(int argc,char **argv)
{
    uint32_t map,car,steps,control,i,result,spawn[3],velocity[3],camera[3];int status;
    if(argc!=6)return 2;
    map=(uint32_t)strtoul(argv[2],0,10);car=(uint32_t)strtoul(argv[3],0,10);steps=(uint32_t)strtoul(argv[4],0,10);if(steps>32)return 2;
    control=(uint32_t)strtoul(argv[5],0,10);
    oracle_binary();script_size=oracle_require();scenery_count=oracle_require();low_gravity=oracle_require();
    for(i=0;i<3;i++)spawn[i]=oracle_require();
    for(i=0;i<3;i++)velocity[i]=oracle_require();
    for(i=0;i<3;i++)camera[i]=oracle_require();
    if(script_size>=sizeof(script)||fread(script,1,script_size,stdin)!=script_size)return 3;
    status=reference_game_start(argv[1],map,car,0);if(status!=4||!world_loaded)return 6;
    ram=(unsigned char *)(uintptr_t)tank_data();
    if(word(0x55c68c)!=scenery_count)return 6;
    check(frame_entities_drain_queues(read_data,write_data,data_ctx,services->method,services->method_ctx,services->release,services->system_ctx,100000));
    check(frame_class_lookup_name(read_data,write_data,data_ctx,(const unsigned char *)"Fall",5,services,&fall_class));
    check(frame_class_lookup_name(read_data,write_data,data_ctx,(const unsigned char *)"Crush",6,services,&crush_class));
    check(frame_class_lookup_name(read_data,write_data,data_ctx,(const unsigned char *)"Bounce",7,services,&bounce_class));
    check(frame_class_lookup_name(read_data,write_data,data_ctx,(const unsigned char *)"Closer",7,services,&closer_class));
    check(frame_class_lookup_name(read_data,write_data,data_ctx,(const unsigned char *)"Spin",5,services,&spin_class));
    check(frame_class_lookup_name(read_data,write_data,data_ctx,(const unsigned char *)"Mover",6,services,&mover_class));
    check(frame_class_lookup_name(read_data,write_data,data_ctx,(const unsigned char *)"Explode",8,services,&explosion_class));
    check(frame_player_create(read_data,write_data,data_ctx,0,services,&entity));set(0x53e8b8,entity,4);object=object_of(entity);
    for(i=0;i<3;i++){set(object+0x78+i*4u,spawn[i],4);set(object+i*4u,spawn[i],4);}
    check(services->method(services->method_ctx,0x4350a0u,entity,1,0x5303c0u,&result));
    check(services->method(services->method_ctx,0x437b00u,entity,0,0,&result));
    check(frame_pickup_effect_initialize(read_data,write_data,data_ctx,material,0));
    check(frame_bootstrap_environment(read_data,write_data,data_ctx,0x4427c0u,material,0));
    for(i=0;i<9;i++)set(0x53e8e0+i*4u,i?0u:entity,4);
    for(i=0;i<3;i++){set(object+0x84+i*4u,spawn[i],4);set(object+0x50+i*4u,velocity[i],4);set(0x74aee0+i*4u,camera[i],4);set(0x74af18+i*4u,i==2?4096u:0u,4);}
    set(0x54b4b0,1,4);set(0x58e7cc,0,4);set(0x53e8b4,0,1);set(0x53e8b0,0,1);set(0x545ffc,0,4);set(0x58e7ac,0,4);
    set(0x55c688,0,4); /* End the controlled level arena before dynamic effects. */
    if(control==15||control==20){uint32_t k;for(k=0;k<16;k++){set(0x590564,control==20?(15-k)%7:0,4);check(frame_explosion_spawn(read_data,write_data,data_ctx,object,0,0,services,&((frame_native_context *)services->method_ctx)->explosion,&result));}set(0x590564,0,4);check(frame_entities_drain_queues(read_data,write_data,data_ctx,services->method,services->method_ctx,services->release,services->system_ctx,100000));}
    fwrite("SR06",1,4,stdout);oracle_put(entity);snapshot();
    for(i=0;i<steps;i++){
        if(control==19&&i==12){uint32_t id,p,o;for(id=1;id<word(0x55c690);id++){p=word(word(0x55c694)+id*8);if(!p||word(p)!=explosion_class||!word(p+4))continue;o=word(p+4);set(o+0x362,9,1);set(o+0x35a,96,4);}}
        if((control==14||control==16||control==17)&&i==12){uint32_t id,o,j,p;
            for(id=1;id<word(0x55c690);id++){p=word(word(0x55c694)+id*8);if(!p||word(p)!=explosion_class||!word(p+4))continue;o=word(p+4);
                set(o+0x9a,96,1);set(o+0x9b,96,1);set(o+0xa4,31,4);set(o+0x362,10,1);for(j=0;j<10;j++)set(o+0xec+j*66,0,2);
                if(control==17){set(o+0xec,1,2);set(o+0xd2,1,1);set(o+0xd3,1,1);set(o+0xd4,1,1);}
            }
        }
        if(control==16&&i==13)check(frame_explosion_spawn(read_data,write_data,data_ctx,object,0,0,services,&((frame_native_context *)services->method_ctx)->explosion,&result));
        set(0x5904fc,16,4);set(0x590564,i+1u,4);set(object+0x94,65536,4);set(object+0x98,0,4);set(object+0x9c,0,4);set(object+0xdea,1,1);
        status=frame_entities_tick(read_data,write_data,data_ctx,tick,services->method_ctx,100000);
        if(!status)status=frame_entities_drain_queues(read_data,write_data,data_ctx,services->method,services->method_ctx,services->release,services->system_ctx,100000);
        oracle_put((uint32_t)status);snapshot();if(status)break;
    }
    tank_destroy();return 0;
}
