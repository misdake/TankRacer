/* Typed Chasecam oracle; no original process/dump is required. */
#include "frame_camera.h"
#include "frame_math.h"
#include "reference_io.h"
static unsigned char ram[0x800000];
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *in,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(ram+at,in,n);return 1;}
static void put(uint32_t at,uint32_t v,unsigned n){oracle_set(ram+at,v,n);}
static void input(uint32_t at,unsigned count){unsigned i;for(i=0;i<count;i++)put(at+i*4,oracle_require(),4);}
static void output(uint32_t at,unsigned count){unsigned i;for(i=0;i<count;i++)oracle_put(oracle_get(ram+at+i*4,4));}
int main(void)
{
    uint32_t op,i;const uint32_t camera=0x10000,player=0x12000,table=0x14000,record=0x14100;
    const uint32_t config[]={0x53cc40,0x52e3a8,0x52e3ac,0x53cc44,0x52e3b4,0x52e3b8,0x52e3b0,0x52e3bc,
        0x53cc50,0x52e3d8,0x52e3dc,0x52e3e0,0x52e3e8,0x52e3ec,0x52e3f0,0x592e6c,0x592e70,0x592e78,0x592e68,0x592e74};
    frame_entity_factory_services services={0};uint32_t tail[3]={0};services.work_limit=100000;
    oracle_binary();fwrite("CC01",1,4,stdout);
    while(oracle_word(&op)) {
        if(op==0) {
            memset(ram,0,sizeof(ram));put(0x55c694,table,4);put(table+8,record,4);put(table+16,record+8,4);
            put(record+4,camera,4);put(record+12,player,4);put(camera+20,camera+24,4);
            put(0x592038,1,4);put(0x592034,2,4);
            for(i=0;i<sizeof(config)/sizeof(config[0]);i++)put(config[i],oracle_require(),4);
            input(camera,3);input(camera+24,9);input(camera+0x98,1);input(camera+0x9c,3);input(camera+0xa8,1);
            input(camera+0xb8,3);input(camera+0xdc,3);input(camera+0x78,3);
            put(camera+0x97,oracle_require(),1);put(camera+0x96,oracle_require(),1);
            input(camera+0x84,3);put(camera+0x90,oracle_require(),2);put(camera+0x92,oracle_require(),2);put(camera+0x95,oracle_require(),1);
        } else if(op==1) {
            input(player,3);input(player+0x78,3);input(player+24,9);
            put(0x5904fc,oracle_require(),4);put(0x74b601,oracle_require(),1);put(0x74b602,oracle_require(),1);
            put(0x53e924,oracle_require(),4);put(0x53e40c,oracle_require(),4);put(player+0xdcb,oracle_require(),1);put(0x53e928,oracle_require(),4);
            oracle_put((uint32_t)frame_camera_chase_tick(read_data,write_data,0,1,2,&services,frame_math_trig(),tail));
            output(camera,3);output(0x74af00,9);output(camera+0x98,1);output(camera+0x9c,3);output(camera+0xa8,1);
            output(camera+0xb8,3);output(camera+0xdc,3);output(camera+0x78,3);
            oracle_put(oracle_get(ram+camera+0x97,1));oracle_put(oracle_get(ram+camera+0x96,1));
            output(camera+0x84,3);oracle_put((uint32_t)(int32_t)(int16_t)oracle_get(ram+camera+0x90,2));
            oracle_put((uint32_t)(int32_t)(int16_t)oracle_get(ram+camera+0x92,2));oracle_put(oracle_get(ram+camera+0x95,1));
        } else if(op==2) {
            for(i=0;i<3;i++){uint32_t v=oracle_require();if(oracle_get(ram+camera+0x98,4)==2)put(camera+0x78+i*4,v,4);}
        } else if(op==3) {
            uint32_t duration=oracle_require(),amplitude=oracle_require();
            if(frame_camera_shake(read_data,write_data,0,1,duration,amplitude))return 3;
        } else return 2;
    }return 0;
}
