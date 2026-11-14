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
static uint32_t membership(uint32_t global,uint32_t entity)
{
    uint32_t head=word(global),node=word(head),n=0,used=0;
    while(node!=head){if(used++==65536)exit(8);if(word(node+8)==entity)n++;node=word(node);}
    return n;
}
#include "reference_motion_io.h"
#include "reference_player_io.h"
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
    player_state();
    emit(object+16,1);oracle_put(word(word(word(0x55c694)+entity*8u))==word(0x544f98));
    oracle_put(membership(0x55b6a8,entity));oracle_put(membership(0x54b488,entity));oracle_put(membership(0x54b48c,entity));
    narrow(object+0xdf0,1);
    if(variant){
        unsigned char bytes[4];uint32_t at[3]={0x545ffc,0x58e7ac,0x59054c};
        for(i=0;i<3;i++){oracle_set(bytes,saved_environment[i],4);if(!write(ctx,at[i],bytes,4))return -1;}
    }
    return 0;
}
int main(int argc,char **argv)
{
    int status;if(argc!=7)return 2;variant=(uint32_t)strtoul(argv[6],0,10);if(variant>3)return 2;
    oracle_binary();fwrite("VS05",1,4,stdout);
    status=reference_game_start_cup(argv[1],(uint32_t)strtoul(argv[2],0,10),(uint32_t)strtoul(argv[3],0,10),
        (uint32_t)strtoul(argv[4],0,10),(uint32_t)strtoul(argv[5],0,10),0);
    if(status)return status;
    oracle_put(0);oracle_put(captured);tank_destroy();return 0;
}
