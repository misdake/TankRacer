#include "physics_attitude.h"

static int32_t s32(uint32_t x) { return x<=INT32_MAX ? (int32_t)x : (int32_t)((int64_t)x-INT64_C(4294967296)); }
static int32_t add(int32_t a,int32_t b) { return s32((uint32_t)a+(uint32_t)b); }
static int32_t sub(int32_t a,int32_t b) { return s32((uint32_t)a-(uint32_t)b); }
static int32_t mul(int32_t a,int32_t b) { return s32((uint32_t)a*(uint32_t)b); }
static int32_t sar(int32_t a,unsigned n) { uint32_t x=(uint32_t)a;n&=31u;return s32(a<0 ? ~(~x>>n) : x>>n); }
static int32_t neg(int32_t a) { return sub(0,a); }
static int32_t abs32(int32_t a) { return a<0 ? neg(a) : a; }
static int32_t trunc_shift(int32_t a,unsigned n) { return a<0 ? neg(sar(neg(a),n)) : sar(a,n); }
static int divide(int32_t n,int32_t d,int32_t *out)
{
    if (!d || (n==INT32_MIN && d==-1)) return PHYSICS_ATTITUDE_ARITH_ERROR;
    *out=n/d;return 0;
}
int physics_angle_lookup(int32_t n,int32_t d,const physics_trig *t,int32_t *out)
{
    int32_t a=abs32(n),b=abs32(d),v,index,numerator,denominator;int complement=0;
    if (n==INT32_MIN || d==INT32_MIN) return PHYSICS_ATTITUDE_ARITH_ERROR;
    if (!a) v=0;
    else if (!b) v=16384;
    else if (a==b) v=8192;
    else {
        if (a>b) { int32_t swap=a;a=b;b=swap;complement=1; }
        numerator=b<65536 ? mul(a,32768) : a;
        denominator=b<65536 ? mul(b,128) : sar(b,8);
        if (divide(numerator,denominator,&index)<0 || index<0 || index>256) return PHYSICS_ATTITUDE_ARITH_ERROR;
        v=t->angle[index];if (complement) v=sub(16384,v);
    }
    *out=d>=0 ? (n<0 ? neg(v) : v) : (n<0 ? sub(v,32768) : sub(32768,v));
    return 0;
}
int32_t physics_length2_approx(int32_t x,int32_t y)
{
    int32_t a=abs32(x),b=abs32(y);
    return b<=a ? add(a,sub(b,sar(b,1))) : add(a,sub(b,sar(a,1)));
}
void physics_matrix_rotate(int32_t m[9],unsigned axis,uint32_t angle,const physics_trig *t)
{
    unsigned i,first=axis==0 ? 3 : 0,second=axis==2 ? 3 : 6;
    int32_t cosine,negative_sine;
    if (!angle) return;
    cosine=t->cosine[(angle&65532u)>>2];negative_sine=t->sine[((0u-angle)&65532u)>>2];
    for (i=0;i<3;++i) {
        int32_t a=m[first+i],b=m[second+i];
        if (axis==1) {
            m[first+i]=trunc_shift(sub(mul(cosine,a),mul(negative_sine,b)),12);
            m[second+i]=trunc_shift(add(mul(negative_sine,a),mul(cosine,b)),12);
        } else {
            m[first+i]=trunc_shift(add(mul(cosine,a),mul(negative_sine,b)),12);
            m[second+i]=trunc_shift(sub(mul(cosine,b),mul(negative_sine,a)),12);
        }
    }
}
void physics_matrix_from_angles(int32_t m[9],const uint32_t a[3],const physics_trig *t)
{
    int32_t cx=t->cosine[(a[0]&65532u)>>2],sx=t->sine[(a[0]&65532u)>>2];
    int32_t cy=t->cosine[(a[1]&65532u)>>2],sy=t->sine[(a[1]&65532u)>>2];
    int32_t u=sar(mul(sy,sx),12),v=sar(mul(cy,sx),12),cz,sz;
    if (!a[2]) {
        m[0]=cy;m[2]=sy;m[4]=cx;m[1]=0;m[3]=u;m[5]=neg(v);
    } else {
        cz=t->cosine[(a[2]&65532u)>>2];sz=t->sine[(a[2]&65532u)>>2];
        m[0]=sar(sub(mul(cy,cz),mul(u,sz)),12);m[1]=sar(neg(mul(cx,sz)),12);
        m[2]=sar(add(mul(sy,cz),mul(v,sz)),12);m[3]=sar(add(mul(cy,sz),mul(u,cz)),12);
        m[5]=sar(sub(mul(sz,sy),mul(v,cz)),12);m[4]=sar(mul(cx,cz),12);
    }
    m[7]=sx;m[8]=sar(mul(cy,cx),12);m[6]=sar(neg(mul(cx,sy)),12);
}
int physics_matrix_normalize(int32_t m[9])
{
    unsigned pass,row,j;
    for (pass=0;pass<2;++pass) for (row=0;row<3;++row) {
        int32_t value[3],sum=0,denominator;
        for (j=0;j<3;++j) { value[j]=m[pass ? row+j*3u : row*3u+j];sum=add(sum,mul(value[j],value[j])); }
        denominator=add(sar(sum,13),2048);
        for (j=0;j<3;++j) {
            int32_t result;
            if (divide(mul(value[j],4096),denominator,&result)<0) return PHYSICS_ATTITUDE_ARITH_ERROR;
            m[pass ? row+j*3u : row*3u+j]=result;
        }
    }
    return 0;
}
static void point_transpose(const int32_t m[9],const int32_t local[3],int32_t out[3])
{
    unsigned i;
    for (i=0;i<3;++i) out[i]=sar(add(add(mul(m[i],local[0]),mul(m[3+i],local[1])),mul(m[6+i],local[2])),12);
}
void physics_pivot_rotate(int32_t m[9],int32_t position[3],const int32_t pivot[3],
                           const int32_t angles[3],const physics_trig *t)
{
    int32_t before[3],after[3];unsigned i;
    point_transpose(m,pivot,before);
    physics_matrix_rotate(m,1,(uint32_t)angles[1],t);
    physics_matrix_rotate(m,0,(uint32_t)angles[0],t);
    physics_matrix_rotate(m,2,(uint32_t)angles[2],t);
    point_transpose(m,pivot,after);
    for (i=0;i<3;++i) position[i]=add(position[i],sub(before[i],after[i]));
}
int physics_attitude_step(physics_attitude_state *s,const physics_attitude_config *p,
                           int32_t dt,uint32_t frame,const physics_trig *t)
{
    int32_t angle,horizontal,angles[3];
    s->angular[0]=add(s->angular[0],trunc_shift(mul(s->longitudinal_delta,s32(p->longitudinal_multiplier&65535u)),p->longitudinal_shift));
    s->angular[2]=sub(s->angular[2],trunc_shift(mul(s->lateral_delta,s32(p->lateral_multiplier&65535u)),p->lateral_shift));
    physics_matrix_rotate(s->matrix,1,(uint32_t)trunc_shift(mul(s->angular[1],dt),14),t);
    horizontal=physics_length2_approx(abs32(s->reference[0]),abs32(s->reference[2]));
    if (physics_angle_lookup(abs32(s->reference[1]),horizontal,t,&angle)<0) return PHYSICS_ATTITUDE_ARITH_ERROR;
    if (angle>12740) {
        if (s->reference[1]>=0) {
            if (s->angular[2]<0) s->angular[2]=sar(neg(s->angular[2]),2);
            s->angular[2]=add(s->angular[2],5000);
        } else {
            if (s->angular[2]>0) s->angular[2]=neg(sar(s->angular[2],2));
            s->angular[2]=sub(s->angular[2],5000);
        }
    }
    horizontal=physics_length2_approx(abs32(s->reference[6]),abs32(s->reference[8]));
    if (physics_angle_lookup(abs32(s->reference[7]),horizontal,t,&angle)<0) return PHYSICS_ATTITUDE_ARITH_ERROR;
    if (angle>12740) {
        if (s->reference[7]<=0) {
            if (s->angular[0]<0) s->angular[0]=neg(trunc_shift(s->angular[2],2));
            s->angular[0]=add(s->angular[0],5000);
        } else {
            if (s->angular[0]>0) s->angular[0]=neg(trunc_shift(s->angular[2],2));
            s->angular[0]=sub(s->angular[0],5000);
        }
    }
    angles[0]=trunc_shift(mul(s->angular[0],dt),14);angles[1]=0;angles[2]=trunc_shift(mul(s->angular[2],dt),14);
    physics_pivot_rotate(s->matrix,s->position,s->pivot,angles,t);
    if (!(frame%10u)) return physics_matrix_normalize(s->matrix);
    return 0;
}
