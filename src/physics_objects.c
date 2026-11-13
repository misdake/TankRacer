#include "physics_objects.h"
#include "physics_vector.h"
#include "physics_memory.h"

int physics_segment_box(const int32_t a[3],const int32_t b[3],const int32_t low[3],
                          const int32_t high[3],int32_t point[3],int32_t normal[3])
{
    int32_t direction[3],raw[3],candidate[3],side[3],distance[3];unsigned i,axis;int inside=1;
    for (i=0;i<3;++i) direction[i]=raw[i]=pm_sub(b[i],a[i]);
    physics_vector_normalize(direction);
    for (i=0;i<3;++i) {
        if (a[i]<low[i]) { side[i]=1;candidate[i]=low[i];inside=0; }
        else if (a[i]>high[i]) { side[i]=0;candidate[i]=high[i];inside=0; }
        else { side[i]=2;candidate[i]=0; }
    }
    if (inside) { for (i=0;i<3;++i) point[i]=a[i];return 2; }
    for (i=0;i<3;++i) {
        int32_t numerator=pm_mul(pm_sub(candidate[i],a[i]),4096);
        if (side[i]==2 || !direction[i]) distance[i]=-4096;
        else {
            if (numerator==INT32_MIN && direction[i]==-1) return -2;
            distance[i]=numerator/direction[i];
        }
    }
    axis=distance[0]<distance[1] ? 1 : 0;if (distance[axis]<distance[2]) axis=2;
    if (distance[axis]<0) return 0;
    for (i=0;i<3;++i) point[i]=pm_add(a[i],pm_sar(pm_mul(direction[i],distance[axis]),12));
    for (i=0;i<3;++i) if (point[i]<pm_sub(low[i],2) || point[i]>pm_add(high[i],2)) return 0;
    if (distance[axis]>physics_approximate_distance(raw)) return 0;
    for (i=0;i<3;++i) normal[i]=0;
    normal[axis]=side[axis]==1 ? -4096 : 4096;return 1;
}
static void vector_read(physics_memory *m,uint32_t a,int32_t *out,unsigned count)
{ unsigned i;for (i=0;i<count;++i) out[i]=pm_i32(m,a+i*4u); }
static void transform(const int32_t v[3],const int32_t matrix[9],int32_t out[3],int inverse)
{
    int32_t tmp[3];unsigned i,j;
    for (i=0;i<3;++i) { int32_t sum=0;for (j=0;j<3;++j) sum=pm_add(sum,pm_mul(v[j],matrix[inverse ? j*3u+i : i*3u+j]));tmp[i]=pm_sar(sum,12); }
    for (i=0;i<3;++i) out[i]=tmp[i];
}
static int broadphase(const int32_t origin[3],const int32_t start[3],int32_t length,int32_t radius)
{ unsigned i;for (i=0;i<3;++i) if (pm_sub(pm_abs(pm_sub(origin[i],start[i])),length)>=radius) return 0;return 1; }
static int query_list(physics_memory *m,uint32_t sentinel,uint32_t self_id,const int32_t a[3],const int32_t b[3],int deformable,physics_object_hit *out)
{
    unsigned step,i;uint32_t node=pm_load(m,sentinel,4);int32_t delta[3],length;
    for (i=0;i<3;++i) delta[i]=pm_sub(b[i],a[i]);
    length=physics_approximate_distance(delta);
    for (step=0;node!=sentinel && step<65536u;++step,node=pm_load(m,node,4)) {
        uint32_t id=pm_load(m,node+8,4),link=pm_load(m,pm_load(m,0x55c694,4)+id*8u,4),obj=link ? pm_load(m,link+4,4) : 0;
        int32_t origin[3],matrix[9],local_a[3],local_b[3],low[3],high[3],hit[3]={0},normal[3]={0},radius;int code;
        if (m->error) return m->error;
        vector_read(m,obj,origin,3);radius=(int32_t)(pm_load(m,pm_load(m,obj+0x48,4)+0xc,2)+96u)*16;
        if (m->error) return m->error;
        if (!broadphase(origin,a,length,radius) || (!deformable && id==self_id)) continue;
        for (i=0;i<3;++i) { local_a[i]=pm_sub(a[i],origin[i]);local_b[i]=pm_sub(b[i],origin[i]); }
        {
            uint32_t matrix_address=deformable ? pm_load(m,obj+0x14,4) : obj+0x18;
            if (matrix_address) { vector_read(m,matrix_address,matrix,9);transform(local_a,matrix,local_a,0);transform(local_b,matrix,local_b,0); }
            else { for (i=0;i<9;++i) matrix[i]=(i%4u)==0 ? 4096 : 0; }
        }
        vector_read(m,obj+0x60,low,3);vector_read(m,obj+0x6c,high,3);
        if (m->error) return m->error;
        code=physics_segment_box(local_a,local_b,low,high,hit,normal);
        if (code<0) return code;
        if (!deformable && code==2) {
            int32_t direction[3],behind[3];for (i=0;i<3;++i) direction[i]=pm_sub(local_b[i],local_a[i]);
            if (physics_approximate_distance(direction)>0) {
                physics_vector_normalize(direction);
                for (i=0;i<3;++i) behind[i]=pm_sub(local_a[i],pm_mul(direction[i],pm_sar(radius,11)));
                code=physics_segment_box(behind,local_a,low,high,hit,normal);
                if (code<0) return code;
            }
        }
        if (deformable && (code==1 || code==2)) {
            if (code==2) { for (i=0;i<3;++i) normal[i]=local_a[i];physics_vector_normalize(normal); }
            out->entity=id;out->object=obj;out->code=code;
            for (i=0;i<3;++i) { out->point[i]=hit[i];out->normal[i]=normal[i]; }return 1;
        }
        if (!deformable && code==1) {
            int32_t direction[3],projection=0;
            transform(normal,matrix,normal,1);
            for (i=0;i<3;++i) direction[i]=delta[i];
            physics_vector_normalize(direction);
            for (i=0;i<3;++i) projection=pm_add(projection,pm_trunc(pm_mul(direction[i],normal[i]),12));
            if (projection<0) {
                transform(hit,matrix,hit,1);out->entity=id;out->object=obj;out->code=code;
                for (i=0;i<3;++i) { out->point[i]=pm_add(hit[i],origin[i]);out->normal[i]=normal[i]; }return 1;
            }
        }
    }
    if (m->error) return m->error;
    return node!=sentinel ? -3 : 0;
}
int physics_obstacle_query(physics_world_read read,void *ctx,uint32_t self_id,const int32_t a[3],const int32_t b[3],physics_object_hit *out)
{
    physics_memory mem={read,0,ctx,0},*m=&mem;int32_t x,z;uint32_t sentinel;
    x=pm_sar(pm_sub(a[0],pm_i32(m,0x544f70)),pm_load(m,0x544f78,4));
    z=pm_sar(pm_sub(a[2],pm_i32(m,0x544f74)),pm_load(m,0x544f7c,4));
    if (x<0) x=0;
    if (x>7) x=7;
    if (z<0) z=0;
    if (z>7) z=7;
    sentinel=pm_load(m,UINT32_C(0x544e70)+(uint32_t)(x+z*8)*4u,4);
    if (m->error) return m->error;
    return query_list(m,sentinel,self_id,a,b,0,out);
}
int physics_deformable_query(physics_world_read read,void *ctx,const int32_t a[3],const int32_t b[3],physics_object_hit *out)
{
    physics_memory mem={read,0,ctx,0};uint32_t sentinel=pm_load(&mem,0x53cd18,4);
    if (mem.error) return mem.error;
    return query_list(&mem,sentinel,0,a,b,1,out);
}
static int corners_inside(physics_memory *m,uint32_t self,uint32_t other)
{
    int32_t origin[3],matrix[9],v[3],x0=pm_i32(m,self+0x632),z0=pm_i32(m,self+0x63a),x1=pm_i32(m,self+0x63e),z1=pm_i32(m,self+0x646);unsigned i,j;
    vector_read(m,self,origin,3);vector_read(m,self+0x18,matrix,9);
    for (i=0;i<4;++i) {
        vector_read(m,other+0x128+i*70u,v,3);
        for (j=0;j<3;++j) v[j]=pm_sub(v[j],origin[j]);
        transform(v,matrix,v,0);
        if (v[0]>x0 && v[0]<x1 && v[2]>z0 && v[2]<z1) return 1;
    }
    return 0;
}
static int overlap(physics_memory *m,uint32_t self,uint32_t other)
{
    int a=corners_inside(m,self,other),b=corners_inside(m,other,self);
    return m->error ? m->error : a || b;
}
int physics_tank_overlap(physics_world_read read,void *ctx,uint32_t self,uint32_t other)
{ physics_memory mem={read,0,ctx,0};return overlap(&mem,self,other); }
int physics_tank_query(physics_world_read read,void *ctx,uint32_t self,const int32_t a[3],const int32_t b[3],physics_object_hit *out)
{
    physics_memory mem={read,0,ctx,0},*m=&mem;unsigned slot,i;uint32_t self_slot=pm_load(m,self+0xe07,1);
    int32_t radius=(int32_t)pm_load(m,pm_load(m,self+0x48,4)+0xc,2)*64,delta[3];
    for (i=0;i<3;++i) delta[i]=pm_sub(b[i],a[i]);
    for (slot=0;slot<9;++slot) {
        uint32_t id=pm_load(m,UINT32_C(0x53e8e0)+slot*4u,4),link,obj,matrix_address;
        int32_t origin[3],matrix[9],local_a[3],local_b[3],low[3],high[3],hit[3]={0},normal[3]={0};int code;
        if (m->error) return m->error;
        if (!id || slot==self_slot || !pm_load(m,self+0xded,1)) continue;
        link=pm_load(m,pm_load(m,0x55c694,4)+id*8u,4);obj=link ? pm_load(m,link+4,4) : 0;
        if ((pm_load(m,obj+0xd90,1)&1u) || !pm_load(m,obj+0xde9,1)) continue;
        vector_read(m,obj,origin,3);
        if (m->error) return m->error;
        if (!(origin[0]<pm_add(a[0],radius) && origin[0]>pm_sub(a[0],radius) && origin[2]<pm_add(a[2],radius) && origin[2]>pm_sub(a[2],radius))) continue;
        code=overlap(m,self,obj);if (code<0) return code;
        if (code) { out->entity=id;out->object=obj;out->code=2;return 2; }
        for (i=0;i<3;++i) { local_a[i]=pm_sub(a[i],origin[i]);local_b[i]=pm_sub(b[i],origin[i]); }
        matrix_address=pm_load(m,obj+0x14,4);
        if (matrix_address) { vector_read(m,matrix_address,matrix,9);transform(local_a,matrix,local_a,0);transform(local_b,matrix,local_b,0); }
        else for (i=0;i<9;++i) matrix[i]=(i%4u)==0 ? 4096 : 0;
        vector_read(m,obj+0x60,low,3);vector_read(m,obj+0x6c,high,3);
        if (m->error) return m->error;
        code=physics_segment_box(local_a,local_b,low,high,hit,normal);if (code<0) return code;
        if (code==2) { out->entity=id;out->object=obj;out->code=2;return 2; }
        if (code==1) {
            int32_t direction[3],projection=0;
            transform(normal,matrix,normal,1);
            for (i=0;i<3;++i) direction[i]=delta[i];
            physics_vector_normalize(direction);
            for (i=0;i<3;++i) projection=pm_add(projection,pm_trunc(pm_mul(direction[i],normal[i]),12));
            if (projection<0) {
                transform(hit,matrix,hit,1);out->entity=id;out->object=obj;out->code=1;
                for (i=0;i<3;++i) { out->point[i]=pm_add(hit[i],origin[i]);out->normal[i]=normal[i]; }return 1;
            }
        }
    }
    if (m->error) return m->error;
    out->object=0;out->entity=0;out->code=0;return 0;
}
