/* Original Z/X input with external keys and explicit state, no render code. */
#include "reference_io.h"
#include "frame_turret.h"
static unsigned char ram[0x74c000];
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n){(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *in,uint32_t n){(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(ram+at,in,n);return 1;}
static void set(uint32_t at,uint32_t value,unsigned n){oracle_set(ram+at,value,n);}
static void emit(uint32_t at,unsigned n){uint32_t v=oracle_get(ram+at,n);oracle_put(n==2&&v>32767?v|0xffff0000u:v);}
int main(void)
{
    uint32_t count,i;const uint32_t o=0x1000;int status;oracle_binary();
    set(0x530c6c,22,4);set(0x531ba0,0x07010001,4);set(0x592028,10,4);set(0x59202c,11,4);
    count=oracle_require();
    for(i=0;i<count;i++){
        set(o+0x762,oracle_require(),2);set(o+0xe0f,oracle_require(),4);set(0x544fa0,oracle_require(),4);set(0x544fa8,oracle_require(),1);
        set(o+0xe0b,oracle_require(),2);set(0x5904fc,oracle_require(),4);set(0x74b60a,oracle_require(),1);set(0x74b60b,oracle_require(),1);
        status=frame_turret_input(read_data,write_data,0,o);if(status)return 8;
        emit(o+0x762,2);emit(o+0xe0f,4);emit(0x544fa0,4);emit(0x544fa8,1);emit(o+0xd0a,1);emit(o+0xe0b,2);emit(0x544fac,1);emit(0x544fb0,1);
    }
    return 0;
}
