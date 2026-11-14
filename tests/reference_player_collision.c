/* Unchanged Player indication kernel and its exact dominant-axis estimate. */
#include "frame_player_collision.h"
#include "reference_io.h"
static unsigned char ram[0x800000];
static int read_data(void *c,uint32_t a,void *out,uint32_t n)
{(void)c;if(a>sizeof(ram)||n>sizeof(ram)-a)return 0;memcpy(out,ram+a,n);return 1;}
static int write_data(void *c,uint32_t a,const void *in,uint32_t n)
{(void)c;if(a>sizeof(ram)||n>sizeof(ram)-a)return 0;memcpy(ram+a,in,n);return 1;}
static void set(uint32_t a,uint32_t v){oracle_set(ram+a,v,4);}
static uint32_t get(uint32_t a){return oracle_get(ram+a,4);}
int main(void)
{
    const uint32_t o=0x10000;uint32_t active,i;int status;int32_t v[3];
    oracle_binary();fwrite("PC01",1,4,stdout);
    while(oracle_word(&active)){
        memset(ram,0,sizeof(ram));set(0x53e3fc,active);set(0x5904fc,oracle_require());
        set(o+0x107e,oracle_require());set(o+0x118,oracle_require());
        for(i=0;i<6;i++){set(o+0xfc+i*4,oracle_require());}
        set(o+0x114,oracle_require());
        status=frame_player_collision_state(read_data,write_data,0,o);
        oracle_put((uint32_t)status);oracle_put(get(o+0x107e));oracle_put(get(o+0x118));
        for(i=0;i<7;i++)oracle_put(get(o+0xfc+i*4));
        for(i=0;i<3;i++){v[i]=(int32_t)get(o+0xfc+i*4);}
        oracle_put((uint32_t)frame_collision_change_magnitude(v));
        for(i=0;i<3;i++){v[i]=(int32_t)get(o+0x108+i*4);}
        oracle_put((uint32_t)frame_collision_change_magnitude(v));
    }
    return ferror(stdin)?3:0;
}
