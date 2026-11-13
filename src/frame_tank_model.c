#include "frame_tank_model.h"
#include "physics_memory.h"
#include "physics_attitude.h"
#define TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
static uint32_t object_of(physics_memory *m,uint32_t id)
{ uint32_t r=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);return r ? pm_load(m,r+4u,4) : 0; }
int frame_tank_track_uv(frame_data_read read,frame_data_write write,void *ctx,uint32_t shape,uint32_t out,uint32_t limit)
{
    physics_memory m={read,write,ctx,0};uint32_t i,j,n,triangle,coord[3],low,high,index,at,counter,base,cap,offset,value;int32_t nx;int flat;
    if (!read || !write || !limit) return -1;
    if (!shape) return -4;
    pm_store(&m,out+296u,0,1);pm_store(&m,out+297u,0,1);pm_store(&m,out+298u,0,1);pm_store(&m,out+299u,0,1);pm_store(&m,out,0,4);pm_store(&m,out+4u,0,4);
    n=pm_load(&m,shape+2u,2);
    for (i=0;i<n;i++) {
        if (i>=limit) return -3;
        triangle=pm_load(&m,shape+8u,4)+i*24u;nx=pm_i16(&m,triangle);flat=nx<1001 && nx>-1001;
        if (flat && (pm_abs(nx)>=64 || pm_i16(&m,triangle+2u)<=-3500)) continue;
        if (!pm_load(&m,out+(flat ? 0u : 4u),4)) pm_store(&m,out+(flat ? 0u : 4u),pm_load(&m,triangle+20u,4),4);
        low=255;high=0;
        for (j=0;j<3u;j++) { coord[j]=pm_load(&m,triangle+13u+j*2u,1);if (coord[j]<low) low=coord[j];if (coord[j]>high) high=coord[j]; }
        for (j=0;j<3u;j++) {
            if (flat) { counter=out+(coord[j]==low ? 299u : 298u);base=coord[j]==low ? 152u : 56u;cap=24;offset=coord[j]==low ? 272u : 248u;value=(coord[j]==low ? low : high)>>1;value=(value-value%5u)*2u; }
            else { counter=out+(coord[j]==low ? 297u : 296u);base=coord[j]==low ? 32u : 8u;cap=6;offset=value=0; }
            index=pm_load(&m,counter,1);
            if (index<cap) {
                at=triangle+13u+j*2u;pm_store(&m,out+base+index*4u,at,4);
                if (flat) pm_store(&m,out+offset+pm_load(&m,counter,1),value,1);
                pm_store(&m,counter,pm_load(&m,counter,1)+1u,1);
            }
        }
    }
    return m.error;
}
int frame_tank_corner_vertices(frame_data_read read,frame_data_write write,void *ctx,uint32_t o,uint32_t limit)
{
    physics_memory m={read,write,ctx,0};uint32_t shape,vertices,body,quadrant,i,n;int32_t x,y,z,best,d;int wanted;
    if (!read || !write || !limit) return -1;
    for (body=0;body<2u;body++) {
        shape=pm_load(&m,o+(body ? 0x2feu : 0x296u),4);if (!shape) return m.error ? m.error : -4;
        for (quadrant=0;quadrant<4u;quadrant++) {
            best=0;n=pm_load(&m,shape,2);
            for (i=0;i<n;i++) {
                if (i>=limit) return -3;
                vertices=pm_load(&m,shape+4u,4)+i*8u;x=pm_i16(&m,vertices);y=pm_i16(&m,vertices+2u);z=pm_i16(&m,vertices+4u);
                wanted=y>0 && ((quadrant&1u) ? x>0 : x<0) && ((quadrant&2u) ? z<0 : z>0);
                if (wanted) { d=physics_length2_approx(x,z);if (best<d) { best=d;pm_store(&m,o+0xdffu+body*4u+quadrant,i,1); } }
            }
        }
    }
    return m.error;
}
static int cstring(physics_memory *m,uint32_t at,unsigned char *name,uint32_t capacity,uint32_t *bytes)
{
    uint32_t i;for (i=0;i<capacity;i++) { name[i]=(unsigned char)pm_load(m,at+i,1);if (m->error) return m->error;if (!name[i]) { *bytes=i+1u;return 0; } }return -4;
}
static int load_shape(physics_memory *memory,uint32_t name,uint32_t space,uint32_t copy,uint32_t size,const frame_entity_factory_services *e,const frame_shape_resources *r,uint32_t *out)
{
    physics_memory m=*memory;uint32_t shape;int status;
    TRY(frame_shape_load(m.read,m.write,m.ctx,name,space,e,r,&shape));
    if (copy) { if (!shape) return -4;TRY(frame_shape_clone_loaded(m.read,m.write,m.ctx,shape,size,e,&shape)); }
    *out=shape;memory->error=m.error;return m.error;
}
int frame_tank_model_load_span(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                                   const unsigned char *bytes,uint32_t length,const frame_entity_factory_services *e,
                                   const frame_shape_resources *resources,const frame_tank_config_ambient *ambient,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,used,i,j,p,shape,space,copy,unused,descriptor,vertices,n,selected,quad;
    int32_t value,minimum=0,minx,maxx,zextent;int status,is_null;unsigned char name[260];uint32_t name_bytes;
    static const uint32_t corner_dest[]={0x11c,0x120,0x124,0x234,0x238,0x23c,0x162,0x166,0x16a,0x1a8,0x1ac,0x1b0,0x251,0x255,0x259,0x1ee,0x1f2,0x1f6};
    static const uint32_t corner_src[]={0x1c,0x1e,0x26,0x1c,0x1e,0x20,0x1c,0x1e,0x20,0x22,0x1e,0x26,0x22,0x1e,0x20,0x22,0x1e,0x20};
    if (!read || !write || !e || !e->work_limit || !out) return -1;
    o=object_of(&m,entity);TRY(frame_tank_configure_span(read,write,ctx,o,bytes,length,e->work_limit,ambient,&used));
    TRY(frame_tank_configure_derived(read,write,ctx,o));TRY(frame_tank_effects_initialize(read,write,ctx,o));
    for (i=0;i<9u;i++) {
        p=o+i*104u;TRY(cstring(&m,p+0x27eu,name,260,&name_bytes));
        is_null=name_bytes==5u && (name[0]=='N' || name[0]=='n') && (name[1]=='U' || name[1]=='u') && (name[2]=='L' || name[2]=='l') && (name[3]=='L' || name[3]=='l');
        if (is_null) { pm_store(&m,p+0x296u,0,4);continue; }
        space=pm_load(&m,i==7u ? 0x55ae48u : 0x53d184u,4);copy=i==2u || (i<2u && pm_load(&m,o+0xde5u,1));
        TRY(load_shape(&m,p+0x27eu,space,copy,p+0x2e2u,e,resources,&shape));pm_store(&m,p+0x296u,shape,4);
    }
    TRY(frame_tank_track_uv(read,write,ctx,pm_load(&m,o+0x3ceu,4),o+0xe22u,e->work_limit));
    TRY(frame_tank_track_uv(read,write,ctx,pm_load(&m,o+0x436u,4),o+0xf4eu,e->work_limit));
    for (i=1;i<9u;i++) if (i!=7u && i!=8u) for (j=0;j<3u;j++) pm_store(&m,o+0x29au+i*104u+j*4u,pm_load(&m,o+0x29au+i*104u+j*4u,4)-pm_load(&m,o+0x29au+j*4u,4),4);
    for (j=0;j<3u;j++) pm_store(&m,o+0x29au+j*4u,0,4);
    for (j=0;j<3u;j++) pm_store(&m,o+0x36au+j*4u,pm_load(&m,o+0x36au+j*4u,4)-pm_load(&m,o+0x302u+j*4u,4),4);
    shape=pm_load(&m,o+0x3ceu,4);if (!shape) return -4;
    for (j=0;j<3u;j++) pm_write32(&m,o+0x626u+j*4u,pm_mul(pm_sub(pm_i16(&m,shape+0x22u+j*2u),pm_i16(&m,shape+0x1cu+j*2u)),8));
    for (j=0;j<6u;j++) pm_store(&m,o+0x632u+j*4u,j<3u ? 16000000u : 0xff0bdc00u,4);
    for (i=0;i<9u;i++) {
        p=o+i*104u;shape=pm_load(&m,p+0x296u,4);if (!shape || i==8u) continue;
        for (j=0;j<3u;j++) { value=pm_add(pm_i32(&m,p+0x29au+j*4u),pm_mul(pm_i16(&m,shape+0x1cu+j*2u),8));if (value<pm_i32(&m,o+0x632u+j*4u)) pm_write32(&m,o+0x632u+j*4u,value); }
        for (j=0;j<3u;j++) { value=pm_add(pm_i32(&m,p+0x29au+j*4u),pm_mul(pm_i16(&m,shape+0x22u+j*2u),8));if (pm_i32(&m,o+0x63eu+j*4u)<value) pm_write32(&m,o+0x63eu+j*4u,value); }
    }
    for (i=0;i<18u;i++) {
        shape=pm_load(&m,o+(i<9u ? 0x3ceu : 0x436u),4);value=pm_i16(&m,shape+corner_src[i]);
        if (i==5u || i==14u) value=pm_add(value,pm_i32(&m,o+0x6aau));
        pm_write32(&m,o+corner_dest[i],pm_mul(value,8));
    }
    descriptor=pm_load(&m,0x545ee0u,4);TRY(frame_entity_dispatch(read,write,ctx,entity,descriptor,0,0,e->method,e->method_ctx,&unused));
    shape=pm_load(&m,o+0x56eu,4);if (shape) pm_write32(&m,o+0x652u,pm_mul(pm_sub(pm_i16(&m,shape+0x24u),pm_i16(&m,shape+0x1eu)),8));
    pm_write32(&m,o+0x60u,pm_sub(pm_i32(&m,o+0x632u),1536));pm_write32(&m,o+0x74u,pm_add(pm_i32(&m,o+0x646u),1536));pm_write32(&m,o+0x68u,pm_sub(pm_i32(&m,o+0x63au),1536));
    pm_write32(&m,o+0x6cu,pm_add(pm_i32(&m,o+0x63eu),1536));value=pm_sub(pm_i32(&m,o+0x636u),1536);pm_write32(&m,o+0x64u,value);pm_write32(&m,o+0x108au,value);
    pm_write32(&m,o+0x109au,pm_add(pm_i32(&m,o+0x646u),600));pm_write32(&m,o+0x1086u,pm_sub(pm_i32(&m,o+0x632u),600));
    pm_write32(&m,o+0x10a2u,pm_sub(pm_i32(&m,o+0x636u),3036));pm_write32(&m,o+0x10b2u,pm_add(pm_i32(&m,o+0x646u),2100));value=pm_add(pm_i32(&m,o+0x642u),1536);
    pm_write32(&m,o+0x70u,value);pm_write32(&m,o+0x108eu,pm_sub(pm_i32(&m,o+0x63au),600));pm_write32(&m,o+0x1092u,pm_add(pm_i32(&m,o+0x63eu),600));pm_write32(&m,o+0x1096u,value);
    pm_write32(&m,o+0x109eu,pm_sub(pm_i32(&m,o+0x632u),2100));pm_write32(&m,o+0x10a6u,pm_sub(pm_i32(&m,o+0x63au),2100));pm_write32(&m,o+0x10aau,pm_add(pm_i32(&m,o+0x63eu),2100));pm_write32(&m,o+0x10aeu,pm_add(pm_i32(&m,o+0x642u),3036));
    pm_store(&m,o+0x48u,pm_load(&m,o+0x296u,4),4);shape=pm_load(&m,o+0x366u,4);
    if (shape) {
        n=pm_load(&m,shape,2);selected=0;
        for (i=0;i<n;i++) { if (i>=e->work_limit) return -3;value=pm_i16(&m,pm_load(&m,shape+4u,4)+i*8u+4u);if (value<minimum) minimum=value; }
        for (i=0;i<pm_load(&m,shape,2);i++) {
            vertices=pm_load(&m,shape+4u,4)+i*8u+4u;value=pm_i16(&m,vertices);
            if (value<pm_add(minimum,40) && pm_sub(minimum,40)<value) {
                if (selected<4u) { pm_store(&m,o+0x784u+selected,i,1);pm_store(&m,o+0x788u+selected*2u,pm_load(&m,vertices,2),2); }selected++;
            }
        }
        pm_store(&m,shape+12u,pm_load(&m,shape+12u,2)<<1,2);
    }
    space=pm_load(&m,0x53d184u,4);TRY(frame_shape_load(read,write,ctx,o+0xd1cu,space,e,resources,&shape));pm_store(&m,o+0xd34u,shape,4);
    minx=pm_i32(&m,o+0x632u);maxx=pm_i32(&m,o+0x63eu);zextent=pm_mul(pm_i16(&m,pm_load(&m,o+0x3ceu,4)+0x26u),8);
    space=pm_load(&m,0x55ae48u,4);
    TRY(frame_shape_load_name(read,write,ctx,(const unsigned char *)"shadow.sm",10,space,e,resources,&shape));
    TRY(frame_shape_clone_loaded(read,write,ctx,shape,o+0x75eu,e,&quad));pm_store(&m,o+0x71eu,quad,4);
    for (i=0;i<4u;i++) {
        vertices=pm_load(&m,pm_load(&m,o+0x71eu,4)+4u,4)+i*8u;
        pm_store(&m,vertices,(uint32_t)pm_sar(i==0u || i==3u ? minx : maxx,3),2);
        pm_store(&m,vertices+2u,0,2);pm_store(&m,vertices+4u,(uint32_t)pm_sar(i<2u ? pm_neg(zextent) : zextent,3),2);
    }
    for (i=0;i<12u;i++) pm_store(&m,o+0x72eu+i*4u,pm_load(&m,0x532888u+i*4u,4),4);
    for (i=0;i<2u;i++) pm_store(&m,pm_load(&m,pm_load(&m,o+0x71eu,4)+8u,4)+i*24u+6u,0x2108u,2);
    TRY(frame_tank_corner_vertices(read,write,ctx,o,e->work_limit));
    pm_store(&m,o+0x10b6u,pm_load(&m,0x546048u,2),2);pm_store(&m,o+0x10b8u,pm_load(&m,o+0x6c2u,2),2);
    value=pm_i32(&m,0x52ee9cu);if (value==4 || value==5) { pm_write32(&m,o+0x66au,pm_sar(pm_mul(pm_i32(&m,o+0x66au),3),1));pm_store(&m,o+0x6c2u,pm_load(&m,o+0x6c2u,2)*12u,2); }
    if (!pm_load(&m,0x58e7acu,4) && pm_load(&m,0x545ffcu,4)) pm_store(&m,o+0x6a8u,pm_load(&m,o+0x6a8u,2)+1u,2);
    if (m.error) return m.error;
    *out=entity;return 0;
}
int frame_tank_model_load(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t filename,
                              const frame_entity_factory_services *e,const frame_shape_resources *r,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};unsigned char name[260];const unsigned char *bytes;uint32_t length,n,found,space;int status;
    if (!read || !write || !e || !r || !r->fetch || !out) return -1;
    TRY(cstring(&m,filename,name,260,&n));space=pm_load(&m,0x53d188u,4);
    TRY(r->fetch(r->file_ctx,space,name,n,&bytes,&length,&found));if (!found) return -4;
    return frame_tank_model_load_span(read,write,ctx,entity,bytes,length,e,r,0,out);
}
#undef TRY
