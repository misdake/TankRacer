/* Unchanged auxiliary-brake and restoration kernels, semantic inputs. */
#include "frame_player_state.h"
#include "frame_player_input.h"
#include "reference_io.h"
static unsigned char ram[0x800000];
static int read_data(void *c,uint32_t a,void *out,uint32_t n)
{(void)c;if(a>sizeof(ram)||n>sizeof(ram)-a)return 0;memcpy(out,ram+a,n);return 1;}
static int write_data(void *c,uint32_t a,const void *in,uint32_t n)
{(void)c;if(a>sizeof(ram)||n>sizeof(ram)-a)return 0;memcpy(ram+a,in,n);return 1;}
static void set(uint32_t a,uint32_t v,unsigned n){oracle_set(ram+a,v,n);}
static void load(uint32_t a,unsigned n){set(a,oracle_require(),n);}
static void emit(uint32_t a,unsigned n,int sign)
{uint32_t v=oracle_get(ram+a,n);oracle_put(sign&&n==2&&v>32767?v|0xffff0000u:v);}
int main(void)
{
    const uint32_t o=0x10000;uint32_t first,down,restore;int status;
    oracle_binary();fwrite("PB01",1,4,stdout);
    while(oracle_word(&first)){
        memset(ram,0,sizeof(ram));set(o+0x94,first,4);load(o+0x98,4);load(o+0x9c,4);
        load(o+0x10bc,1);load(o+0x10b6,2);load(o+0x10b8,2);load(o+0x10ba,2);load(o+0x10bd,1);
        load(o+0x66a,4);load(o+0x10be,4);load(0x5904fc,4);load(o+0xec,4);load(0x52ee9c,4);load(o+0xdd0,2);
        down=oracle_require();load(0x546020,2);load(o+0x6c2,2);restore=oracle_require();
        status=restore?frame_player_restore_overrides(read_data,write_data,0,o):frame_player_aux_brake(read_data,write_data,0,o,down);
        oracle_put((uint32_t)status);emit(o+0x94,4,0);emit(o+0x98,4,0);emit(o+0x9c,4,0);
        emit(o+0x10bc,1,0);emit(o+0x10b6,2,1);emit(o+0x10b8,2,1);emit(o+0x10ba,2,1);emit(o+0x10bd,1,0);
        emit(o+0x66a,4,0);emit(o+0x10be,4,0);
    }
    return ferror(stdin)?3:0;
}
