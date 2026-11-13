#include "frame_scene_effects.h"
#include "physics_memory.h"
int frame_scene_pools_initialize(frame_data_write write,void *ctx)
{
    physics_memory m={0,write,ctx,0};uint32_t i;if (!write) return -1;
    for (i=0;i<160u;i++) pm_store(&m,0x54cc8cu+i*64u,0xe0000000u,4);
    pm_store(&m,0x55ae1cu,0,4);
    for (i=0;i<100u;i++) pm_store(&m,0x54f494u+i*32u,0xe0000000u,4);
    pm_store(&m,0x55ae20u,0,4);return m.error;
}

static void release_record(physics_memory *m,uint32_t marker,uint32_t count)
{
    uint32_t remaining;
    if (pm_load(m,marker,4)==0xe0000000u) return;
    remaining=pm_load(m,count,4);pm_store(m,marker,0xe0000000u,4);
    pm_store(m,count,remaining-1u,4);
}
int frame_line_release(frame_data_read read,frame_data_write write,void *ctx,uint32_t id)
{
    physics_memory m={read,write,ctx,0};if (!read || !write) return -1;
    release_record(&m,0x54f494u+id*32u,0x55ae20u);return m.error;
}
int frame_light_release(frame_data_read read,frame_data_write write,void *ctx,uint32_t id)
{
    physics_memory m={read,write,ctx,0};if (!read || !write) return -1;
    release_record(&m,0x54cc8cu+id*64u,0x55ae1cu);return m.error;
}
static uint32_t reserve_line(physics_memory *m)
{
    uint32_t id;
    if (pm_load(m,0x55ae20u,4)==100u) return UINT32_MAX;
    for (id=0;id<100u;id++) if (pm_load(m,0x54f494u+id*32u,4)==0xe0000000u) break;
    if (id<100u) pm_store(m,0x55ae20u,pm_load(m,0x55ae20u,4)+1u,4);
    return id;
}
static void scale_line(physics_memory *m,uint32_t record)
{
    uint32_t i;for (i=0;i<6u;i++) pm_write32(m,record+i*4u,pm_sar(pm_i32(m,record+i*4u),3));
}
static void store_line_values(physics_memory *m,uint32_t record,const int32_t endpoints[6],const unsigned char colors[8])
{
    uint32_t i,j,value;
    for (i=0;i<6u;i++) pm_write32(m,record+i*4u,endpoints[i]);
    for (i=0;i<2u;i++) {
        value=0;for (j=0;j<4u;j++) value|=(uint32_t)colors[i*4u+j]<<(j*8u);
        pm_store(m,record+24u+i*4u,value,4);
    }
    scale_line(m,record);
}
int frame_line_update(frame_data_read read,frame_data_write write,void *ctx,uint32_t id,uint32_t source)
{
    physics_memory m={read,write,ctx,0};uint32_t i,record=0x54f490u+id*32u;
    if (!read || !write) return -1;
    for (i=0;i<8u;i++) pm_store(&m,record+i*4u,pm_load(&m,source+i*4u,4),4);
    scale_line(&m,record);return m.error;
}
int frame_line_update_values(frame_data_read read,frame_data_write write,void *ctx,uint32_t id,
                                const int32_t endpoints[6],const unsigned char colors[8])
{
    physics_memory m={read,write,ctx,0};
    if (!read || !write || !endpoints || !colors) return -1;
    store_line_values(&m,0x54f490u+id*32u,endpoints,colors);return m.error;
}
int frame_line_allocate(frame_data_read read,frame_data_write write,void *ctx,uint32_t source,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t id,record,i;
    if (!read || !write || !out) return -1;
    id=reserve_line(&m);
    if (m.error) return m.error;
    if (id<100u) {
        record=0x54f490u+id*32u;
        for (i=0;i<8u;i++) pm_store(&m,record+i*4u,pm_load(&m,source+i*4u,4),4);
        scale_line(&m,record);
    }
    if (m.error) return m.error;
    *out=id;return 0;
}
int frame_line_allocate_values(frame_data_read read,frame_data_write write,void *ctx,
                                  const int32_t endpoints[6],const unsigned char colors[8],uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t id;
    if (!read || !write || !endpoints || !colors || !out) return -1;
    id=reserve_line(&m);if (m.error) return m.error;
    if (id<100u) store_line_values(&m,0x54f490u+id*32u,endpoints,colors);
    if (m.error) return m.error;
    *out=id;return 0;
}
int frame_temp_effects_expire(frame_data_read read,frame_data_write write,void *ctx,uint32_t mode)
{
    physics_memory m={read,write,ctx,0};uint32_t row,column,id;
    if (!read || !write) return -1;
    for (row=0;row<3u;row++) for (column=0;column<3u;column++) {
        id=pm_load(&m,0x5461d4u+mode*136u+row*44u+column*4u,4);
        release_record(&m,0x54f494u+id*32u,0x55ae20u);
    }
    pm_store(&m,0x54669cu,mode,4);pm_store(&m,0x5461b0u+mode*136u,0,4);return m.error;
}
static uint32_t random_next(physics_memory *m)
{
    uint32_t seed=pm_load(m,0x55b6acu,4)*1664525u+1013904223u;pm_store(m,0x55b6acu,seed,4);return seed;
}
static void endpoint(physics_memory *m,uint32_t object,uint32_t yaw,uint32_t pitch,int32_t out[3])
{
    int32_t radius=pm_sar(pm_mul(pm_i32(m,0x59b380u+(pitch&0x3ffcu)),2700),12);
    out[0]=pm_add(pm_sar(pm_mul(pm_i32(m,0x59b380u+(yaw&0xfffcu)),radius),12),pm_i32(m,object));
    out[1]=pm_sub(pm_add(pm_sar(pm_mul(pm_i32(m,0x597380u+(pitch&0x3ffcu)),2700),12),pm_i32(m,object+4u)),1000);
    out[2]=pm_add(pm_sar(pm_mul(pm_i32(m,0x597380u+(yaw&0xfffcu)),radius),12),pm_i32(m,object+8u));
}
int frame_temp_effects_start(frame_data_read read,frame_data_write write,void *ctx,uint32_t object)
{
    physics_memory m={read,write,ctx,0};uint32_t slot,i,row,line,p,a,b,id,oldest;int32_t first[3],last[3],middle[3],points[6];unsigned char color[8]={0,0,0,1,0,0,0,0};int status;
    if (!read || !write) return -1;
    if (pm_i16(&m,object+0xdd0u)) { pm_store(&m,0x5461b0u+(uint32_t)pm_i16(&m,object+0xdceu)*136u,pm_load(&m,0x59055cu,4),4);return m.error; }
    slot=pm_load(&m,0x54669cu,4);
    if (slot==UINT32_MAX) {
        slot=0;oldest=pm_load(&m,0x5461b0u,4);for (i=1;i<4u;i++) { a=pm_load(&m,0x5461b0u+i*136u,4);if (a<oldest) { slot=i;oldest=a; } }
        if (m.error) return m.error;
        status=frame_temp_effects_expire(read,write,ctx,slot);if (status) return status;pm_store(&m,0x54669cu,UINT32_MAX,4);pm_store(&m,0x5461b0u+slot*136u,pm_load(&m,0x59055cu,4),4);
    } else {
        pm_store(&m,0x54669cu,UINT32_MAX,4);pm_store(&m,0x5461b0u+slot*136u,pm_load(&m,0x59055cu,4),4);
        for (i=0;i<4u;i++) if (!pm_load(&m,0x5461b0u+i*136u,4)) { pm_store(&m,0x54669cu,i,4);break; }
    }
    pm_store(&m,object+0xdceu,slot,2);p=0x5461c0u+slot*136u;
    for (row=0;row<3u;row++,p+=44u) {
        a=(random_next(&m)>>4)&65535u;b=(random_next(&m)>>4)&16383u;endpoint(&m,object,a,b,first);
        a=(random_next(&m)>>4)&65535u;b=(random_next(&m)>>4)&16383u;endpoint(&m,object,a,b,last);
        for (i=0;i<3u;i++) middle[i]=pm_sar(pm_add(first[i],last[i]),1);
        for (i=0;i<3u;i++) pm_write32(&m,p-12u+i*4u,first[i]);
        for (i=0;i<3u;i++) pm_write32(&m,p+i*4u,last[i]);
        for (line=0;line<2u;line++) {
            for (i=0;i<3u;i++) points[i]=first[i];
            for (i=0;i<3u;i++) points[i+3u]=pm_sub(pm_add(middle[i],(int32_t)((random_next(&m)>>4)&1023u)),512);
            color[0]=color[1]=(unsigned char)(((random_next(&m)>>13)&127u)+20u);pm_store(&m,p+12u,color[0],1);
            color[4]=color[5]=(unsigned char)(((random_next(&m)>>4)&127u)+20u);
            color[2]=(unsigned char)(((random_next(&m)>>13)&127u)+70u);pm_store(&m,p+13u,color[2],1);
            color[6]=(unsigned char)(((random_next(&m)>>4)&127u)+70u);
            if (m.error) return m.error;
            status=frame_line_allocate_values(read,write,ctx,points,color,&id);if (status) return status;pm_store(&m,p+20u+line*4u,id,4);
        }
        for (i=0;i<3u;i++) points[i+3u]=last[i];
        color[4]=color[5]=(unsigned char)(((random_next(&m)>>4)&127u)+20u);pm_store(&m,p+14u,color[4],1);
        color[6]=(unsigned char)(((random_next(&m)>>4)&127u)+70u);pm_store(&m,p+15u,color[6],1);
        if (m.error) return m.error;
        status=frame_line_allocate_values(read,write,ctx,points,color,&id);if (status) return status;pm_store(&m,p+28u,id,4);
    }
    return m.error;
}
int frame_temp_effects_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t object)
{
    physics_memory m={read,write,ctx,0};uint32_t p,row,i,a,b,id;int32_t points[6],first[3],last[3],middle[3];
    unsigned char color[8]={0,0,0,1,0,0,0,0};
    if (!read || !write) return -1;
    if (!pm_load(&m,object+0xdd0u,2)) return m.error;
    p=0x5461c0u+(uint32_t)pm_i16(&m,object+0xdceu)*136u;
    for (row=0;row<3u;row++,p+=44u) {
        a=(random_next(&m)>>4)&65535u;b=(random_next(&m)>>4)&16383u;endpoint(&m,object,a,b,first);
        a=(random_next(&m)>>4)&16383u;b=(random_next(&m)>>4)&65535u;endpoint(&m,object,b,a,last);
        for (i=0;i<3u;i++) middle[i]=pm_sar(pm_add(first[i],last[i]),1);
        for (i=0;i<3u;i++) pm_write32(&m,p-12u+i*4u,first[i]);
        for (i=0;i<3u;i++) pm_write32(&m,p+i*4u,last[i]);
        for (i=0;i<3u;i++) points[i]=first[i];
        for (i=0;i<3u;i++) points[i+3u]=pm_sub(pm_add(middle[i],(int32_t)((random_next(&m)>>4)&1023u)),512);
        color[0]=color[1]=(unsigned char)(((random_next(&m)>>13)&127u)+20u);pm_store(&m,p+12u,color[0],1);
        color[4]=color[5]=(unsigned char)(((random_next(&m)>>4)&127u)+20u);
        color[2]=(unsigned char)(((random_next(&m)>>13)&127u)+70u);pm_store(&m,p+13u,color[2],1);
        color[6]=(unsigned char)(((random_next(&m)>>4)&127u)+70u);
        id=pm_load(&m,p+20u,4);store_line_values(&m,0x54f490u+id*32u,points,color);
        for (i=0;i<3u;i++) points[i]=pm_sub(pm_add(middle[i],(int32_t)((random_next(&m)>>4)&1023u)),512);
        color[0]=color[1]=(unsigned char)(((random_next(&m)>>4)&127u)+20u);
        color[2]=(unsigned char)(((random_next(&m)>>4)&127u)+70u);
        id=pm_load(&m,p+24u,4);store_line_values(&m,0x54f490u+id*32u,points,color);
        for (i=0;i<3u;i++) points[i+3u]=last[i];
        color[4]=color[5]=(unsigned char)(((random_next(&m)>>4)&127u)+20u);pm_store(&m,p+14u,color[4],1);
        color[6]=(unsigned char)(((random_next(&m)>>4)&127u)+70u);pm_store(&m,p+15u,color[6],1);
        id=pm_load(&m,p+28u,4);store_line_values(&m,0x54f490u+id*32u,points,color);
        pm_store(&m,p+16u,(random_next(&m)>>4)&511u,2);pm_store(&m,p+18u,(random_next(&m)>>4)&511u,2);
        if (m.error) return m.error;
    }
    return m.error;
}
int frame_light_allocate_values(frame_data_read read,frame_data_write write,void *ctx,const int32_t vertices[12],uint32_t material,const unsigned char style[12],uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t id,at,i,j,value;
    if (!read || !write || !vertices || !style || !out) return -1;
    if (pm_load(&m,0x55ae1cu,4)==160u) { if (m.error) return m.error;*out=UINT32_MAX;return 0; }
    for (id=0;id<160u;id++) if (pm_load(&m,0x54cc8cu+id*64u,4)==0xe0000000u) break;
    if (id<160u) {
        at=0x54cc88u+id*64u;pm_store(&m,0x55ae1cu,pm_load(&m,0x55ae1cu,4)+1u,4);
        for (i=0;i<12u;i++) pm_write32(&m,at+i*4u,vertices[i]);
        pm_store(&m,at+48u,material,4);
        for (i=0;i<3u;i++) { value=0;for (j=0;j<4u;j++) value|=(uint32_t)style[i*4u+j]<<(j*8u);pm_store(&m,at+52u+i*4u,value,4); }
        for (i=0;i<12u;i++) pm_write32(&m,at+i*4u,pm_sar(pm_i32(&m,at+i*4u),3));
    }
    if (m.error) return m.error;
    *out=id;return 0;
}
int frame_light_update_values(frame_data_read read,frame_data_write write,void *ctx,uint32_t id,const int32_t vertices[12])
{
    physics_memory m={read,write,ctx,0};uint32_t i;
    if (!read || !write || !vertices) return -1;
    for (i=0;i<12u;i++) pm_write32(&m,0x54cc88u+id*64u+i*4u,pm_sar(vertices[i],3));
    return m.error;
}
static void replace_quad(physics_memory *m,uint32_t destination,uint32_t source)
{
    uint32_t i;for (i=0;i<16u;i++) pm_store(m,destination+i*4u,pm_load(m,source+i*4u,4),4);
    for (i=0;i<12u;i++) pm_write32(m,destination+i*4u,pm_sar(pm_i32(m,destination+i*4u),3));
}
int frame_light_replace(frame_data_read read,frame_data_write write,void *ctx,uint32_t id,uint32_t source)
{
    physics_memory m={read,write,ctx,0};if (!read || !write) return -1;
    replace_quad(&m,0x54cc88u+id*64u,source);return m.error;
}
int frame_light_allocate(frame_data_read read,frame_data_write write,void *ctx,uint32_t source,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t id;
    if (!read || !write || !out) return -1;
    if (pm_load(&m,0x55ae1cu,4)==160u) { if (m.error) return m.error;*out=UINT32_MAX;return 0; }
    for (id=0;id<160u;id++) if (pm_load(&m,0x54cc8cu+id*64u,4)==0xe0000000u) break;
    if (id<160u) { pm_store(&m,0x55ae1cu,pm_load(&m,0x55ae1cu,4)+1u,4);replace_quad(&m,0x54cc88u+id*64u,source); }
    if (m.error) return m.error;
    *out=id;return 0;
}
