#include "physics_pose.h"

typedef struct memory { physics_world_read read;physics_world_write write;void *ctx;int error; } memory;
static int32_t s32(uint32_t x) { return x<=INT32_MAX ? (int32_t)x : (int32_t)((int64_t)x-INT64_C(4294967296)); }
static int32_t add(int32_t a,int32_t b) { return s32((uint32_t)a+(uint32_t)b); }
static int32_t sub(int32_t a,int32_t b) { return s32((uint32_t)a-(uint32_t)b); }
static int32_t mul(int32_t a,int32_t b) { return s32((uint32_t)a*(uint32_t)b); }
static int32_t sar(int32_t a,unsigned n) { uint32_t x=(uint32_t)a;n&=31u;return s32(a<0 ? ~(~x>>n) : x>>n); }
static int32_t trunc_shift(int32_t a,unsigned n) { return a<0 ? sub(0,sar(sub(0,a),n)) : sar(a,n); }
static uint32_t load(memory *m,uint32_t a,unsigned n)
{
    unsigned char b[4];uint32_t v=0;unsigned i;
    if (m->error) return 0;
    if (!m->read(m->ctx,a,b,n)) { m->error=-1;return 0; }
    for (i=0;i<n;++i) v|=(uint32_t)b[i]<<(i*8u);
    return v;
}
static int32_t i32(memory *m,uint32_t a) { return s32(load(m,a,4)); }
static int32_t i16(memory *m,uint32_t a) { uint32_t v=load(m,a,2);return v>32767u ? (int32_t)v-65536 : (int32_t)v; }
static void store(memory *m,uint32_t a,uint32_t v,unsigned n)
{
    unsigned char b[4];unsigned i;
    if (m->error) return;
    for (i=0;i<n;++i) b[i]=(unsigned char)(v>>(i*8u));
    if (!m->write || !m->write(m->ctx,a,b,n)) m->error=-1;
}
static void vector_read(memory *m,uint32_t a,int32_t *v,unsigned n)
{ unsigned i;for (i=0;i<n;++i) v[i]=i32(m,a+i*4u); }
static void vector_write(memory *m,uint32_t a,const int32_t *v,unsigned n)
{ unsigned i;for (i=0;i<n;++i) store(m,a+i*4u,(uint32_t)v[i],4); }
static void copy(memory *m,uint32_t dst,uint32_t src,unsigned words)
{ unsigned i;for (i=0;i<words;++i) store(m,dst+i*4u,load(m,src+i*4u,4),4); }
static void point(const int32_t *a,const int32_t *matrix,int32_t *out)
{
    unsigned i;int32_t temp[3];
    for (i=0;i<3;++i) temp[i]=sar(add(add(mul(matrix[i],a[0]),mul(matrix[3+i],a[1])),mul(matrix[6+i],a[2])),12);
    for (i=0;i<3;++i) out[i]=temp[i];
}
static void point_from(memory *m,uint32_t src,uint32_t matrix,uint32_t dst,const int32_t origin[3])
{
    int32_t v[3],mat[9],out[3];unsigned i;
    vector_read(m,src,v,3);vector_read(m,matrix,mat,9);point(v,mat,out);
    for (i=0;i<3;++i) out[i]=add(out[i],origin[i]);
    vector_write(m,dst,out,3);
}
int physics_pose_update(physics_world_read read,physics_world_write write,void *ctx,
                         uint32_t o,const physics_trig *t)
{
    memory m={read,write,ctx,0};int32_t origin[3],mat[9],v[3],out[3],angles[3],pivot[3];
    int32_t wobble=0,mode,shift,factor,travel_sum=0;uint32_t timer;unsigned i,side;
    copy(&m,o+0x2a6,o+0x78,3);copy(&m,o+0x18,o+0x2b2,9);
    timer=load(&m,o+0xdd0,2);
    if (timer) {
        wobble=sar(mul(t->sine[(timer&511u)*32u],3),7);
        store(&m,o+0x2aa,(uint32_t)add(i32(&m,o+0x2aa),wobble),4);
    }
    if (load(&m,0x545ffc,4) && !load(&m,0x58e7ac,4)) {
        wobble=sar(mul(t->sine[(load(&m,0x59054c,4)&511u)*32u],3),7);
        store(&m,o+0x2aa,(uint32_t)add(i32(&m,o+0x2aa),wobble),4);
    }
    vector_read(&m,o+0x2a6,origin,3);
    for (side=0;side<2;++side) {
        uint32_t part=o+0x3b6+side*104u;int32_t front,rear,difference,angle;
        shift=(int32_t)load(&m,o+0x700,2);
        front=trunc_shift(i32(&m,o+0x148+side*140u),(unsigned)shift);
        rear=trunc_shift(i32(&m,o+0x18eu+side*140u),(unsigned)shift);difference=sub(front,rear);
        if (physics_angle_lookup(difference<0 ? sub(0,difference) : difference,i32(&m,o+0x62e),t,&angle)<0) return -2;
        if (front<rear) angle=sub(0,angle);
        copy(&m,part+0x34,o+0x2b2,9);vector_read(&m,part+0x34,mat,9);vector_read(&m,part+0x1c,v,3);
        v[1]=sub(add(v[1],sar(add(front,rear),1)),wobble);point(v,mat,out);
        for (i=0;i<3;++i) out[i]=add(out[i],origin[i]);
        vector_write(&m,part+0x28,out,3);physics_matrix_rotate(mat,0,(uint32_t)angle,t);vector_write(&m,part+0x34,mat,9);
    }
    copy(&m,o+0x31a,o+0x2b2,9);mode=i32(&m,o+0xe13);
    shift=mode==3 ? 2 : mode==5 ? 0 : 1;factor=mode==3 ? 3 : 1;
    store(&m,o+0xdf6,(uint32_t)shift,1);store(&m,o+0xdf5,(uint32_t)factor,1);
    v[0]=0;v[1]=0;v[2]=mul(trunc_shift(i32(&m,o+0x764),(unsigned)shift),factor);
    {
        uint32_t angle=(uint32_t)i16(&m,o+0x762),index=(angle&65532u)>>2;
        int32_t x=v[0],z=v[2],sine=t->sine[index],cosine=t->cosine[index];
        v[0]=trunc_shift(sub(mul(x,cosine),mul(z,sine)),12);
        v[2]=trunc_shift(add(mul(x,sine),mul(z,cosine)),12);
    }
    {
        uint32_t external_matrix=load(&m,o+0x14,4);
        if (external_matrix) { vector_read(&m,external_matrix,mat,9);point(v,mat,v); }
    }
    point_from(&m,o+0x302,o+0x31a,o+0x30e,origin);
    for (i=0;i<3;++i) store(&m,o+0x30eu+i*4u,(uint32_t)add(i32(&m,o+0x30eu+i*4u),v[i]),4);
    vector_read(&m,o+0x31a,mat,9);physics_matrix_rotate(mat,1,(uint32_t)i16(&m,o+0x762),t);vector_write(&m,o+0x31a,mat,9);
    copy(&m,o+0x382,o+0x31a,9);vector_read(&m,o+0x36a,v,3);v[2]=add(v[2],i32(&m,o+0x764));
    vector_read(&m,0x531bd8,pivot,3);angles[0]=sub(0,i32(&m,o+0x76c));angles[1]=0;angles[2]=0;
    physics_pivot_rotate(mat,v,pivot,angles,t);vector_write(&m,o+0x382,mat,9);
    vector_read(&m,o+0x31a,mat,9);point(v,mat,out);vector_read(&m,o+0x30e,v,3);
    for (i=0;i<3;++i) out[i]=add(out[i],v[i]);
    vector_write(&m,o+0x376,out,3);
    for (side=0;side<2;++side) {
        uint32_t part=o+0x4aeu+side*104u;
        copy(&m,part+12,o+0x2b2,12);point_from(&m,part-12,part+12,part,origin);
    }
    copy(&m,o,o+0x78,3);factor=(int32_t)load(&m,o+0x6fe,2);shift=(int32_t)load(&m,o+0x700,2);
    for (i=0;i<4;++i) travel_sum=add(travel_sum,trunc_shift(mul(i32(&m,o+0x148+i*70u),factor),(unsigned)shift));
    store(&m,o+4,(uint32_t)add(i32(&m,o+4),sar(travel_sum,3)),4);
    copy(&m,o+0x78,o+0x2a6,3);point_from(&m,o+0x5da,o+0x2b2,o+0x5e6,origin);
    return m.error;
}
