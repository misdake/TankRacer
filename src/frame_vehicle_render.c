#include "frame_vehicle_render.h"
#include "frame_math.h"
#include "physics_memory.h"
#define TRY(x) do { int rc;if (m.error) return m.error;rc=(x);if (rc) return rc; } while (0)
static void matrix_read(physics_memory *m,uint32_t at,int32_t matrix[12])
{ uint32_t i;for (i=0;i<12u;i++) matrix[i]=pm_i32(m,at+i*4u); }
static int position(physics_memory *m,uint32_t o,const int32_t v[3],int32_t out[3])
{
    uint32_t matrix=pm_load(m,o+20u,4),i,j;
    if (!matrix) return m->error ? m->error : -4; /* Original output stack remains uninitialized. */
    for (i=0;i<3u;i++) { int32_t n=0;for (j=0;j<3u;j++) n=pm_add(n,pm_mul(v[j],pm_i32(m,matrix+(j*3u+i)*4u)));out[i]=pm_add(pm_i32(m,o+i*4u),pm_sar(n,12)); }return m->error;
}
static void mesh_colour(physics_memory *m,uint32_t shape,uint32_t colour)
{ uint32_t i,count=pm_load(m,shape+2u,2),faces=pm_load(m,shape+8u,4);for (i=0;i<count;i++) pm_store(m,faces+i*24u+6u,colour,2); }
static uint32_t random_next(physics_memory *m)
{ uint32_t n=pm_load(m,0x55b6acu,4)*1664525u+1013904223u;pm_store(m,0x55b6acu,n,4);return n; }
int frame_vehicle_smoke_templates(frame_data_read read,frame_data_write write,void *ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t material;if (!read || !write) return -1;
    material=pm_load(&m,0x5466a4u,4);pm_store(&m,0x54b494u,material,4);
    pm_store(&m,0x545de8u,material,4);pm_store(&m,0x545dfcu,0,4);pm_store(&m,0x545e08u,1,1);pm_store(&m,0x545e09u,1,1);pm_store(&m,0x545e0au,63,1);pm_store(&m,0x545e0bu,63,1);return m.error;
}
static uint32_t cockpit_camera(physics_memory *m,uint32_t o)
{
    uint32_t slot,id,record,camera;
    if (!pm_load(m,o+0xde5u,1)) return 0;
    slot=(uint32_t)pm_i8(m,o+0xdf0u);id=pm_load(m,0x53cc38u+slot*4u,4);record=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);camera=record ? pm_load(m,record+4u,4) : 0;
    if (!camera) { if (!m->error) m->error=-4;return 0; }
    return pm_load(m,camera+0x98u,4)==2u && slot==pm_load(m,0x54b4a8u,4) ? camera : 0u;
}
int frame_vehicle_view_feedback(frame_data_read read,frame_data_write write,void *ctx,uint32_t o)
{
    physics_memory m={read,write,ctx,0};uint32_t camera,i;
    if (!read || !write) return -1;
    camera=cockpit_camera(&m,o);pm_store(&m,o+0xdecu,camera!=0u,1);
    if (camera) for (i=0;i<3u;i++) pm_store(&m,camera+0x78u+i*4u,pm_load(&m,o+0x302u+i*4u,4),4);
    return m.error;
}
int frame_vehicle_self_layer(frame_data_read read,void *ctx,uint32_t o,uint32_t part,uint32_t *mode)
{
    physics_memory m={read,0,ctx,0};uint32_t game;
    if (!read || !mode) return -1;
    *mode=0;
    if (!(pm_load(&m,0x5964d4u,4)&32768u) || !pm_load(&m,o+0xde5u,1) || (part!=0u && part!=1u && part!=6u)) return m.error;
    game=pm_load(&m,0x52ee9cu,4);if (game==2u || game==3u) return m.error;
    /* Case6 deliberately lacks the shield/DE4 gate used by cases0/1. */
    if (part!=6u && (pm_i32(&m,o+0xdafu)>0 || pm_load(&m,o+0xde4u,1))) return m.error;
    *mode=pm_load(&m,0x53e924u,4) ? 2u : 1u;return m.error;
}
int frame_vehicle_cockpit_draw(frame_data_read read,frame_data_write write,void *ctx,uint32_t o,uint32_t *handled,const frame_vehicle_render_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t camera,i,j;int32_t pos[3],matrix[12],local_z;
    if (!read || !write || !handled || !s || !s->mesh || !s->sprite) return -1;
    *handled=0;pm_store(&m,o+0xdecu,0,1);
    camera=cockpit_camera(&m,o);if (!camera) return m.error;
    *handled=1;pm_write32(&m,0x55ade4u,pm_sub(pm_i16(&m,o+0x4eu),100));
    for (i=0;i<3u;i++) pos[i]=pm_i32(&m,o+0x5e6u+i*4u);
    matrix_read(&m,o+24u,matrix);
    TRY(s->mesh(s->ctx,pm_load(&m,o+0x5d6u,4),pos,matrix,pm_load(&m,o+0xde4u,1)!=0u));
    for (i=0;i<3u;i++) pm_write32(&m,camera+0x78u+i*4u,pm_i32(&m,o+0x302u+i*4u));
    local_z=pm_i32(&m,o+0x764u);if (local_z>=0) local_z=0;local_z=pm_add(pm_mul(local_z,4),5120);
    for (i=0;i<3u;i++) { int32_t n=0;for (j=0;j<3u;j++) n=pm_add(n,pm_mul(j==2u ? local_z : 0,pm_i32(&m,o+0x31au+(j*3u+i)*4u)));pm_write32(&m,0x531bacu+i*4u,pm_add(pm_i32(&m,o+0x30eu+i*4u),pm_sar(n,12))); }
    pm_store(&m,0x531ba8u,pm_load(&m,0x5466c0u,4),4);TRY(s->sprite(s->ctx,0x531ba8u));pm_store(&m,o+0xdecu,1,1);return m.error;
}
int frame_vehicle_aerial_draw(frame_data_read read,frame_data_write write,void *ctx,uint32_t o,const frame_vehicle_render_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t shape,segment,i,j;int32_t matrix[12],socket[3],pos[3],pitch,roll,height;
    if (!read || !s || !s->mesh) return -1;
    shape=pm_load(&m,o+0x56eu,4);
    if (!shape || !pm_load(&m,o+0xde5u,1) || pm_load(&m,0x54b480u,4)) return m.error;
    for (i=0;i<3u;i++) socket[i]=pm_i32(&m,o+0x572u+i*4u);
    for (i=0;i<3u;i++) { int32_t n=0;for (j=0;j<3u;j++) n=pm_add(n,pm_mul(socket[j],pm_i32(&m,o+0x31au+(j*3u+i)*4u)));pos[i]=pm_add(pm_i32(&m,o+0x30eu+i*4u),pm_sar(n,12)); }
    matrix_read(&m,o+0x2b2u,matrix);pitch=pm_i32(&m,o+0x774u);roll=pm_i32(&m,o+0x77cu);height=pm_i32(&m,o+0x652u);
    if (m.error) return m.error;
    for (segment=0;segment<4u;segment++) {
        physics_matrix_rotate(matrix,0,(uint32_t)pitch,frame_math_trig());physics_matrix_rotate(matrix,2,(uint32_t)roll,frame_math_trig());
        TRY(s->mesh(s->ctx,shape,pos,matrix,0));
        pitch=pm_trunc(pm_mul(pitch,900),10);roll=pm_trunc(pm_mul(roll,900),10);
        for (i=0;i<3u;i++) pos[i]=pm_add(pos[i],pm_sar(pm_mul(height,matrix[3u+i]),12));
    }
    return m.error;
}
static int rear_lights(physics_memory *m,uint32_t o,uint32_t at,const frame_vehicle_render_services *s)
{
    uint32_t side,i,j,matrix=pm_load(m,o+20u,4);int32_t v[3],out[3];
    if (!matrix) return m->error ? m->error : -4;
    pm_store(m,at,pm_load(m,0x5466a0u,4),4);pm_write32(m,at+20u,pm_i16(m,o+0x4eu));
    for (side=0;side<2u;side++) {
        for (i=0;i<3u;i++) v[i]=pm_i32(m,o+0x656u+i*4u);
        if (!side) v[0]=pm_neg(v[0]);
        for (i=0;i<3u;i++) { int32_t n=0;for (j=0;j<3u;j++) n=pm_add(n,pm_mul(v[j],pm_i32(m,matrix+(j*3u+i)*4u)));out[i]=pm_add(pm_i32(m,o+0x78u+i*4u),pm_sar(n,12)); }
        for (i=0;i<3u;i++) pm_write32(m,at+4u+i*4u,out[i]);
        if (m->error) return m->error;
        { int status=s->sprite(s->ctx,at);if (status) return status; }
    }
    return m->error;
}
int frame_vehicle_brake_lights_draw(frame_data_read read,frame_data_write write,void *ctx,uint32_t o,const frame_vehicle_render_services *s)
{
    physics_memory m={read,write,ctx,0};int32_t speed;
    if (!read || !write || !s || !s->sprite) return -1;
    speed=pm_i32(&m,o+0xecu);
    if (!pm_i32(&m,o+0x98u) || pm_i32(&m,o+0x94u) || (speed<1000 && (!pm_load(&m,o+0x10bdu,1) || speed>=-1000))) return m.error;
    return rear_lights(&m,o,0x5322d0u,s);
}
int frame_vehicle_reverse_lights_draw(frame_data_read read,frame_data_write write,void *ctx,uint32_t o,const frame_vehicle_render_services *s)
{
    physics_memory m={read,write,ctx,0};
    if (!read || !write || !s || !s->sprite) return -1;
    if (pm_i32(&m,o+0x94u)>=0 || pm_i32(&m,o+0x98u)) return m.error;
    return rear_lights(&m,o,0x532300u,s);
}
int frame_vehicle_boost_draw(frame_data_read read,frame_data_write write,void *ctx,uint32_t o,const frame_vehicle_render_services *s)
{
    physics_memory m={read,write,ctx,0};int32_t matrix[12],pos[3],offset[3],angle,delta;uint32_t packed=1,shape,i,shade,depth;
    if (!read || !write || !s || !s->sprite || !s->mesh) return -1;
    if (!pm_load(&m,o+0xdccu,1)) return m.error;
    matrix_read(&m,o+24u,matrix);
    if (!pm_load(&m,0x53e96cu,4)) { uint32_t roll=(random_next(&m)>>4)&65535u;physics_matrix_rotate(matrix,2,roll,frame_math_trig());packed=((random_next(&m)&48u)<<7)|1u; }
    for (i=0;i<3u;i++) offset[i]=pm_i32(&m,o+0xdb3u+i*4u);
    TRY(position(&m,o,offset,pos));depth=(uint32_t)pm_add(pm_i16(&m,o+0x4eu),pm_i32(&m,o+0xd95u));pm_store(&m,0x55ade4u,depth,4);
    /* MOV AX preserves depth's high word before the signed SAR. */
    shade=(uint32_t)pm_sar(pm_signed((depth&0xffff0000u)|pm_load(&m,0x58ee12u,2)),4)*2u+16u;
    packed^=(shade^packed)&62u;packed=(packed&0xfffff83fu)|((packed&62u)<<5);packed^=(((packed&0xfffff800u)+0x4000u)^packed)&0xf800u;
    shape=pm_load(&m,0x58ee0cu,4);mesh_colour(&m,shape,packed);
    if (pm_load(&m,0x5964d4u,4)&32768u) { TRY(s->mesh(s->ctx,shape,pos,matrix,1)); }
    for (i=0;i<3u;i++) pm_write32(&m,0x5466f4u+i*4u,pos[i]);
    pm_store(&m,0x546704u,depth,4);pm_store(&m,0x546714u,((packed>>1)&31u)*4u+10u,1);pm_store(&m,0x546715u,((packed>>1)&31u)*4u+10u,1);pm_store(&m,0x546716u,((packed>>11)&31u)*8u+10u,1);
    TRY(physics_angle_lookup(pm_i32(&m,o+0x30u),pm_i32(&m,o+0x38u),frame_math_trig(),&angle));pm_store(&m,0x546718u,(uint32_t)pm_neg(angle),2);delta=pm_i16(&m,0x58ee12u);
    pm_write32(&m,0x546708u,pm_add(pm_mul(delta,4),1300));pm_write32(&m,0x54670cu,pm_add(pm_mul(delta,2),110));TRY(s->sprite(s->ctx,0x5466f0u));
    pm_store(&m,0x546718u,pm_load(&m,0x546718u,2)+16380u,2);pm_write32(&m,0x546708u,pm_add(pm_mul(delta,4),1100));pm_write32(&m,0x54670cu,pm_add(pm_mul(delta,2),100));TRY(s->sprite(s->ctx,0x5466f0u));
    pm_write32(&m,0x546708u,pm_add(pm_mul(delta,4),700));pm_write32(&m,0x54670cu,pm_add(pm_mul(delta,4),700));TRY(s->sprite(s->ctx,0x5466f0u));return m.error;
}
int frame_vehicle_shield_draw(frame_data_read read,frame_data_write write,void *ctx,uint32_t o,const frame_vehicle_render_services *s)
{
    physics_memory m={read,write,ctx,0};int32_t matrix[12],pos[3];uint32_t shape;
    static const int32_t offset[3]={0,-1200,0}; /* Immutable5317F0, absent from gameplay bootstrap. */
    if (!read || !write || !s || !s->mesh) return -1;
    matrix_read(&m,o+24u,matrix);TRY(position(&m,o,offset,pos));shape=pm_load(&m,0x58fa50u,4);
    if (pm_load(&m,0x545de4u,1) || pm_i8(&m,0x545de0u)>1) mesh_colour(&m,shape,pm_load(&m,o+0xda3u,2));
    physics_matrix_rotate(matrix,1,pm_load(&m,o+0xdabu,4),frame_math_trig());TRY(s->mesh(s->ctx,shape,pos,matrix,1));return m.error;
}
int frame_vehicle_orbs_draw(frame_data_read read,frame_data_write write,void *ctx,uint32_t o,const frame_vehicle_render_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t i,j,k,base,size,colour;
    if (!read || !write || !s || !s->sprite) return -1;
    if (!pm_load(&m,o+0xdd0u,2)) return m.error;
    for (i=0;i<3u;i++) { base=0x5461b4u+(uint32_t)pm_i16(&m,o+0xdceu)*136u+i*44u;
        for (j=0;j<2u;j++) { for (k=0;k<3u;k++) pm_store(&m,0x546434u+k*4u,pm_load(&m,base+j*12u+k*4u,4),4);
            size=pm_load(&m,base+28u+j*2u,2)+100u;pm_store(&m,0x546448u,size,4);pm_store(&m,0x54644cu,size,4);
            colour=pm_load(&m,base+24u+j*2u,1)-(j ? 0u : 10u);pm_store(&m,0x546454u,colour,1);pm_store(&m,0x546455u,colour,1);pm_store(&m,0x546456u,pm_load(&m,base+25u+j*2u,1)-(j ? 0u : 10u),1);TRY(s->sprite(s->ctx,0x546430u));
        }
    }return m.error;
}
int frame_vehicle_mine_glow_draw(frame_data_read read,frame_data_write write,void *ctx,uint32_t o,const frame_vehicle_render_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t i,j,bank,at,size;
    if (!read || !write || !s || !s->sprite) return -1;
    if (!pm_load(&m,o+0xe08u,1)) return m.error;
    bank=pm_load(&m,o+0xde5u,1);if (pm_load(&m,0x58e7acu,4) && !bank) bank=pm_load(&m,o+0x1135u,4);
    for (i=0;i<4u;i++) { at=0x58ee34u+bank*160u+i*39u;for (j=0;j<3u;j++) pm_store(&m,0x546404u+j*4u,pm_load(&m,at+7u+j*4u,4),4);
        size=pm_load(&m,at,4);pm_write32(&m,0x546414u,pm_i16(&m,o+0x4eu));pm_store(&m,0x546418u,size,4);pm_store(&m,0x54641cu,size,4);pm_store(&m,0x546424u,pm_sar(pm_signed(size),2),1);pm_store(&m,0x546426u,pm_sar(pm_signed(size),2),1);pm_store(&m,0x546428u,pm_load(&m,at+5u,2),2);TRY(s->sprite(s->ctx,0x546400u));
    }return m.error;
}
#undef TRY
