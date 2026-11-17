/* Preserved production helpers, no original process or dump. */
#include "frame_vehicle_render.h"
#include "frame_present.h"
#include "reference_io.h"
static unsigned char ram[0x800000];
static int rd(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1;}
static int wr(void *ctx,uint32_t at,const void *in,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(ram+at,in,n);return 1;}
static void put(uint32_t at,uint32_t v,unsigned n){oracle_set(ram+at,v,n);}
static void input(uint32_t at,unsigned n){unsigned i;for(i=0;i<n;i++)put(at+i*4,oracle_require(),4);}
static unsigned draws;
static int sprite(void *ctx,uint32_t at){(void)ctx;(void)at;return -99;}
static int mesh(void *ctx,uint32_t shape,const int32_t p[3],const int32_t m[12],uint32_t blend)
{unsigned i;(void)ctx;(void)shape;(void)blend;draws++;for(i=0;i<3;i++)oracle_put((uint32_t)p[i]);for(i=0;i<9;i++)oracle_put((uint32_t)m[i]);return 0;}
int main(void)
{
    uint32_t op;const uint32_t o=0x4000;oracle_binary();fwrite("VL01",1,4,stdout);
    while(oracle_word(&op)) {
        if(op==0) {
            uint32_t mode=0,part=oracle_require();memset(ram,0,sizeof(ram));
            put(0x5964d4,oracle_require()?32768:0,4);put(o+0xde5,oracle_require(),1);
            put(0x52ee9c,oracle_require(),4);put(0x53e924,oracle_require(),4);
            put(o+0xdaf,oracle_require(),4);put(o+0xde4,oracle_require(),1);
            if(frame_vehicle_self_layer(rd,0,o,part,&mode))return 2;
            oracle_put(mode);
        } else if(op==1) {
            int32_t a[9],b[9],out[9],n[3],uv[2];uint32_t i,mode=oracle_require();
            for(i=0;i<9;i++)a[i]=(int32_t)oracle_require();
            for(i=0;i<9;i++)b[i]=(int32_t)oracle_require();
            for(i=0;i<3;i++)n[i]=(int16_t)oracle_require();
            frame_present_layer_matrix(a,b,mode,out);frame_present_layer_uv(n,out,uv);
            for(i=0;i<9;i++)oracle_put((uint32_t)out[i]);
            for(i=0;i<2;i++)oracle_put((uint32_t)uv[i]);
        } else if(op==2) {
            frame_vehicle_render_services s={sprite,mesh,0};memset(ram,0,sizeof(ram));
            put(o+0x56e,oracle_require(),4);put(o+0xde5,oracle_require(),1);put(0x54b480,oracle_require(),4);
            input(o+0x572,3);input(o+0x30e,3);input(o+0x31a,9);input(o+0x2b2,9);
            put(o+0x774,oracle_require(),4);put(o+0x77c,oracle_require(),4);put(o+0x652,oracle_require(),4);
            draws=0;if(frame_vehicle_aerial_draw(rd,wr,0,o,&s))return 3;oracle_put(draws);
        } else return 4;
    }return 0;
}
