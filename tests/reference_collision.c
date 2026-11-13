#include "physics_collision.h"
#include "physics_vector.h"
#include "frame_math.h"
#include "reference_io.h"
static unsigned char ram[0x600000];
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *data,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(ram+at,data,n);return 1;}
static void vector(int32_t *v,unsigned n) {unsigned i;for(i=0;i<n;i++)v[i]=oracle_signed(oracle_require());}
static void load(uint32_t at,unsigned n) {unsigned i;for(i=0;i<n;i++)oracle_set(ram+at+i*4,oracle_require(),4);}
static void emit(uint32_t at,unsigned n) {unsigned i;for(i=0;i<n;i++)oracle_put(oracle_get(ram+at+i*4,4));}
static int scale(physics_collision_scale *s)
{uint32_t enabled=oracle_require(),packed=oracle_require();s->multiplier=(uint16_t)packed;s->shift=(uint8_t)(packed>>16);return enabled!=0;}
int main(void)
{
    uint32_t op;oracle_binary();fwrite("CL01",1,4,stdout);
    while(oracle_word(&op)) {
        unsigned i;
        if(op==1) {
            physics_collision_scale local,final;physics_collision_response out;
            int32_t normal[3],velocity[3],mode,player;int a,b,status;
            vector(normal,3);vector(velocity,3);a=scale(&local);b=scale(&final);
            mode=oracle_signed(oracle_require());player=oracle_signed(oracle_require());
            for(i=0;i<3;i++){out.normal[i]=(int32_t)(0x7b000000u+i);out.velocity[i]=(int32_t)(0x7b000003u+i);}
            out.approach=0x7b000006;out.speed_units=0x7b000007;
            status=physics_collision_respond(normal,velocity,a?&local:0,b?&final:0,mode,player,frame_math_trig(),&out);
            oracle_put((uint32_t)status);for(i=0;i<3;i++)oracle_put((uint32_t)out.normal[i]);
            for(i=0;i<3;i++)oracle_put((uint32_t)out.velocity[i]);
            oracle_put((uint32_t)out.approach);oracle_put((uint32_t)out.speed_units);
        } else if(op==2) {
            int32_t normal[3],incoming[3],approach=0;int status;
            load(0x1018,9);load(0x10e4,3);load(0x1050,3);load(0x3050,3);vector(normal,3);vector(incoming,3);
            status=physics_tank_pair_response(read_data,write_data,0,0x1000,0x3000,normal,incoming,&approach);
            oracle_put((uint32_t)status);oracle_put((uint32_t)approach);emit(0x1050,3);emit(0x3050,3);
        } else if(op==3)oracle_put((uint32_t)physics_binary_one_point_seven(oracle_signed(oracle_require())));
        else if(op==4) {int32_t x=oracle_signed(oracle_require()),z=oracle_signed(oracle_require());oracle_put(physics_collision_direction(x,z));}
        else if(op==5) {load(0x1000,3);load(0x1050,3);load(0x3000,3);load(0x3050,3);load(0x5904fc,1);oracle_put((uint32_t)physics_tanks_approaching(read_data,0,0x1000,0x3000));}
        else return 2;
    }
    return ferror(stdin)?3:0;
}
