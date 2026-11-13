#include "physics_terrain.h"

typedef struct cursor { physics_world_read read; void *ctx; int failed; } cursor;
static int32_t s32(uint32_t x) { return x<=INT32_MAX ? (int32_t)x : (int32_t)((int64_t)x-INT64_C(4294967296)); }
static int32_t add(int32_t a,int32_t b) { return s32((uint32_t)a+(uint32_t)b); }
static int32_t sub(int32_t a,int32_t b) { return s32((uint32_t)a-(uint32_t)b); }
static int32_t mul(int32_t a,int32_t b) { return s32((uint32_t)a*(uint32_t)b); }
static int32_t sar(int32_t a,unsigned n) { uint32_t x=(uint32_t)a;n&=31u;return s32(a<0 ? ~(~x>>n) : x>>n); }
static int32_t abs32(int32_t a) { return a<0 ? sub(0,a) : a; }
static uint32_t word(cursor *c,uint32_t a,unsigned n)
{
    unsigned char bytes[4];unsigned i;uint32_t v=0;
    if (c->failed) return 0;
    if (!c->read(c->ctx,a,bytes,n)) { c->failed=1;return 0; }
    for (i=0;i<n;++i) v|=(uint32_t)bytes[i]<<(8u*i);
    return v;
}
static uint32_t u32(cursor *c,uint32_t a) { return word(c,a,4); }
static int32_t i32(cursor *c,uint32_t a) { return s32(u32(c,a)); }
static int32_t i16(cursor *c,uint32_t a) { uint32_t x=word(c,a,2);return x>32767u ? (int32_t)x-65536 : (int32_t)x; }
static int edge(int32_t x,int32_t z,int32_t ax,int32_t az,int32_t bx,int32_t bz)
{
    return mul(sub(x,ax),sub(bz,az))<=mul(sub(z,az),sub(bx,ax));
}

typedef struct terrain_selection { uint32_t node,triangle,mesh,vertices; } terrain_selection;
static int terrain_select(cursor *c,uint32_t root,int32_t x,int32_t z,terrain_selection *out)
{
    int32_t sx,sz,gx,gz,cell;uint32_t list,node=0,triangle=0,mesh=0,vertices=0,iter;
    sx=sar(x,3);sz=sar(z,3);
    gx=sar(sub(sx,i32(c,root+0x108)),word(c,root+0x110,1));
    gz=sar(sub(sz,i32(c,root+0x10c)),word(c,root+0x112,1));
    if (c->failed) return PHYSICS_TERRAIN_READ_ERROR;
    if (gx<0 || gx>=8 || gz<0 || gz>=8) { return 0; }
    cell=i16(c,root+8+(uint32_t)(gx+8*gz)*2u);
    if (c->failed) return PHYSICS_TERRAIN_READ_ERROR;
    if (cell==-1) { return 0; }
    list=u32(c,root)+(uint32_t)cell*4u;
    /* Bounded rejection for corrupt/nonterminating resource data. Current
     * captured worlds terminate far below these explicit traversal limits. */
    for (iter=0;iter<65536u;++iter,list+=4u) {
        int32_t dx,dz,bucket;uint32_t indices,t;
        node=u32(c,list);
        if (c->failed) return PHYSICS_TERRAIN_READ_ERROR;
        if (!node) { return 0; }
        mesh=u32(c,node+4);dz=sub(sz,i32(c,mesh+0x2c));dx=sub(sx,i32(c,mesh+0x24));
        if (abs32(dz)>i16(c,node+0x96) || abs32(dx)>i16(c,node+0x92)) {
            if (c->failed) return PHYSICS_TERRAIN_READ_ERROR;
            continue;
        }
        bucket=add(sar(dx,13u-word(c,node+8,1)),mul(sar(dz,13u-word(c,node+10,1)),-8));
        bucket=i16(c,node+0x48+(uint32_t)bucket*2u);
        if (c->failed) return PHYSICS_TERRAIN_READ_ERROR;
        if (bucket==-1) continue;
        indices=u32(c,node+0xc)+(uint32_t)bucket;vertices=u32(c,mesh+4);
        for (t=0;t<65536u;++t,++indices) {
            uint32_t which=word(c,indices,1),packed;int32_t ax,az,bx,bz,cx,cz;
            if (c->failed) return PHYSICS_TERRAIN_READ_ERROR;
            if (which==255u) break;
            triangle=u32(c,mesh+8)+which*24u;
            packed=u32(c,triangle+8);
            ax=i16(c,vertices+(packed&255u)*8u);az=i16(c,vertices+(packed&255u)*8u+4);
            bx=i16(c,vertices+((packed>>8)&255u)*8u);bz=i16(c,vertices+((packed>>8)&255u)*8u+4);
            cx=i16(c,vertices+((packed>>16)&255u)*8u);cz=i16(c,vertices+((packed>>16)&255u)*8u+4);
            if (c->failed) return PHYSICS_TERRAIN_READ_ERROR;
            if (edge(dx,dz,ax,az,bx,bz) && edge(dx,dz,bx,bz,cx,cz) && edge(dx,dz,cx,cz,ax,az)) goto found;
        }
        if (t==65536u) return PHYSICS_TERRAIN_LIMIT;
    }
    return PHYSICS_TERRAIN_LIMIT;
found:
    out->node=node;out->triangle=triangle;out->mesh=mesh;out->vertices=vertices;return PHYSICS_TERRAIN_HIT;
}

int physics_terrain_query(physics_world_read read,void *ctx,uint32_t root,
                          int32_t x,int32_t z,physics_terrain_hit *out)
{
    cursor c={read,ctx,0};terrain_selection selected;int status;
    uint32_t node,triangle,mesh,vertices;int32_t sx=sar(x,3),sz=sar(z,3);
    status=terrain_select(&c,root,x,z,&selected);
    if (status<=0) { if (!status) out->height=-2147483647;return status; }
    node=selected.node;triangle=selected.triangle;mesh=selected.mesh;vertices=selected.vertices;
    {
        int32_t nx=i16(&c,triangle),ny=i16(&c,triangle+2),nz=i16(&c,triangle+4);
        uint32_t v=vertices+word(&c,triangle+8,1)*8u,flags;
        int32_t vx=i16(&c,v),vy=i16(&c,v+2),vz=i16(&c,v+4);
        int32_t numerator=mul(add(mul(sub(add(i32(&c,mesh+0x2c),vz),sz),nz),
                                  mul(sub(add(i32(&c,mesh+0x24),vx),sx),nx)),8);
        int32_t base=mul(add(i32(&c,mesh+0x28),vy),8);
        if (c.failed) return PHYSICS_TERRAIN_READ_ERROR;
        if (!ny || (numerator==INT32_MIN && ny==-1)) return PHYSICS_TERRAIN_ARITH_ERROR;
        out->height=add(numerator/ny,base);out->node=node;
        out->normal[0]=nx;out->normal[1]=ny;out->normal[2]=nz;
        out->surface=word(&c,node+0x98,2);
        out->special=(word(&c,triangle+0x13,1)&1u)!=0;
        flags=(word(&c,triangle+0x12,2)&0x2000u)|word(&c,mesh+0x1c,2);
        if (u32(&c,node+0xa4)) flags|=0x20u;
        out->flags_word=(out->flags_word&0xffff0000u)|(flags&65535u);
        if (word(&c,mesh+0x1c,1)&0x10u) {
            unsigned i;for (i=0;i<4;++i) out->extra[i]=i16(&c,node+0x9a+i*2u);
        }
        return c.failed ? PHYSICS_TERRAIN_READ_ERROR : PHYSICS_TERRAIN_HIT;
    }
}

static int terrain_height(physics_world_read read,void *ctx,uint32_t root,int32_t x,int32_t z,int32_t *out,int32_t normal[3])
{
    cursor c={read,ctx,0};terrain_selection selected;int status;uint32_t v;
    int32_t nx,ny,nz,numerator,base,vx,vy,vz,value;
    if (!read || !out) return PHYSICS_TERRAIN_READ_ERROR;
    status=terrain_select(&c,root,x,z,&selected);
    if (status<=0) { if (!status) *out=-2147483647;return status; }
    if (normal) { normal[0]=i16(&c,selected.triangle);normal[1]=i16(&c,selected.triangle+2u);normal[2]=i16(&c,selected.triangle+4u); }
    ny=i16(&c,selected.triangle+2u);v=selected.vertices+word(&c,selected.triangle+8u,1)*8u;
    if (!ny) value=mul(add(i32(&c,selected.mesh+0x28u),i16(&c,v)),8);
    else {
        nx=i16(&c,selected.triangle);nz=i16(&c,selected.triangle+4u);
        vx=i16(&c,v);vy=i16(&c,v+2u);vz=i16(&c,v+4u);
        numerator=mul(add(mul(sub(add(i32(&c,selected.mesh+0x2cu),vz),sar(z,3)),nz),
                          mul(sub(add(i32(&c,selected.mesh+0x24u),vx),sar(x,3)),nx)),8);
        base=mul(add(i32(&c,selected.mesh+0x28u),vy),8);
        if (c.failed) return PHYSICS_TERRAIN_READ_ERROR;
        if (numerator==INT32_MIN && ny==-1) return PHYSICS_TERRAIN_ARITH_ERROR;
        value=add(numerator/ny,base);
    }
    if (c.failed) return PHYSICS_TERRAIN_READ_ERROR;
    *out=value;return PHYSICS_TERRAIN_HIT;
}
int physics_terrain_height(physics_world_read read,void *ctx,uint32_t root,int32_t x,int32_t z,int32_t *out)
{ return terrain_height(read,ctx,root,x,z,out,0); }
int physics_terrain_normal_height(physics_world_read read,void *ctx,uint32_t root,int32_t x,int32_t z,int32_t *out,int32_t normal[3])
{ if (!normal) return PHYSICS_TERRAIN_READ_ERROR;return terrain_height(read,ctx,root,x,z,out,normal); }

int physics_auxiliary_query(physics_world_read read,void *ctx,uint32_t root,
                            const int32_t local[3],const int32_t m[9],
                            const int32_t origin[3],int force_surface,
                            physics_auxiliary_contact *out)
{
    unsigned i;int status;physics_terrain_hit hit={0};
    for (i=0;i<3;++i) {
        int32_t rotated=sar(add(add(mul(m[i],local[0]),mul(m[3+i],local[1])),mul(m[6+i],local[2])),12);
        out->world[i]=add(rotated,origin[i]);
    }
    status=physics_terrain_query(read,ctx,root,out->world[0],out->world[2],&hit);
    if (status<0) return status;
    if (!status) { out->kind=3;out->surface=0; }
    else {
        int32_t gap=sub(out->world[1],hit.height);
        out->kind=gap < -75 ? 0 : gap < 75 ? 1 : 2;
        out->surface=(int32_t)hit.surface;
    }
    if (force_surface) out->surface=1;
    out->cached=1;
    return status;
}

void physics_contact_materials(int32_t a,int32_t c,const int32_t aux[2],int32_t out[2])
{
    out[0]=a==c ? a : aux[0];
    out[1]=a==c ? a : aux[1];
}
