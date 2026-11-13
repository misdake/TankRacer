/* Real C contact->drive->collisions->motion, with explicit scene boundaries. */
#include "reference_game.h"
#include "physics_step.h"
#include "physics_effects.h"
#include "frame_math.h"
#include "frame_scene_effects.h"
static unsigned char *ram;
static unsigned char baseline[TANKRACER_DATA_BYTES];
static uint32_t object,ids[9],actors[9],count,own,captured,variant,after_calls;
static uint32_t word(uint32_t a){return oracle_get(ram+a,4);}
static void set(uint32_t a,uint32_t v,unsigned n){oracle_set(ram+a,v,n);}
static void emit(uint32_t a,unsigned n){unsigned i;for(i=0;i<n;i++)oracle_put(word(a+i*4u));}
static void narrow(uint32_t a,unsigned n){uint32_t v=oracle_get(ram+a,n);oracle_put(n==2&&v>32767?v|0xffff0000u:v);}
static void raw(uint32_t a,unsigned n){oracle_put(oracle_get(ram+a,n));}
static uint32_t object_of(uint32_t id){uint32_t p=word(word(0x55c694)+id*8u);return p?word(p+4):0;}
static uint32_t owner_entity(uint32_t o){unsigned i;if(!o)return 0;for(i=0;i<count;i++)if(actors[i]==o)return ids[i];exit(8);}
static uint32_t tank_index(uint32_t o){unsigned i;if(!o)return 0;for(i=0;i<count;i++)if(actors[i]==o)return i+1u;exit(8);}
#include "reference_motion_io.h"
static void pools(void)
{
    emit(0x55ae20,1);emit(0x55ae1c,1);emit(0x54f490,800);emit(0x54f448,16);emit(0x54f488,2);emit(0x550110,8);
    {unsigned i,j,k;for(i=0;i<4;i++){
        uint32_t b=0x58f400u+i*404u;
        for(j=0;j<10;j++){
            uint32_t p=b+j*38u;narrow(p,2);narrow(p+2,2);emit(p+4,2);emit(p+12,1);emit(p+20,1);emit(p+16,1);emit(p+24,1);emit(p+28,1);
            for(k=0;k<6;k++)raw(p+32+k,1);
        }
        emit(b+0x17c,2);oracle_put(owner_entity(word(b+0x184)));emit(b+0x188,3);
    }}
}
static void shape(uint32_t mesh)
{
    uint32_t nv=oracle_get(ram+mesh,2),nf=oracle_get(ram+mesh+2,2),i,j;
    oracle_put(nv);oracle_put(nf);
    for(i=0;i<nv;i++)for(j=0;j<3;j++)narrow(word(mesh+4)+i*8+j*2,2);
    emit(word(mesh+8),nf*6u);
}
static void pose_inputs(void)
{
    static const uint32_t offsets[]={0x3d2,0x43a,0x302,0x36a,0x4a2,0x50a,0x5da};unsigned i;
    uint32_t e=word(object+0x14),tag=e==object+0x18||e==object+0x2b2?1u:e?2u:0u;
    for(i=0;i<7;i++)emit(object+offsets[i],3);
    emit(object+0x62e,1);emit(object+0x6fe,1);emit(0x531bd8,3);
    raw(object+0xdd0,2);emit(0x545ffc,1);emit(0x58e7ac,1);emit(0x59054c,1);emit(object+0xe13,1);narrow(object+0x762,2);oracle_put(tag);
    if(tag==2)emit(e,9);else for(i=0;i<9;i++)oracle_put(i%4==0?4096u:0u);
}
static void actor(unsigned index)
{
    unsigned i;object=actors[index];oracle_put(ids[index]);raw(object+0xde5,1);state();
    emit(object+0xe4,3);emit(object+0xa4,1);emit(object+0xac,1);emit(object+0xb0,1);raw(object+0xdeb,1);narrow(object+0xdde,1);emit(object+0xf4,1);
    pose_inputs();emit(object+0x10be,1);
    oracle_put((ram[object+0xd90]&1u)!=0);emit(object+0x10c6,1);emit(object+0x118,1);emit(object+0x107e,1);emit(object+0xd95,1);oracle_put(owner_entity(word(object+0xd8c)));emit(object+0x114,1);emit(object+0x108,3);emit(object+0xfc,3);
    for(i=0;i<4;i++){
        uint32_t r=object+0x11c+i*70u;emit(r,3);narrow(r+24,2);emit(r+32,2);raw(r+68,2);emit(r+56,3);emit(r+40,1);emit(r+48,2);
    }
    raw(object+0xde1,1);raw(object+0xde2,1);raw(object+0xde3,1);raw(object+0xdee,1);emit(object+0x26e,4);
    narrow(object+0xe0b,2);narrow(object+0xe0d,2);raw(object+0xe08,1);emit(object+0xe0f,1);
    for(i=0;i<2;i++){emit(object+0x240+i*29u,4);raw(object+0x250+i*29u,1);}
    emit(object+0x716,2);emit(object+0xd0,2);emit(object+0xc8,1);emit(object+0xd14,1);
    for(i=0;i<4;i++)raw(object+0xdf7+i,1);
    for(i=0;i<4;i++)raw(object+0xdfb+i,1);
    for(i=0;i<2;i++)raw(object+0x292+i,1);
    emit(object+0x1082,1);for(i=0;i<4;i++)raw(object+0xdff+i,1);
    shape(word(object+0x296));gun(1);
    raw(word(object+0x48)+12,2);emit(object+0x60,6);emit(object+0x632,6);raw(object+0xe07,1);raw(object+0xded,1);
}
static void frame_config(void)
{
    static const uint32_t drive[]={0x662,0x666,0x66a,0x66e,0x672,0x676,0x67a,0x67e,0x682,0x686,0x68a,0x68e,0x696,0x6a6,0x6ae,0x6b2,0x6b6,0x6ba,0x6be,0x6c2,0x6c6,0x6ca,0x6ce,0x70e,0x712};
    static const uint32_t contact[]={0x64a,0x64e,0x6d2,0x6d6,0x6da,0x6de,0x6e2,0x6e6,0x6ea,0x6ee,0x6f2},layout[]={0x632,0x636,0x63a,0x63e,0x646};unsigned i;
    emit(0x5904fc,1);emit(0x590564,1);emit(0x52ee9c,1);
    emit(object+0x94,3);emit(object+0x107a,1);raw(object+0x10bc,1);narrow(object+0x10b6,2);narrow(object+0x10b8,2);
    raw(object+0xdea,1);oracle_put(word(word(word(0x55c694)+ids[own]*8u))==word(0x544f98)&&ram[object+0x1107]!=0);narrow(object+0xdd6,2);emit(object+0xc9e,1);emit(object+0xcaa,1);
    for(i=0;i<25;i++)emit(object+drive[i],1);
    emit(0x546018,100);emit(0x531b80,5);emit(0x531b68,5);emit(0x5319e0,5);emit(0x5319d4,3);emit(0x531c98,3);
    for(i=0;i<11;i++)emit(object+contact[i],1);
    for(i=0;i<5;i++)emit(object+layout[i],1);
    for(i=0;i<16;i++)raw(0x546748+i*2,2);
    emit(0x53e414,1);raw(0x531b9c,2);raw(0x531b9e,1);emit(object+0x69a,3);
    emit(object+0x234,3);emit(object+0x251,3);
    emit(0x546000,1);emit(0x58e7ac,1);emit(0x52ed90,1);
    emit(0x531be8,1);emit(0x531bf0,1);emit(0x531bf8,1);emit(0x531c00,1);
    raw(0x531948,2);raw(0x53194c,1);raw(0x531950,2);raw(0x531954,1);raw(object+0x700,1);
    raw(object+0x6f6,2);raw(object+0x6f8,1);raw(object+0x6fa,2);raw(object+0x6fc,1);
    oracle_put(word(0x5319c8)>>24);narrow(0x5319cc,2);emit(0x5319d0,1);oracle_put(word(0x531ca4)>>24);
    emit(0x531b98,1);emit(0x531b94,1);emit(0x544f70,4);
}
static void snapshot(void)
{
    unsigned i;for(i=0;i<count;i++)actor(i);object=actors[own];
    emit(0x55b6ac,1);emit(0x55c698,1);emit(0x58eebc,1);emit(0x58ef5c,1);
    oracle_put(tank_index(word(0x546014)));if(word(0x546014)){emit(0x546008,1);emit(0x546010,1);}else{oracle_put(0);oracle_put(0);}
    emit(0x545e1c,1);pools();
}
static void capture(void)
{
    unsigned i;frame_config();oracle_put(count);oracle_put(own);
    for(i=0;i<9;i++)oracle_put(tank_index(object_of(word(0x53e8e0+i*4u))));
    snapshot();
}
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{
    (void)ctx;if(at>TANKRACER_DATA_BYTES||n>TANKRACER_DATA_BYTES-at)return 0;
    if(!captured&&at==actors[own]+0x11c){captured=1;object=actors[own];capture();}
    memcpy(out,ram+at,n);return 1;
}
static int write_data(void *ctx,uint32_t at,const void *data,uint32_t n)
{(void)ctx;if(at>TANKRACER_DATA_BYTES||n>TANKRACER_DATA_BYTES-at)return 0;memcpy(ram+at,data,n);return 1;}
static int scene(void *ctx,int phase,uint32_t o,uint32_t entity,int32_t arg)
{
    int status;(void)ctx;(void)entity;(void)arg;
    if(phase==PHYSICS_SCENE_AFTER_POSE){after_calls++;status=physics_impact_particles_tick(read_data,write_data,0,o,frame_math_trig());if(status)return status;return variant==7?9:0;}
    return 0;
}
static int method(void *ctx,uint32_t fn,uint32_t id,uint32_t payload,uint32_t *out)
{(void)ctx;(void)fn;(void)id;(void)payload;(void)out;return -6;}
static int feedback(void *ctx,int32_t strength){(void)ctx;(void)strength;return 0;}
static void empty_list(uint32_t p){set(p,p,4);set(p+4,p,4);}
int main(int argc,char **argv)
{
    uint32_t map,vehicle,i,j,step,turn,root,peer;int status;physics_effect_environment effect;physics_collision_services collision;physics_obstacle_services obstacle;physics_vehicle_services services;
    if(argc!=4)return 2;
    map=(uint32_t)strtoul(argv[2],0,10);vehicle=(uint32_t)strtoul(argv[3],0,10);
    status=reference_game_start_with_opponents(argv[1],map,vehicle,0,1);if(status)return status;
    ram=(unsigned char *)(uintptr_t)tank_data();
    for(i=0;i<9;i++){uint32_t id=word(0x53e8e0+i*4u);if(id){ids[count]=id;actors[count]=object_of(id);if(id==word(0x53e8b8))own=count;count++;}}
    if(count<2)return 6;
    root=own;peer=root?0u:1u;
    memcpy(baseline,ram,TANKRACER_DATA_BYTES);
    effect=(physics_effect_environment){read_data,write_data,0,frame_math_trig()};collision=(physics_collision_services){physics_impact_particles_callback,&effect,0};obstacle=(physics_obstacle_services){&collision,method,feedback,0};services=(physics_vehicle_services){&obstacle,scene,0};
    oracle_binary();fwrite("VC01",1,4,stdout);
    for(variant=0;variant<8;variant++){
        own=root;memcpy(ram,baseline,TANKRACER_DATA_BYTES);object=actors[own];
        for(i=0;i<64;i++)empty_list(word(0x544e70+i*4u));
        empty_list(word(0x53cd18));
        set(0x531868,0,4);set(0x53186c,0,4);set(0x531870,2000,4);set(0x531878,0,4);set(0x53187c,0,4);set(0x531880,(uint32_t)-2000,4);
        for(i=0;i<count;i++){
            uint32_t a=actors[i];set(a+0xd8c,0,4);set(a+0xde9,1,1);set(a+0xd90,0,1);
            if(i!=own){
                int32_t shift=i==(own?0u:1u)?(variant%2?1500:1500000):1500000;
                for(j=0;j<3;j++){uint32_t p=word(object+j*4u)+(j==2?(uint32_t)shift:0u);set(a+j*4u,p,4);set(a+0x78+j*4u,p,4);set(a+0x84+j*4u,p,4);set(a+0x2a6+j*4u,p,4);}
                for(j=0;j<4;j++){set(a+0x128+j*70u,word(object)+(j<2?(uint32_t)-512:512u),4);set(a+0x130+j*70u,word(object+8)+(j%2?(uint32_t)-1000:1000u)+(uint32_t)shift,4);}
                set(a+0x50,0,4);set(a+0x54,0,4);set(a+0x58,(uint32_t)-300000,4);
            }
        }
        set(object+0x50,variant&1u?70000u:600001u,4);set(object+0x58,variant&1u?300000u:0u,4);
        if(variant==3)set(object+0xd90,1,1);
        if(variant==4){int32_t points[6]={0};unsigned char colors[8]={0};uint32_t h;for(i=0;i<95;i++){status=frame_line_allocate_values(read_data,write_data,0,points,colors,&h);if(status)return 7;}}
        captured=1;set(object+0x1082,500,4);set(0x590564,0,4);
        status=physics_impact_particles(read_data,write_data,0,object,object+0x11c,1,frame_math_trig());if(status)return 7;
        for(step=0;step<4;step++)for(turn=0;turn<2;turn++){
            own=turn?peer:root;
            object=actors[own];set(0x5904fc,variant%3==0?17u:16u,4);set(0x590564,step+1u,4);set(0x52ee9c,(uint32_t[]){0,1,2,3,4,5,0,0}[variant],4);
            set(object+0x94,65536,4);set(object+0x9c,step%2?16384u:0u,4);set(object+0xdea,1,1);set(object+0xded,1,1);set(object+0xdaf,0,4);set(object+0xdcb,0,1);set(object+0xdd0,0,2);set(object+0xdd6,0,2);
            captured=after_calls=0;oracle_put(variant);oracle_put(step*2u+turn);
            status=physics_vehicle_step(read_data,write_data,0,ids[own],frame_math_trig(),&services);
            if(!captured){fprintf(stderr,"no contact boundary: map=%u vehicle=%u variant=%u step=%u status=%d\n",map,vehicle,variant,step,status);return 9;}
            oracle_put((uint32_t)status);snapshot();oracle_put(after_calls);
            if(status)goto next_variant;
        }
next_variant: ;
    }
    tank_destroy();return 0;
}
