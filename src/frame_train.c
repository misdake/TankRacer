#include "frame_train.h"
#include "frame_shatter.h"
#include "physics_vector.h"
#include "physics_memory.h"
#include "frame_text.h"
#include "frame_player_collision.h"
static uint32_t object_of(physics_memory *m,uint32_t id)
{ uint32_t r=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);return r ? pm_load(m,r+4u,4) : 0; }
static uint32_t random_next(physics_memory *m)
{ uint32_t seed=pm_load(m,0x55b6acu,4)*1664525u+1013904223u;pm_store(m,0x55b6acu,seed,4);return seed; }
static int near_player(physics_memory *m,uint32_t o)
{
    uint32_t player=object_of(m,pm_load(m,0x53e8b8u,4)),at=(pm_load(m,o+0x7du,1)&4u) ? o+0x158u : o;
    if (!player) return 0;
    return pm_abs(pm_sub(pm_i32(m,player),pm_i32(m,at)))<pm_i32(m,o+0x8cu) && pm_abs(pm_sub(pm_i32(m,player+8u),pm_i32(m,at+8u)))<pm_i32(m,o+0x8cu);
}
static void inactive(physics_memory *m,uint32_t o)
{ pm_store(m,o+0x7cu,0,1);pm_store(m,o+0x80u,0x43eac0,4); }
static void normalize_at(physics_memory *m,uint32_t at)
{
    int32_t v[3],a,b,c,length;uint32_t i;
    for (i=0;i<3u;i++) v[i]=pm_i32(m,at+i*4u);
    a=pm_abs(v[0]);b=pm_abs(v[1]);c=pm_abs(v[2]);
    if (a<b && c<b) length=pm_add(pm_add(pm_sar(c,2),pm_sar(a,2)),b);
    else if (a>=b && c<a) length=pm_add(pm_add(a,pm_sar(b,2)),pm_sar(c,2));
    else length=pm_add(pm_add(pm_sar(b,2),pm_sar(a,2)),c);
    if (length<=0) return;
    physics_vector_normalize(v);for (i=0;i<3u;i++) pm_write32(m,at+i*4u,v[i]);
}
#define TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
int frame_train_start(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o,head,unused;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,entity);if (pm_load(&m,o+0x84u,4)!=0x43e090u) { head=pm_load(&m,0x54b488u,4);TRY(frame_list_append(read,write,ctx,entity,head,e->allocate,e->release,e->system_ctx,e->work_limit,&unused)); }
    return frame_entity_activate(read,write,ctx,entity,e->method,e->method_ctx,&unused);
}
int frame_train_stop(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t head;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    head=pm_load(&m,0x54b488u,4);TRY(frame_list_remove_value(read,write,ctx,entity,head,e->release,e->system_ctx,e->work_limit));
    return frame_entity_queue(read,write,ctx,entity,0,e->allocate,e->release,e->system_ctx,e->work_limit);
}
static int rotor(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t o,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};int32_t speed,goal,value;uint32_t axis;int status;
    axis=pm_load(&m,o+0x7fu,1) ? 2u : 0u;TRY(frame_matrix_rotate(read,write,ctx,o+0xf8u,axis,(uint32_t)pm_mul(pm_i16(&m,o+0xf4u),pm_i32(&m,0x5904fcu)),trig));
    speed=pm_i16(&m,o+0xf4u);goal=pm_i16(&m,o+0xf6u);
    if (speed!=goal) {
        value=speed<goal ? pm_add(speed,pm_sar(pm_i32(&m,0x5904fcu),3)) : pm_sub(speed,pm_sar(pm_i32(&m,0x5904fcu),3));pm_store(&m,o+0xf4u,(uint32_t)value,2);
        value=pm_i16(&m,o+0xf4u);if ((speed<goal && value>goal) || (speed>goal && value<goal)) pm_store(&m,o+0xf4u,(uint32_t)goal,2);
    }
    if (pm_i16(&m,o+0xf4u)==goal) pm_store(&m,o+0xf6u,((random_next(&m)>>4)&255u)+144u,2);
    if ((entity^pm_load(&m,0x590564u,4))&7u) TRY(frame_matrix_normalize(read,write,ctx,o+0xf8u));return m.error;
}
static void swap_endpoints(physics_memory *m,uint32_t o,int vertical)
{
    uint32_t i;int32_t a[3],b[3];
    for (i=0;i<3u;i++) { a[i]=pm_i32(m,o+0x94u+i*4u);b[i]=pm_i32(m,o+0xa0u+i*4u); }
    if (!vertical) {
        for (i=0;i<3u;i++) pm_write32(m,o+0x94u+i*4u,b[i]);
        for (i=0;i<3u;i++) pm_write32(m,o+0xa0u+i*4u,a[i]);
    } else {
        pm_write32(m,o+0x94u,b[0]);pm_write32(m,o+0x98u,b[1]);pm_write32(m,o+0xa0u,a[0]);pm_write32(m,o+0xa4u,a[1]);pm_write32(m,o+0x9cu,b[2]);pm_write32(m,o+0xa8u,a[2]);
    }
}
int frame_train_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t o,state,frame,matrix,i,unused,angles[3]={0,0,0},seed;int32_t dt,value,step,old_x,old_z;int status,near;
    if (!read || !write || !e || !e->work_limit || !trig) return -1;
    o=object_of(&m,entity);state=pm_load(&m,o+0x80u,4);dt=pm_i32(&m,0x5904fcu);frame=pm_load(&m,0x590564u,4);matrix=pm_load(&m,o+20u,4);
    if (state==0x43eda0u) { value=pm_i32(&m,o+0xb0u);if (value<=dt) pm_store(&m,o+0x80u,0x43eac0,4);else pm_write32(&m,o+0xb0u,pm_sub(value,dt));return m.error; }
    if (state==0x43e090u) {
        uint32_t player=object_of(&m,pm_load(&m,0x53e8b8u,4));pm_store(&m,o+0x80u,0x43e160,4);
        for (i=0;i<3u;i++) pm_store(&m,o+0x94u+i*4u,pm_load(&m,player+i*4u,4),4);
        pm_write32(&m,o+0x98u,pm_add(pm_i32(&m,o+0x98u),10240));
        pm_write32(&m,o+0x94u,pm_add(pm_i32(&m,o+0x94u),pm_trunc(pm_mul(pm_i32(&m,player+80u),200),14)));
        step=pm_trunc(pm_mul(pm_i32(&m,player+88u),200),14);value=pm_i32(&m,o+0x9cu);pm_store(&m,o+0x8cu,51200,4);pm_write32(&m,o+0x9cu,pm_add(value,step));
        normalize_at(&m,o+0x94u);return m.error;
    }
    if (state==0x43eac0u && ((entity^frame)&7u)) return m.error;
    near=near_player(&m,o);if (m.error) return m.error;
    if (state==0x43eac0u) {
        if (!((entity^frame)&7u) && near) { pm_store(&m,o+0x7cu,1,1);pm_store(&m,o+0x80u,pm_load(&m,o+0x84u,4),4);if (pm_load(&m,o+0x134u,4)) pm_store(&m,o+0x13cu,0,4); }return m.error;
    }
    if (state==0x43e160u) {
        if (!near) { value=pm_load(&m,0x54b488u,4);TRY(frame_list_remove_value(read,write,ctx,entity,(uint32_t)value,e->release,e->system_ctx,e->work_limit));value=pm_load(&m,0x545ed0u,4);TRY(frame_entity_dispatch(read,write,ctx,entity,(uint32_t)value,0,0,e->method,e->method_ctx,&unused));return m.error; }
        for (i=0;i<3u;i++) { value=pm_i32(&m,o+0xa0u+i*4u);pm_write32(&m,o+0xa0u+i*4u,pm_add(value,pm_trunc(pm_mul(pm_i32(&m,o+0x94u+i*4u),dt),8))); }
        for (i=0;i<3u;i++) pm_write32(&m,o+i*4u,pm_add(pm_i32(&m,o+i*4u),pm_trunc(pm_mul(pm_i32(&m,o+0xa0u+i*4u),dt),8)));
        return m.error;
    }
    if (state!=0x43eb50u && state!=0x43edd0u && state!=0x43ef70u && state!=0x43eff0u && state!=0x43f0e0u && state!=0x43f150u) return -5;
    if (!near) { inactive(&m,o);return m.error; }
    switch (state) {
    case 0x43eb50:
        pm_write32(&m,o,pm_add(pm_i32(&m,o),pm_sar(pm_mul(dt,pm_i32(&m,o+80u)),8)));
        pm_write32(&m,o+8u,pm_add(pm_i32(&m,o+8u),pm_sar(pm_mul(dt,pm_i32(&m,o+88u)),8)));
        if (pm_load(&m,o+0x7eu,1)) TRY(rotor(read,write,ctx,entity,o,trig));
        value=pm_i32(&m,o+80u);step=pm_i32(&m,o+88u);
        if ((value>0 && pm_i32(&m,o)>=pm_i32(&m,o+0xa0u)) || (value<0 && pm_i32(&m,o)<=pm_i32(&m,o+0xa0u)) || (step>0 && pm_i32(&m,o+8u)>=pm_i32(&m,o+0xa8u)) || (step<0 && pm_i32(&m,o+8u)<=pm_i32(&m,o+0xa8u))) {
            seed=random_next(&m);
            if (seed&16u) { for (i=0;i<3u;i++) pm_store(&m,o+i*4u,pm_load(&m,o+0x94u+i*4u,4),4); }
            else {
                swap_endpoints(&m,o,0);for (i=0;i<3u;i++) pm_write32(&m,o+80u+i*4u,pm_sub(pm_i32(&m,o+0xa0u+i*4u),pm_i32(&m,o+i*4u)));
                normalize_at(&m,o+80u);
                for (i=0;i<3u;i++) pm_write32(&m,o+80u+i*4u,pm_mul(pm_i32(&m,o+80u+i*4u),pm_i32(&m,o+0xacu)));
                TRY(frame_matrix_rotate(read,write,ctx,matrix,1,32760,trig));TRY(frame_matrix_normalize(read,write,ctx,matrix));
            }
            pm_store(&m,o+0xb0u,5000,4);pm_store(&m,o+0x7cu,0,1);pm_store(&m,o+0x80u,0x43eda0,4);
        }
        break;
    case 0x43edd0:
        step=pm_sar(pm_mul(pm_i32(&m,o+0xacu),dt),10);value=(pm_load(&m,o+0x7du,1)&2u) ? pm_sub(pm_i32(&m,o+0xb8u),step) : pm_add(pm_i32(&m,o+0xb8u),step);pm_write32(&m,o+0xb8u,value);
        if (pm_load(&m,o+0x7du,1)&2u) { if (value<0) { value=pm_add(value,65536);pm_write32(&m,o+0xb8u,value); }angles[1]=(uint32_t)value; }
        else { if (value>65535) { value=pm_sub(value,65536);pm_write32(&m,o+0xb8u,value); }angles[1]=(uint32_t)pm_sub(value,32760); }
        i=pm_load(&m,o+0xb8u,2)&65532u;value=pm_i32(&m,o+0xb4u);old_x=pm_i32(&m,o+0x94u);old_z=pm_i32(&m,o+0x9cu);
        pm_write32(&m,o,pm_add(pm_sar(pm_mul(trig->cosine[i>>2],value),12),old_x));pm_write32(&m,o+8u,pm_add(pm_sar(pm_mul(trig->sine[i>>2],value),12),old_z));TRY(frame_matrix_from_angles(read,write,ctx,matrix,angles,trig));
        if (pm_load(&m,o+0x13cu,4)) pm_store(&m,o+0x13cu,0,4);
        else { seed=random_next(&m);if (!(seed&0x3f0u)) { (void)random_next(&m);pm_store(&m,o+0x13cu,0,4); } }
        break;
    case 0x43ef70:
        step=pm_sar(pm_mul(dt,pm_i32(&m,o+0xacu)),8);value=pm_add(pm_i32(&m,o+0xb8u),step);pm_write32(&m,o+0xb8u,value);TRY(frame_matrix_rotate(read,write,ctx,matrix,2,(uint32_t)step,trig));pm_write32(&m,o+0xacu,pm_sub(pm_i32(&m,o+0xacu),pm_sar(pm_mul(dt,pm_i32(&m,o+0xb8u)),10)));
        if ((entity^frame)&7u) TRY(frame_matrix_normalize(read,write,ctx,matrix));break;
    case 0x43eff0:
        pm_write32(&m,o+4u,pm_add(pm_i32(&m,o+4u),pm_sar(pm_mul(dt,pm_i32(&m,o+84u)),8)));TRY(frame_matrix_rotate(read,write,ctx,matrix,1,(uint32_t)dt,trig));
        value=pm_i32(&m,o+84u);
        if ((value>0 && pm_i32(&m,o+4u)>=pm_i32(&m,o+0xa4u)) || (value<0 && pm_i32(&m,o+4u)<=pm_i32(&m,o+0xa4u))) {
            swap_endpoints(&m,o,1);pm_write32(&m,o+84u,pm_i32(&m,o+0xa4u)>pm_i32(&m,o+0x98u) ? pm_i32(&m,o+0xacu) : pm_neg(pm_i32(&m,o+0xacu)));pm_store(&m,o+0xb0u,5000,4);pm_store(&m,o+0x80u,0x43eda0,4);
        }
        break;
    case 0x43f0e0:
        value=pm_add(pm_i32(&m,o+0xb8u),dt);pm_write32(&m,o+0xb8u,value);if (value>65535) pm_write32(&m,o+0xb8u,pm_sub(value,65536));TRY(frame_matrix_rotate(read,write,ctx,matrix,2,(uint32_t)dt,trig));if ((entity^frame)&7u) TRY(frame_matrix_normalize(read,write,ctx,matrix));break;
    case 0x43f150:
        value=pm_sub(pm_i32(&m,o+0xacu),pm_trunc(pm_mul(pm_sub(pm_i32(&m,o+4u),pm_i32(&m,o+0x98u)),dt),8));pm_write32(&m,o+0xacu,value);pm_write32(&m,o+4u,pm_add(pm_i32(&m,o+4u),pm_trunc(pm_mul(dt,value),8)));
        pm_write32(&m,o+0x14cu,pm_sub(pm_i32(&m,o+0x14cu),pm_trunc(pm_mul(dt,pm_i32(&m,o+0x140u)),8)));
        pm_write32(&m,o+0x154u,pm_sub(pm_i32(&m,o+0x154u),pm_trunc(pm_mul(dt,pm_i32(&m,o+0x148u)),8)));
        step=pm_trunc(pm_mul(dt,pm_i32(&m,o+0x14cu)),8);TRY(frame_matrix_rotate(read,write,ctx,matrix,0,(uint32_t)step,trig));pm_write32(&m,o+0x140u,pm_add(pm_i32(&m,o+0x140u),step));
        step=pm_trunc(pm_mul(dt,pm_i32(&m,o+0x154u)),8);TRY(frame_matrix_rotate(read,write,ctx,matrix,2,(uint32_t)step,trig));pm_write32(&m,o+0x148u,pm_add(pm_i32(&m,o+0x148u),step));if ((entity^frame)&7u) TRY(frame_matrix_normalize(read,write,ctx,matrix));break;
    default:return -5;
    }
    return m.error;
}


static int find(physics_memory *m,uint32_t parser,uint32_t a,uint32_t b,uint32_t limit,uint32_t *found)
{ int status=frame_text_find(m->read,m->write,m->ctx,parser,a,limit,found);if (status || *found || !b) return status;return frame_text_find(m->read,m->write,m->ctx,parser,b,limit,found); }
static int read_fixed(physics_memory *m,uint32_t parser,uint32_t at,uint32_t limit,uint32_t *bits,uint32_t *defined)
{
    uint32_t more;int status=frame_text_read_f32_value(m->read,m->write,m->ctx,parser,limit,bits,&more);if (status) return status;if (more) *defined=1;if (!*defined) return -4;pm_store(m,at,frame_text_fixed1024(*bits),4);return m->error;
}
static int shape_name(physics_memory *m,uint32_t parser,uint32_t at,const frame_entity_factory_services *e,const frame_object_parameters_services *s)
{
    unsigned char name[64];uint32_t more,n,value;int status=frame_text_next(m->read,m->write,m->ctx,parser,name,sizeof(name),e->work_limit,&more);if (status) return status;
    for (n=0;n<sizeof(name) && name[n];n++) { }if (n==sizeof(name)) return -3;
    status=frame_shape_load_name(m->read,m->write,m->ctx,name,n+1u,pm_load(m,0x55ae40u,4),e,s->shapes,&value);if (status) return status;pm_store(m,at,value,4);return m->error;
}
/* x87's reciprocal order is retained. Binary64 error interval conservatively
 * encloses the original extended intermediates. Return-5 if that interval can
 * cross an integer, instead of silently accepting a one-unit scene drift. */
static int eagle_rate(int32_t radius,int32_t speed,uint32_t *out)
{
    volatile double circumference,period,value;double magnitude,error,fraction;uint64_t whole;int32_t twice=pm_mul(radius,2);
    if (!twice || !speed) { *out=0;return 0; }
    circumference=(double)twice*3.141593;period=circumference/(double)speed;value=65535.0/period;
    magnitude=value<0.0 ? -value : value;
    if (!(magnitude<9223372036854775808.0)) { *out=0;return 0; }
    whole=(uint64_t)magnitude;fraction=magnitude-(double)whole;error=(magnitude+1.0)*0x1p-48;
    if (fraction<=error || 1.0-fraction<=error) return -5;
    *out=value<0.0 ? 0u-(uint32_t)whole : (uint32_t)whole;return 0;
}
int frame_train_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *e,const frame_object_parameters_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t o,parent,found,more,unused,i,j,bits=0,defined=0,angles[3]={0,0,0},value,a,b,head;int32_t x,z,v[3],angle;unsigned char name[64];int status;
#define FIND_TRAIN(a,b) find(&m,parser,(a),(b),e->work_limit,&found)
#define READ_TRAIN(at) frame_text_read_i32(read,write,ctx,parser,(at),e->work_limit,&more)
    if (!read || !write || !e || !e->work_limit || !s || !s->trig) return -1;
    o=object_of(&m,entity);parent=pm_load(&m,0x53e4b0u,4);TRY(frame_class_dispatch_payload(read,ctx,parent,entity,parser,e->method,e->method_ctx,&unused));
    pm_store(&m,o+0x7du,0,1);pm_store(&m,o+0x8cu,163840,4);pm_store(&m,o+0xacu,3,4);pm_store(&m,o+0x88u,0x43f390,4);
    TRY(FIND_TRAIN(0x53273c,0));if (found) { TRY(frame_text_next(read,write,ctx,parser,name,sizeof(name),e->work_limit,&more));pm_store(&m,o+0x134u,0,4); }
    TRY(FIND_TRAIN(0x530570,0x530578));if (found) { TRY(READ_TRAIN(o+0xacu)); }
    TRY(FIND_TRAIN(0x532734,0));
    if (found) {
        pm_store(&m,o+0x7du,pm_load(&m,o+0x7du,1)|1u,1);for (i=0;i<3u;i++) pm_store(&m,o+0x94u+i*4u,pm_load(&m,o+i*4u,4),4);
        a=pm_load(&m,o+72u,4);pm_store(&m,a+12u,10240,2);TRY(FIND_TRAIN(0x53272c,0x532724));
        if (found) { TRY(READ_TRAIN(o+0xb4u));pm_store(&m,o+0xb4u,pm_load(&m,o+0xb4u,4)<<10,4);TRY(eagle_rate(pm_i32(&m,o+0xb4u),pm_i32(&m,o+0xacu),&value));pm_store(&m,o+0xacu,value,4);pm_store(&m,o+0xb8u,0,4);pm_store(&m,o+0x138u,0,4); }
        TRY(FIND_TRAIN(0x532710,0x532704));if (found) pm_store(&m,o+0x7du,pm_load(&m,o+0x7du,1)|2u,1);pm_store(&m,o+0x84u,0x43edd0,4);
    } else {
        TRY(FIND_TRAIN(0x5326f4,0));
        if (found) {
            pm_store(&m,o+0x84u,0x43f0e0,4);pm_store(&m,o+0x88u,0x43f460,4);TRY(FIND_TRAIN(0x53272c,0x532724));if (found) { TRY(READ_TRAIN(o+0xb4u));pm_store(&m,o+0xb4u,pm_load(&m,o+0xb4u,4)<<10,4); }
            TRY(FIND_TRAIN(0x5326ec,0));if (found) TRY(shape_name(&m,parser,o+0xbcu,e,s));TRY(FIND_TRAIN(0x5326e4,0));if (found) TRY(shape_name(&m,parser,o+0xc0u,e,s));
            TRY(physics_angle_lookup(pm_i32(&m,o+0x30u),pm_i32(&m,o+0x38u),s->trig,&angle));angles[1]=(uint32_t)pm_neg(angle);TRY(frame_matrix_from_angles(read,write,ctx,o+0xc4u,angles,s->trig));
        } else {
            TRY(FIND_TRAIN(0x5326d4,0));
            if (found) { pm_store(&m,o+0xb8u,0,4);pm_store(&m,o+0x84u,0x43ef70,4);pm_store(&m,o+0xacu,pm_load(&m,o+0xacu,4)*182u,4); }
            else {
                TRY(FIND_TRAIN(0x5326cc,0));
                if (found) {
                    a=(random_next(&m)>>4)&3u;b=random_next(&m);pm_store(&m,o+0x84u,0x43f150,4);pm_write32(&m,o+0xacu,pm_mul((int32_t)(a+5u)*100,(b&16u) ? 1 : -1));
                    for (i=0;i<3u;i++) pm_store(&m,o+0x94u+i*4u,pm_load(&m,o+i*4u,4),4);
                    a=(random_next(&m)>>4)&3u;b=random_next(&m);pm_write32(&m,o+0x14cu,pm_mul((int32_t)(a+5u)*100,(b&16u) ? 1 : -1));
                    a=(random_next(&m)>>4)&3u;b=random_next(&m);pm_write32(&m,o+0x154u,pm_mul((int32_t)(a+5u)*100,(b&16u) ? 1 : -1));
                } else {
                    TRY(FIND_TRAIN(0x5326bc,0));
                    if (found) {
                        pm_store(&m,o+0x7cu,1,1);pm_store(&m,o+0x84u,0x43e090,4);head=pm_load(&m,0x54b488u,4);TRY(frame_list_contains(read,ctx,entity,head,e->work_limit,&value));if (!value) TRY(frame_list_append(read,write,ctx,entity,head,e->allocate,e->release,e->system_ctx,e->work_limit,&unused));
                    } else {
                        TRY(FIND_TRAIN(0x5326b0,0x5326a4));if (found) { for (i=0;i<3u;i++) TRY(read_fixed(&m,parser,o+0x94u+i*4u,e->work_limit,&bits,&defined));for (i=0;i<3u;i++) pm_store(&m,o+i*4u,pm_load(&m,o+0x94u+i*4u,4),4); }
                        TRY(FIND_TRAIN(0x532698,0x53268c));if (found) { for (i=0;i<3u;i++) TRY(read_fixed(&m,parser,o+0xa0u+i*4u,e->work_limit,&bits,&defined)); }
                        TRY(FIND_TRAIN(0x532680,0));if (found) {
                            x=pm_sar(pm_sub(pm_i32(&m,o+0x94u),pm_i32(&m,o+0xa0u)),1);z=pm_sar(pm_sub(pm_i32(&m,o+0x9cu),pm_i32(&m,o+0xa8u)),1);
                            pm_write32(&m,o+0x158u,pm_add(x,pm_i32(&m,o+0xa0u)));pm_write32(&m,o+0x160u,pm_add(z,pm_i32(&m,o+0xa8u)));pm_write32(&m,o+0x94u,pm_add(pm_i32(&m,o+0x94u),x));pm_write32(&m,o+0x9cu,pm_add(pm_i32(&m,o+0x9cu),z));pm_write32(&m,o+0xa0u,pm_sub(pm_i32(&m,o+0xa0u),x));pm_write32(&m,o+0xa8u,pm_sub(pm_i32(&m,o+0xa8u),z));
                            v[0]=x;v[1]=0;v[2]=z;pm_store(&m,o+0x8cu,pm_load(&m,o+0x8cu,4)+(uint32_t)frame_collision_change_magnitude(v)*2u,4);pm_store(&m,o+0x7du,pm_load(&m,o+0x7du,1)|4u,1);
                        }
                        TRY(FIND_TRAIN(0x532674,0));
                        if (found) { pm_store(&m,o+0x84u,0x43eff0,4);value=pm_i32(&m,o+0x98u)<pm_i32(&m,o+0xa4u) ? pm_load(&m,o+0xacu,4) : 0u-pm_load(&m,o+0xacu,4);pm_store(&m,o+0x58u,0,4);pm_store(&m,o+0x54u,value,4);pm_store(&m,o+0x50u,0,4); }
                        else {
                            pm_write32(&m,o+0x50u,pm_sub(pm_i32(&m,o+0xa0u),pm_i32(&m,o+0x94u)));pm_store(&m,o+0x84u,0x43eb50,4);pm_write32(&m,o+0x58u,pm_sub(pm_i32(&m,o+0xa8u),pm_i32(&m,o+0x9cu)));pm_write32(&m,o+0x54u,pm_sub(pm_i32(&m,o+0xa4u),pm_i32(&m,o+0x98u)));normalize_at(&m,o+0x50u);
                            pm_write32(&m,o+0x50u,pm_mul(pm_i32(&m,o+0x50u),pm_i32(&m,o+0xacu)));pm_write32(&m,o+0x58u,pm_mul(pm_i32(&m,o+0x58u),pm_i32(&m,o+0xacu)));
                            TRY(FIND_TRAIN(0x532668,0));if (found) {
                                TRY(shape_name(&m,parser,o+0xbcu,e,s));for (i=0;i<3u;i++) { TRY(READ_TRAIN(o+0x128u+i*4u)); }pm_store(&m,o+0xb8u,0,4);pm_store(&m,o+0xf4u,400,2);a=(random_next(&m)>>4)&255u;
                                for (j=0;j<12u;j++) pm_store(&m,o+0xf8u+j*4u,pm_load(&m,o+24u+j*4u,4),4);
                                pm_store(&m,o+0xf6u,a+144u,2);pm_store(&m,o+0x7eu,1,1);pm_store(&m,o+0x88u,0x43f3b0,4);TRY(FIND_TRAIN(0x532660,0));if (found) pm_store(&m,o+0x7fu,1,1);
                            }
                        }
                    }
                }
            }
        }
    }
    TRY(FIND_TRAIN(0x532654,0x532648));if (found) { TRY(READ_TRAIN(o+0x8cu));pm_store(&m,o+0x8cu,pm_load(&m,o+0x8cu,4)<<10,4); }
    a=pm_load(&m,o+72u,4);if (a) pm_store(&m,a+14u,pm_load(&m,o+0x4cu,2),2);pm_store(&m,o+0x80u,0x43eac0,4);head=pm_load(&m,0x54b48cu,4);TRY(frame_list_remove_value(read,write,ctx,entity,head,e->release,e->system_ctx,e->work_limit));return m.error;
#undef FIND_TRAIN
#undef READ_TRAIN
}
#undef TRY
