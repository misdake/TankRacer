/* Capture both freshly executed production Player creation paths at their
 * second pose call, before session ticks. Model/config loading is unchanged. */
#define physics_pose_update original_pose_update
#include "../src/physics_pose.c"
#undef physics_pose_update
#include "reference_game.h"
#include "frame_math.h"
static uint32_t object,captured,poses,spawn[12],phase,variant,saved_environment[3];
static uint32_t word(uint32_t at){return reference_game_word(at,4);}
static void emit(uint32_t at,unsigned n){unsigned i;for(i=0;i<n;i++)oracle_put(word(at+i*4u));}
static void narrow(uint32_t at,unsigned n)
{uint32_t v=reference_game_word(at,n);oracle_put(n==2&&v>32767?v|0xffff0000u:v);}
#include "reference_motion_io.h"
static void snapshot(void)
{
    uint32_t i;
    state();
    emit(object+0xe4,3);emit(object+0xa4,1);emit(object+0xac,1);emit(object+0xb0,1);
    narrow(object+0xdeb,1);narrow(object+0xdde,1);emit(object+0xf4,1);
    for(i=0;i<4;i++){
        uint32_t p=object+0x11c+i*70u;emit(p,3);narrow(p+24,2);emit(p+32,2);
        narrow(p+68,2);emit(p+56,3);emit(p+40,1);emit(p+48,2);
    }
    emit(object+0x234,3);emit(object+0x251,3);
    emit(object+0x10be,1);narrow(object+0xd90,1);emit(object+0x10c6,1);emit(object+0x118,1);
    emit(object+0x107e,1);emit(object+0xd95,1);oracle_put(word(object+0xd8c)!=0);emit(object+0x114,1);emit(object+0x108,3);emit(object+0xfc,3);
    emit(object+0xd91,1);emit(object+0x90,1);emit(object+0xa8,1);emit(object+0xc9a,1);
    narrow(object+0xdd0,2);narrow(object+0xdce,2);emit(object+0xdd2,1);emit(object+0x6a6,1);
    emit(object+0xc9e,1);emit(object+0xcaa,1);emit(object+0xcb2,1);narrow(object+0xdd6,2);
    emit(object+0xdbf,1);emit(object+0xdc3,1);emit(object+0xdc7,1);
    narrow(object+0xdcb,1);narrow(object+0xdcc,1);narrow(object+0xdcd,1);
    narrow(object+0xd99,1);narrow(object+0xd9a,1);emit(object+0xd9b,6);narrow(object+0xde4,1);
}
int physics_pose_update(physics_world_read read,physics_world_write write,void *ctx,uint32_t o,const physics_trig *trig)
{
    uint32_t entity=word(0x53e8b8),record=word(word(0x55c694)+entity*8u),i;int status;
    int player=record&&word(record)==word(0x544f98)&&word(record+4u)==o;
    if(player&&!poses){
        saved_environment[0]=word(0x545ffc);saved_environment[1]=word(0x58e7ac);saved_environment[2]=word(0x59054c);
        if(variant){
            unsigned char bytes[4];uint32_t values[3]={1,variant==2,93},at[3]={0x545ffc,0x58e7ac,0x59054c};
            for(i=0;i<3;i++){oracle_set(bytes,values[i],4);if(!write(ctx,at[i],bytes,4))return -1;}
            if(variant==3){
                int32_t matrix[9]={4096,0,0,0,4096,0,0,0,4096};
                uint32_t position[3]={123456,(uint32_t)-1024,(uint32_t)-245678};
                physics_matrix_rotate(matrix,1,8192,trig);physics_matrix_rotate(matrix,0,1638,trig);physics_matrix_rotate(matrix,2,3276,trig);
                for(i=0;i<3;i++){oracle_set(bytes,position[i],4);if(!write(ctx,o+0x78u+i*4u,bytes,4)||!write(ctx,o+i*4u,bytes,4))return -1;}
                for(i=0;i<9;i++){oracle_set(bytes,(uint32_t)matrix[i],4);if(!write(ctx,o+0x2b2u+i*4u,bytes,4)||!write(ctx,o+0x18u+i*4u,bytes,4))return -1;}
            }
        }
        phase=word(0x59054c);for(i=0;i<3;i++)spawn[i]=word(o+0x78u+i*4u);
        for(i=0;i<9;i++)spawn[i+3]=word(o+0x2b2u+i*4u);
    }
    status=original_pose_update(read,write,ctx,o,trig);if(status)return status;
    if(!player||++poses!=2)return 0;
    object=o;captured++;
    oracle_put(1);oracle_put(entity);for(i=0;i<12;i++)oracle_put(spawn[i]);
    emit(0x52ee9c,1);emit(0x52ed90,1);emit(0x53e414,1);emit(0x545ffc,1);emit(0x58e7ac,1);oracle_put(phase);emit(0x5319c4,1);emit(0x531bd8,3);
    snapshot();gun(1);
    narrow(object+0xe17,1);narrow(object+0xe18,1);narrow(object+0xe19,1);emit(object+0xe1a,2);
    narrow(object+0xc54,1);narrow(object+0xc55,1);emit(object+0xc56,12);emit(object+0xc86,3);
    for(i=0;i<68;i++)emit(object+0xaac+i*6u,1);
    emit(object+0xc44,1);emit(object+0xc4c,1);emit(object+0xc50,1);
    for(i=0;i<8;i++){
        uint32_t p=object+0x990+i*34u;emit(p,6);narrow(p+24,2);narrow(p+26,2);
        narrow(p+28,2);narrow(p+30,2);narrow(p+32,1);narrow(p+33,1);
    }
    emit(object+0xaa0,3);
    for(i=0;i<15;i++){
        uint32_t p=object+0x798+i*32u;emit(p,6);narrow(p+24,2);narrow(p+26,2);
        narrow(p+28,2);narrow(p+30,1);narrow(p+31,1);
    }
    emit(object+0x978,4);
    narrow(object+0xdf1,1);narrow(object+0xdf2,1);narrow(object+0xdf3,1);narrow(object+0xdf4,1);
    emit(object+0x988,2);
    emit(object+0xc92,2);narrow(object+0xdef,1);
    if(variant){
        unsigned char bytes[4];uint32_t at[3]={0x545ffc,0x58e7ac,0x59054c};
        for(i=0;i<3;i++){oracle_set(bytes,saved_environment[i],4);if(!write(ctx,at[i],bytes,4))return -1;}
    }
    return 0;
}
int main(int argc,char **argv)
{
    int status;if(argc!=7)return 2;variant=(uint32_t)strtoul(argv[6],0,10);if(variant>3)return 2;
    oracle_binary();fwrite("VS04",1,4,stdout);
    status=reference_game_start_cup(argv[1],(uint32_t)strtoul(argv[2],0,10),(uint32_t)strtoul(argv[3],0,10),
        (uint32_t)strtoul(argv[4],0,10),(uint32_t)strtoul(argv[5],0,10),0);
    if(status)return status;
    oracle_put(0);oracle_put(captured);tank_destroy();return 0;
}
