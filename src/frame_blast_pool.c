#include "frame_blast_pool.h"
#include "physics_memory.h"
static int32_t origin_coordinate(physics_memory *m,uint32_t address,const int32_t *values,uint32_t i)
{ return values ? values[i] : pm_i32(m,address+i*4u); }
static uint32_t object_of(physics_memory *m,uint32_t id)
{ uint32_t r=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);return r ? pm_load(m,r+4u,4) : 0; }
static uint32_t random_next(physics_memory *m)
{ uint32_t v=pm_load(m,0x55b6acu,4)*1664525u+1013904223u;pm_store(m,0x55b6acu,v,4);return v; }
int frame_blast_pool_initialize(frame_data_read read,frame_data_write write,void *ctx,frame_object_material_name material,void *material_ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t value,i,j;int status;
    static const unsigned char standard_style[]={1,127,127,127,0,63,0,0,31,0,31,63};
    static const struct { const char *name;uint32_t length,address; } items[]={
        {"flare3",7,0x53d0f0},{"helix",6,0x53cf60},{"smke2",6,0x53cfa0}};
    static const uint32_t counters[]={0x53d118,0x53d11c,0x53d110,0x53d120,0x53d100,0x53d108,0x53d124,0x53d114,0x53d104};
    if (!read || !write || !material) return -1;
    status=material(material_ctx,(const unsigned char *)"flame2b",8,1,&value);if (status) return status;pm_store(&m,0x53d0a8u,value,4);
    for (i=0;i<8u;i++) pm_store(&m,0x53d0b0u+i,(i>=3u && i<=6u) ? 63u : 0u,1);
    for (i=0;i<4u;i++) pm_store(&m,0x53d0acu+i,i ? 127u : 1u,1);
    pm_store(&m,0x53d0b8u,12,4);
    for (i=0;i<3u;i++) {
        if (m.error) return m.error;
        status=material(material_ctx,(const unsigned char *)items[i].name,items[i].length,1,&value);if (status) return status;pm_store(&m,items[i].address,value,4);
        for (j=0;j<12u;j++) pm_store(&m,items[i].address+4u+j,standard_style[j],1);
    }
    if (m.error) return m.error;
    status=material(material_ctx,(const unsigned char *)"flare3",7,1,&value);if (status) return status;pm_store(&m,0x53d048u,value,4);
    pm_store(&m,0x53d058u,1,4);pm_store(&m,0x53d05cu,0xfffffff6u,4);pm_store(&m,0x53d06cu,127,1);pm_store(&m,0x53d06eu,127,1);pm_store(&m,0x53d06du,127,1);
    pm_store(&m,0x53d069u,0,1);pm_store(&m,0x53d068u,0,1);pm_store(&m,0x53d06bu,63,1);pm_store(&m,0x53d06au,63,1);pm_store(&m,0x53d064u,3000,4);pm_store(&m,0x53d060u,3000,4);
    for (i=0;i<9u;i++) { if (i==5u) pm_store(&m,0x53d070u,0,2);pm_store(&m,counters[i],0,4); }return m.error;
}
static void release_metadata(physics_memory *m,uint32_t kind,uint32_t slot)
{
    pm_store(m,0x53d100u+kind*4u,slot,4);pm_store(m,0x53d118u+(slot+kind*2u)*4u,0,4);
    pm_store(m,0x53d110u+kind*4u,pm_load(m,0x53d110u+kind*4u,4)-1u,4);pm_store(m,0x53d108u,pm_load(m,0x53d108u,4)-1u,4);
}
int frame_blast_slot_release(frame_data_read read,frame_data_write write,void *ctx,uint32_t kind,uint32_t slot,uint32_t limit)
{
    physics_memory m={read,write,ctx,0};uint32_t at,owner,o,i,n,id;int status;
    if (!read || !write || kind>1u || !limit) return -1;
    at=(kind ? 0x53cd20u+slot*264u : 0x53cfb0u+slot*74u);owner=pm_load(&m,at+(kind ? 256u : 68u),4);o=object_of(&m,owner);
    if (!kind) pm_store(&m,o+0x363u,UINT32_MAX,4);
    n=kind ? 12u : pm_load(&m,0x53d0b8u,4);
    for (i=0;pm_signed(i)<pm_signed(n);i++) {
        if (i>=limit) return -3;
        id=pm_load(&m,at+4u+i*(kind ? 20u : 4u)+(kind ? 0u : 4u),4);if (m.error) return m.error;
        status=frame_light_release(read,write,ctx,id);if (status) return status;
        if (!kind) n=pm_load(&m,0x53d0b8u,4);
    }
    if (kind) pm_store(&m,o+0x367u,UINT32_MAX,4);
    release_metadata(&m,kind,slot);return m.error;
}
static void publish_slot(physics_memory *m,uint32_t kind,uint32_t slot)
{
    uint32_t i;pm_store(m,0x53d100u+kind*4u,UINT32_MAX,4);pm_store(m,0x53d118u+(slot+kind*2u)*4u,pm_load(m,0x59055cu,4),4);
    for (i=0;i<2u;i++) if (!pm_load(m,0x53d118u+(kind*2u+i)*4u,4)) { pm_store(m,0x53d100u+kind*4u,i,4);break; }
}
int frame_blast_slot_allocate(frame_data_read read,frame_data_write write,void *ctx,uint32_t kind,uint32_t limit,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t total,n,i,j,victim_kind=0,victim=0,slot=0,found=0;int32_t oldest=0,time;int status;
    if (!read || !write || kind>1u || !limit || !out) return -1;
    total=pm_load(&m,0x53d108u,4);n=pm_load(&m,0x53d110u+kind*4u,4);
    if (total==2u && n==2u) {
        for (i=0;i<2u;i++) { time=pm_i32(&m,0x53d118u+(kind*2u+i)*4u);if (time && (!oldest || time<oldest)) { oldest=time;slot=i;found=1; } }
        if (m.error) return m.error;
        if (!found) return -4; /* Original selects an undefined caller word. */
        status=frame_blast_slot_release(read,write,ctx,kind,slot,limit);if (status) return status;
        pm_store(&m,0x53d100u+kind*4u,UINT32_MAX,4);pm_store(&m,0x53d118u+(kind*2u+slot)*4u,pm_load(&m,0x59055cu,4),4);
        n=pm_load(&m,0x53d110u+kind*4u,4);total=pm_load(&m,0x53d108u,4);pm_store(&m,0x53d110u+kind*4u,n+1u,4);pm_store(&m,0x53d108u,total+1u,4);
    } else {
        if (total==2u) {
            for (i=0;i<2u;i++) for (j=0;j<2u;j++) { time=pm_i32(&m,0x53d118u+(i*2u+j)*4u);if (time && (!oldest || time<oldest)) { oldest=time;victim_kind=i;victim=j;found=1; } }
            if (m.error) return m.error;
            if (!found) return -4;
            status=frame_blast_slot_release(read,write,ctx,victim_kind,victim,limit);if (status) return status;
        }
        n=pm_load(&m,0x53d110u+kind*4u,4);total=pm_load(&m,0x53d108u,4);pm_store(&m,0x53d108u,total+1u,4);slot=pm_load(&m,0x53d100u+kind*4u,4);pm_store(&m,0x53d110u+kind*4u,n+1u,4);publish_slot(&m,kind,slot);
    }
    if (m.error) return m.error;
    *out=slot;return 0;
}
static void camera(physics_memory *m,int32_t matrix[9])
{ uint32_t i,c=pm_load(m,0x54b4a8u,4);for (i=0;i<9u;i++) matrix[i]=pm_i32(m,0x74af00u+c*48u+i*4u); }
static void quad(physics_memory *m,uint32_t dst,uint32_t parameters,uint32_t angle,uint32_t origin,const int32_t frozen[9],uint32_t matrix_address,const int32_t *origin_values)
{
    uint32_t corner,i,j;int32_t x,y,c,s,v[3],rotated[3],world[3],values[9];const int32_t *matrix=frozen;
    for (corner=0;corner<4u;corner++) {
        x=corner<2u ? pm_i32(m,parameters) : 0;y=pm_i32(m,parameters+4u);if (corner==1u || corner==2u) y=pm_neg(y);
        s=pm_i32(m,0x597380u+(angle&65532u));c=pm_i32(m,0x59b380u+(angle&65532u));v[0]=pm_trunc(pm_sub(pm_mul(x,c),pm_mul(y,s)),12);v[1]=pm_trunc(pm_add(pm_mul(x,s),pm_mul(y,c)),12);v[2]=0;
        if (matrix_address) { for (i=0;i<9u;i++) values[i]=pm_i32(m,matrix_address+i*4u);matrix=values; }
        for (i=0;i<3u;i++) {
            if (!matrix) rotated[i]=v[i];
            else { rotated[i]=0;for (j=0;j<3u;j++) rotated[i]=pm_add(rotated[i],pm_mul(matrix[j*3u+i],v[j]));rotated[i]=pm_sar(rotated[i],12); }
        }
        for (i=0;i<3u;i++) world[i]=pm_add(rotated[i],origin_coordinate(m,origin,origin_values,i));
        for (i=0;i<3u;i++) pm_write32(m,dst+corner*12u+i*4u,world[i]);
    }
}
int frame_blast_quad(frame_data_read read,frame_data_write write,void *ctx,uint32_t dst,uint32_t parameters,uint32_t angle,uint32_t origin,uint32_t matrix)
{
    physics_memory m={read,write,ctx,0};if (!read || !write) return -1;
    quad(&m,dst,parameters,angle,origin,0,matrix,0);return m.error;
}
static uint32_t template_of(uint32_t kind) { return kind==1u ? 0x53d0c0u : kind==2u ? 0x53cf30u : 0x53cf70u; }
static void ring_colour(physics_memory *m,uint32_t kind,int32_t lifetime,int project,uint32_t record)
{
    uint32_t value=(uint32_t)pm_sar(lifetime,2),alpha=pm_load(m,record+260u,1);
    if (kind==1u) {
        pm_store(m,0x53d0f5u,127,1);
        if (project) { pm_store(m,0x53d0f7u,value+60u,1);pm_store(m,0x53d0f6u,value+60u,1);pm_store(m,0x53d06cu,alpha,1);pm_store(m,0x53d06eu,alpha>>1,1);pm_store(m,0x53d06du,alpha>>1,1); }
        else { pm_store(m,0x53d0f7u,(uint32_t)lifetime,1);pm_store(m,0x53d0f6u,(uint32_t)lifetime,1); }
    } else if (kind==2u) {
        pm_store(m,0x53cf67u,127,1);
        if (project) { pm_store(m,0x53cf66u,value,1);pm_store(m,0x53cf65u,value,1);pm_store(m,0x53d06eu,alpha,1); }
        else { pm_store(m,0x53cf66u,value,1);pm_store(m,0x53cf65u,value,1); }
    } else if (kind==3u) {
        if (pm_signed(value)>100) value=100u;
        if (project) { pm_store(m,0x53cfa7u,value,1);pm_store(m,0x53cfa5u,value,1);pm_store(m,0x53cfa6u,127,1);pm_store(m,0x53d06eu,alpha,1); }
        else { pm_store(m,0x53cfa6u,127,1);pm_store(m,0x53cfa7u,value,1);pm_store(m,0x53cfa5u,value,1); }
    }
    if (project && (kind==2u || kind==3u)) { pm_store(m,0x53d06cu,alpha>>1,1);pm_store(m,0x53d06du,alpha>>1,1); }
}
static int ring_start(frame_data_read read,frame_data_write write,void *ctx,uint32_t origin,const int32_t *origin_values,uint32_t owner,uint32_t kind,uint32_t limit,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t slot,b,i,j,at,angle,id,shape;int32_t matrix[9],radius,width;int status;
    if (!read || !write || !limit || !out || kind<1u || kind>3u) return -1;
    camera(&m,matrix);if (m.error) return m.error;status=frame_blast_slot_allocate(read,write,ctx,1,limit,&slot);if (status) return status;b=0x53cd20u+slot*264u;
    pm_store(&m,b+244u,(uint32_t)origin_coordinate(&m,origin,origin_values,0),4);pm_store(&m,b+248u,(uint32_t)origin_coordinate(&m,origin,origin_values,1),4);id=(uint32_t)origin_coordinate(&m,origin,origin_values,2);pm_store(&m,b+256u,owner,4);pm_store(&m,b,250,4);pm_store(&m,b+252u,id,4);pm_store(&m,b+261u,kind,1);
    ring_colour(&m,kind,pm_i32(&m,b),0,b);shape=template_of(kind);
    for (i=0;i<12u;i++) {
        at=b+4u+i*20u;width=kind==2u ? pm_add((int32_t)((random_next(&m)>>4)&511u),pm_i32(&m,b)) : 200;
        radius=pm_add((int32_t)((random_next(&m)>>4)&8191u),pm_mul(pm_i32(&m,b),4));angle=(random_next(&m)>>4)&((kind==2u || pm_load(&m,0x53e40cu,4)) ? 65535u : 32767u);
        pm_store(&m,at+16u,angle,4);pm_write32(&m,at+4u,radius);pm_write32(&m,at+8u,width);pm_store(&m,at+12u,0,4);quad(&m,shape,at+4u,angle,origin,matrix,0,origin_values);if (m.error) return m.error;
        status=frame_light_allocate(read,write,ctx,shape,&id);if (status) return status;pm_store(&m,at,id,4);
        if (id==UINT32_MAX) {
            for (j=i;j;j--) { id=pm_load(&m,b+4u+(j-1u)*20u,4);if (m.error) return m.error;status=frame_light_release(read,write,ctx,id);if (status) return status; }
            *out=UINT32_MAX;return m.error;
        }
    }
    if (m.error) return m.error;
    *out=slot;return 0;
}
int frame_blast_ring_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t slot,uint32_t limit,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t b,kind,i,at,angle;int32_t life,dt;int status;
    if (!read || !write || !limit || !out) return -1;
    b=0x53cd20u+slot*264u;life=pm_i32(&m,b);dt=pm_i32(&m,0x5904fcu);
    if (life<=dt) { if (m.error) return m.error;status=frame_blast_slot_release(read,write,ctx,1,slot,limit);if (status) return status;*out=UINT32_MAX;return 0; }
    life=pm_sub(life,dt);pm_write32(&m,b,life);pm_store(&m,b+260u,(uint32_t)pm_sar(life,1),1);pm_store(&m,b+262u,random_next(&m)>>4,2);
    for (i=0;i<12u;i++) {
        at=b+4u+i*20u;pm_store(&m,at+12u,0,4);kind=pm_load(&m,b+261u,1);
        angle=(random_next(&m)>>4)&((kind==2u || pm_load(&m,0x53e40cu,4)) ? 65535u : 32767u);pm_store(&m,at+16u,angle,4);
        pm_write32(&m,at+8u,kind==2u ? 200 : pm_add((int32_t)((random_next(&m)>>4)&511u),pm_i32(&m,b)));pm_write32(&m,at+4u,pm_add((int32_t)((random_next(&m)>>4)&8191u),pm_mul(pm_i32(&m,b),4)));
    }
    if (m.error) return m.error;
    *out=slot;return 0;
}
int frame_blast_ring_project(frame_data_read read,frame_data_write write,void *ctx,uint32_t slot,const frame_actor_services *graphics)
{
    physics_memory m={read,write,ctx,0};uint32_t b,kind,i,shape,id;int32_t matrix[9];int status;
    if (!read || !write) return -1;
    camera(&m,matrix);b=0x53cd20u+slot*264u;kind=pm_load(&m,b+261u,1);if (kind<1u || kind>3u) return m.error ? m.error : -4;
    shape=template_of(kind);ring_colour(&m,kind,pm_i32(&m,b),1,b);
    for (i=0;i<12u;i++) {
        quad(&m,shape,b+8u+i*20u,pm_load(&m,b+20u+i*20u,4),b+244u,matrix,0,0);id=pm_load(&m,b+4u+i*20u,4);if (m.error) return m.error;
        status=frame_light_replace(read,write,ctx,id,shape);if (status) return status;
    }
    for (i=0;i<3u;i++) pm_store(&m,0x53d04cu+i*4u,pm_load(&m,b+244u+i*4u,4),4);
    pm_store(&m,0x53d070u,pm_load(&m,b+262u,2),2);pm_store(&m,0x55ae2cu,1,4);
    if (m.error) return m.error;
    if (!graphics || !graphics->sprite) return -5;
    status=graphics->sprite(graphics->sprite_ctx,0x53d048u);if (status) return status;pm_store(&m,0x55ae2cu,0,4);return m.error;
}
static void band_point(physics_memory *m,uint32_t destination,int32_t radius,uint32_t angle,uint32_t origin,const int32_t matrix[9],int first,int rotate,const int32_t *origin_values)
{
    uint32_t i,j;int32_t v[3],out[3];
    v[0]=rotate ? pm_trunc(pm_mul(radius,pm_i32(m,0x59b380u+(angle&65532u))),12) : radius;v[1]=rotate ? pm_trunc(pm_mul(radius,pm_i32(m,0x597380u+(angle&65532u))),12) : 0;v[2]=0;
    for (i=0;i<3u;i++) { out[i]=0;for (j=0;j<3u;j++) out[i]=pm_add(out[i],pm_mul(v[j],matrix[j*3u+i]));out[i]=pm_add(pm_sar(out[i],12),origin_coordinate(m,origin,origin_values,i)); }
    if (first) { pm_write32(m,destination+4u,out[1]);pm_write32(m,destination,out[0]);pm_write32(m,destination+8u,out[2]); }
    else for (i=0;i<3u;i++) pm_write32(m,destination+i*4u,out[i]);
}
static void band_quad(physics_memory *m,int32_t inner,int32_t outer,uint32_t *angle,uint32_t origin,const int32_t matrix[9],int initial,const int32_t *origin_values)
{
    uint32_t i;
    if (initial) {
        band_point(m,0x53d078u,inner,0,origin,matrix,1,0,origin_values);band_point(m,0x53d084u,outer,0,origin,matrix,0,0,origin_values);
    } else {
        for (i=0;i<3u;i++) pm_store(m,0x53d078u+i*4u,pm_load(m,0x53d09cu+i*4u,4),4);
        for (i=0;i<3u;i++) pm_store(m,0x53d084u+i*4u,pm_load(m,0x53d090u+i*4u,4),4);
    }
    band_point(m,0x53d090u,outer,*angle,origin,matrix,0,1,origin_values);band_point(m,0x53d09cu,inner,*angle,origin,matrix,0,1,origin_values);
    *angle+=0x1555u;if (!initial && pm_signed(*angle)>65536) *angle=0;
}
static void band_colour(physics_memory *m,uint32_t at,int project)
{
    uint32_t alpha=((pm_load(m,at+4u,4)-pm_load(m,at,4))>>8)+(project ? 15u : 25u);
    if (!pm_load(m,at+72u,1)) { pm_store(m,0x53d0afu,100,1);pm_store(m,0x53d0adu,alpha,1); }
    else { alpha=((pm_load(m,at+4u,4)-pm_load(m,at,4))>>8)+25u;pm_store(m,0x53d0afu,alpha,1);pm_store(m,0x53d0adu,100,1); }
    pm_store(m,0x53d0aeu,pm_load(m,0x53d0adu,1),1);
}
static int band_start(frame_data_read read,frame_data_write write,void *ctx,uint32_t origin,const int32_t *origin_values,uint32_t outer,uint32_t owner,uint32_t flipped,uint32_t limit,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t slot,b,i,j,n,id,angle=0x1555u;int32_t matrix[9];int status;
    if (!read || !write || !limit || !out) return -1;
    camera(&m,matrix);if (m.error) return m.error;status=frame_blast_slot_allocate(read,write,ctx,0,limit,&slot);if (status) return status;b=0x53cfb0u+slot*74u;
    pm_store(&m,b,10,4);pm_store(&m,b+56u,(uint32_t)origin_coordinate(&m,origin,origin_values,0),4);pm_store(&m,b+60u,(uint32_t)origin_coordinate(&m,origin,origin_values,1),4);id=(uint32_t)origin_coordinate(&m,origin,origin_values,2);pm_store(&m,b+4u,outer,4);pm_store(&m,b+64u,id,4);pm_store(&m,b+68u,owner,4);pm_store(&m,b+72u,flipped,1);band_colour(&m,b,0);
    for (i=0;;i++) {
        if (i>=limit) return -3;
        band_quad(&m,pm_i32(&m,b),8,&angle,origin,matrix,i==0u,origin_values);if (m.error) return m.error;
        status=frame_light_allocate(read,write,ctx,0x53d078u,&id);if (status) return status;pm_store(&m,b+8u+i*4u,id,4);
        if (id==UINT32_MAX) {
            for (j=i;j;j--) { id=pm_load(&m,b+8u+(j-1u)*4u,4);if (m.error) return m.error;status=frame_light_release(read,write,ctx,id);if (status) return status; }
            *out=UINT32_MAX;return m.error;
        }
        n=pm_load(&m,0x53d0b8u,4);if (pm_signed(i+1u)>=pm_signed(n)) break;
    }
    if (m.error) return m.error;
    *out=slot;return 0;
}
int frame_blast_band_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t slot,uint32_t limit,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t b;int32_t radius;int status;
    if (!read || !write || !limit || !out) return -1;
    b=0x53cfb0u+slot*74u;radius=pm_add(pm_i32(&m,b),pm_mul(pm_i32(&m,0x5904fcu),45));pm_write32(&m,b,radius);
    if (pm_i32(&m,b+4u)<radius) { if (m.error) return m.error;status=frame_blast_slot_release(read,write,ctx,0,slot,limit);if (status) return status;*out=UINT32_MAX;return 0; }
    if (m.error) return m.error;
    *out=slot;return 0;
}
int frame_blast_band_project(frame_data_read read,frame_data_write write,void *ctx,uint32_t slot,uint32_t limit)
{
    physics_memory m={read,write,ctx,0};uint32_t b,i,id,angle=0x1555u;int32_t matrix[9];int status;
    if (!read || !write || !limit) return -1;
    camera(&m,matrix);b=0x53cfb0u+slot*74u;band_colour(&m,b,1);
    for (i=0;;i++) {
        if (i>=limit) return -3;
        band_quad(&m,pm_i32(&m,b),pm_sub(pm_i32(&m,b),pm_sar(pm_i32(&m,b),2)),&angle,b+56u,matrix,i==0u,0);id=pm_load(&m,b+8u+i*4u,4);if (m.error) return m.error;
        status=frame_light_replace(read,write,ctx,id,0x53d078u);if (status) return status;if (pm_signed(i+1u)>=pm_i32(&m,0x53d0b8u)) break;
    }
    return m.error;
}

int frame_blast_ring_start(frame_data_read read,frame_data_write write,void *ctx,uint32_t origin,uint32_t owner,uint32_t kind,uint32_t limit,uint32_t *out)
{ return ring_start(read,write,ctx,origin,0,owner,kind,limit,out); }
int frame_blast_band_start(frame_data_read read,frame_data_write write,void *ctx,uint32_t origin,uint32_t outer,uint32_t owner,uint32_t flipped,uint32_t limit,uint32_t *out)
{ return band_start(read,write,ctx,origin,0,outer,owner,flipped,limit,out); }
int frame_blast_ring_start_values(frame_data_read read,frame_data_write write,void *ctx,const int32_t origin[3],uint32_t owner,uint32_t kind,uint32_t limit,uint32_t *out)
{ if (!origin) return -1;return ring_start(read,write,ctx,0,origin,owner,kind,limit,out); }
int frame_blast_band_start_values(frame_data_read read,frame_data_write write,void *ctx,const int32_t origin[3],uint32_t outer,uint32_t owner,uint32_t flipped,uint32_t limit,uint32_t *out)
{ if (!origin) return -1;return band_start(read,write,ctx,0,origin,outer,owner,flipped,limit,out); }
