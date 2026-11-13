#include "frame_spin.h"
#include "frame_text.h"
#include "physics_memory.h"
#include "physics_vector.h"
static uint32_t object_of(physics_memory *m,uint32_t id)
{ uint32_t r=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);return r ? pm_load(m,r+4u,4) : 0; }
#define TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
static int find(physics_memory *m,uint32_t parser,uint32_t a,uint32_t b,uint32_t limit,uint32_t *found)
{ int status=frame_text_find(m->read,m->write,m->ctx,parser,a,limit,found);if (status || *found) return status;return frame_text_find(m->read,m->write,m->ctx,parser,b,limit,found); }
static int transform(physics_memory *m,uint32_t matrix,int32_t v[3],int inverse)
{ unsigned i,j;int32_t out[3];if (!matrix) return m->error;for (i=0;i<3u;i++) { out[i]=0;for (j=0;j<3u;j++) out[i]=pm_add(out[i],pm_mul(v[j],pm_i32(m,matrix+(inverse ? i*3u+j : j*3u+i)*4u)));out[i]=pm_sar(out[i],12); }for (i=0;i<3u;i++) v[i]=out[i];return m->error; }
static int send(physics_memory *m,uint32_t id,uint32_t global,uint32_t has,uint32_t payload,const frame_entity_factory_services *e)
{ uint32_t d=pm_load(m,global,4),unused;if (m->error) return m->error;return frame_entity_dispatch(m->read,m->write,m->ctx,id,d,has,payload,e->method,e->method_ctx,&unused); }
int frame_spin_initialize(frame_data_read read,frame_data_write write,void *ctx,uint32_t id,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o,parent,unused,i;int status;static const unsigned values[]={10,1,8,1,7,1,2,80,0,10,4,800};
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,id);parent=pm_load(&m,0x544f80u,4);TRY(frame_class_dispatch(read,ctx,parent,id,e->method,e->method_ctx,&unused));
    pm_store(&m,o+0xb2u,70,2);pm_store(&m,o+0xb4u,25,1);pm_store(&m,o+0x7cu,10,4);pm_store(&m,o+0x5cu,20,4);
    for (i=0;i<12u;i++) pm_store(&m,o+0xdau+i*2u,values[i],2);
    return m.error;
}
int frame_spin_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o,parent,unused,found,more,base,count,i,j,a,b,equal,value;unsigned char name[28];int status;
    static const uint32_t fields[][4]={{0x52e380u,0x52e374u,0xb2u,0x2u},{0x52e368u,0x52e35cu,0xb4u,0x1u},{0x52e330u,0x52e324u,0x7cu,0x4u},{0x52e318u,0x52e30cu,0x5cu,0x4u},{0x5313f4u,0x5318fcu,0xdau,0x2u},{0x52e2bcu,0x52e2acu,0xdcu,0x2u},{0x52e29cu,0x52e28cu,0xdeu,0x2u},{0x5313e4u,0x5318ecu,0xe0u,0x2u},{0x5313d4u,0x5318dcu,0xe2u,0x2u},{0x5313c4u,0x5318ccu,0xe4u,0x2u},{0x5313b4u,0x5318bcu,0xe6u,0x2u},{0x52e2fcu,0x52e2ecu,0xe8u,0x2u},{0x52e2dcu,0x52e2ccu,0xeau,0x2u},{0x5313a4u,0x5318acu,0xecu,0x2u},{0x531394u,0x53189cu,0xeeu,0x2u},{0x531384u,0x53188cu,0xf0u,0x2u},{0x52e27cu,0x52e26cu,0xaeu,0x4u}};
    static const unsigned schema[][3]={{0xb2,0x1a,0x2},{0xb4,0x19,0x1},{0x7c,0x1d,0x0},{0x5c,0x20,0x3},{0xda,0x1e,0x2},{0xdc,0x22,0x2},{0xde,0x24,0x2},{0xe0,0x26,0x2},{0xe2,0x28,0x2},{0xe4,0x2a,0x2},{0xe6,0x2c,0x2},{0xe8,0x2e,0x2},{0xea,0x30,0x2},{0xec,0x32,0x2},{0xee,0x34,0x2},{0xf0,0x36,0x2},{0xae,0x38,0x3}};
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,entity);parent=pm_load(&m,0x544f80u,4);TRY(frame_class_dispatch_payload(read,ctx,parent,entity,parser,e->method,e->method_ctx,&unused));
    TRY(find(&m,parser,0x52e394u,0x52e38cu,e->work_limit,&found));
    if (found) {
        base=pm_load(&m,0x544f84u,4);count=pm_load(&m,0x544f88u,4);TRY(frame_text_next(read,write,ctx,parser,name,sizeof(name),e->work_limit,&more));
        for (i=0;pm_signed(i)<pm_signed(count);i++) {
            if (i>=e->work_limit) return -3;
            equal=0;
            for (j=0;j<sizeof(name);j++) {
                a=name[j];b=pm_load(&m,base+i*76u+j,1);if (m.error) return m.error;
                if (a>=128u || b>=128u) return -5;
                if (a>=65u && a<=90u) a+=32u;
                if (b>=65u && b<=90u) b+=32u;
                if (a!=b) break;
                if (!a) { equal=1;break; }
            }
            if (equal) {
                for (j=0;j<17u;j++) {
                    a=base+i*76u+schema[j][1];b=schema[j][2];
                    value=b==0u ? (uint32_t)pm_i8(&m,a) : b==3u ? (uint32_t)pm_i16(&m,a) : pm_load(&m,a,b);
                    pm_store(&m,o+schema[j][0],value,b==0u || b==3u ? 4u : b);
                }
                break;
            }
        }
    }
    for (i=0;i<17u;i++) {
        TRY(find(&m,parser,fields[i][0],fields[i][1],e->work_limit,&found));
        if (found) {
            if (fields[i][3]==4u) TRY(frame_text_read_i32(read,write,ctx,parser,o+fields[i][2],e->work_limit,&more));
            else { value=parser;TRY(frame_text_read_value(read,write,ctx,parser,e->work_limit,&value,&more));pm_store(&m,o+fields[i][2],value,fields[i][3]); }
            if (i==16u) pm_store(&m,o+0xaeu,pm_load(&m,o+0xaeu,4)*182u,4);
        }
    }
    pm_store(&m,o+0x80u,0,4);return m.error;
}
int frame_spin_hit(frame_data_read read,frame_data_write write,void *ctx,uint32_t id,uint32_t vehicle,const frame_entity_factory_services *e,const physics_trig *trig,frame_effect_sink feedback,void *feedback_ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t o,tank,i,matrix,unused;int32_t relative[3],local[3],velocity[3],rotated[3],angle,spin,factor;int status;
    if (!read || !write || !e || !e->work_limit || !trig) return -1;
    o=object_of(&m,id);tank=object_of(&m,vehicle);relative[0]=pm_sub(pm_i32(&m,tank),pm_i32(&m,o));relative[1]=0;relative[2]=pm_sub(pm_i32(&m,tank+8u),pm_i32(&m,o+8u));
    TRY(transform(&m,pm_load(&m,tank+20u,4),relative,1));angle=pm_sar(pm_mul(pm_i32(&m,o+0xaeu),relative[0]),11);
    pm_write32(&m,o+0xd2u,pm_sar(pm_mul(pm_mul(pm_i16(&m,o+0xdau),pm_sar(pm_i32(&m,tank+0xecu),6)),relative[0]),11));pm_store(&m,o+0xd6u,0,4);pm_store(&m,o+0xceu,0,4);
    matrix=pm_load(&m,o+20u,4);if (!matrix) return -4;
    for (i=0;i<3u;i++) local[i]=pm_i32(&m,tank+0x50u+i*4u);
    TRY(transform(&m,matrix,local,1));
    if (pm_abs(local[0])<pm_abs(local[2])) {
        pm_store(&m,o+0xb6u,0,4);spin=pm_sar(pm_mul(pm_i16(&m,o+0xdcu),pm_sar(pm_abs(pm_i32(&m,tank+0xecu)),6)),pm_load(&m,o+0xdeu,1));pm_write32(&m,o+0xceu,spin);if (local[2]>0) pm_write32(&m,o+0xceu,pm_neg(spin));
    } else if (pm_abs(local[0])>pm_abs(local[2])) {
        pm_store(&m,o+0xb6u,1,4);spin=pm_sar(pm_mul(pm_i16(&m,o+0xe0u),pm_sar(pm_abs(pm_i32(&m,tank+0xecu)),6)),pm_load(&m,o+0xe2u,1));pm_write32(&m,o+0xd6u,spin);if (local[0]<0) pm_write32(&m,o+0xd6u,pm_neg(spin));
    }
    factor=pm_add(pm_sub(pm_i16(&m,o+0xb2u),pm_i32(&m,o+0x5cu)),128);
    for (i=0;i<3u;i++) pm_write32(&m,o+0x50u+i*4u,pm_sar(pm_mul(factor,pm_i32(&m,tank+0x50u+i*4u)),13));
    for (i=0;i<3u;i++) velocity[i]=pm_i32(&m,o+0x50u+i*4u);
    physics_vector_yaw(velocity,rotated,(uint32_t)angle,trig);pm_write32(&m,o+0x50u,rotated[0]);pm_write32(&m,o+0x58u,rotated[2]);pm_write32(&m,o+0x54u,rotated[1]);
    pm_store(&m,o+0xaau,1,4);
    if (!pm_load(&m,o+0x80u,4) && !(pm_load(&m,o+16u,1)&32u)) { /*41C2B0 is audio only.*/pm_store(&m,o+0x80u,1,4);TRY(frame_entity_activate(read,write,ctx,id,e->method,e->method_ctx,&unused)); }
    if (pm_load(&m,o+0x88u,4)) { TRY(send(&m,id,0x545ee4u,1,vehicle,e));pm_store(&m,o+0x80u,0,4); }
    if (pm_load(&m,tank+0xde5u,1)) { i=pm_load(&m,o+0x48u,4);TRY(frame_feedback_impact(read,write,ctx,pm_load(&m,i+12u,2),feedback,feedback_ctx)); }
    return m.error;
}
/* Original writes the un-clamped angular step first, then a second zero on
 * crossing. Keep both stores; integration uses the pre-damped velocity. */
static void damp(physics_memory *m,uint32_t at,int32_t deceleration,int include_zero)
{
    int32_t v=pm_i32(m,at),next;
    if (v>0) { next=pm_sub(v,deceleration);pm_write32(m,at,next);if (next<0) pm_store(m,at,0,4); }
    else if (v<0 || include_zero) { next=pm_add(v,deceleration);pm_write32(m,at,next);if (next>0) pm_store(m,at,0,4); }
}
static int pivot(physics_memory *m,uint32_t o,const int32_t edge[3],unsigned axis,uint32_t angle,const physics_trig *trig)
{
    uint32_t matrix=pm_load(m,o+20u,4);int32_t before[3],after[3];unsigned i;int status;
    if (!matrix) return -4;
    for (i=0;i<3u;i++) before[i]=after[i]=edge[i];
    status=transform(m,matrix,before,0);if (status) return status;
    /*443E10 applies Y, X, Z even for zero angles; these emit matrix stores.*/
    status=frame_matrix_rotate(m->read,m->write,m->ctx,matrix,1,0,trig);if (status) return status;
    status=frame_matrix_rotate(m->read,m->write,m->ctx,matrix,0,axis==0u ? angle : 0u,trig);if (status) return status;
    status=frame_matrix_rotate(m->read,m->write,m->ctx,matrix,2,axis==2u ? angle : 0u,trig);if (status) return status;
    status=transform(m,matrix,after,0);if (status) return status;
    pm_write32(m,o+4u,pm_add(pm_i32(m,o+4u),pm_sub(before[1],after[1])));pm_write32(m,o,pm_add(pm_i32(m,o),pm_sub(before[0],after[0])));pm_write32(m,o+8u,pm_add(pm_i32(m,o+8u),pm_sub(before[2],after[2])));return m->error;
}
int frame_spin_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t id,const frame_entity_factory_services *e,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t o,matrix,shape,root,timer,i,j,aux,axis;int32_t dt,ground,n[3],local[3],corners[4][3],angle[2],v,dec,edge[3],dot,x,z,position[3];int status;
    if (!read || !write || !e || !e->work_limit || !trig) return -1;
    o=object_of(&m,id);
    if (pm_load(&m,o+0x80u,4)) {
        matrix=pm_load(&m,o+20u,4);shape=pm_load(&m,o+0x48u,4);root=pm_load(&m,0x55ae60u,4);dt=pm_i32(&m,0x5904fcu);
        if (pm_load(&m,o+0xaau,4)) {
            TRY(frame_matrix_rotate(read,write,ctx,matrix,1,(uint32_t)pm_sar(pm_mul(dt,pm_i32(&m,o+0xd2u)),12),trig));
            damp(&m,o+0xd2u,pm_sar(pm_mul(pm_i16(&m,o+0xe8u),dt),pm_load(&m,o+0xeau,1)),1);
            if (pm_abs(pm_i32(&m,o+0xd2u))<=pm_i16(&m,o+0xf0u)) pm_store(&m,o+0xaau,0,4);
        }
        timer=pm_load(&m,o+0xb5u,1);pm_store(&m,o+0xb5u,timer+1u,1);
        if (!(timer%3u)) {
            status=physics_terrain_normal_height(read,ctx,root,pm_i32(&m,o),pm_i32(&m,o+8u),&ground,n);if (status<0) return status;
            if (ground!=-2147483647) for (i=0;i<3u;i++) pm_write32(&m,o+0xc2u+i*4u,n[i]);
            pm_write32(&m,o+0xbau,ground);
            if (pm_load(&m,shape+12u,2)>6144u) {
                for (i=0;i<4u;i++) { corners[i][0]=pm_mul(pm_i16(&m,shape+((i&1u) ? 0x22u : 0x1cu)),8);corners[i][1]=0;corners[i][2]=pm_mul(pm_i16(&m,shape+((i&2u) ? 0x20u : 0x26u)),8);TRY(transform(&m,matrix,corners[i],0)); }
                for (i=0;i<4u;i++) { status=physics_terrain_height(read,ctx,root,pm_add(corners[i][0],pm_i32(&m,o)),pm_add(corners[i][2],pm_i32(&m,o+8u)),&ground);if (status<0) return status;pm_write32(&m,o+0xbeu,ground);if (ground==-2147483647) break; }
            }
        }
        if (!(((timer+1u)&255u)%10u)) TRY(frame_matrix_normalize(read,write,ctx,matrix));
        if (!matrix) return -4;
        for (i=0;i<3u;i++) local[i]=pm_i32(&m,o+0xc2u+i*4u);
        TRY(transform(&m,matrix,local,1));TRY(physics_angle_lookup(local[1],local[2],trig,&angle[0]));angle[0]=pm_sub(angle[0],16384);TRY(physics_angle_lookup(pm_neg(local[0]),local[1],trig,&angle[1]));angle[1]=pm_neg(angle[1]);
        TRY(frame_matrix_rotate(read,write,ctx,matrix,0,(uint32_t)pm_sar(pm_mul(angle[0],pm_i16(&m,o+0xecu)),pm_load(&m,o+0xeeu,1)),trig));TRY(frame_matrix_rotate(read,write,ctx,matrix,2,(uint32_t)pm_sar(pm_mul(angle[1],pm_i16(&m,o+0xecu)),pm_load(&m,o+0xeeu,1)),trig));
        if (pm_load(&m,o+0xceu,4) || pm_load(&m,o+0xd6u,4)) {
            axis=pm_load(&m,o+0xb6u,4);if (axis<=1u) {
                j=axis ? 0xd6u : 0xceu;v=pm_i32(&m,o+j);for (i=0;i<3u;i++) edge[i]=0;
                i=axis ? 0u : 2u;edge[i]=pm_sar(pm_sub(pm_i16(&m,shape+(axis ? 0x22u : 0x26u)),pm_i16(&m,shape+(axis ? 0x1cu : 0x20u))),1);if (v<0) edge[i]=pm_neg(edge[i]);
                dec=pm_sar(pm_mul(pm_i16(&m,o+0xe4u),dt),pm_load(&m,o+0xe6u,1));damp(&m,o+j,dec,0);TRY(pivot(&m,o,edge,axis ? 2u : 0u,(uint32_t)pm_mul(v,dt),trig));
            }
        }
        v=pm_i32(&m,o+0x50u);pm_write32(&m,o+0x50u,pm_sub(v,pm_sar(pm_mul(v,pm_i8(&m,o+0xb4u)),7)));v=pm_i32(&m,o+0x58u);pm_write32(&m,o+0x58u,pm_sub(v,pm_sar(pm_mul(v,pm_i8(&m,o+0xb4u)),7)));
        ground=pm_i32(&m,o+0xbau);if (pm_i32(&m,o+4u)<ground) { pm_store(&m,o+0x54u,0,4);pm_write32(&m,o+4u,ground); }else if (ground<pm_i32(&m,o+4u) && ground!=-2147483647) pm_write32(&m,o+0x54u,pm_sub(pm_i32(&m,o+0x54u),pm_mul(pm_i32(&m,0x530ac8u),dt)));
        if (pm_i32(&m,o+0xbeu)==-2147483647 || ground==-2147483647) {
            for (i=0;i<3u;i++) position[i]=pm_i32(&m,o+i*4u);
            status=physics_fallback_query(read,ctx,root,position,0,n);if (status<0) return status;
            if (status) { dot=pm_add(pm_trunc(pm_mul(n[0],pm_i32(&m,o+0x50u)),11),pm_trunc(pm_mul(n[2],pm_i32(&m,o+0x58u)),11));if (dot<0) { x=pm_sub(pm_i32(&m,o+0x50u),pm_trunc(pm_mul(dot,n[0]),12));pm_write32(&m,o+0x50u,x);z=pm_trunc(pm_mul(dot,n[2]),12);pm_write32(&m,o+0x50u,pm_sar(pm_mul(x,pm_i16(&m,o+0xb2u)),7));pm_write32(&m,o+0x58u,pm_sar(pm_mul(pm_sub(pm_i32(&m,o+0x58u),z),pm_i16(&m,o+0xb2u)),7)); } }
        }
        for (i=0;i<3u;i++) pm_write32(&m,o+i*4u,pm_add(pm_i32(&m,o+i*4u),pm_sar(pm_mul(dt,pm_i32(&m,o+0x50u+i*4u)),8)));
        v=pm_mul(pm_i32(&m,o+0x7cu),pm_i8(&m,o+0xb4u));if (!pm_load(&m,o+0xaau,4) && pm_i32(&m,o+0x50u)<=v && pm_i32(&m,o+0x58u)<=v && pm_i32(&m,o+4u)==pm_i32(&m,o+0xbau) && !angle[0] && !angle[1]) { pm_store(&m,o+0x80u,0,4);TRY(frame_entity_queue(read,write,ctx,id,0,e->allocate,e->release,e->system_ctx,e->work_limit)); }
    }
    aux=pm_load(&m,o+0x88u,4);if (aux && pm_load(&m,aux+0x22u,2)) TRY(send(&m,id,0x545ee8u,0,0,e));return m.error;
}
#undef TRY
