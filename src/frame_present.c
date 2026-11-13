#include "frame_present.h"
#include "physics_memory.h"
#include "frame_vehicle_scene.h"
#include "frame_blast_pool.h"
#include "frame_actors.h"
#include "frame_vehicle_render.h"
#include "frame_scenery_render.h"
#include "frame_lamps.h"
#include "frame_render_geometry.h"
#include <string.h>
typedef struct vertex { float x,y,z,u,v; } vertex;
typedef struct scene_polygon { vertex v[3];uint32_t colour,end_colour,texture,mode,count,next; } scene_polygon;
typedef struct track_motion { uint32_t object,sequence,left,right; } track_motion;
typedef struct present {
    physics_memory m;const frame_present_resources *resources;frame_commands *commands;frame_present_stats stats;
    float camera[3],basis[9];unsigned char batch[4095u*32u];uint32_t count,texture,mode,work,blending,polygon_count,sort_bucket;
    vertex transformed[256];
    scene_polygon polygons[65536];uint32_t heads[2][1000],queue_phase;
    frame_data_read source_read;void *source_ctx;
    uint32_t scratch_count;uint32_t scratch_address[16384];unsigned char scratch_value[16384];
    track_motion tracks[16];
    uint32_t mesh_blend,mesh_shaded,self_layer,shield_shape,shield_colour,shield_colour_valid,object_bucket,object_sorted;
    int32_t sort_bias;
    frame_present_fog_settings fog;uint32_t fog_minimum;
    frame_render_clip_settings clip;int32_t mesh_plane[4],cloud_origin[3];uint32_t has_plane,cloud_layer,damage_object,damage_part;
    int32_t fixed_vertices[256][3],mesh_input[9];
} present;
/* Original draw methods publish temporary quads/styles. Redirect every byte
 * into a bounded, frame-local copy-on-write map. The caller supplies no write
 * callback, so even those legacy presentation side effects cannot reach DATA. */
static uint32_t scratch_slot(present *p,uint32_t address)
{ uint32_t i=(address*2654435761u)>>18,n;for (n=0;n<16384u;n++,i=(i+1u)&16383u) if (!p->scratch_address[i] || p->scratch_address[i]==address) return i;return UINT32_MAX; }
static int draw_read(void *ctx,uint32_t address,void *out,uint32_t bytes)
{
    present *p=(present *)ctx;uint32_t i,slot;unsigned char *dst=(unsigned char *)out;
    if (!p->source_read(p->source_ctx,address,out,bytes)) return 0;
    if (!p->scratch_count) return 1;
    for (i=0;i<bytes;i++) { slot=scratch_slot(p,address+i);if (slot!=UINT32_MAX && p->scratch_address[slot]) dst[i]=p->scratch_value[slot]; }return 1;
}
static int draw_write(void *ctx,uint32_t address,const void *in,uint32_t bytes)
{
    present *p=(present *)ctx;const unsigned char *src=(const unsigned char *)in;uint32_t i,slot;
    for (i=0;i<bytes;i++) { if (!address || address+i<address) return 0;slot=scratch_slot(p,address+i);if (slot==UINT32_MAX) return 0;if (!p->scratch_address[slot]) p->scratch_count++;p->scratch_address[slot]=address+i;p->scratch_value[slot]=src[i]; }return 1;
}
int frame_present_read(void *workspace,uint32_t address,void *out,uint32_t bytes)
{ if (!workspace || !out || !((present *)workspace)->source_read) return 0;return draw_read(workspace,address,out,bytes); }
size_t frame_present_workspace_size(void) { return sizeof(present); }
int frame_present_workspace_initialize(void *workspace,size_t bytes)
{ if (!workspace || bytes<sizeof(present)) return -1;memset(workspace,0,sizeof(present));return 0; }
uint32_t frame_present_colour(uint32_t packed,uint32_t textured)
{
    uint32_t r=((packed>>1)&31u)<<3,g=((packed>>6)&31u)<<3,b=((packed>>11)&31u)<<3;
    if (textured) { if ((packed&65535u)==0x7bdeu) return 0xffffffffu;r=(r<<1)&255u;g=(g<<1)&255u;b=(b<<1)&255u; }
    return 0xff000000u|(r<<16)|(g<<8)|b;
}
uint32_t frame_present_mesh_colour(uint32_t packed,uint32_t textured,uint32_t shaded)
{
    uint32_t colour=frame_present_colour(packed,textured);
    return shaded ? (colour&0xff000000u)|((colour&0x00fefefeu)>>1) : colour;
}
uint32_t frame_present_vertex_fog(uint32_t specular,uint32_t depth,uint32_t near_distance,uint32_t far_distance,uint32_t minimum)
{
    uint32_t alpha;if (depth<=near_distance || far_distance<=near_distance) return specular;
    alpha=depth>=far_distance ? minimum : ((far_distance-depth)*(255u-minimum))/(far_distance-near_distance)+minimum;
    return (specular&0xffffffu)|((alpha&255u)<<24);
}
uint32_t frame_present_mesh_clipped_colour(uint32_t packed,uint32_t textured,uint32_t shaded)
{
    uint32_t i,colour=0xff000000u;
    if(!textured || (packed&65535u)==0x7bdeu)return frame_present_mesh_colour(packed,textured,shaded);
    for(i=0;i<3u;i++){uint32_t channel=(packed>>(1u+i*5u))&31u;channel=channel<15u ? channel*16u : 255u;if(shaded)channel>>=1;colour|=channel<<(16u-i*8u);}
    return colour;
}
void frame_present_layer_uv(const int32_t normal[3],const int32_t matrix[9],int32_t uv[2])
{
    uint32_t i,j;for (i=0;i<2u;i++) { int32_t sum=0;for (j=0;j<3u;j++) sum=pm_add(sum,pm_mul(normal[j],matrix[i*6u+j]));uv[i]=pm_add(pm_sar(sum,9),32768); }
}
void frame_present_layer_matrix(const int32_t model[9],const int32_t view[9],uint32_t mode,int32_t out[9])
{
    int32_t joined[9];uint32_t j,k;
    if (mode!=2u) { memcpy(out,model,9u*sizeof(*out));return; }
    for (j=0;j<3u;j++) for (k=0;k<3u;k++) { uint32_t axis;int32_t sum=0;for (axis=0;axis<3u;axis++) sum=pm_add(sum,pm_mul(model[j*3u+axis],view[k*3u+axis]));joined[j*3u+k]=pm_trunc(sum,12); }
    memcpy(out,joined,sizeof(joined));
}
void frame_present_billboard(int32_t points[9],int32_t center_x,int32_t center_z,uint32_t flags,int32_t right_x,int32_t right_z)
{
    uint32_t i,index=(flags&30u)>>1;int32_t x=pm_sar(pm_mul(right_x,(int32_t)index),6),z=pm_sar(pm_mul(right_z,(int32_t)index),6);
    for (i=0;i<3u;i++) { int positive=i==1u || (i==2u && !(flags&32u));uint32_t vx=(uint32_t)(center_x+(positive ? x : -x))&65535u,vz=(uint32_t)(center_z+(positive ? z : -z))&65535u;points[i*3u]=vx>32767u ? (int32_t)vx-65536 : (int32_t)vx;points[i*3u+2u]=vz>32767u ? (int32_t)vz-65536 : (int32_t)vz; }
}
static void word(unsigned char *p,uint32_t v)
{ unsigned i;for (i=0;i<4u;i++) p[i]=(unsigned char)(v>>(8u*i)); }
static void real(unsigned char *p,float v)
{ uint32_t bits;memcpy(&bits,&v,4);word(p,bits); }
static int flush(present *p)
{
    int status;if (!p->count) return 0;
    status=frame_commands_submit(p->commands,4,p->texture,0,p->batch,p->count,0);p->count=0;return status;
}
uint32_t frame_present_bucket(int32_t depth,int32_t bias)
{ int32_t n=pm_add(pm_sar(depth,5),bias);if (n<1) n=1;if (n>998) n=998;return 999u-(uint32_t)n; }
uint32_t frame_present_octant(const int32_t relative[3],const int32_t matrix[9])
{
    int32_t local[3];uint32_t i,j,octant=0;
    for (i=0;i<3u;i++) { local[i]=relative[i];if (matrix) { int32_t sum=0;for (j=0;j<3u;j++) sum=pm_add(sum,pm_mul(relative[j],matrix[i*3u+j]));local[i]=pm_sar(sum,12); }if (pm_neg(local[i])<1) octant|=4u>>i; }return octant;
}
void frame_present_vehicle_order(const int32_t body[3],const int32_t turret[3],uint32_t order[9])
{
    static const unsigned char parts[8][8]={
        {3,5,4,0,6,1,7,0},{3,5,4,0,1,7,6,0},{6,7,1,0,3,5,4,0},{7,1,6,0,3,5,4,0},
        {4,5,3,0,6,1,7,0},{4,5,3,0,1,7,6,0},{6,7,1,0,4,5,3,0},{7,1,6,0,4,5,3,0}};
    uint32_t side=(body[0]>0 ? 4u : 0u)|(body[1]>0 ? 2u : 0u)|(body[2]>0 ? 1u : 0u),i,n=0;
    for (i=0;i<8u;i++) { uint32_t part=parts[side][i];if (part==1u) { order[n++]=turret[2]>0 ? 2u : 1u;order[n++]=turret[2]>0 ? 1u : 2u; }else order[n++]=part; }
}
static int queue_polygon(present *p,const vertex *v,uint32_t count,uint32_t colour,uint32_t end_colour,uint32_t texture,uint32_t mode)
{
    scene_polygon *q;uint32_t id=p->polygon_count;if (id>=65536u || p->sort_bucket>=1000u) return -3;
    q=&p->polygons[id];memcpy(q->v,v,count*sizeof(*v));q->colour=colour;q->end_colour=end_colour;q->texture=texture;q->mode=mode;q->count=count;
    q->next=p->heads[p->queue_phase][p->sort_bucket];p->heads[p->queue_phase][p->sort_bucket]=id;p->polygon_count++;return 0;
}
int frame_present_pipeline(frame_commands *commands,uint32_t texture_flags,uint32_t alpha,uint32_t mode)
{
    uint32_t decal=mode&8u;mode&=~8u;
    if (!commands || mode>5u) return -1;
    frame_commands_state(commands,7,0);frame_commands_state(commands,14,1);frame_commands_state(commands,23,4);frame_commands_state(commands,15,0);
    frame_commands_state(commands,27,mode>=2u);frame_commands_state(commands,19,mode<2u || mode==5u ? 2u : 5u);frame_commands_state(commands,20,mode<2u || mode==5u ? 1u : mode==4u ? 2u : 6u);
    frame_commands_state(commands,21,decal ? 1u : mode!=3u && mode>=2u && alpha ? 4u : 2u);return frame_commands_state(commands,41,texture_flags&1u);
}
static int draw_states(present *p,uint32_t texture,uint32_t mode)
{
    int alpha=texture && p->resources && p->resources->texture_alpha ? p->resources->texture_alpha(p->resources->ctx,texture) : 0;if (alpha<0) return alpha;
    /*44E800's painter profile: Z disabled; ZWRITE1 and LEQUAL are dormant.
     *44C4C0: texture alpha alone uses MODULATE; explicit diffuse alpha uses
     *MODULATEALPHA. SRC/DST are chosen per packet, not per texture name. */
    if (p->m.error) return p->m.error;
    frame_commands_state(p->commands,28,p->fog.enabled);frame_commands_state(p->commands,34,p->fog.rgba);
    return frame_present_pipeline(p->commands,texture ? pm_load(&p->m,texture,2) : 0u,(uint32_t)alpha,mode);
}
static int triangle(present *p,const vertex *v,uint32_t colour,uint32_t texture,uint32_t mode)
{
    unsigned i;float x[3],y[3],w=(float)p->commands->width,h=(float)p->commands->height,f=h*(512.0f/480.0f);int status;
    for (i=0;i<3u;i++) { x[i]=w*.5f+f*v[i].x/v[i].z;y[i]=h*.5f-f*v[i].y/v[i].z; }
    if ((x[0]<0 && x[1]<0 && x[2]<0) || (x[0]>w && x[1]>w && x[2]>w) || (y[0]<0 && y[1]<0 && y[2]<0) || (y[0]>h && y[1]>h && y[2]>h)) return 0;
    if (!p->blending) return queue_polygon(p,v,3,colour,colour,texture,mode);
    if (p->count && (texture!=p->texture || mode!=p->mode || p->count+3u>4095u)) { status=flush(p);if (status) return status; }
    p->texture=texture;p->mode=mode;
    status=draw_states(p,texture,mode);if (status) return status;
    for (i=0;i<3u;i++) {
        unsigned char *out=p->batch+p->count++*32u;
        real(out,x[i]);real(out+4,y[i]);real(out+8,1.0f-4.0f/v[i].z);real(out+12,1.0f/v[i].z);
        word(out+16,colour);word(out+20,p->fog.enabled ? frame_present_vertex_fog(0xff000000u,(uint32_t)(int32_t)(v[i].z*8.0f),p->fog.near_distance,p->fog.far_distance,p->fog_minimum) : 0xff000000u);real(out+24,v[i].u);real(out+28,v[i].v);
    }
    p->stats.triangles++;return 0;
}
static int clipped(present *p,vertex in[3],uint32_t colour,uint32_t texture,uint32_t mode)
{
    vertex out[4];uint32_t i,n=0;int status;
    for (i=0;i<3u;i++) {
        vertex a=in[i],b=in[(i+1u)%3u];int ai=a.z>=4.0f,bi=b.z>=4.0f;
        if (ai) out[n++]=a;
        if (ai!=bi) { float t=(4.0f-a.z)/(b.z-a.z);out[n++]=(vertex){a.x+t*(b.x-a.x),a.y+t*(b.y-a.y),4.0f,a.u+t*(b.u-a.u),a.v+t*(b.v-a.v)}; }
    }
    if (n!=3u) p->stats.clipped++;
    if (n<3u) return 0;
    if (n==4u) { vertex tail[3]={out[0],out[2],out[3]};status=triangle(p,tail,colour,texture,mode);if (status) return status; }
    return triangle(p,out,colour,texture,mode);
}
static vertex transform(present *p,const float v[3],const float position[3],const float matrix[9])
{
    float world[3];vertex out={0,0,0,0,0};uint32_t j,k;
    for (j=0;j<3u;j++) { world[j]=position[j]-p->camera[j];for (k=0;k<3u;k++) world[j]+=v[k]*matrix[k*3u+j]; }
    for (j=0;j<3u;j++) { out.x+=world[j]*p->basis[j];out.y+=world[j]*p->basis[3u+j];out.z+=world[j]*p->basis[6u+j]; }return out;
}

static uint32_t texture_of(present *p,uint32_t material)
{ return material ? pm_load(&p->m,material+16u,4) : 0; }
static uint32_t doubled(uint32_t value) { return value<128u ? value*2u : 255u; }
static uint32_t sprite_colour(present *p,uint32_t at,int half)
{
    uint32_t r=doubled(pm_load(&p->m,at+36u,1)),g=doubled(pm_load(&p->m,at+37u,1)),b=doubled(pm_load(&p->m,at+38u,1));
    uint32_t kind=pm_load(&p->m,at+16u,4),alpha=kind!=UINT32_MAX && ((half && !(pm_load(&p->m,0x5964d4u,4)&32768u)) || !kind) ? 128u : 255u;
    /* D3D's PC smoke/glow sprites use brightness as alpha and white diffuse. */
    if (!(pm_load(&p->m,0x5964d4u,4)&32768u) && (kind==3u || (kind==1u && pm_load(&p->m,at,4)==pm_load(&p->m,0x54b494u,4)))) { alpha=kind==3u ? ((r+g+b)/3u)>>2 : (pm_load(&p->m,at+36u,1)+pm_load(&p->m,at+37u,1)+pm_load(&p->m,at+38u,1))/3u;r=g=b=255; }
    return (alpha<<24)|(r<<16)|(g<<8)|b;
}
static int sprite(present *p,uint32_t at,uint32_t position,int half)
{
    static const float identity[9]={1,0,0,0,1,0,0,0,1};
    physics_memory *m=&p->m;float pos[3],zero[3]={0,0,0},uv[4],dim;vertex center,q[4],t[3];uint32_t i,angle,texture,kind,mode,colour;int status;
    int32_t camera[3],sx[4],sy[4],cx,cy,hw,hh,denominator,sine=0,cosine=4096;
    texture=texture_of(p,pm_load(m,at,4));if (!texture) return m->error;
    for (i=0;i<3u;i++) pos[i]=(float)pm_sar(pm_i32(m,(position ? position : at+4u)+i*4u),3);
    center=transform(p,pos,zero,identity);
    /*4FD9B0 quantizes the camera center and projected half extents before
     *rotating in quarter-pixel screen space. A float-sized world billboard
     *changes every filtered texel of a small projectile/glow. */
    for (i=0;i<3u;i++) { uint32_t j;int32_t sum=0;for (j=0;j<3u;j++) sum=pm_add(sum,pm_mul((int32_t)(pos[j]-p->camera[j]),(int32_t)(p->basis[i*3u+j]*4096.0f)));camera[i]=pm_sar(sum,12); }
    if (camera[2]<4 || pm_i32(m,at+24u)<=0 || pm_i32(m,at+28u)<=0) return m->error;
    center.z=(float)camera[2];denominator=pm_mul(camera[2],4096)/(int32_t)(p->commands->height*2048u/480u);if (denominator<=0) return m->error;
    hw=(int32_t)((pm_load(m,at+24u,4)<<9)/(uint32_t)denominator);hh=(int32_t)((pm_load(m,at+28u,4)<<9)/(uint32_t)denominator);
    cx=pm_mul(camera[0],4096)/denominator+(int32_t)p->commands->width*2+2;cy=(int32_t)p->commands->height*2+2-pm_mul(camera[1],4096)/denominator;
    angle=pm_load(m,at+40u,2)&65532u;if (angle) { sine=pm_i32(m,0x597380u+angle);cosine=pm_i32(m,0x59b380u+angle); }
    { int32_t sw=pm_mul(sine,hw),sh=pm_mul(sine,hh),cw=pm_mul(cosine,hw),ch=pm_mul(cosine,hh);
      sx[0]=pm_add(cx,pm_sar(pm_sub(sh,cw),12));sy[0]=pm_add(cy,pm_sar(pm_neg(pm_add(sw,ch)),12));
      sx[1]=pm_add(cx,pm_sar(pm_add(sh,cw),12));sy[1]=pm_add(cy,pm_sar(pm_sub(sw,ch),12));
      sx[2]=pm_add(cx,pm_sar(pm_sub(cw,sh),12));sy[2]=pm_add(cy,pm_sar(pm_add(sw,ch),12));
      sx[3]=pm_add(cx,pm_sar(pm_neg(pm_add(sh,cw)),12));sy[3]=pm_add(cy,pm_sar(pm_sub(ch,sw),12)); }
    dim=(float)pm_load(m,texture+2u,2);if (!dim) return -4;
    /*4FD9B0 uses4F9560's material-size UV shift, unlike mesh UV's fixed8. */
    for (i=0;i<4u;i++) uv[i]=(float)pm_load(m,at+32u+i,1)/dim;
    kind=pm_load(m,at+16u,4);mode=kind==UINT32_MAX ? ((pm_load(m,texture,2)&2u) ? 3u : (pm_load(m,texture,2)&1u) ? 1u : 0u) : ((kind==1u || kind==3u) && (pm_load(m,0x5964d4u,4)&32768u)) ? 4u : 2u;colour=sprite_colour(p,at,half);
    p->sort_bucket=frame_present_bucket((int32_t)center.z,pm_i32(m,at+20u));
    for (i=0;i<4u;i++) { float f=(float)p->commands->height*(512.0f/480.0f);q[i]=(vertex){((float)sx[i]*.25f-(float)p->commands->width*.5f)*center.z/f,((float)p->commands->height*.5f-(float)sy[i]*.25f)*center.z/f,center.z,(i==0u || i==3u) ? uv[0] : uv[2],i<2u ? uv[1] : uv[3]}; }
    for (i=2u;i;i--) { t[0]=q[0];t[1]=q[i];t[2]=q[i+1u];status=clipped(p,t,colour,texture,mode);if (status) return status; }return m->error;
}
static int sprite_sink(void *ctx,uint32_t at) { present *p=(present *)ctx;return sprite(p,at,0,pm_load(&p->m,0x55ae2cu,4)!=0u); }

/* Build a caller-owned particle descriptor through the same sprite path.
 * Save/restore the shared scratch template even inside the private overlay. */
static int particle(present *p,uint32_t material,const int32_t position[3],int32_t width,int32_t height,uint32_t kind,uint32_t colour,uint32_t angle,const unsigned char uv[4])
{
    physics_memory m={draw_read,draw_write,p,0};unsigned char previous[42];uint32_t at=0x545de8u,i;int status;
    if (!material || width<=0 || height<=0) return 0;
    if (!draw_read(p,at,previous,42)) return -4;
    pm_store(&m,at,material,4);for (i=0;i<3u;i++) pm_write32(&m,at+4u+i*4u,position[i]);
    pm_store(&m,at+16u,kind,4);pm_store(&m,at+20u,0,4);pm_write32(&m,at+24u,width);pm_write32(&m,at+28u,height);
    for (i=0;i<4u;i++) pm_store(&m,at+32u+i,uv[i],1);
    for (i=0;i<3u;i++) pm_store(&m,at+36u+i,(colour>>(16u-i*8u))&255u,1);
    pm_store(&m,at+40u,angle,2);status=m.error ? m.error : sprite(p,at,0,0);
    if (!draw_write(p,at,previous,42)) return -3;
    return status;
}

static int track_uv(present *p,uint32_t o)
{
    physics_memory m={draw_read,draw_write,p,0};track_motion *motion=0;uint32_t side,i,j,seq=p->commands->sequence,steps=0;
    int32_t speed=pm_i32(&m,o+0xa4u),turn=pm_trunc(pm_mul(pm_i32(&m,o+0xb8u),400),10);
    for (i=0;i<16u;i++) if (p->tracks[i].object==o || !p->tracks[i].object) { motion=&p->tracks[i];break; }
    if (!motion) return -3;
    if (!motion->object) { motion->object=o;motion->sequence=seq;motion->left=pm_load(&m,o+0xd84u,4);motion->right=pm_load(&m,o+0xd88u,4); }
    else { steps=seq-motion->sequence;motion->sequence=seq;if (steps>8u) steps=8u; }
    if (speed>300000) speed=300000;
    if (speed<-300000) speed=-300000;
    if (pm_load(&m,o+0x10bcu,1) || pm_load(&m,0x53e96cu,4)) steps=0;
    motion->left+=(uint32_t)pm_sub(speed,turn)*steps;motion->right+=(uint32_t)pm_add(speed,turn)*steps;
    /*434C60: wheel atlas rows32 apart, tread rows2 apart. Native UV pointers
     * and base rows come from the original model's two track descriptors. */
    for (side=0;side<2u;side++) {
        uint32_t at=o+(side ? 0xf4eu : 0xe22u),phase=(side ? motion->right : motion->left)>>17;
        static const uint32_t counts[]={296,297,298,299},pointers[]={8,32,56,152},bases[]={0,0,248,272};
        for (j=0;j<4u;j++) {
            uint32_t count=pm_load(&m,at+counts[j],1);if (count>(j<2u ? 6u : 24u)) return -4;
            for (i=0;i<count;i++) { uint32_t value=j<2u ? (phase&7u)*32u+(j ? 31u : 0u) : pm_load(&m,at+bases[j]+i,1)+(phase%5u)*2u;pm_store(&m,pm_load(&m,at+pointers[j]+i*4u,4),value,1); }
        }
    }return m.error;
}
static int vehicle_mesh_sink(void *,uint32_t,const int32_t[3],const int32_t[12],uint32_t);
static int tank_particles(present *p,uint32_t o)
{
    physics_memory *m=&p->m;uint32_t i,j,at,material;int status;int32_t pos[3];static const unsigned char uv[4]={0,0,63,63};
    frame_vehicle_render_services graphics={sprite_sink,vehicle_mesh_sink,p};
    status=frame_vehicle_orbs_draw(draw_read,draw_write,p,o,&graphics);if (status) return status;
    /*439E30 damage/tyre smoke:15 packed particles; brightness/life are native. */
    if (pm_load(m,o+0x984u,4)) for (i=0;i<15u;i++) {
        int32_t size;at=o+0x798u+i*32u;if (pm_i16(m,at+24u)<=0) continue;
        for (j=0;j<3u;j++) pos[j]=pm_i32(m,at+j*4u);
        size=pm_sar(pm_i16(m,at+26u),4);{ uint32_t gray=(uint32_t)pm_sar(pm_i16(m,at+24u),4)&255u;status=particle(p,pm_load(m,0x5466a4u,4),pos,size,size,1u,gray*0x10101u,pm_load(m,at+28u,2),uv);if (status) return status; }
    }
    /* Track spray has eight native particles and a23-byte surface style. */
    if (pm_load(m,o+0xaa8u,4)) for (i=0;i<8u;i++) {
        uint32_t cfg,colour=0;int32_t size;at=o+0x990u+i*34u;if (pm_i16(m,at+24u)<=0) continue;
        cfg=0x5321b0u+pm_load(m,at+32u,1)*23u;material=pm_load(m,cfg+19u,4);
        for (j=0;j<3u;j++) { pos[j]=pm_i32(m,at+j*4u);colour=(colour<<8)|pm_load(m,cfg+j,1); }
        size=pm_trunc(pm_i16(m,at+26u),1);{ unsigned char spray_uv[4]={1,0,63,63};if (pm_load(m,at+33u,1)) { spray_uv[0]=63;spray_uv[2]=1; }status=particle(p,material,pos,size,size,pm_load(m,cfg+5u,2),colour,pm_load(m,at+30u,2),spray_uv);if (status) return status; }
    }
    /*43C260 landing dirt;43C650 is electric-mine glow, not the invincibility pickup. */
    if (pm_load(m,o+0xc92u,4)) for (i=0;i<8u;i++) {
        uint32_t colour=0;int32_t size;at=0x546460u+pm_load(m,o+0xde5u,1)*264u+i*32u;if (!pm_load(m,at+27u,1)) continue;
        for (j=0;j<3u;j++) { pos[j]=pm_i32(m,at+12u+j*4u);colour=(colour<<8)|pm_load(m,at+24u+j,1); }
        size=pm_i32(m,at+28u);status=particle(p,pm_load(m,0x546670u,4),pos,size,size,1u,colour,0,uv);if (status) return status;
    }
    status=frame_vehicle_mine_glow_draw(draw_read,draw_write,p,o,&graphics);if (status) return status;
    if (pm_load(m,0x5461a8u,4)) { status=sprite(p,0x545fb0u,o+0x30eu,0);if (status) return status; }
    return m->error;
}
static int explosion_debris(present *p,uint32_t o)
{
    physics_memory *m=&p->m;uint32_t count=pm_load(m,o+0x579u,1),owner,record,i,j,at;unsigned char uv[4];int status;
    if (!count) return m->error;
    if (count>15u) return -4;
    record=pm_load(m,pm_load(m,0x55c694u,4)+pm_load(m,o+0x36bu,4)*8u,4);owner=record ? pm_load(m,record+4u,4) : 0;if (!owner) return m->error;
    for (i=0;i<4u;i++) uv[i]=(unsigned char)pm_load(m,o+0x571u+i,1);
    for (i=0;i<count;i++) { int32_t pos[3];at=o+0x36fu+i*34u;for (j=0;j<3u;j++) pos[j]=pm_i32(m,at+j*4u);
        status=particle(p,pm_load(m,o+0x56du,4),pos,pm_i16(m,at+24u),pm_i16(m,at+26u),UINT32_MAX,0x808080u,pm_load(m,at+32u,2),uv);if (status) return status;
    }return m->error;
}
uint32_t frame_present_quad_colour(uint32_t r,uint32_t g,uint32_t b,uint32_t clipped,uint32_t textured)
{
    uint32_t rr=r<128u ? r&248u : 120u,gg=g<128u ? g&248u : 120u,bb=b<128u ? b&248u : 120u;
    if (r==128u && g==128u && b==128u) return 0xffffffffu;
    if (clipped && textured) { if (rr==120u && gg==120u && bb==120u) return 0xffffffffu;rr=rr<120u ? rr*2u : 255u;gg=gg<120u ? gg*2u : 255u;bb=bb<120u ? bb*2u : 255u; }
    return 0xff000000u|(rr<<16)|(gg<<8)|bb;
}
uint32_t frame_present_quad_mode(uint32_t style,uint32_t clipped,uint32_t capability,uint32_t flags)
{
    uint32_t packet=0,explicit_alpha=style!=255u && (!clipped || style<=3u);
    if (clipped) { if (style<=3u) packet=style==1u && !capability ? 1u : style==3u ? 2u : style+1u; }
    else if (explicit_alpha) packet=style==1u && !capability ? 1u : (style+1u)&3u;
    if (!packet && (flags&2u)) return explicit_alpha ? 2u : 3u;
    if (packet==2u) return 4u;
    if (packet) return 2u;
    if (explicit_alpha) return 5u; /* Enabled ONE/ZERO is a real original branch. */
    return flags&1u ? 1u : 0u;
}
static int quad_pool(present *p)
{
    static const float identity[9]={1,0,0,0,1,0,0,0,1};static const float zero[3]={0,0,0};
    physics_memory *m=&p->m;uint32_t i,j,k,at,texture,kind,colour,mode,pc,clip;int status;
    if (!pm_load(m,0x55ae1cu,4)) return m->error;
    for (i=0;i<160u;i++) {
        vertex q[4],t[3];float v[3],dim=256;
        at=0x54cc88u+i*64u;if (pm_load(m,at+4u,4)==0xe0000000u) continue;
        texture=texture_of(p,pm_load(m,at+48u,4));if (texture) { dim=(float)pm_load(m,texture+2u,2);if (!dim) return -4; }
        kind=pm_load(m,at+52u,1);pc=!(pm_load(m,0x5964d4u,4)&32768u);
        for (j=0;j<4u;j++) { for (k=0;k<3u;k++) v[k]=(float)pm_i32(m,at+j*12u+k*4u);q[j]=transform(p,v,zero,identity);q[j].u=(float)pm_load(m,at+56u+j*2u,1)/dim;q[j].v=(float)pm_load(m,at+57u+j*2u,1)/dim; }
        { float f=(float)p->commands->height*(512.0f/480.0f);clip=0;for (j=0;j<4u;j++) if (q[j].z<4 || q[j].x*f<-(float)p->commands->width*.5f*q[j].z || q[j].x*f>(float)p->commands->width*.5f*q[j].z || q[j].y*f<-(float)p->commands->height*.5f*q[j].z || q[j].y*f>(float)p->commands->height*.5f*q[j].z) clip=1;
            colour=frame_present_quad_colour(pm_load(m,at+53u,1),pm_load(m,at+54u,1),pm_load(m,at+55u,1),clip,texture!=0u); }
        /*4FED90's direct quad packet adds half a texel after4F9560's UV
         *shift. Its4FA370 clipped triangles deliberately do not add it. */
        if (texture && !clip) for (j=0;j<4u;j++) { q[j].u+=.5f/dim;q[j].v+=.5f/dim; }
        /* Match the integer quarter-pixel projection of4FED90 before GPU
         *rasterization. Keep world Z so UV interpolation stays perspective. */
        for (j=0;j<4u;j++) if (q[j].z>=4) {
            int32_t d=(int32_t)(q[j].z*4096.0f)/(int32_t)(p->commands->height*2048u/480u);float f=(float)p->commands->height*(512.0f/480.0f);
            if (d>0) { int32_t x=(int32_t)(q[j].x*4096.0f)/d+(int32_t)p->commands->width*2+2,y=(int32_t)p->commands->height*2+2-(int32_t)(q[j].y*4096.0f)/d;q[j].x=((float)x*.25f-(float)p->commands->width*.5f)*q[j].z/f;q[j].y=((float)p->commands->height*.5f-(float)y*.25f)*q[j].z/f; }
        }
        mode=frame_present_quad_mode(kind,clip,!pc,texture ? pm_load(m,texture,2) : 0u);
        if (!kind || (kind==1u && pc)) colour=(colour&0xffffffu)|0x80000000u;
        if (kind==3u) colour=(colour&0xffffffu)|0x40000000u;
        { float far=q[0].z;for (j=1;j<4u;j++) if (q[j].z>far) far=q[j].z;p->sort_bucket=frame_present_bucket((int32_t)far,-4); }
        for (j=2u;j;j--) { t[0]=q[0];t[1]=q[j];t[2]=q[j+1u];status=clipped(p,t,colour,texture,mode);if (status) return status; }
    }return m->error;
}
static int line_pool(present *p)
{
    static const float identity[9]={1,0,0,0,1,0,0,0,1},zero[3]={0,0,0};
    physics_memory *m=&p->m;uint32_t i,j,k,at,kind,colour[2],mode;int status;
    if (!pm_load(m,0x55ae20u,4)) return m->error;
    status=flush(p);if (status) return status;
    for (i=0;i<100u;i++) {
        vertex v[2];float point[3];
        at=0x54f490u+i*32u;if (pm_load(m,at+4u,4)==0xe0000000u) continue;
        for (j=0;j<2u;j++) { for (k=0;k<3u;k++) point[k]=(float)pm_i32(m,at+j*12u+k*4u);v[j]=transform(p,point,zero,identity); }
        if (v[0].z<4 && v[1].z<4) continue;
        if (v[0].z<4 || v[1].z<4) { uint32_t a=v[0].z<4 ? 0u : 1u,b=1u-a;float t=(4-v[a].z)/(v[b].z-v[a].z);v[a].x+=t*(v[b].x-v[a].x);v[a].y+=t*(v[b].y-v[a].y);v[a].z=4; }
        kind=pm_load(m,at+27u,1);mode=kind==255u ? 0u : kind==1u ? 4u : 2u;
        for (j=0;j<2u;j++) colour[j]=(kind==0u ? 0x80000000u : kind==3u ? 0x40000000u : 0xff000000u)|(pm_load(m,at+24u+j*4u,1)<<16)|(pm_load(m,at+25u+j*4u,1)<<8)|pm_load(m,at+26u+j*4u,1);
        p->sort_bucket=pm_load(m,at+31u,1) ? 1u : 998u;
        status=queue_polygon(p,v,2,colour[0],colour[1],0,mode);if (status) return status;
    }return m->error;
}
/*502DF0 panorama:10 half-screen tiles,100/300 high texture bands,
 *80-unit horizon at reference480px height. Pitch/roll affect only drawing. */
static int background_quad(present *p,float left,float right,float top,float bottom,uint32_t ctop,uint32_t cbottom,uint32_t material,uint32_t uv_address)
{
    unsigned char out[192];static const unsigned corners[]={0,1,2,0,2,3};uint32_t i,texture=texture_of(p,material);float h=(float)p->commands->height/480.0f,w=(float)p->commands->width*.5f,cy=(float)p->commands->height*.5f;
    float roll_s=p->basis[1],roll_c=1.0f-roll_s*roll_s*.5f,pitch=p->basis[7]*512.0f;float uv[4]={0,0,0,0};
    if (texture) for (i=0;i<4u;i++) uv[i]=((float)pm_load(&p->m,uv_address+i,1)+.5f)/256.0f;
    for (i=0;i<6u;i++) { unsigned n=corners[i];float x=(n==0u || n==3u) ? left : right,y=n<2u ? top : bottom;unsigned char *dst=out+i*32u;real(dst,w+(x*roll_c-y*roll_s)*h);real(dst+4,cy+(x*roll_s+y*roll_c+pitch)*h);real(dst+8,1);real(dst+12,1);word(dst+16,n<2u ? ctop : cbottom);word(dst+20,0xff000000u);real(dst+24,(n==0u || n==3u) ? uv[0] : uv[2]);real(dst+28,n<2u ? uv[1] : uv[3]); }
    { int alpha=texture && p->resources && p->resources->texture_alpha ? p->resources->texture_alpha(p->resources->ctx,texture) : 0;if (alpha<0) return alpha;frame_commands_state(p->commands,21,alpha ? 4u : 2u); }
    frame_commands_state(p->commands,41,texture && (pm_load(&p->m,texture,2)&1u));
    frame_commands_state(p->commands,27,texture!=0u);frame_commands_state(p->commands,19,5);frame_commands_state(p->commands,20,6);
    return frame_commands_submit(p->commands,4,texture,0,out,6,0);
}
static uint32_t rgb_at(present *p,uint32_t at)
{ return 0xff000000u|(pm_load(&p->m,at,1)<<16)|(pm_load(&p->m,at+1u,1)<<8)|pm_load(&p->m,at+2u,1); }
static int background(present *p)
{
    physics_memory *m=&p->m;uint32_t i,tile,at;int status;int32_t x=(int32_t)(-p->basis[6]*4096),z=(int32_t)(p->basis[8]*4096),a=x<0 ? -x : x,b=z<0 ? -z : z,yaw;
    float shift;int active=0;
    for (i=0;i<10u;i++) if (pm_load(m,0x5367b0u+i*32u,4) || pm_load(m,0x5367b4u+i*32u,4) || pm_load(m,0x5367c0u+i*32u,4)) active=1;
    if (!active) return m->error;
    yaw=!a ? 0 : !b ? 16384 : a==b ? 8192 : a<b ? pm_i32(m,0x596f80u+((uint32_t)a*256u/(uint32_t)b)*4u) : 16384-pm_i32(m,0x596f80u+((uint32_t)b*256u/(uint32_t)a)*4u);
    if (z<0) yaw=32768-yaw;
    if (x<0) yaw=-yaw;
    shift=(float)((65536u-(uint32_t)yaw)&65535u)*10.0f/65536.0f;tile=(uint32_t)shift;shift=-(shift-(float)tile)*320.0f;
    frame_commands_state(p->commands,7,0);frame_commands_state(p->commands,14,0);frame_commands_state(p->commands,15,0);frame_commands_state(p->commands,27,0);
    for (i=0;i<5u;i++) { float left=shift+((float)i-2)*320;at=0x5367b0u+((tile+i+8u)%10u)*32u;status=background_quad(p,left,left+320,-600,600,rgb_at(p,at+16u),rgb_at(p,at+28u),0,0);if (status) return status; }
    for (i=0;i<5u;i++) { float left=shift+((float)i-2)*320;at=0x5367b0u+((tile+i+8u)%10u)*32u;if (pm_load(m,at,4)) { status=background_quad(p,left,left+320,-20,80,0xffffffffu,0xffffffffu,pm_load(m,at,4),at+8u);if (status) return status; }if (pm_load(m,at+4u,4)) { status=background_quad(p,left,left+320,-320,-20,0xffffffffu,0xffffffffu,pm_load(m,at+4u,4),at+12u);if (status) return status; } }
    frame_commands_state(p->commands,7,0);frame_commands_state(p->commands,14,1);return m->error;
}
static uint32_t mesh_face_clipped(present *p,const vertex tri[3])
{
    uint32_t i;for(i=0;i<3u;i++){
        int32_t z=p->mesh_input[i*3u+2u],d,x,y;(void)tri;if(z<p->clip.near_z || z>p->clip.far_z)return 1;
        d=z/p->clip.focal;if(!d)return 1;x=p->mesh_input[i*3u]/d+p->clip.center[0];y=p->clip.center[1]-p->mesh_input[i*3u+1u]/d;
        if(x<0 || x>p->clip.bounds[1] || y<0 || y>p->clip.bounds[3])return 1;
    }return 0;
}
static int mesh_submit(present *p,vertex tri[3],uint32_t colour,uint32_t texture,uint32_t mode,uint32_t cloud)
{
    frame_render_vertex in[3],out[16];uint32_t i,j;int count,status;float f=(float)p->commands->height*(512.0f/480.0f);
    for(i=0;i<3u;i++){memcpy(in[i].xyz,p->mesh_input+i*3u,sizeof(in[i].xyz));in[i].uv[0]=(uint32_t)(tri[i].u*65536.0f);in[i].uv[1]=(uint32_t)(tri[i].v*65536.0f);}
    count=frame_render_clip_triangle(&p->clip,in,p->has_plane ? p->mesh_plane : 0,out);if(count<0)return count;
    if(count!=3)p->stats.clipped++;
    for(i=0;i<(uint32_t)count;i++){
        if(cloud){int32_t view[9];for(j=0;j<9u;j++)view[j]=(int32_t)(p->basis[j]*4096.0f);frame_render_cloud_uv(out[i].xyz,view,p->cloud_origin,out[i].uv);}
    }
    for(i=1;i+1u<(uint32_t)count;i++){
        uint32_t ids[3]={0,i,i+1u};vertex polygon[3];
        for(j=0;j<3u;j++){frame_render_vertex *v=&out[ids[j]];float z=(float)pm_sar(v->xyz[2],9)/8.0f;if(z<4)z=4;polygon[j]=(vertex){((float)v->screen[0]*.25f-(float)p->commands->width*.5f)*z/f,((float)p->commands->height*.5f-(float)v->screen[1]*.25f)*z/f,z,(float)v->uv[0]/65536.0f,(float)v->uv[1]/65536.0f};}
        status=triangle(p,polygon,colour,texture,mode);if(status)return status;
    }return 0;
}
static void vehicle_clip_plane(present *p,uint32_t object)
{
    uint32_t i,j;int32_t normal[3],sum=0;
    p->has_plane=pm_load(&p->m,object+0xdeeu,1)!=0;if(!p->has_plane)return;
    for(i=0;i<3u;i++){normal[i]=pm_i32(&p->m,object+0x26eu+i*4u);sum=pm_add(sum,pm_mul(normal[i],(int32_t)p->camera[i]));}
    p->mesh_plane[3]=pm_add(pm_sar(sum,12),pm_i32(&p->m,object+0x27au));
    for(i=0;i<3u;i++){sum=0;for(j=0;j<3u;j++)sum=pm_add(sum,pm_mul(normal[j],(int32_t)(p->basis[i*3u+j]*4096.0f)));p->mesh_plane[i]=pm_sar(sum,12);}
}
static int mesh(present *p,uint32_t shape,const float position[3],const float matrix[9])
{
    physics_memory *m=&p->m;vertex *transformed=p->transformed;uint32_t n,faces,vertices,polys,i,j,index,at,material,texture,colour,flags,mode,order=0,face;int32_t model[9],view[9],combined[9],translation[3];int status;
    if (!shape) return 0;
    n=pm_load(m,shape,2);faces=pm_load(m,shape+2u,2);vertices=pm_load(m,shape+4u,4);polys=pm_load(m,shape+8u,4);
    if (m->error) return m->error;
    if (!n || !faces) return 0;
    if (n>256u || !vertices || !polys) return -4;
    p->stats.meshes++;
    for(i=0;i<9u;i++){model[i]=(int32_t)(matrix[i]*4096.0f);view[i]=(int32_t)(p->basis[i]*4096.0f);}
    frame_present_layer_matrix(model,view,2,combined);
    for(i=0;i<3u;i++){int32_t sum=0;for(j=0;j<3u;j++)sum=pm_add(sum,pm_mul((int32_t)position[j]-(int32_t)p->camera[j],view[i*3u+j]));translation[i]=sum;}
    if (p->object_sorted) { int32_t relative[3],fixed[9];for (j=0;j<3u;j++) relative[j]=(int32_t)(position[j]-p->camera[j]);for (j=0;j<9u;j++) fixed[j]=(int32_t)(matrix[j]*4096.0f);order=pm_load(m,shape+40u+frame_present_octant(relative,fixed)*4u,4);if (!order) return m->error ? m->error : -4; }
    for (i=0;i<n;i++) {
        int32_t point[3];for (j=0;j<3u;j++) point[j]=pm_i16(m,vertices+i*8u+j*2u);
        frame_render_transform(point,combined,translation,p->fixed_vertices[i]);transformed[i]=(vertex){(float)p->fixed_vertices[i][0]/4096.0f,(float)p->fixed_vertices[i][1]/4096.0f,(float)p->fixed_vertices[i][2]/4096.0f,0,0};
    }
    for (i=0;i<faces;i++) {
        vertex tri[3];if (p->work++>=200000u) return -3;
        face=order ? pm_load(m,order+i,1) : i;if (order && face==255u) break;if (face>=faces) return -4;
        at=polys+face*24u;
        /* Broken mesh triangles retain a disabled marker in their flags. */
        flags=pm_load(m,at+18u,2);if (flags&64u) continue;
        for (j=0;j<3u;j++) { index=pm_load(m,at+8u+j,1);if (index>=n) return -4;tri[j]=transformed[index];memcpy(p->mesh_input+j*3u,p->fixed_vertices[index],3u*sizeof(int32_t)); }
        if (flags&30u) {
            int32_t points[9];uint32_t center_x=pm_load(m,shape+16u,4),center_z=pm_load(m,shape+24u,4);
            if (!center_x || !center_z) return -4;
            for (j=0;j<3u;j++) { index=pm_load(m,at+8u+j,1);points[j*3u+1u]=pm_i16(m,vertices+index*8u+2u); }
            frame_present_billboard(points,pm_i16(m,center_x+face*2u),pm_i16(m,center_z+face*2u),flags,(int32_t)(p->basis[0]*4096.0f),(int32_t)(p->basis[2]*4096.0f));
            for (j=0;j<3u;j++) { frame_render_transform(points+j*3u,combined,translation,p->mesh_input+j*3u);tri[j]=(vertex){(float)p->mesh_input[j*3u]/4096.0f,(float)p->mesh_input[j*3u+1u]/4096.0f,(float)p->mesh_input[j*3u+2u]/4096.0f,0,0}; }
        }
        else if (!(flags&1u)) {
            int32_t normal[3],rotated[3],zero[3]={0,0,0},dot=0;
            for (j=0;j<3u;j++) normal[j]=pm_i16(m,at+j*2u);
            frame_render_transform(normal,combined,zero,rotated);
            for (j=0;j<3u;j++) dot=pm_add(dot,pm_mul(pm_sar(p->mesh_input[j],9),pm_sar(rotated[j],12)));
            if (dot>=0) continue;
        }
        for (j=0;j<3u;j++) { tri[j].u=(float)pm_load(m,at+12u+j*2u,1)/256.0f;tri[j].v=(float)pm_load(m,at+13u+j*2u,1)/256.0f; }
        material=pm_load(m,at+20u,4);texture=material ? pm_load(m,material+16u,4) : 0;
        mode=texture ? pm_load(m,texture,2)&3u : 0;if (mode==2u) mode=3u;
        colour=(!p->has_plane && mesh_face_clipped(p,tri) ? frame_present_mesh_clipped_colour : frame_present_mesh_colour)(pm_load(m,at+6u,2),texture!=0u,p->mesh_shaded);
        if (p->mesh_blend) { mode=p->mesh_blend==2u ? 4u : 2u;if (p->mesh_blend==1u) colour=(colour&0xffffffu)|0x80000000u; }
        if (p->object_sorted) p->sort_bucket=p->object_bucket;
        else { float far=tri[0].z;int32_t bias=(int32_t)((flags&0xe00u)>>9);if (tri[1].z>far) far=tri[1].z;if (tri[2].z>far) far=tri[2].z;if (!(flags&0x1000u)) bias=-bias;p->sort_bucket=frame_present_bucket((int32_t)far,bias); }
        if (m->error) return m->error;
        if(p->cloud_layer){uint32_t cloud_texture=texture_of(p,pm_load(m,0x54b490u,4));if(cloud_texture){status=mesh_submit(p,tri,0x80ffffffu,cloud_texture,2,1);if(status)return status;}}
        if(p->damage_object && !p->mesh_blend){uint32_t sides=pm_load(m,at+11u,1),selected=0,mask=0,k,damage,damage_texture;
            for(k=0;k<6u;k++){damage=pm_load(m,p->damage_object+0x28eu+p->damage_part*104u+k,1);if(damage)mask|=1u<<k;}
            if(sides&mask){vertex layer[3];uint32_t key=sides;while(key>1u){selected++;key>>=1;}if(selected>5u)return -4;
                damage=pm_load(m,p->damage_object+0x28eu+p->damage_part*104u+selected,1);damage_texture=texture_of(p,pm_load(m,damage<4u ? 0x5466b8u : 0x5466bcu,4));
                if(damage_texture){for(j=0;j<3u;j++){int32_t point[3];uint32_t uv[2];index=pm_load(m,at+8u+j,1);for(k=0;k<3u;k++)point[k]=pm_i16(m,vertices+index*8u+k*2u);frame_render_damage_uv(point,sides,p->has_plane || mesh_face_clipped(p,tri),uv);layer[j]=tri[j];layer[j].u=(float)uv[0]/65536.0f;layer[j].v=(float)uv[1]/65536.0f;}
                    status=mesh_submit(p,layer,colour,damage_texture,pm_load(m,damage_texture,2)&2u ? 3u : pm_load(m,damage_texture,2)&1u,0);if(status)return status;}
            }
        }
        if (p->self_layer && pm_i16(m,at+2u)>1500) {
            vertex layer[3];int32_t fixed[9];uint32_t material_layer=pm_load(m,0x54b498u,4),texture_layer=material_layer ? pm_load(m,material_layer+16u,4) : 0u,k;
            if (!texture_layer) return m->error ? m->error : -4;
            for (j=0;j<9u;j++) fixed[j]=(int32_t)(matrix[j]*4096.0f);
            if (p->self_layer==2u) { int32_t view[9];for (j=0;j<9u;j++) view[j]=(int32_t)(p->basis[j]*4096.0f);frame_present_layer_matrix(fixed,view,2,fixed); }
            for (j=0;j<3u;j++) { int32_t normal[3],uv[2];uint32_t id=pm_load(m,at+8u+j,1);layer[j]=tri[j];for (k=0;k<3u;k++) { uint32_t normals=pm_load(m,shape+72u+k*4u,4);if (!normals) return m->error ? m->error : -4;normal[k]=pm_i16(m,normals+id*2u); }frame_present_layer_uv(normal,fixed,uv);if(p->has_plane || mesh_face_clipped(p,tri)){uv[0]=(int32_t)(((uint32_t)uv[0]>>8)&255u)*256;uv[1]=(int32_t)(((uint32_t)uv[1]>>8)&255u)*256;}layer[j].u=(float)uv[0]/65536.0f;layer[j].v=(float)uv[1]/65536.0f; }
            /*4F913A/4F9D11 insert the extra style2 packet BEFORE its base
             *packet. Bucket head insertion therefore draws base, then layer. */
            status=mesh_submit(p,layer,p->mesh_shaded ? 0xff202020u : 0xff808080u,texture_layer,4,0);if (status) return status;
        }
        status=mesh_submit(p,tri,colour,texture,mode|(p->cloud_layer && texture ? 8u : 0u),0);if (status) return status;
    }
    return m->error;
}
static int object_offset_y(present *p,uint32_t shape,uint32_t position,uint32_t matrix,int32_t offset_y)
{
    float pos[3],mat[9],z=0;unsigned i;uint32_t previous=p->object_sorted,old_bucket=p->object_bucket;int status;
    for (i=0;i<3u;i++) pos[i]=(float)pm_sar(pm_add(pm_i32(&p->m,position+i*4u),i==1u ? offset_y : 0),3);
    for (i=0;i<9u;i++) mat[i]=matrix ? (float)pm_i32(&p->m,matrix+i*4u)/4096.0f : (i%4u==0u ? 1.0f : 0.0f);
    for (i=0;i<3u;i++) z+=(pos[i]-p->camera[i])*p->basis[6u+i];
    if (!previous) { p->object_sorted=1;p->object_bucket=frame_present_bucket((int32_t)z,p->sort_bias); }
    status=p->m.error ? p->m.error : mesh(p,shape,pos,mat);p->object_sorted=previous;p->object_bucket=old_bucket;return status;
}
static int object(present *p,uint32_t shape,uint32_t position,uint32_t matrix)
{ return object_offset_y(p,shape,position,matrix,0); }
static int vehicle_mesh_sink(void *ctx,uint32_t shape,const int32_t position[3],const int32_t matrix[12],uint32_t blend1)
{
    present *p=(present *)ctx;float pos[3],mat[9],z=0;uint32_t i,previous=p->mesh_blend,sorted=p->object_sorted,old_bucket=p->object_bucket;int status;
    for (i=0;i<3u;i++) pos[i]=(float)pm_sar(position[i],3);
    for (i=0;i<9u;i++) mat[i]=(float)matrix[i]/4096.0f;
    for (i=0;i<3u;i++) z+=(pos[i]-p->camera[i])*p->basis[6u+i];
    p->object_sorted=1;p->object_bucket=frame_present_bucket((int32_t)z,pm_i32(&p->m,0x55ade4u));
    p->mesh_blend=blend1 ? ((pm_load(&p->m,0x5964d4u,4)&32768u) ? 2u : 1u) : 0u;
    status=mesh(p,shape,pos,mat);p->mesh_blend=previous;p->object_sorted=sorted;p->object_bucket=old_bucket;return status;
}
static int vehicle_part_mesh_sink(void *ctx,uint32_t shape,const int32_t position[3],const int32_t matrix[12],uint32_t unused)
{
    present *p=(present *)ctx;float pos[3],mat[9];uint32_t i;(void)unused;
    for (i=0;i<3u;i++) pos[i]=(float)pm_sar(position[i],3);
    for (i=0;i<9u;i++) mat[i]=(float)matrix[i]/4096.0f;
    return mesh(p,shape,pos,mat); /* Keep owner's bucket, blend and shade. */
}
static int scenery_mesh_sink(void *ctx,uint32_t shape,const int32_t position[3],const int32_t matrix[12],int32_t bias,uint32_t style)
{
    present *p=(present *)ctx;float pos[3],mat[9],z=0;uint32_t i,sorted=p->object_sorted,bucket=p->object_bucket,blend=p->mesh_blend;int status;
    for (i=0;i<3u;i++) pos[i]=(float)pm_sar(position[i],3);
    for (i=0;i<9u;i++) mat[i]=(float)matrix[i]/4096.0f;
    for (i=0;i<3u;i++) z+=(pos[i]-p->camera[i])*p->basis[6u+i];
    p->object_sorted=1;p->object_bucket=frame_present_bucket((int32_t)z,bias);p->mesh_blend=style==2u && (pm_load(&p->m,0x5964d4u,4)&32768u) ? 2u : style ? 1u : 0u;
    status=mesh(p,shape,pos,mat);p->object_sorted=sorted;p->object_bucket=bucket;p->mesh_blend=blend;return status;
}
static int scene_flush(present *p)
{
    uint32_t bucket,id,phase;int status=flush(p);if (status) return status;
    p->blending=1;
    /*4FB520/4FB370: every primitive enters the same1000 buckets. Ascending
     * buckets, head insertion/LIFO within each; no opaque/alpha pass split. */
    for (bucket=0;bucket<1000u;bucket++) for (phase=0;phase<2u;phase++) for (id=p->heads[phase][bucket];id!=UINT32_MAX;id=p->polygons[id].next) {
        scene_polygon *q=&p->polygons[id];
        if (q->count==3u) { status=triangle(p,q->v,q->colour,q->texture,q->mode);if (status) return status; }
        else { unsigned char out[64];uint32_t j;float f=(float)p->commands->height*(512.0f/480.0f);status=flush(p);if (status) return status;status=draw_states(p,0,q->mode);if (status) return status;
            for (j=0;j<2u;j++) { unsigned char *dst=out+j*32u;vertex v=q->v[j];real(dst,(float)p->commands->width*.5f+f*v.x/v.z);real(dst+4,(float)p->commands->height*.5f-f*v.y/v.z);real(dst+8,1-4/v.z);real(dst+12,1/v.z);word(dst+16,j ? q->end_colour : q->colour);word(dst+20,0xff000000u);real(dst+24,0);real(dst+28,0); }
            status=frame_commands_submit(p->commands,2,0,0,out,2,0);if (status) return status;
        }
    }
    return flush(p);
}
static int entity(present *p,uint32_t id,int special,uint32_t slot)
{
    physics_memory *m=&p->m;uint32_t record,klass,o,methods,target=0,i;int status;
    record=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);if (!record) return -4;klass=pm_load(m,record,4);o=pm_load(m,record+4u,4);if (!o || !klass) return -4;
    if (special && slot<=pm_load(m,klass+28u,4)) { methods=pm_load(m,klass+36u,4);target=pm_load(m,methods+slot*4u,4); }
    /*403D60 delegates visible AITank geometry to the Tank render slot. The
     * original simplified/far simulation branch deliberately emits no draw. */
    if (special && target==0x403d60u) {
        if (pm_load(m,o+0x10f3u,1)&1u) return m->error;
        klass=pm_load(m,0x545fdcu,4);target=slot<=pm_load(m,klass+28u,4) ? pm_load(m,pm_load(m,klass+36u,4)+slot*4u,4) : 0;
    }
    if (special && target==0x433d10u) {
        frame_vehicle_render_services graphics={sprite_sink,vehicle_mesh_sink,p};
        physics_memory overlay={draw_read,draw_write,p,0};float z=0;int32_t view[2][3],depth=0;uint32_t order[9],j,k,handled;
        pm_store(&overlay,0x55ade4u,0,4);if (overlay.error) return overlay.error;
        p->mesh_shaded=pm_i8(m,o+0xde2u)>1;
        status=frame_vehicle_cockpit_draw(draw_read,draw_write,p,o,&handled,&graphics);p->mesh_shaded=0;if (status || handled) return status;
        /*433EB0: Q12 camera dot, then >>9 (eight times render-space depth). */
        for (i=0;i<3u;i++) depth=pm_add(depth,pm_mul(pm_sub(pm_sar(pm_i32(m,o+i*4u),3),(int32_t)p->camera[i]),(int32_t)(p->basis[6u+i]*4096.0f)));
        depth=pm_sar(depth,9);if (depth < -0x1400) return m->error;
        if (pm_i32(m,o+0xdafu)>0) {
            status=frame_vehicle_shield_draw(draw_read,draw_write,p,o,&graphics);if (status) return status;
            if ((pm_load(m,0x545de4u,1) || pm_i8(m,0x545de0u)>1) && pm_load(m,pm_load(m,0x58fa50u,4)+2u,2)) { p->shield_shape=pm_load(m,0x58fa50u,4);p->shield_colour=pm_load(m,pm_load(m,p->shield_shape+8u,4)+6u,2);p->shield_colour_valid=1; }
        }
        if (pm_load(m,o+0xdcbu,1) || pm_load(m,o+0xacu,4)) { status=frame_vehicle_boost_draw(draw_read,draw_write,p,o,&graphics);if (status) return status; }
        if (depth>=0x10400 || !pm_load(m,o+0xdedu,1)) {
            /*434870: distant simulation refreshes only the hull pose. D34 is
             * the complete distant tank mesh, not near component zero. */
            p->sort_bias=pm_i16(m,o+0x4eu);
            p->mesh_blend=pm_load(m,o+0xde4u,1) ? ((pm_load(m,0x5964d4u,4)&32768u) ? 2u : 1u) : 0u;
            p->mesh_shaded=pm_i8(m,o+0xde2u)>1;
            status=object_offset_y(p,pm_load(m,o+0xd34u,4),o+0x2a6u,o+0x2b2u,-0x546);p->mesh_blend=0;p->mesh_shaded=0;p->sort_bias=0;if (status) return status;
            if (!pm_load(m,0x53e414u,4) || !pm_load(m,o+0xde5u,1)) {
                /*434AA8 does not force the near shadow's alpha style. */
                p->sort_bias=pm_add(pm_i16(m,o+0x4eu),6);status=object(p,pm_load(m,o+0x71eu,4),o+0x722u,o+0x72eu);p->sort_bias=0;if (status) return status;
            }
            return frame_pickup_effect_project(draw_read,draw_write,p,o);
        }
        /*433D10->439270 submits the lights before the body. Head insertion
         *within a shared painter bucket must retain that order. */
        status=frame_vehicle_brake_lights_draw(draw_read,draw_write,p,o,&graphics);if (status) return status;
        status=frame_vehicle_reverse_lights_draw(draw_read,draw_write,p,o,&graphics);if (status) return status;
        status=track_uv(p,o);if (status) return status;
        vehicle_clip_plane(p,o);
        /*433D10->4FD1C0: all body components share the owner's bucket;
         * individual sprites retain their own descriptor depth bias. */
        for (i=0;i<3u;i++) z+=((float)pm_sar(pm_i32(m,o+0x78u+i*4u),3)-p->camera[i])*p->basis[6u+i];
        p->object_sorted=1;p->object_bucket=frame_present_bucket((int32_t)z,pm_add(pm_i16(m,o+0x4eu),pm_i32(m,o+0xd95u)));
        /*433F99: DE4 affects the entire tank, independently of the rotating
         *429EE0 shield.4F7550(1) selects additive on an8000-capable device. */
        p->mesh_blend=pm_load(m,o+0xde4u,1) ? ((pm_load(m,0x5964d4u,4)&32768u) ? 2u : 1u) : 0u;
        /*433D10 calls4F7350 when more than one suspension sample is in
         *shade (DE2). The owner bucket preserves it across every body part;
         *434636->4FD270 resets it before the independent ground shadow. */
        p->mesh_shaded=pm_i8(m,o+0xde2u)>1;
        for (j=0;j<2u;j++) { int32_t relative[3];uint32_t center=j ? 0x30eu : 0x78u,matrix=j ? 0x31au : 24u;
            for (k=0;k<3u;k++) relative[k]=pm_sub(pm_mul((int32_t)p->camera[k],8),pm_i32(m,o+center+k*4u));
            for (k=0;k<3u;k++) { uint32_t axis;int32_t sum=0;for (axis=0;axis<3u;axis++) sum=pm_add(sum,pm_mul(relative[axis],pm_i32(m,o+matrix+(k*3u+axis)*4u)));view[j][k]=pm_sar(sum,12); }
        }
        frame_present_vehicle_order(view[0],view[1],order);
        for (i=0;i<9u;i++) {
            uint32_t part=order[i];
            if ((part==1u || part==2u) && !pm_load(m,o+0x296u+104u,4)) continue;
            status=frame_vehicle_self_layer(draw_read,p,o,part,&p->self_layer);if (status) return status;
            p->damage_object=o;p->damage_part=part==2u ? 1u : part;
            if (part==7u) { frame_vehicle_render_services aerial={sprite_sink,vehicle_part_mesh_sink,p};status=frame_vehicle_aerial_draw(draw_read,draw_write,p,o,&aerial); }
            else status=object(p,pm_load(m,o+0x296u+part*104u,4),o+0x2a6u+part*104u,o+0x2b2u+part*104u);
            p->self_layer=0;p->damage_object=0;
            if (status) return status;
        }p->object_sorted=0;p->mesh_blend=0;p->mesh_shaded=0;
        /*434636 unlocks the owner bucket and resets its forced style.
         *434663 then draws the separate ground mesh using4F7550(0): alpha128,
         *never the invincible body's additive style or body depth bias. */
        if (!pm_load(m,0x53e414u,4) || !pm_load(m,o+0xde5u,1)) {
            p->sort_bias=pm_add(pm_i16(m,o+0x4eu),6);p->mesh_blend=1;
            status=object(p,pm_load(m,o+0x71eu,4),o+0x722u,o+0x72eu);p->sort_bias=0;p->mesh_blend=0;if (status) return status;
        }
        p->has_plane=0;
        status=tank_particles(p,o);if (status) return status;
        return frame_pickup_effect_project(draw_read,draw_write,p,o);
    }
    if (special && target==0x429540u) {
        uint32_t kind=pm_load(m,o+0x7cu,4);
        if (!(pm_load(m,o+0x84u,4)&1u)) {
            if (kind==2u || (kind==4u && pm_load(m,o+72u,4))) { p->sort_bias=kind==4u && (pm_load(m,o+0x84u,4)&16u) ? -10 : -1;status=object(p,pm_load(m,o+72u,4),o,pm_load(m,o+20u,4));p->sort_bias=0;if (status) return status; }
            /* Model-mounted flares use the published local nozzle offset. */
            if (kind==2u || (kind==4u && pm_load(m,o+72u,4))) {
                physics_memory overlay={draw_read,draw_write,p,0};uint32_t at=kind==2u ? 0x545d88u : o+0x88u,local=kind==2u ? 0x545d48u : o+0x4e5u,mat=pm_load(m,o+20u,4),j,k;
                for (j=0;j<3u;j++) { int32_t v=0;for (k=0;k<3u;k++) v=pm_add(v,pm_mul(pm_i32(m,local+k*4u),pm_i32(m,mat+(k*3u+j)*4u)));pm_write32(&overlay,at+4u+j*4u,pm_add(pm_i32(m,o+j*4u),pm_sar(v,12))); }
                if (overlay.error) return overlay.error;
                status=sprite(p,at,0,0);
            } else status=sprite(p,o+0x88u,o,0);
            if (status) return status;
            if (kind==1u) { status=sprite(p,0x545d58u,o,0);if (status) return status; }
        }
        for (i=0;i<15u;i++) if (pm_load(m,o+0xb2u+i*65u,1)) { status=sprite(p,o+0xc5u+i*65u,0,0);if (status) return status; }
        return m->error;
    }
    if (special && target==0x409070u) {
        frame_actor_services graphics={sprite_sink,p};uint32_t blast;
        if (pm_load(m,o+0xadu,1)) { status=sprite(p,o+0x7au,0,1);if (status) return status; }
        for (i=0;i<10u;i++) if (pm_load(m,o+0xaeu+i*66u+62u,2)) { status=sprite(p,o+0xaeu+i*66u,0,0);if (status) return status; }
        status=explosion_debris(p,o);if (status) return status;
        blast=pm_load(m,o+0x363u,4);if (blast!=UINT32_MAX) { if (blast>1u) return -4;status=frame_blast_band_project(draw_read,draw_write,p,blast,12);if (status) return status; }
        blast=pm_load(m,o+0x367u,4);if (blast!=UINT32_MAX) { if (blast>1u) return -4;status=frame_blast_ring_project(draw_read,draw_write,p,blast,&graphics);if (status) return status; }
        return m->error;
    }
    if (special && target==0x429fc0u) { frame_actor_services graphics={sprite_sink,p};return frame_smoker_render(draw_read,draw_write,p,id,&graphics); }
    if (special && target==0x413a10u) { frame_actor_services graphics={sprite_sink,p};return frame_lamps_render(draw_read,draw_write,p,id,1000000,&graphics); }
    if (special && (target==0x40b550u || target==0x43e030u || target==0x407620u || target==0x416080u)) {
        frame_scenery_render_services graphics={scenery_mesh_sink,sprite_sink,p};return frame_scenery_render(draw_read,draw_write,p,target,o,&graphics);
    }
    if (special && target==0x422580u) {
        uint32_t flags=pm_load(m,o+0xaau,4);status=object(p,pm_load(m,o+72u,4),o,pm_load(m,o+20u,4));if (status || (flags&8u)) return status;
        if (!(flags&4u)) return object(p,pm_load(m,0x5450a8u,4),o,pm_load(m,o+20u,4));
        if (flags&2u) { /*4225B4:4F7550(0), regardless of additive device. */
            p->mesh_blend=1;status=object(p,pm_load(m,0x5450a8u,4),o+0xfbu,o+0x107u);p->mesh_blend=0;return status;
        }
        return m->error;
    }
    if (special && target==0x41c6a0u) {
        uint32_t fragments=pm_load(m,o+0x8cu,4),flags=fragments ? pm_load(m,fragments+24u,4) : 0u;
        /*41C77E is a luminous whole mesh, not the standard-list path. */
        if (!(flags&0x400u)) { p->mesh_blend=(pm_load(m,0x5964d4u,4)&32768u) ? 2u : 1u;status=object(p,pm_load(m,o+72u,4),o,pm_load(m,o+20u,4));p->mesh_blend=0;return status; }
        { uint32_t count=pm_load(m,fragments+20u,2),base=pm_load(m,fragments+12u,4);if (count>1024u) return -3;
          for (i=0;i<count;i++) { uint32_t at=base+i*104u;if (pm_load(m,at+84u,1)&2u) continue;
            if (flags&128u) { physics_memory overlay={draw_read,draw_write,p,0};uint32_t descriptor=pm_load(m,fragments+48u,4);pm_store(&overlay,descriptor+40u,pm_load(m,at+100u,2),2);if (overlay.error) return overlay.error;status=sprite(p,descriptor,at+12u,0); }
            else status=object(p,pm_load(m,at+24u,4),at+12u,at+28u);
            if (status) return status;
          }
        }
        if (flags&8u) { status=sprite(p,pm_load(m,fragments+40u,4)+6u,0,0);if (status) return status; }
        if (flags&16u) return object(p,pm_load(m,o+72u,4),o,pm_load(m,o+20u,4));
        return m->error;
    }
    if (special && target==0x440ba0u) {
        /*440BBC: the lamp's mesh is alpha128 with bias-10; its glow is a
         *separate4FD9B0 descriptor, even when the mesh bit is clear. */
        if (pm_load(m,o+120u,1)&4u) { p->sort_bias=-10;p->mesh_blend=1;status=object(p,pm_load(m,o+72u,4),o,pm_load(m,o+20u,4));p->sort_bias=0;p->mesh_blend=0;if (status) return status; }
        return sprite(p,o+152u,0,0);
    }
    /* Standard draw-list objects need no :draw method at all. The original
     *4F7130 directly draws their base shape; scanning method bindings loses
     * these buildings/scenery and also includes invisible/inactive objects. */
    if (pm_load(m,klass+20u,4)>=76u) { uint32_t shape=pm_load(m,o+72u,4);p->sort_bias=pm_i16(m,o+0x4eu);if (shape) { status=object(p,shape,o,pm_load(m,o+20u,4));p->sort_bias=0;return status; } }
    return m->error;
}
int frame_present_scene_resources(frame_data_read read,void *ctx,frame_commands *commands,const frame_present_resources *resources,void *workspace,size_t workspace_bytes,frame_present_stats *stats)
{
    present *p=(present *)workspace;physics_memory *m;uint32_t i,node,used=0,table,record,o,id,slot,list,head;float norm=0;int status;
    static const float identity[9]={1,0,0,0,1,0,0,0,1};track_motion tracks[16];uint32_t shield_shape,shield_colour,shield_colour_valid;
    if (!read || !commands || !stats || !p || workspace_bytes<sizeof(*p)) return -1;
    shield_shape=p->shield_shape;shield_colour=p->shield_colour;shield_colour_valid=p->shield_colour_valid;
    memcpy(tracks,p->tracks,sizeof(tracks));memset(p,0,sizeof(*p));memcpy(p->tracks,tracks,sizeof(tracks));p->shield_shape=shield_shape;p->shield_colour=shield_colour;p->shield_colour_valid=shield_colour_valid;p->resources=resources;p->source_read=read;p->source_ctx=ctx;p->m=(physics_memory){draw_read,0,p,0};p->commands=commands;m=&p->m;
    memset(p->heads,255,sizeof(p->heads));
    if (resources && resources->fog) { p->fog=*resources->fog;p->fog.enabled=p->fog.enabled && pm_load(m,0x54b4c8u,4); }
    else if (pm_load(m,0x54b4c8u,4) && pm_load(m,0x534900u,4)) { p->fog=(frame_present_fog_settings){1,pm_load(m,0x594cd4u,4),pm_load(m,0x594cecu,4),pm_load(m,0x5348fcu,4)};p->fog_minimum=pm_load(m,0x594580u,4); }
    status=frame_vehicle_smoke_templates(draw_read,draw_write,p);if (status) return status;
    if (shield_colour_valid) { physics_memory overlay={draw_read,draw_write,p,0};uint32_t count=pm_load(&overlay,shield_shape+2u,2),faces=pm_load(&overlay,shield_shape+8u,4);for (i=0;i<count;i++) pm_store(&overlay,faces+i*24u+6u,shield_colour,2);if (overlay.error) return overlay.error; }
    for (i=0;i<3u;i++) p->camera[i]=(float)pm_i32(m,0x74aee0u+i*4u);
    for (i=0;i<9u;i++) { p->basis[i]=(float)pm_i32(m,0x74af00u+i*4u)/4096.0f;norm+=p->basis[i]*p->basis[i]; }
    /* Before the first camera tick no publication exists yet. Read its initial
     * object pose; this is presentation only, with no extra simulation step. */
    table=pm_load(m,0x55c694u,4);
    if (norm<.01f) { id=pm_load(m,0x53cc38u,4);record=pm_load(m,table+id*8u,4);o=record ? pm_load(m,record+4u,4) : 0;if (!o) return -4;for (i=0;i<3u;i++) p->camera[i]=(float)pm_sar(pm_i32(m,o+i*4u),3);node=pm_load(m,o+20u,4);for (i=0;i<9u;i++) p->basis[i]=node ? (float)pm_i32(m,node+i*4u)/4096.0f : identity[i]; }
    frame_commands_state(commands,4,1);frame_commands_state(commands,7,0);frame_commands_state(commands,14,1);frame_commands_state(commands,22,1);frame_commands_state(commands,23,4);
    frame_commands_state(commands,3,3);frame_commands_state(commands,17,2);frame_commands_state(commands,18,4);frame_commands_state(commands,21,4);frame_commands_state(commands,15,0);frame_commands_state(commands,24,128);frame_commands_state(commands,25,5);
    status=background(p);if (status) return status;
    frame_render_clip_initialize(&p->clip,commands->width,commands->height,4*4096,25600*4096);
    node=pm_load(m,0x54b49cu,4);
    while (node) { float pos[3];if (used++>=10000u) return -3;if (!(pm_load(m,node+28u,1)&1u)) { for (i=0;i<3u;i++){pos[i]=(float)pm_i32(m,node+36u+i*4u);p->cloud_origin[i]=(int32_t)pos[i]-(int32_t)p->camera[i];}p->cloud_origin[1]=25600-(int32_t)p->camera[1];p->cloud_layer=(pm_load(m,node+28u,1)&8u)!=0 && pm_load(m,0x54b490u,4)!=0;status=mesh(p,node,pos,identity);p->cloud_layer=0;if (status) return status; }node=pm_load(m,node+32u,4); }
    slot=pm_load(m,pm_load(m,0x545ec0u,4)+28u,4);
    for (list=0;list<2u;list++) {
        head=pm_load(m,list ? 0x54b48cu : 0x54b488u,4);if (!head) continue;node=pm_load(m,head,4);
        while (node!=head) { if (used++>=10000u) return -3;id=pm_load(m,node+8u,4);status=entity(p,id,!list,slot);if (status) return status;node=pm_load(m,node,4);if (m->error) return m->error; }
    }
    /*500540 produces shared pools before world/entities. Generate their
     * geometry after private projections, but merge them behind the newer
     * world/entity submissions within each bucket, preserving original LIFO. */
    p->queue_phase=1;status=quad_pool(p);if (status) return status;status=line_pool(p);if (status) return status;
    status=scene_flush(p);if (status) return status;if (m->error) return m->error;*stats=p->stats;return 0;
}

int frame_present_scene(frame_data_read read,void *ctx,frame_commands *commands,void *workspace,size_t bytes,frame_present_stats *stats)
{ return frame_present_scene_resources(read,ctx,commands,0,workspace,bytes,stats); }
