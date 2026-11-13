#include "physics_body.h"
#include "physics_memory.h"
#include "physics_vector.h"

static void read_vector(physics_memory *m,uint32_t a,int32_t *v,unsigned n)
{ unsigned i;for (i=0;i<n;++i) v[i]=pm_i32(m,a+i*4u); }
static void transform_row(const int32_t v[3],const int32_t mat[9],int32_t out[3])
{
    int32_t temp[3];unsigned i,j;
    for (i=0;i<3;++i) { int32_t sum=0;for (j=0;j<3;++j) sum=pm_add(sum,pm_mul(mat[i*3u+j],v[j]));temp[i]=pm_sar(sum,12); }
    for (i=0;i<3;++i) out[i]=temp[i];
}
int physics_body_articulation(physics_world_read read,physics_world_write write,void *ctx,uint32_t o)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;int32_t dt=pm_i32(m,0x5904fc),v,rate,force;uint32_t mesh;unsigned i;
    mesh=pm_load(m,o+0x366,4);if (!mesh) return m->error;
    v=pm_i32(m,o+0x764);force=pm_trunc(pm_mul(v,v),v<1 ? 16 : 14);
    rate=pm_i32(m,o+0x768);rate=v<1 ? pm_add(rate,pm_mul(dt,force)) : pm_sub(rate,pm_mul(dt,force));
    rate=pm_decay(rate,pm_load(m,0x5319c8,4),dt);v=pm_add(v,rate);
    if (v<-500) { v=-500;rate=0; }else if (v>200) { v=200;rate=0; }
    pm_write32(m,o+0x768,rate);v=pm_rate_step(v,pm_i16(m,0x5319cc),dt);pm_write32(m,o+0x764,v);
    v=pm_i32(m,o+0x76c);force=pm_trunc(pm_mul(v,v),v<1 ? 15 : 14);
    rate=pm_i32(m,o+0x770);rate=v<1 ? pm_add(rate,pm_mul(dt,force)) : pm_sub(rate,pm_mul(dt,force));
    rate=pm_damp(rate,pm_load(m,0x5319d0,4),dt);v=pm_add(v,rate);
    if (v<-1456) { v=-1456;rate=0; }else if (v>364) { v=364;rate=0; }
    pm_write32(m,o+0x76c,v);pm_write32(m,o+0x770,rate);
    for (i=0;i<4;++i) {
        uint32_t vertex=pm_load(m,o+0x784+i,1),vertices=pm_load(m,mesh+4,4);
        int32_t value=pm_sub(pm_i16(m,o+0x788+i*2u),pm_sar(pm_i32(m,o+0x764),3));
        pm_store(m,vertices+vertex*8u+4,(uint32_t)value,2);
    }
    return m->error;
}
int physics_player_lean(physics_world_read read,physics_world_write write,void *ctx,uint32_t o)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;int32_t dt=pm_i32(m,0x5904fc),delta[2];unsigned i;
    delta[0]=pm_trunc(pm_mul(pm_i32(m,o+0xc4),-60),10);delta[1]=pm_trunc(pm_mul(pm_i32(m,o+0xcc),60),10);
    for (i=0;i<2;++i) {
        uint32_t position=o+(i ? 0x774u : 0x77cu),rate_address=position+4;
        int32_t rate=pm_add(pm_i32(m,rate_address),delta[i]),v=pm_i32(m,position),force;
        if (v<-2730) v=-2730;
        if (v>2730) v=2730;
        force=pm_trunc(pm_mul(v,v),15);rate=v<1 ? pm_add(rate,pm_mul(dt,force)) : pm_sub(rate,pm_mul(dt,force));
        rate=pm_decay(rate,pm_load(m,0x531ca4,4),dt);pm_write32(m,rate_address,rate);pm_write32(m,position,pm_add(v,rate));
    }
    return m->error;
}
int physics_contact_hull_refresh(physics_world_read read,physics_world_write write,void *ctx,uint32_t o)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;int32_t current[3],previous[3],maximum=0;uint32_t mesh;unsigned i,j;
    read_vector(m,o+0x78,current,3);read_vector(m,o+0x84,previous,3);
    for (i=0;i<3;++i) pm_write32(m,o+0x722+i*4u,current[i]);
    for (i=0;i<12;++i) pm_write32(m,o+0x72eu+i*4u,pm_i32(m,UINT32_C(0x532888)+i*4u));
    mesh=pm_load(m,o+0x71e,4);
    for (i=0;i<4;++i) {
        uint32_t index=pm_load(m,UINT32_C(0x531c88)+i*4u,4),p=o+0x128+index*70u,vertices=pm_load(m,mesh+4,4);int32_t v[3],distance=0;
        read_vector(m,p,v,3);
        for (j=0;j<3;++j) {
            v[j]=pm_sub(pm_add(v[j],pm_sub(current[j],previous[j])),current[j]);
            if (j==1) v[j]=pm_sub(v[j],pm_i32(m,p+0x10));
            distance=pm_add(distance,pm_abs(v[j]));pm_store(m,vertices+i*8u+j*2u,(uint32_t)pm_trunc(v[j],3),2);
        }
        if (distance>maximum) maximum=distance;
    }
    {
        uint32_t radius=(uint32_t)pm_sar(maximum,4)&65535u;
        pm_store(m,mesh+0xc,(radius+(radius>>5))&65535u,2);
    }
    return m->error;
}
static void mesh_normals(physics_memory *m,uint32_t mesh,uint32_t vertex)
{
    uint32_t count=pm_load(m,mesh+2,2),triangles=pm_load(m,mesh+8,4),vertices=pm_load(m,mesh+4,4),i;unsigned j;
    for (i=0;i<count && !m->error;++i) {
        uint32_t t=triangles+i*24u,indices[3];int32_t p[3][3],a[3],b[3],normal[3];
        for (j=0;j<3;++j) indices[j]=pm_load(m,t+8+j,1);
        if (indices[0]!=vertex && indices[1]!=vertex && indices[2]!=vertex) continue;
        for (j=0;j<9;++j) p[j/3][j%3]=pm_i16(m,vertices+indices[j/3]*8u+(j%3)*2u);
        for (j=0;j<3;++j) { a[j]=pm_sub(p[2][j],p[0][j]);b[j]=pm_sub(p[1][j],p[0][j]); }
        physics_vector_normalize(a);physics_vector_normalize(b);physics_vector_cross(a,b,normal);physics_vector_normalize(normal);
        for (j=0;j<3;++j) pm_store(m,t+j*2u,(uint32_t)normal[j],2);
    }
}
int physics_mesh_vertex_normals(physics_world_read read,physics_world_write write,void *ctx,uint32_t mesh,uint32_t vertex)
{ physics_memory mem={read,write,ctx,0};mesh_normals(&mem,mesh,vertex);return mem.error; }
static void body_damage(physics_memory *m,uint32_t o,const int32_t world[3])
{
    int32_t local[3],origin[3],matrix[9];unsigned i;uint32_t quadrant,mesh,vertex,vertices;
    read_vector(m,o+0x2a6,origin,3);read_vector(m,o+0x2b2,matrix,9);
    for (i=0;i<3;++i) local[i]=pm_sub(world[i],origin[i]);
    transform_row(local,matrix,local);quadrant=(local[2]>0 ? 0u : 2u)+(local[0]>0 ? 1u : 0u);
    if (pm_load(m,o+0xdf7+quadrant,1)) return;
    pm_store(m,o+0xdf7+quadrant,1,1);vertex=pm_load(m,o+0xdff+quadrant,1);mesh=pm_load(m,o+0x296,4);vertices=pm_load(m,mesh+4,4);
    pm_store(m,vertices+vertex*8u,(uint32_t)pm_sar(pm_mul(pm_i16(m,vertices+vertex*8u),3500),12),2);
    pm_store(m,vertices+vertex*8u+4,(uint32_t)pm_sar(pm_mul(pm_i16(m,vertices+vertex*8u+4),2500),12),2);
    mesh_normals(m,mesh,vertex);
}
int physics_body_damage(physics_world_read read,physics_world_write write,void *ctx,uint32_t o,const int32_t world[3])
{ physics_memory mem={read,write,ctx,0};body_damage(&mem,o,world);return mem.error; }
int physics_turret_damage(physics_world_read read,physics_world_write write,void *ctx,uint32_t o,const int32_t world[3])
{
    physics_memory m={read,write,ctx,0};int32_t local[3],matrix[9];uint32_t i,q,mesh,vertex,at;
    if (!read || !write || !world) return -1;
    for (i=0;i<3u;i++) local[i]=pm_sub(world[i],pm_i32(&m,o+0x30eu+i*4u));
    read_vector(&m,o+0x31au,matrix,9);transform_row(local,matrix,local);q=(local[2]>0 ? 0u : 2u)+(local[0]>0 ? 1u : 0u);
    if (pm_load(&m,o+0xdfbu+q,1)) return m.error;
    pm_store(&m,o+0xdfbu+q,1,1);mesh=pm_load(&m,o+0x2feu,4);vertex=pm_load(&m,o+0xe03u+q,1);at=pm_load(&m,mesh+4u,4)+vertex*8u;
    pm_store(&m,at,(uint32_t)pm_sar(pm_mul(pm_i16(&m,at),3000),12),2);
    pm_store(&m,at+2u,(uint32_t)pm_sar(pm_mul(pm_i16(&m,at+2u),(q&2u) ? 1200 : 2200),12),2);
    pm_store(&m,at+4u,(uint32_t)pm_sar(pm_mul(pm_i16(&m,at+4u),3900),12),2);mesh_normals(&m,mesh,vertex);return m.error;
}
int physics_track_damage(physics_world_read read,physics_world_write write,void *ctx,uint32_t o,int32_t kind,const int32_t world[3])
{
    physics_memory mem={read,write,ctx,0};uint32_t address=o+0x292u+(kind!=2),value=pm_load(&mem,address,1);
    if (value<10) { pm_store(&mem,address,value+1,1);if (pm_load(&mem,o+0xde5,1)) body_damage(&mem,o,world); }
    return mem.error;
}
