#include "physics_effects.h"
#include "physics_collision.h"
#include "frame_scene_effects.h"
#include "frame_math.h"
#include "reference_io.h"
static unsigned char ram[0x600000];
static const uint32_t actors[2]={0x10000,0x20000};
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *data,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(ram+at,data,n);return 1;}
static void field(uint32_t at,unsigned n,unsigned width,int output)
{
    unsigned i;for(i=0;i<n;i++){uint32_t p=at+i*width;if(output){uint32_t v=oracle_get(ram+p,width);oracle_put(width==2&&v>32767?v|0xffff0000u:v);}else oracle_set(ram+p,oracle_require(),width);}
}
static void pools(int output)
{
    unsigned i;field(0x55ae20,1,4,output);field(0x55ae1c,1,4,output);
    for(i=0;i<100;i++)field(0x54f490u+i*32u,8,4,output);
    field(0x54f448,16,4,output);field(0x54f488,2,4,output);field(0x550110,8,4,output);
}
static void batches(int output)
{
    unsigned i,j;for(i=0;i<4;i++){
        uint32_t base=0x58f400u+i*404u,owner;
        for(j=0;j<10;j++){
            uint32_t p=base+j*38u;field(p,2,2,output);field(p+4,2,4,output);field(p+12,1,4,output);field(p+20,1,4,output);field(p+16,1,4,output);field(p+24,1,4,output);field(p+28,1,4,output);field(p+32,6,1,output);
        }
        field(base+0x17c,2,4,output);
        if(output){owner=oracle_get(ram+base+0x184,4);oracle_put(!owner?0:owner==actors[0]?1:2);}
        else{owner=oracle_require();if(owner>2)exit(2);oracle_set(ram+base+0x184,owner?actors[owner-1]:0,4);}
        field(base+0x188,3,4,output);
    }
}
static void shape(int output)
{
    uint32_t mesh=0x40000,nv,nf;unsigned i;
    if(output){nv=oracle_get(ram+mesh,2);nf=oracle_get(ram+mesh+2,2);}
    else{
        nv=oracle_require();nf=oracle_require();if(nv>256||nf>256)exit(2);
        oracle_set(ram+mesh,nv,2);oracle_set(ram+mesh+2,nf,2);
        oracle_set(ram+mesh+4,mesh+0x1000,4);oracle_set(ram+mesh+8,mesh+0x2000,4);
    }
    for(i=0;i<nv;i++)field(mesh+0x1000+i*8u,3,2,output);
    field(mesh+0x2000,nf*6u,4,output);
}
static void ground(uint32_t object,int output)
{
    unsigned i;
    if(!output){
        field(0x52ee9c,1,4,0);field(object+0xde5,1,1,0);
        field(object,3,4,0);field(object+0x2a6,12,4,0);
        field(object+0xdff,4,1,0);oracle_set(ram+object+0x296,0x40000,4);
    }
    field(object+0xdf7,4,1,output);field(object+0x292,2,1,output);
    field(object+0x50,3,4,output);field(object+0xb8,1,4,output);
    for(i=0;i<4;i++){
        uint32_t r=object+0x11c+i*70u;
        field(r+24,1,2,output);field(r+40,1,4,output);field(r+56,3,4,output);
    }
    shape(output);
}
int main(void)
{
    uint32_t op;oracle_binary();fwrite("IP01",1,4,stdout);
    while(oracle_word(&op)){
        uint32_t actor=oracle_require(),object,external,kind,corner,handle,out=0x7182;unsigned i;int32_t points[6];unsigned char colors[8];int status=0;
        if(actor<1||actor>2)return 2;
        object=actors[actor-1];
        field(0x590564,1,4,0);field(0x5904fc,1,4,0);field(0x55b6ac,1,4,0);field(0x545e1c,1,4,0);
        field(object+0x1082,1,4,0);field(object+0xec,1,4,0);external=oracle_require();field(object+0x1000,9,4,0);oracle_set(ram+object+0x14,external?object+0x1000:0,4);
        for(i=0;i<4;i++)field(object+0x128+i*70u,3,4,0);
        field(0x531868,3,4,0);field(0x531878,3,4,0);pools(0);batches(0);
        kind=oracle_require();corner=oracle_require();handle=oracle_require();
        for(i=0;i<6;i++)points[i]=oracle_signed(oracle_require());
        for(i=0;i<8;i++)colors[i]=(unsigned char)oracle_require();
        if(op==7)ground(object,0);
        if(op==1)status=frame_line_allocate_values(read_data,write_data,0,points,colors,&out);
        else if(op==2)status=frame_line_update_values(read_data,write_data,0,handle,points,colors);
        else if(op==3)status=frame_line_release(read_data,write_data,0,handle);
        else if(op==4)status=frame_scene_pools_initialize(write_data,0);
        else if(op==5){oracle_set(ram+0x30028,corner,4);status=physics_impact_particles(read_data,write_data,0,object,0x30000,oracle_signed(kind),frame_math_trig());}
        else if(op==6)status=physics_impact_particles_tick(read_data,write_data,0,object,frame_math_trig());
        else if(op==7){
            physics_effect_environment env={read_data,write_data,0,frame_math_trig()};
            physics_collision_services services={physics_impact_particles_callback,&env,0};
            status=physics_ground_collision_step(read_data,write_data,0,object,frame_math_trig(),&services);
        }
        else return 2;
        oracle_put((uint32_t)status);oracle_put(out);field(0x55b6ac,1,4,1);field(object+0x1082,1,4,1);field(0x545e1c,1,4,1);pools(1);batches(1);
        if(op==7)ground(object,1);
    }
    return ferror(stdin)?3:0;
}
