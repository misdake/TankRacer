#include "physics_effects.h"
#include "frame_scene_effects.h"
#include "physics_memory.h"

static void vector_read(physics_memory *m,uint32_t a,int32_t *v,unsigned n)
{ unsigned i;for (i=0;i<n;++i) v[i]=pm_i32(m,a+i*4u); }
static void transform_column(int32_t v[3],const int32_t matrix[9])
{
    int32_t t[3];unsigned i,j;
    for (i=0;i<3;++i) { int32_t sum=0;for (j=0;j<3;++j) sum=pm_add(sum,pm_mul(v[j],matrix[j*3u+i]));t[i]=pm_sar(sum,12); }
    for (i=0;i<3;++i) v[i]=t[i];
}
static uint32_t random_next(physics_memory *m)
{
    uint32_t value=pm_load(m,0x55b6ac,4)*UINT32_C(0x19660d)+UINT32_C(0x3c6ef35f);
    pm_store(m,0x55b6ac,value,4);return value;
}
static void pool_free(physics_memory *m,uint32_t handle)
{
    if (!m->error) m->error=frame_line_release(m->read,m->write,m->ctx,handle);
}
static int32_t pool_allocate(physics_memory *m,const int32_t endpoints[6],const unsigned char colors[8])
{
    uint32_t slot=0;
    if (!m->error) m->error=frame_line_allocate_values(m->read,m->write,m->ctx,endpoints,colors,&slot);
    return pm_signed(slot);
}
static void particle_endpoint(int32_t magnitude,uint32_t yaw,uint32_t pitch,
    const int32_t origin[3],const int32_t offset[3],const physics_trig *trig,int32_t out[3])
{
    int32_t v[3]={0,0,magnitude},sine,cosine,y,z;unsigned index,i;
    physics_vector_yaw(v,v,yaw,trig);
    index=(pitch&65532u)>>2;sine=trig->sine[index];cosine=trig->cosine[index];y=v[1];z=v[2];
    v[1]=pm_trunc(pm_add(pm_mul(y,cosine),pm_mul(z,sine)),12);
    v[2]=pm_trunc(pm_sub(pm_mul(z,cosine),pm_mul(y,sine)),12);
    for (i=0;i<3;++i) out[i]=pm_add(pm_add(v[i],origin[i]),offset[i]);
}
int physics_impact_particles(physics_world_read read,physics_world_write write,void *ctx,
    uint32_t object,uint32_t record,int32_t kind,const physics_trig *trig)
{
    physics_memory memory={read,write,ctx,0},*m=&memory;
    uint32_t ring=pm_load(m,0x545e1c,4),corner=pm_load(m,record+0x28,4),base,origin_address;
    int32_t spread=0,angle=0,offset[3]={0},origin[3],direction_x,direction_z;unsigned i,j;int status;
    if (m->error) return m->error;
    if (kind!=3 && kind!=4 && pm_load(m,object+0x1082,4)<500u) return m->error;
    if (ring>=4u || kind<0 || kind>6) return -4;
    base=UINT32_C(0x58f400)+ring*404u;origin_address=object+0x128+corner*70u;
    if (pm_load(m,base+0x188,4)) for (i=0;i<10;++i) {
        uint32_t handle=pm_load(m,base+0x1c+i*38u,4);
        if (handle!=UINT32_C(0xfe5cbe12)) pool_free(m,handle);
    }
    if (kind==0) spread=65520;
    else {
        unsigned first=(kind==1 || kind==3 || kind==5) ? 0u : 2u;
        uint32_t a=object+0x128+first*70u,b=a+70u;
        direction_x=pm_sub(pm_i32(m,b),pm_i32(m,a));direction_z=pm_sub(pm_i32(m,b+8),pm_i32(m,a+8));
        if (pm_i32(m,object+0xec)<0) { direction_x=pm_neg(direction_x);direction_z=pm_neg(direction_z); }
        status=physics_angle_lookup(direction_x,direction_z,trig,&angle);if (status) return status;
        angle=pm_neg(angle);
        if (kind==3 || kind==4) pm_write32(m,object+0x1082,0);
        if (kind==1 || kind==2 || kind==5 || kind==6) {
            uint32_t matrix_address=pm_load(m,object+0x14,4);
            if (matrix_address) {
                int32_t matrix[9];vector_read(m,pm_i32(m,object+0xec)<0 ? UINT32_C(0x531878) : UINT32_C(0x531868),offset,3);
                vector_read(m,matrix_address,matrix,9);transform_column(offset,matrix);
            }
        }
        spread=(kind==5 || kind==6) ? 3640+5460 : 5460;
    }
    if (m->error) return m->error;
    vector_read(m,origin_address,origin,3);
    for (i=0;i<10;++i) {
        uint32_t p=base+i*38u,r0=(random_next(m)>>4)&65535u,r1=(random_next(m)>>4)&65535u;
        int32_t magnitude=(int32_t)(r0%55u+10u)*256;
        int32_t pitch=(int32_t)(r0%(uint32_t)spread)-pm_sar(spread,1);
        int32_t yaw=pm_sub(pm_add((int32_t)(r1%(uint32_t)spread),angle),pm_sar(spread,1));
        unsigned char colors[8]={0,0,0,1,255,(unsigned char)(r0%126u-126u),0,0};
        int32_t endpoints[6],handle;
        pm_store(m,p,1000,2);pm_store(m,p+2,1800,2);pm_store(m,p+0x23,255,1);
        pm_write32(m,p+8,magnitude);pm_write32(m,p+4,magnitude);
        pm_write32(m,p+12,pitch);pm_write32(m,p+20,pitch);pm_write32(m,p+16,yaw);pm_write32(m,p+24,yaw);
        pm_store(m,p+0x24,colors[5],1);pm_store(m,p+0x21,0,1);pm_store(m,p+0x25,0,1);pm_store(m,p+0x20,0,1);pm_store(m,p+0x22,0,1);
        /* SHR DL,9 is always zero: only an eight-bit operand is shifted. */
        for (j=0;j<2;++j) particle_endpoint(j ? 1800 : 1000,(uint32_t)yaw,(uint32_t)pitch,origin,offset,trig,endpoints+j*3u);
        handle=pool_allocate(m,endpoints,colors);pm_write32(m,p+28,handle);
        if (m->error) return m->error;
    }
    pm_store(m,base+0x184,object,4);pm_store(m,base+0x188,1,4);pm_store(m,base+0x180,pm_load(m,record+0x28,4),4);
    pm_store(m,base+0x190,pm_load(m,0x590564,4),4);pm_store(m,base+0x18c,0,4);pm_write32(m,base+0x17c,kind);
    pm_store(m,0x545e1c,ring+1u,4);
    if (ring==3u) pm_store(m,0x545e1c,0,4);
    return m->error;
}
int physics_impact_particles_callback(void *ctx,uint32_t object,uint32_t record,int32_t kind)
{
    physics_effect_environment *e=(physics_effect_environment *)ctx;
    if (!e) return -5;
    return physics_impact_particles(e->read,e->write,e->ctx,object,record,kind,e->trig);
}
int physics_impact_particles_tick(physics_world_read read,physics_world_write write,void *ctx,uint32_t object,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t ring,base,p,i,j,kind,at,matrix;int32_t dt,origin[3],offset[3],points[6],age;unsigned char colors[8];
    if (!read || !write || !trig) return -1;
    dt=pm_i32(&m,0x5904fcu);pm_write32(&m,object+0x1082u,pm_add(pm_i32(&m,object+0x1082u),dt));
    for (ring=0;ring<4u;ring++) {
        base=0x58f400u+ring*404u;
        if (pm_load(&m,base+0x184u,4)!=object || !pm_load(&m,base+0x188u,4)) continue;
        at=object+0x128u+pm_load(&m,base+0x180u,4)*70u;
        if (pm_load(&m,base+0x190u,4)==pm_load(&m,0x590564u,4)) return m.error;
        for (i=0;i<10u;i++) {
            p=base+i*38u;
            pm_store(&m,p+2u,pm_load(&m,p+2u,2)+(uint32_t)pm_sar(pm_mul(pm_i32(&m,p+8u),dt),8),2);
            pm_store(&m,p,pm_load(&m,p,2)+(uint32_t)pm_sar(pm_mul(pm_i32(&m,p+4u),dt),8),2);
            vector_read(&m,at,origin,3);offset[0]=offset[1]=offset[2]=0;
            if (m.error) return m.error;
            particle_endpoint(pm_i16(&m,p),pm_load(&m,p+16u,4),pm_load(&m,p+12u,4),origin,offset,trig,points);
            particle_endpoint(pm_i16(&m,p+2u),pm_load(&m,p+24u,4),pm_load(&m,p+20u,4),origin,offset,trig,points+3u);
            for (j=0;j<3u;j++) { colors[j]=(unsigned char)pm_load(&m,p+32u+j,1);colors[j+4u]=(unsigned char)pm_load(&m,p+35u+j,1); }colors[3]=1;colors[7]=0;
            kind=pm_load(&m,base+0x17cu,4);
            if (kind==1u || kind==2u || kind==5u || kind==6u) {
                matrix=pm_load(&m,object+0x14u,4);if (m.error) return m.error;
                if (!matrix) return -4;
                { int32_t transform[9];vector_read(&m,pm_i32(&m,object+0xecu)<0 ? 0x531878u : 0x531868u,offset,3);vector_read(&m,matrix,transform,9);transform_column(offset,transform); }
                for (j=0;j<6u;j++) points[j]=pm_add(points[j],offset[j%3u]);
            }
            if (m.error) return m.error;
            matrix=pm_load(&m,p+28u,4);if (m.error) return m.error;
            m.error=frame_line_update_values(read,write,ctx,matrix,points,colors);if (m.error) return m.error;
        }
        age=pm_add(pm_i32(&m,base+0x18cu),pm_i32(&m,0x5904fcu));pm_write32(&m,base+0x18cu,age);
        if (age>60) {
            for (i=0;i<10u;i++) {
                p=base+28u+i*38u;matrix=pm_load(&m,p,4);
                if (matrix!=0xfe5cbe12u) { pool_free(&m,matrix);pm_store(&m,p,0xfe5cbe12u,4); }
            }
            pm_store(&m,base+0x188u,0,4);pm_store(&m,base+0x18cu,0,4);
        }
    }
    return m.error;
}
