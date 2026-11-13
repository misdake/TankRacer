/* Production light/mark kernels and shared quad/line operations. */
#include "reference_io.h"
#include "frame_vehicle_scene.h"
#include "frame_scene_effects.h"
static unsigned char ram[0x800000];
enum { OBJECT=0x2000, SOURCE=0x7000 };
static int loading;
static void set(uint32_t at,uint32_t value,unsigned n){oracle_set(ram+at,value,n);}
static uint32_t get(uint32_t at,unsigned n){return oracle_get(ram+at,n);}
static void field(uint32_t at,unsigned n){if(loading)set(at,oracle_require(),n);else oracle_put(get(at,n));}
static void words(uint32_t at,unsigned n){unsigned i;for(i=0;i<n;i++)field(at+i*4u,4);}
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *in,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(ram+at,in,n);return 1;}
static void snapshot(void)
{
    unsigned i;
    field(OBJECT+0xe17,1);field(OBJECT+0xe18,1);field(OBJECT+0xe19,1);words(OBJECT+0xe1a,2);
    field(OBJECT+0xc54,1);field(OBJECT+0xc55,1);words(OBJECT+0xc56,12);words(OBJECT+0xc86,3);
    for(i=0;i<68;i++)field(OBJECT+0xaac+i*6u,4);
    field(OBJECT+0xc44,4);field(OBJECT+0xc4c,4);field(OBJECT+0xc50,4);
    field(0x55ae20,4);field(0x55ae1c,4);words(0x54cc48,16);words(0x54cc88,2560);words(0x54f488,2);words(0x54f490,800);words(0x550110,8);
}
static void light_input(void)
{
    unsigned i;
    set(OBJECT+0xde5,oracle_require(),1);set(OBJECT+0xded,oracle_require(),1);set(0x54b4b0,oracle_require(),4);
    set(OBJECT+0xd90,oracle_require(),1);set(OBJECT+16,oracle_require()?32:0,4);set(OBJECT+0x10bd,oracle_require(),1);
    set(OBJECT+0x94,oracle_require(),4);set(OBJECT+0x98,oracle_require(),4);set(OBJECT+0xec,oracle_require(),4);
    for(i=0;i<3;i++)set(OBJECT+0x78+i*4u,oracle_require(),4);
    set(OBJECT+20,oracle_require()?0x8000:0,4);for(i=0;i<9;i++)set(0x8000+i*4u,oracle_require(),4);
    for(i=0;i<2;i++)set(OBJECT+0x716+i*4u,oracle_require(),4);
    for(i=0;i<4;i++)set(OBJECT+0x140+i*70u,oracle_require(),4);
    for(i=0;i<3;i++)set(OBJECT+0x656+i*4u,oracle_require(),4);
    set(0x5468a8,oracle_require(),4);
}
static void mark_input(void)
{
    unsigned i,j;
    set(OBJECT+0xde5,oracle_require(),1);set(OBJECT+0xd8c,oracle_require(),4);set(0x52ee9c,oracle_require(),4);
    set(0x5904fc,oracle_require(),4);set(OBJECT+0xc0,oracle_require(),4);set(OBJECT+0xa0,oracle_require(),4);
    for(i=0;i<3;i++)set(OBJECT+0x50+i*4u,oracle_require(),4);
    for(i=0;i<2;i++)set(OBJECT+0x716+i*4u,oracle_require(),4);
    for(i=0;i<4;i++)set(OBJECT+0x148+i*70u,oracle_require(),4);
    for(i=0;i<4;i++)set(OBJECT+0x134+i*70u,oracle_require(),2);
    for(i=0;i<4;i++)for(j=0;j<3;j++)set(OBJECT+0x128+i*70u+j*4u,oracle_require(),4);
    for(i=0;i<2;i++)for(j=0;j<9;j++)set(OBJECT+0x3ea+i*104u+j*4u,oracle_require(),4);
}
int main(void)
{
    uint32_t c,cases,n,k,op,handle,result,i,material;int32_t points[12];unsigned char style[12];int status;
    oracle_binary();cases=oracle_require();fwrite("VQ01",1,4,stdout);oracle_put(cases);
    for(c=0;c<cases;c++){
        memset(ram,0,sizeof(ram));loading=1;snapshot();loading=0;
        n=oracle_require();oracle_put(n);
        for(k=0;k<n;k++){
            op=oracle_require();handle=oracle_require();result=0;
            if(op==0){light_input();status=frame_vehicle_lights_tick(read_data,write_data,0,OBJECT);}
            else if(op==1){mark_input();status=frame_vehicle_track_marks(read_data,write_data,0,OBJECT);}
            else if(op==8)status=frame_scene_pools_initialize(write_data,0);
            else {
                for(i=0;i<12;i++)points[i]=oracle_signed(oracle_require());
                material=oracle_require();for(i=0;i<12;i++)style[i]=(unsigned char)oracle_require();
                for(i=0;i<12;i++)set(SOURCE+i*4u,(uint32_t)points[i],4);
                set(SOURCE+48,material,4);memcpy(ram+SOURCE+52,style,12);
                if(op==2)status=frame_light_allocate_values(read_data,write_data,0,points,material,style,&result);
                else if(op==3)status=frame_light_update_values(read_data,write_data,0,handle,points);
                else if(op==4)status=frame_light_replace(read_data,write_data,0,handle,SOURCE);
                else if(op==5)status=frame_light_release(read_data,write_data,0,handle);
                else if(op==6)status=frame_line_update_values(read_data,write_data,0,handle,points,style);
                else if(op==7)status=frame_line_release(read_data,write_data,0,handle);
                else if(op==9)status=frame_light_allocate(read_data,write_data,0,SOURCE,&result);
                else if(op==10)status=frame_line_allocate_values(read_data,write_data,0,points,style,&result);
                else return 2;
            }
            oracle_put((uint32_t)status);oracle_put(result);snapshot();
        }
    }
    return 0;
}
