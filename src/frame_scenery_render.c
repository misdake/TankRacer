#include "frame_scenery_render.h"
#include "frame_math.h"
#include "physics_memory.h"
#define TRY(x) do { int rc;if (m->error) return m->error;rc=(x);if (rc) return rc; } while (0)
static int draw_mesh(physics_memory *m,const frame_scenery_render_services *s,uint32_t shape,uint32_t at,uint32_t matrix,int32_t bias,uint32_t style)
{
    uint32_t i;int32_t pos[3],mat[12];
    for (i=0;i<3u;i++) pos[i]=pm_i32(m,at+i*4u);
    for (i=0;i<12u;i++) mat[i]=matrix ? pm_i32(m,matrix+i*4u) : (i<9u && i%4u==0u ? 4096 : 0);
    if (m->error) return m->error;
    return shape ? s->mesh(s->ctx,shape,pos,mat,bias,style) : 0;
}
static void transformed(physics_memory *m,uint32_t o,uint32_t mat,const int32_t local[3],int32_t out[3])
{
    uint32_t i,j;for (i=0;i<3u;i++) { int32_t n=0;for (j=0;j<3u;j++) n=pm_add(n,pm_mul(local[j],pm_i32(m,mat+(j*3u+i)*4u)));out[i]=pm_add(pm_i32(m,o+i*4u),pm_sar(n,12)); }
}
static int positioned_mesh(physics_memory *m,const frame_scenery_render_services *s,uint32_t shape,const int32_t pos[3],uint32_t matrix,int32_t bias,uint32_t style)
{
    int32_t mat[12];uint32_t i;for (i=0;i<12u;i++) mat[i]=pm_i32(m,matrix+i*4u);if (m->error) return m->error;return shape ? s->mesh(s->ctx,shape,pos,mat,bias,style) : 0;
}
static int fragments(physics_memory *m,uint32_t o,const frame_scenery_render_services *s,uint32_t data)
{
    uint32_t i,j,count=pm_load(m,data+20u,2),base=pm_load(m,data+12u,4),sprite=pm_load(m,data+48u,4);
    if (count>1024u) return -3;
    for (i=0;i<count;i++) {
        uint32_t at=base+i*104u;if (pm_load(m,at+84u,1)&2u) continue;
        if (pm_load(m,data+24u,4)&128u) {
            for (j=0;j<3u;j++) pm_store(m,sprite+4u+j*4u,pm_load(m,at+12u+j*4u,4),4);
            pm_store(m,sprite+40u,pm_load(m,at+100u,2),2);TRY(s->sprite(s->ctx,sprite));
        } else TRY(draw_mesh(m,s,pm_load(m,at+24u,4),at+12u,at+28u,0,0));
    }
    (void)o;return m->error;
}
int frame_scenery_render(frame_data_read read,frame_data_write write,void *ctx,uint32_t entry,uint32_t o,const frame_scenery_render_services *s)
{
    physics_memory memory={read,write,ctx,0},*m=&memory;uint32_t i,j,callback,shape,mat,data;int32_t bias,local[3],pos[3];
    if (!read || !write || !s || !s->mesh || !s->sprite) return -1;
    bias=pm_i16(m,o+0x4eu);mat=pm_load(m,o+20u,4);shape=pm_load(m,o+72u,4);
    if (entry==0x40b550u) {
        callback=pm_load(m,o+0x340u,4);if (callback!=0x40ba10u && callback!=0x40ba60u) return m->error ? m->error : -5;
        for (i=0;i<8u;i++) { uint32_t at=o+0x7cu+i*80u;TRY(draw_mesh(m,s,pm_load(m,o+(callback==0x40ba10u && i<4u ? 0x324u : 72u),4),at,at+32u,0,0)); }
        return m->error;
    }
    if (entry==0x43e030u) {
        callback=pm_load(m,o+0x88u,4);if (!pm_load(m,o+0x7cu,1)) return m->error;
        if (callback!=0x43f390u && callback!=0x43f3b0u && callback!=0x43f460u) return m->error ? m->error : -5;
        TRY(draw_mesh(m,s,shape,o,callback==0x43f460u ? o+24u : mat,bias,0));
        if (callback==0x43f3b0u) {
            for (i=0;i<3u;i++) local[i]=pm_i32(m,o+0x128u+i*4u);
            transformed(m,o,mat,local,pos);
            TRY(positioned_mesh(m,s,pm_load(m,o+0xbcu,4),pos,o+0xf8u,bias,1));
        } else if (callback==0x43f460u) {
            int32_t angle=pm_i32(m,o+0xb8u),radius=pm_i32(m,o+0xb4u);const physics_trig *trig=frame_math_trig();
            TRY(draw_mesh(m,s,pm_load(m,o+0xc0u,4),o,o+0xc4u,pm_sub(bias,20),0));
            for (i=0;i<4u;i++) {
                uint32_t index=((uint32_t)angle&65532u)>>2;local[0]=pm_sar(pm_mul(radius,trig->sine[index]),12);local[1]=pm_sar(pm_mul(radius,trig->cosine[index]),12);local[2]=0;transformed(m,o,mat,local,pos);
                TRY(positioned_mesh(m,s,pm_load(m,o+0xbcu,4),pos,o+0xc4u,pm_add(bias,20),0));angle=pm_add(angle,16384);if (angle>65535) angle=pm_sub(angle,65536);
            }
        }
        return m->error;
    }
    if (entry==0x407620u) {
        TRY(draw_mesh(m,s,shape,o,mat,bias,pm_load(m,o+0xcbu,1) ? 2u : 0u));data=pm_load(m,o+0xd5u,4);
        if (!data) return m->error;
        if (pm_load(m,o+0xcdu,4)>10000u) return -3;
        for (i=0;i<pm_load(m,o+0xcdu,4);i++) {
            uint32_t at=data+i*32u,kind=pm_load(m,at+24u,1);if (kind!=1u && kind!=2u) continue;
            for (j=0;j<3u;j++) { pm_store(m,0x53ccecu+j*4u,pm_load(m,at+12u+j*4u,4),4);pm_store(m,0x53cd0cu+j,pm_load(m,at+25u+j,1),1); }
            if (kind==2u) { pm_store(m,0x53cd00u,3000,4);pm_store(m,0x53cd04u,3000,4); }
            TRY(s->sprite(s->ctx,0x53cce8u));
            if (kind==2u) { pm_store(m,0x53cd00u,1024,4);pm_store(m,0x53cd04u,1024,4); }
        }
        return m->error;
    }
    if (entry==0x416080u) {
        data=pm_load(m,o+0x8cu,4);if (data && (pm_load(m,data+24u,4)&1024u)) return fragments(m,o,s,data);
        if (!(pm_load(m,o+0xf2u,4)&256u)) return m->error;
        TRY(draw_mesh(m,s,shape,o,mat,bias,0));local[0]=local[2]=!pm_load(m,0x53e96cu,4) && (pm_load(m,0x59055cu,1)&32u) ? 500 : -500;local[1]=1500;transformed(m,o,mat,local,pos);
        for (i=0;i<3u;i++) pm_write32(m,0x53e9c4u+i*4u,pos[i]);TRY(s->sprite(s->ctx,0x53e9c0u));return m->error;
    }
    return -5;
}
#undef TRY
