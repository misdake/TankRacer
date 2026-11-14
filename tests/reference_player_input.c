/* Unchanged CUP input kernel; normalized axes and action bindings are devices. */
#include "frame_player_input.h"
#include "reference_io.h"
static unsigned char ram[0x800000];
static uint32_t trace[64],used,recording,fault,previous;
static int read_data(void *ctx,uint32_t a,void *out,uint32_t n)
{
    (void)ctx;
    if (recording && n==1 && a>=0x74b600 && a<0x74b620) trace[used++]=a-0x74b600;
    if (recording && n==4 && ((a==0x53cc80 && previous==0x52e4c8) || (a==0x53cc8c && previous==0x52e4d4))) {
        uint32_t axis=a==0x53cc80 ? 0 : 1;
        trace[used++]=100+axis;
        if (fault==axis) return 0;
    }
    previous=a;
    if (a>sizeof(ram) || n>sizeof(ram)-a) return 0;
    memcpy(out,ram+a,n);return 1;
}
static int write_data(void *ctx,uint32_t a,const void *in,uint32_t n)
{
    (void)ctx;if (a>sizeof(ram) || n>sizeof(ram)-a) return 0;
    memcpy(ram+a,in,n);return 1;
}
static void set(uint32_t a,uint32_t v,unsigned n){oracle_set(ram+a,v,n);}
static void load(uint32_t a,unsigned n){set(a,oracle_require(),n);}
static void emit(uint32_t a,unsigned n){oracle_put(oracle_get(ram+a,n));}
int main(void)
{
    const uint32_t o=0x10000;uint32_t first,i,keys,driving;
    frame_player_input_rates rates;frame_player_input_result result;int status;
    oracle_binary();fwrite("PI01",1,4,stdout);
    while (oracle_word(&first)) {
        memset(ram,0,sizeof(ram));recording=0;used=0;
        set(o+0x90,first,4);load(o+0x94,4);load(o+0x98,4);load(o+0x9c,4);
        load(o+0x1106,1);load(o+0x1107,1);load(0x544fb8,4);
        load(0x5904fc,4);driving=oracle_require();set(o+0xdea,driving,1);
        load(o+0xdf0,1);load(o+0xec,4);load(0x5904e0,4);load(0x53cca8+(uint32_t)(int32_t)(int8_t)ram[o+0xdf0]*4u,4);
        load(0x53cce0,4);load(0x53e404,4);load(0x53cc84,4);load(0x53cc8c,4);
        load(0x530c54,4);load(0x530c58,4);load(0x530c5c,4);
        rates.throttle_return=(uint16_t)oracle_require();rates.brake_return=(uint16_t)oracle_require();load(0x530c68,4);
        keys=oracle_require();for(i=0;i<16;i++){set(0x592000+i*4,i,4);set(0x74b600+i,(keys>>i)&1u,1);}
        /* Axis normalization is identity within the original signed16 domain. */
        set(0x53ccdc,1,4);set(0x53cce4,0,4);set(0x52e51c,3,4);
        for(i=0;i<4;i++){set(0x52e4c8+i*4,0,4);set(0x52e4f8+i*4,32767,4);set(0x52e4e0+i*4,(uint32_t)-32768,4);}
        load(0x53cc80,4);
        /* Longitudinal index3 aliases twin-track right. Twin mode never queries
         * it, so retain the declared track value for that branch. */
        {uint32_t axis=oracle_require();if(!oracle_get(ram+0x53e404,4))set(0x53cc8c,axis,4);}
        fault=oracle_require();result=(frame_player_input_result){333,444};recording=1;
        status=frame_player_input_step(read_data,write_data,0,o,&rates,&result);recording=0;
        oracle_put((uint32_t)status);emit(o+0x90,4);emit(o+0x94,4);emit(o+0x98,4);emit(o+0x9c,4);
        emit(o+0x1106,1);emit(o+0x1107,1);emit(0x544fb8,4);
        oracle_put(result.driving);oracle_put(result.auxiliary_down);oracle_put(used);
        for(i=0;i<used;i++)oracle_put(trace[i]);
    }
    return ferror(stdin)?3:0;
}
