#include "physics_geometry.h"

typedef struct cursor { physics_world_read read;physics_world_write write;void *ctx;int failed; } cursor;
static int32_t s32(uint32_t x) { return x<=INT32_MAX ? (int32_t)x : (int32_t)((int64_t)x-INT64_C(4294967296)); }
static int32_t add(int32_t a,int32_t b) { return s32((uint32_t)a+(uint32_t)b); }
static int32_t sub(int32_t a,int32_t b) { return s32((uint32_t)a-(uint32_t)b); }
static int32_t mul(int32_t a,int32_t b) { return s32((uint32_t)a*(uint32_t)b); }
static int32_t sar(int32_t a,unsigned n) { uint32_t x=(uint32_t)a;n&=31u;return s32(a<0 ? ~(~x>>n) : x>>n); }
static int32_t abs32(int32_t a) { return a<0 ? sub(0,a) : a; }
static int32_t trunc_shift(int32_t a,unsigned n) { return a<0 ? sub(0,sar(sub(0,a),n)) : sar(a,n); }
static uint32_t word(cursor *c,uint32_t a,unsigned n)
{
    unsigned char bytes[4];unsigned i;uint32_t v=0;
    if (c->failed) return 0;
    if (!c->read(c->ctx,a,bytes,n)) { c->failed=1;return 0; }
    for (i=0;i<n;++i) v|=(uint32_t)bytes[i]<<(i*8u);
    return v;
}
static uint32_t u32(cursor *c,uint32_t a) { return word(c,a,4); }
static int32_t i32(cursor *c,uint32_t a) { return s32(u32(c,a)); }
static int32_t i16(cursor *c,uint32_t a) { uint32_t v=word(c,a,2);return v>32767u ? (int32_t)v-65536 : (int32_t)v; }
static void byte_write(cursor *c,uint32_t a,unsigned char value)
{
    if (c->failed) return;
    if (!c->write || !c->write(c->ctx,a,&value,1)) c->failed=1;
}

int32_t physics_approximate_distance(const int32_t d[3])
{
    uint32_t x=(uint32_t)abs32(d[0]),y=(uint32_t)abs32(d[1]),z=(uint32_t)abs32(d[2]);
    if (x>y && x>z) return s32(x+(y>>2)+(z>>2));
    if (y>=x && y>z) return s32(y+(x>>2)+(z>>2));
    return s32(z+(x>>2)+(y>>2));
}
int physics_fallback_query(physics_world_read read,void *ctx,uint32_t root,
                           const int32_t world[3],int32_t mode,int32_t normal[3])
{
    cursor c={read,0,ctx,0};int32_t x=sar(world[0],3),y=sar(world[1],3),z=sar(world[2],3);
    int32_t gx=sar(sub(x,i32(&c,root+0x108)),word(&c,root+0x110,1));
    int32_t gz=sar(sub(z,i32(&c,root+0x10c)),word(&c,root+0x112,1)),cell,best=300000;
    uint32_t list,selected=0,n;
    if (c.failed) return PHYSICS_TERRAIN_READ_ERROR;
    if (gx<0 || gz<0 || gx>=8 || gz>=8) return 0;
    cell=i16(&c,root+0x88+(uint32_t)(gx+8*gz)*2u);
    if (c.failed) return PHYSICS_TERRAIN_READ_ERROR;
    if (cell==-1) return 0;
    list=u32(&c,root+4)+(uint32_t)cell*4u;
    for (n=0;n<65536u;++n,list+=4u) {
        uint32_t node=u32(&c,list),mesh,indices,k;int32_t dx,dz,radius,lx,lz;
        if (c.failed) return PHYSICS_TERRAIN_READ_ERROR;
        if (!node) {
            if (!selected) return 0;
            for (k=0;k<3;++k) normal[k]=i16(&c,selected+k*2u);
            return c.failed ? PHYSICS_TERRAIN_READ_ERROR : 1;
        }
        mesh=u32(&c,node+4);
        if ((mode==1 && (word(&c,mesh+0x1c,1)&4u)) ||
            (mode==2 && !(word(&c,mesh+0x1c,1)&4u))) {
            if (c.failed) return PHYSICS_TERRAIN_READ_ERROR;
            continue;
        }
        dx=sub(x,i32(&c,mesh+0x24));dz=sub(z,i32(&c,mesh+0x2c));radius=(int32_t)word(&c,mesh+0xc,2)*2;
        if (c.failed) return PHYSICS_TERRAIN_READ_ERROR;
        if (abs32(dx)>radius || abs32(dz)>radius) continue;
        lx=sar(add(mul(i16(&c,node+0xe),dz),mul(i16(&c,node+0xc),dx)),12);
        lz=sar(add(mul(i16(&c,node+0x12),dz),mul(i16(&c,node+0x10),dx)),12);
        if (c.failed) return PHYSICS_TERRAIN_READ_ERROR;
        if (lx>i16(&c,node+0x16) || lx<i16(&c,node+0x14) ||
            lz>i16(&c,node+0x1a) || lz<i16(&c,node+0x18)) {
            if (c.failed) return PHYSICS_TERRAIN_READ_ERROR;
            continue;
        }
        indices=u32(&c,node+8);
        for (k=0;k<65536u;++k,++indices) {
            uint32_t index=word(&c,indices,1),tri;int32_t d[3],distance;
            if (c.failed) return PHYSICS_TERRAIN_READ_ERROR;
            if (index==255u) break;
            tri=u32(&c,mesh+8)+index*24u;
            d[0]=sub(dx,i16(&c,u32(&c,mesh+0x10)+index*2u));
            d[1]=sub(sub(y,i32(&c,mesh+0x28)),i16(&c,u32(&c,mesh+0x14)+index*2u));
            d[2]=sub(dz,i16(&c,u32(&c,mesh+0x18)+index*2u));
            if (c.failed) return PHYSICS_TERRAIN_READ_ERROR;
            distance=physics_approximate_distance(d);
            if (distance<best) { best=distance;selected=tri; }
        }
        if (k==65536u) return PHYSICS_TERRAIN_LIMIT;
    }
    return PHYSICS_TERRAIN_LIMIT;
}

int32_t physics_deform_edge_weight(const int32_t p[3],const int32_t b[4],int32_t cap)
{
    int32_t value=abs32(sub(p[0],b[0])),v;unsigned i;
    for (i=1;i<4;++i) { v=abs32(sub(p[i<2 ? 0 : 2],b[i]));if (v<value) value=v; }
    value=mul(value,10);return value>cap ? cap : value;
}
int physics_deform_query(physics_world_read read,physics_world_write write,void *ctx,
                         uint32_t object,const int32_t world[3],physics_deform_result *out)
{
    cursor c={read,write,ctx,0};int32_t relative[3],bounds[4],maximum=0;uint32_t mesh,vertices,end,records,count,i,j;
    out->activated_records=0;
    for (i=0;i<3;++i) relative[i]=sub(world[i],i32(&c,object+i*4u));
    for (i=0;i<3;++i) {
        int32_t value=0;
        for (j=0;j<3;++j) value=add(value,mul(i32(&c,object+0x18+(i*3u+j)*4u),relative[j]));
        out->local_raw[i]=sar(value,12);out->local_clamped[i]=trunc_shift(out->local_raw[i],3);
    }
    for (i=0;i<2;++i) {
        uint32_t off=i ? 0xc2u : 0xbau;unsigned component=i ? 2 : 0;
        int32_t lower=i32(&c,object+off),upper=i32(&c,object+off+4);
        if (out->local_clamped[component]<lower) out->local_clamped[component]=lower;
        if (out->local_clamped[component]>upper) out->local_clamped[component]=upper;
    }
    mesh=u32(&c,object+0x48);vertices=u32(&c,mesh+4);count=word(&c,mesh,2);end=vertices+count*8u;
    if (c.failed) return PHYSICS_TERRAIN_READ_ERROR;
    for (i=0;vertices<end && i<count;++i,vertices+=8u) {
        int32_t dx=sub(out->local_clamped[0],i16(&c,vertices));
        int32_t dz=sub(out->local_clamped[2],i16(&c,vertices+4));
        if (dx<128 && dx>-128 && dz<128 && dz>-128) {
            int32_t h=i16(&c,vertices+2);if (h>maximum) maximum=h;
        }
        if (c.failed) return PHYSICS_TERRAIN_READ_ERROR;
    }
    out->vertex_height=maximum;records=u32(&c,object+0xd5);
    if (records) {
        count=u32(&c,object+0xcd);end=records+count*32u;
        if (c.failed) return PHYSICS_TERRAIN_READ_ERROR;
        for (i=0;records<end && i<65536u;++i,records+=32u) {
            uint32_t state=word(&c,records+0x18,1);int activate=state==0;
            if (state==1) {
                int32_t dx=sub(out->local_raw[0],i32(&c,records)),dz=sub(out->local_raw[2],i32(&c,records+8));
                activate=dx<750 && dx>-750 && dz<750 && dz>-750;
            }
            if (c.failed) return PHYSICS_TERRAIN_READ_ERROR;
            if (activate) {
                byte_write(&c,records+0x18,2);byte_write(&c,records+0x1b,127);
                byte_write(&c,records+0x1a,127);byte_write(&c,records+0x19,127);byte_write(&c,object+0xcc,1);
                if (c.failed) return PHYSICS_TERRAIN_READ_ERROR;
                ++out->activated_records;
            }
        }
        if (records<end) return PHYSICS_TERRAIN_LIMIT;
    }
    bounds[0]=i32(&c,object+0x60);bounds[1]=i32(&c,object+0x6c);
    bounds[2]=i32(&c,object+0x68);bounds[3]=i32(&c,object+0x74);
    out->edge_weight=physics_deform_edge_weight(out->local_raw,bounds,i32(&c,object+0xb6));
    out->offset=trunc_shift(mul(out->edge_weight,mul(maximum,8)),16);
    return c.failed ? PHYSICS_TERRAIN_READ_ERROR : 0;
}
