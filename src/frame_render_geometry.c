#include "frame_render_geometry.h"
#include "physics_memory.h"
#include <string.h>

static int32_t divide(int32_t a,int32_t b)
{ return !b ? 0 : a==INT32_MIN && b==-1 ? INT32_MIN : a/b; }
static int32_t dot(const int32_t a[3],const int32_t b[3])
{ uint32_t i;int32_t sum=0;for(i=0;i<3u;i++)sum=pm_add(sum,pm_mul(a[i],b[i]));return sum; }
void frame_render_transform(const int32_t point[3],const int32_t matrix[9],const int32_t translation[3],int32_t out[3])
{ uint32_t i,j;for(i=0;i<3u;i++){int32_t sum=translation[i];for(j=0;j<3u;j++)sum=pm_add(sum,pm_mul(point[j],matrix[j*3u+i]));out[i]=sum;} }
static int32_t clamp(int32_t v,int32_t low,int32_t high)
{ return v<low ? low : v>high ? high : v; }
static double root(double n)
{ double x=n;uint32_t i;for(i=0;i<32u;i++)x=(x+n/x)*.5;return x; }
static void side_plane(frame_render_clip_settings *s,uint32_t edge)
{
    uint32_t axis=edge/2u,positive=edge&1u,turn;int32_t accepted=0,rejected=(positive!=axis) ? 0x2000000 : -0x2000000,projected=s->center[axis],scale=0x1000000/s->focal;
    int32_t bound=s->bounds[edge],slope;double length;
    for(turn=0;turn<33u && (positive ? projected<bound : projected>bound);turn++) {
        int32_t mid=divide(pm_add(accepted,rejected),2),p=s->center[axis]+(axis ? -1 : 1)*divide(mid,scale);
        if(positive ? p<=bound : p>=bound){accepted=mid;projected=p;}else rejected=mid;
    }
    slope=pm_sar(accepted,12);length=root((double)slope*slope+16777216.0);
    /*442F60/443160 normalize to4096 then truncate, without a libm ABI. */
    s->side[edge][axis]=(int32_t)((positive ? -1.0 : 1.0)*16777216.0/length);
    s->side[edge][2]=(int32_t)((positive ? 1.0 : -1.0)*slope*4096.0/length);
    if(axis){s->side[edge][axis]=-s->side[edge][axis];s->side[edge][2]=-s->side[edge][2];}
}
void frame_render_clip_initialize(frame_render_clip_settings *s,uint32_t width,uint32_t height,int32_t near_z,int32_t far_z)
{
    uint32_t i;memset(s,0,sizeof(*s));s->near_z=near_z;s->far_z=far_z;s->focal=(int32_t)(height*2048u/480u);s->center[0]=(int32_t)width*2+2;s->center[1]=(int32_t)height*2+2;s->bounds[1]=(int32_t)width*4;s->bounds[3]=(int32_t)height*4;
    if(s->focal>0)for(i=0;i<4u;i++)side_plane(s,i);
}
static void project(const frame_render_clip_settings *s,frame_render_vertex *v)
{ int32_t d=divide(v->xyz[2],s->focal);v->screen[0]=pm_add(divide(v->xyz[0],d),s->center[0]);v->screen[1]=pm_sub(s->center[1],divide(v->xyz[1],d)); }
int32_t frame_render_plane_distance(const int32_t xyz[3],const int32_t plane[4])
{ int32_t q[3];uint32_t i;for(i=0;i<3u;i++)q[i]=pm_sar(xyz[i],12);return pm_add(pm_sar(dot(q,plane),12),plane[3]); }
static void lerp_uv(const frame_render_vertex *a,const frame_render_vertex *b,int32_t t,frame_render_vertex *v)
{ uint32_t i;for(i=0;i<2u;i++)v->uv[i]=(uint32_t)pm_add((int32_t)a->uv[i],pm_sar(pm_mul(pm_sub((int32_t)b->uv[i],(int32_t)a->uv[i]),t),12))&65535u; }
static frame_render_vertex near_intersection(const frame_render_clip_settings *s,const frame_render_vertex *a,const frame_render_vertex *b)
{
    frame_render_vertex inside=a->xyz[2]>s->near_z ? *a : *b,outside=a->xyz[2]>s->near_z ? *b : *a,mid;uint32_t i,step;
    /*4F7870 deliberately bisects with signed division toward zero; replacing
     * this by a floating t changes the last fixed-point UV/position bits. */
    for(step=0;step<64u && outside.xyz[2]<s->near_z;step++) {
        for(i=0;i<3u;i++)mid.xyz[i]=divide(pm_add(inside.xyz[i],outside.xyz[i]),2);
        for(i=0;i<2u;i++)mid.uv[i]=(inside.uv[i]+outside.uv[i])>>1;
        if(mid.xyz[2]<=s->near_z)outside=mid;else inside=mid;
    }
    project(s,&outside);return outside;
}
static frame_render_vertex extra_intersection(const frame_render_clip_settings *s,const frame_render_vertex *a,const frame_render_vertex *b,const int32_t plane[4])
{
    frame_render_vertex v;int32_t da=pm_abs(frame_render_plane_distance(a->xyz,plane)),db=pm_abs(frame_render_plane_distance(b->xyz,plane)),den=pm_add(da,db),t=den ? divide(pm_mul(da,4096),den) : 4096;uint32_t i;
    for(i=0;i<3u;i++)v.xyz[i]=pm_add(a->xyz[i],pm_mul(pm_sar(pm_sub(b->xyz[i],a->xyz[i]),12),t));
    lerp_uv(a,b,t,&v);project(s,&v);return v;
}
static frame_render_vertex screen_intersection(const frame_render_clip_settings *s,const frame_render_vertex *a,const frame_render_vertex *b,uint32_t edge)
{
    frame_render_vertex v;int32_t base[3],delta[3],den,numerator,t=0;uint32_t i,axis=edge/2u;
    for(i=0;i<3u;i++){base[i]=pm_sar(a->xyz[i],9);delta[i]=pm_sub(pm_sar(b->xyz[i],9),base[i]);}
    den=pm_sar(dot(s->side[edge],delta),12);numerator=dot(s->side[edge],base);
    if(den){if(numerator>0)numerator=pm_add(numerator,1024);else if(numerator<0)numerator=pm_sub(numerator,1024);t=clamp(pm_neg(divide(numerator,den)),0,4096);}
    for(i=0;i<3u;i++)v.xyz[i]=pm_sar(pm_add(pm_mul(base[i],4096),pm_mul(delta[i],t)),3);
    if(v.xyz[2]<s->near_z)v.xyz[2]=s->near_z;
    lerp_uv(a,b,t,&v);project(s,&v);v.screen[axis]=s->bounds[edge];
    if(axis)v.screen[0]=clamp(v.screen[0],s->bounds[0],s->bounds[1]);
    return v;
}
/*4F7EE0/4F82D0 clip both ends of each original edge independently.
 * Sequential left-then-right Sutherland passes change integer UV rounding. */
static uint32_t screen_edge(const frame_render_clip_settings *s,const frame_render_vertex *a,const frame_render_vertex *b,uint32_t axis,const frame_render_vertex *retained,frame_render_vertex out[2])
{
    int32_t av=a->screen[axis],bv=b->screen[axis],lo=s->bounds[axis*2u],hi=s->bounds[axis*2u+1u];out[0]=*a;
    if(retained)memcpy(out[0].xyz,retained->xyz,sizeof(out[0].xyz));
    if(bv<av) {
        if(av>hi){if(bv>=hi)return 0;out[0]=screen_intersection(s,a,b,axis*2u+1u);}
        if(bv<lo){if(av<lo)return 0;if(av!=lo){out[1]=screen_intersection(s,b,a,axis*2u);return 2;}}
    }else{
        if(av<lo){if(bv<=lo)return 0;out[0]=screen_intersection(s,a,b,axis*2u);}
        if(bv>hi){if(av>hi)return 0;if(av!=hi){out[1]=screen_intersection(s,b,a,axis*2u+1u);return 2;}}
    }return 1;
}
int frame_render_clip_triangle(const frame_render_clip_settings *s,const frame_render_vertex in[3],const int32_t plane[4],frame_render_vertex out[16])
{
    frame_render_vertex a[16],b[16],before_plane[16];uint32_t count=3,i,stage,before_count=0;
    if(!s || !in || !out || s->focal<=0 || s->near_z<=0)return -1;
    memcpy(a,in,3u*sizeof(*a));
    for(i=0;i<3u;i++){if(a[i].xyz[2]>s->far_z)return 0;if(a[i].xyz[2]>=s->near_z)project(s,&a[i]);}
    for(stage=0;stage<4u;stage++) {
        uint32_t n=0;
        if(stage==1u && !plane)continue;
        if(stage==1u){memcpy(before_plane,a,count*sizeof(*a));before_count=count;}
        for(i=0;i<count;i++) {
            const frame_render_vertex *v=&a[i],*next=&a[(i+1u)%count];int inside,other;
            if(stage>=2u){frame_render_vertex ends[2];uint32_t j;const frame_render_vertex *retained=0;
                /*4FBD70 initializes the horizontal slow-edge output from its
                 *pre-plane XYZ array. Its intersection inputs are post-plane.
                 *Keep defined retained slots; for uninitialized original slots
                 *use the computed point instead of depending on host stack. */
                if(stage==2u && plane && i<before_count && (v->screen[0]<s->bounds[0] || v->screen[0]>s->bounds[1] || next->screen[0]<s->bounds[0] || next->screen[0]>s->bounds[1]))retained=&before_plane[i];
                { uint32_t used=screen_edge(s,v,next,stage-2u,retained,ends);if(n+used>16u)return -3;for(j=0;j<used;j++)b[n++]=ends[j]; }continue;}
            if(!stage){inside=v->xyz[2]>=s->near_z;other=next->xyz[2]>=s->near_z;}
            else {inside=frame_render_plane_distance(v->xyz,plane)>=0;other=frame_render_plane_distance(next->xyz,plane)>=0;}
            if(inside){if(n>=16u)return -3;b[n++]=*v;}
            if(inside!=other){if(n>=16u)return -3;b[n++]=!stage ? near_intersection(s,v,next) : inside ? extra_intersection(s,v,next,plane) : extra_intersection(s,next,v,plane);}
        }
        if(n<3u)return 0;
        count=n;memcpy(a,b,count*sizeof(*a));
    }
    memcpy(out,a,count*sizeof(*a));return (int)count;
}
void frame_render_damage_uv(const int32_t point[3],uint32_t sides,uint32_t quantized,uint32_t uv[2])
{
    int32_t u=(sides&3u) ? point[2] : point[0],v=(sides&12u) ? point[2] : pm_mul(point[1],2);
    if(quantized){uv[0]=(uint32_t)(clamp(u,-128,127)+128)*256u;uv[1]=(uint32_t)(clamp(v,-128,127)+128)*256u;}
    else {uv[0]=(uint32_t)(clamp(pm_mul(u,256),-32768,32767)+32768);uv[1]=(uint32_t)(clamp(pm_mul(v,256),-32768,32767)+32768);}
}
void frame_render_cloud_uv(const int32_t camera[3],const int32_t view[9],const int32_t origin[3],uint32_t uv[2])
{
    int32_t ray[3],normal[3],size,q[3];uint32_t i,j;
    for(i=0;i<3u;i++)q[i]=pm_sar(camera[i],12);
    for(i=0;i<3u;i++){int32_t sum=0;for(j=0;j<3u;j++)sum=pm_add(sum,pm_mul(q[j],view[j*3u+i]));ray[i]=normal[i]=pm_sar(sum,12);}
    { int32_t x=pm_abs(normal[0]),y=pm_abs(normal[1]),z=pm_abs(normal[2]);size=x>y ? (x>z ? pm_add(x,pm_add(pm_sar(y,2),pm_sar(z,2))) : pm_add(z,pm_add(pm_sar(x,2),pm_sar(y,2)))) : (y>z ? pm_add(y,pm_add(pm_sar(x,2),pm_sar(z,2))) : pm_add(z,pm_add(pm_sar(x,2),pm_sar(y,2)))); }
    if(size>0)for(i=0;i<3u;i++)normal[i]=size>0x40000 ? divide(normal[i],pm_sar(size,12)) : divide(pm_mul(normal[i],4096),size);
    for(i=0;i<2u;i++){j=i*2u;size=!normal[1] ? 0 : pm_sub(pm_add(divide(pm_mul(normal[j],pm_sub(origin[1],ray[1])),pm_neg(normal[1])),ray[j]),origin[j]);uv[i]=(uint32_t)(clamp(pm_sar(size,2),-32768,32768)+32768);}
}
