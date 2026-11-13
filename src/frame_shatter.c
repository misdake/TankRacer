#include "frame_shatter.h"
#include "frame_player_collision.h"
#include "physics_vector.h"
#include "physics_memory.h"
int frame_matrix_direction(frame_data_read read,frame_data_write write,void *ctx,uint32_t matrix,const int32_t direction[3],const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};int32_t v[3],pitch,yaw;uint32_t i;int status;
    if (!read || !write || !direction || !trig) return -1;
    for (i=0;i<3u;i++) v[i]=direction[i];
    while (pm_abs(v[0])>=32768 || pm_abs(v[1])>=32768 || pm_abs(v[2])>=32768) for (i=0;i<3u;i++) v[i]=pm_sar(v[i],1);
    status=physics_angle_lookup(pm_neg(v[0]),v[2],trig,&yaw);if (status) return status;status=physics_angle_lookup(v[1],physics_length2_approx(v[0],v[2]),trig,&pitch);if (status) return status;
    for (i=0;i<9u;i++) pm_store(&m,matrix+i*4u,pm_load(&m,0x532888u+i*4u,4),4);
    if (m.error) return m.error;
    status=frame_matrix_rotate(read,write,ctx,matrix,1,(uint32_t)yaw,trig);if (status) return status;return frame_matrix_rotate(read,write,ctx,matrix,0,(uint32_t)pitch,trig);
}

typedef struct shatter {
    physics_memory m;const frame_entity_factory_services *e;const frame_shatter_services *s;uint32_t used;
} shatter;
static uint32_t object_of(physics_memory *m,uint32_t id)
{
    uint32_t r=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);return r ? pm_load(m,r+4u,4) : 0;
}
static uint32_t random_next(physics_memory *m)
{
    uint32_t x=pm_load(m,0x55b6acu,4)*1664525u+1013904223u;pm_store(m,0x55b6acu,x,4);return x;
}
static int32_t magnitude(physics_memory *m,uint32_t at)
{
    int32_t v[3];unsigned i;for (i=0;i<3u;i++) v[i]=pm_i32(m,at+i*4u);return frame_collision_change_magnitude(v);
}
static int progress(shatter *p) { return p->used++>=p->e->work_limit ? -3 : 0; }
static int initial_matrix(shatter *p,const uint32_t angles[3],int32_t matrix[9])
{
    if (p->m.error) return p->m.error;
    if (!p->s || !p->s->trig || !p->s->trig->sine || !p->s->trig->cosine) return -5;
    physics_matrix_from_angles(matrix,angles,p->s->trig);return 0;
}
static void transform(int32_t v[3],const int32_t matrix[9])
{
    int32_t result[3];unsigned i,j;
    for (i=0;i<3u;i++) { result[i]=0;for (j=0;j<3u;j++) result[i]=pm_add(result[i],pm_mul(matrix[j*3u+i],v[j]));result[i]=pm_sar(result[i],12); }
    for (i=0;i<3u;i++) v[i]=result[i];
}
static int matrix_store(shatter *p,uint32_t at,const uint32_t angles[3])
{
    int32_t matrix[9];uint32_t i;int status=initial_matrix(p,angles,matrix);
    static const unsigned zero_order[]={0,2,4,1,3,5,7,8,6},roll_order[]={0,1,2,3,5,7,4,8,6};
    const unsigned *order=angles[2] ? roll_order : zero_order;
    if (status) return status;
    for (i=0;i<9u;i++) pm_write32(&p->m,at+order[i]*4u,matrix[order[i]]);
    return p->m.error;
}
/* Original443B00/C00/D10 exposes an unshifted second-row store before the
 * final result. Keep all three writes, including sign-dependent final order. */
static int rotate(shatter *p,uint32_t at,unsigned axis,uint32_t angle)
{
    physics_memory *m=&p->m;uint32_t i,first=axis==0u ? 3u : 0u,second=axis==2u ? 3u : 6u;int32_t c,s,a,b,x,y;
    if (!at || !angle) return m->error;
    if (!p->s || !p->s->trig || !p->s->trig->cosine || !p->s->trig->sine) return -5;
    c=p->s->trig->cosine[(angle&65532u)>>2];s=p->s->trig->sine[((0u-angle)&65532u)>>2];
    for (i=0;i<3u;i++) {
        a=pm_i32(m,at+(first+i)*4u);b=pm_i32(m,at+(second+i)*4u);
        x=pm_trunc(axis==1u ? pm_sub(pm_mul(c,a),pm_mul(s,b)) : pm_add(pm_mul(c,a),pm_mul(s,b)),12);
        y=axis==1u ? pm_add(pm_mul(s,a),pm_mul(c,b)) : pm_sub(pm_mul(c,b),pm_mul(s,a));
        pm_write32(m,at+(second+i)*4u,y);
        if (i==2u && y<0) { pm_write32(m,at+(first+i)*4u,x);pm_write32(m,at+(second+i)*4u,pm_trunc(y,12)); }
        else { pm_write32(m,at+(second+i)*4u,pm_trunc(y,12));pm_write32(m,at+(first+i)*4u,x); }
    }
    return m->error;
}
int frame_matrix_normalize(frame_data_read read,frame_data_write write,void *ctx,uint32_t matrix)
{
    physics_memory m={read,write,ctx,0};unsigned phase,i,j;uint32_t at[3];int32_t v[3],denominator,numerator;
    if (!read || !write) return -1;
    if (!matrix) return 0;
    for (phase=0;phase<2u;phase++) for (i=0;i<3u;i++) {
        denominator=0;
        for (j=0;j<3u;j++) { at[j]=matrix+4u*(phase ? j*3u+i : i*3u+j);v[j]=pm_i32(&m,at[j]);denominator=pm_add(denominator,pm_mul(v[j],v[j])); }
        denominator=pm_add(pm_sar(denominator,13),2048);if (m.error) return m.error;
        for (j=0;j<3u;j++) {
            numerator=pm_mul(v[j],4096);if (!denominator || (numerator==INT32_MIN && denominator==-1)) return -2;
            pm_write32(&m,at[j],numerator/denominator);if (m.error) return m.error;
        }
    }
    return m.error;
}
int frame_matrix_from_angles(frame_data_read read,frame_data_write write,void *ctx,uint32_t matrix,const uint32_t angles[3],const physics_trig *trig)
{
    frame_shatter_services s={0};shatter p={{read,write,ctx,0},0,&s,0};
    if (!read || !write || !angles) return -1;
    if (!matrix) return 0;
    s.trig=trig;return matrix_store(&p,matrix,angles);
}
int frame_matrix_initialize(frame_data_read read,frame_data_write write,void *ctx,uint32_t matrix,const uint32_t angles[3],const physics_trig *trig)
{
    frame_shatter_services s={0};shatter p={{read,write,ctx,0},0,&s,0};uint32_t i;int status;
    if (!read || !write || !angles) return -1;
    if (!matrix) return 0;
    s.trig=trig;
    for (i=0;i<12u;i++) pm_store(&p.m,matrix+i*4u,pm_load(&p.m,0x532888u+i*4u,4),4);
    status=rotate(&p,matrix,1,angles[1]);if (status) return status;
    status=rotate(&p,matrix,0,angles[0]);if (status) return status;
    status=rotate(&p,matrix,2,angles[2]);if (status) return status;
    pm_store(&p.m,matrix+44u,0,4);pm_store(&p.m,matrix+40u,0,4);pm_store(&p.m,matrix+36u,0,4);return p.m.error;
}
int frame_matrix_rotate(frame_data_read read,frame_data_write write,void *ctx,uint32_t matrix,uint32_t axis,uint32_t angle,const physics_trig *trig)
{
    frame_shatter_services s={0};shatter p={{read,write,ctx,0},0,&s,0};
    if (!read || !write || axis>2u) return -1;
    s.trig=trig;return rotate(&p,matrix,axis,angle);
}
int frame_shatter_animation(frame_data_read read,frame_data_write write,void *ctx,uint32_t a)
{
    physics_memory m={read,write,ctx,0};int32_t end;uint32_t value,cell;
    if (!read || !write) return -1;
    pm_store(&m,a,pm_load(&m,a,2)+pm_load(&m,0x5904fcu,2),2);
    if (pm_i16(&m,a)>60) {
        end=pm_sub(pm_sub(pm_i16(&m,a+48u),pm_i16(&m,a+50u)),1);value=pm_load(&m,a+38u,1);pm_store(&m,a,0,2);
        if (pm_signed(value)<end) {
            cell=pm_load(&m,a+50u,1);pm_store(&m,a+38u,value+cell,1);pm_store(&m,a+40u,pm_load(&m,a+40u,1)+cell,1);
        } else if (pm_signed(pm_load(&m,a+39u,1))<end) {
            value=pm_load(&m,a+39u,1);cell=pm_load(&m,a+50u,1);pm_store(&m,a+38u,0,1);pm_store(&m,a+39u,value+cell,1);
            pm_store(&m,a+40u,cell-1u,1);pm_store(&m,a+41u,pm_load(&m,a+41u,1)+cell,1);
        } else {
            cell=pm_load(&m,a+50u,1)-1u;pm_store(&m,a+2u,pm_load(&m,a+2u,2)+1u,2);
            pm_store(&m,a+38u,0,1);pm_store(&m,a+39u,0,1);pm_store(&m,a+40u,cell,1);pm_store(&m,a+41u,cell,1);
        }
    }
    return m.error;
}
static int32_t signed_random(physics_memory *m)
{
    uint32_t sign=random_next(m),value=(random_next(m)>>5)&32767u;
    return sign&32u ? (int32_t)value+20000 : -20000-(int32_t)value;
}
static int player_impulse(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t source,const int32_t *source_values,uint32_t mode,
                                    frame_effect_sink sink,void *sink_ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t o,i;int32_t v[3],speed,force,candidate,product,pitch,roll,half,numerator;int negative;
    if (!read || !write) return -1;
    o=object_of(&m,entity);for (i=0;i<3u;i++) v[i]=pm_sub(pm_i32(&m,o+0x78u+i*4u),(source_values ? source_values[i] : pm_i32(&m,source+i*4u)));
    speed=frame_collision_change_magnitude(v);
    if (mode==1u) force=pm_sub(speed<5120 ? 80 : speed<20480 ? 60 : 40,pm_trunc(speed,9));
    else { if (speed>=7168) return m.error;force=pm_sub(25,pm_trunc(speed,9)); }
    if (force<0) return m.error;
    if (pm_load(&m,0x52ee9cu,4)==4u && pm_load(&m,0x53e3fcu,4)) {
        pm_write32(&m,o+0x10c2u,pm_sub(pm_i32(&m,o+0x10c2u),pm_mul(force,128)));pm_store(&m,o+0x1122u,220,2);
        half=pm_sar(pm_i32(&m,0x531ba4u),1);
        if (pm_i32(&m,o+0x10c2u)<=half) {
            numerator=pm_mul(pm_i32(&m,o+0x10c2u),175);
            if (m.error) return m.error;
            if (!half || (numerator==INT32_MIN && half==-1)) return -2;
            pm_store(&m,o+0x1124u,(uint32_t)(numerator/half),2);
        }
    }
    if (pm_i32(&m,o+0x10c2u)<0) pm_store(&m,o+0x10c2u,0,4);
    if (m.error) return m.error;
    physics_vector_normalize(v);
    for (i=0;i<3u;i++) pm_write32(&m,o+0x50u+i*4u,pm_add(pm_i32(&m,o+0x50u+i*4u),pm_mul(v[i],force)));
    (void)signed_random(&m);candidate=signed_random(&m);pm_write32(&m,o+0xb8u,pm_add(pm_i32(&m,o+0xb8u),pm_mul(force,candidate)));
    candidate=signed_random(&m);negative=pm_mul(force,candidate)<0;product=pm_mul(force,signed_random(&m));pitch=negative ? pm_neg(pm_sar(pm_neg(product),1)) : pm_sar(product,1);
    candidate=signed_random(&m);negative=pm_mul(force,candidate)<0;product=pm_mul(force,signed_random(&m));roll=negative ? pm_neg(pm_sar(pm_neg(product),1)) : pm_sar(product,1);
    if (pm_load(&m,o+0xdedu,1)) {
        pm_write32(&m,o+0xbcu,pm_add(pm_i32(&m,o+0xbcu),pitch));pm_write32(&m,o+0xb4u,pm_add(pm_i32(&m,o+0xb4u),roll));
    }
    if (pm_load(&m,0x58e7acu,4)) { pm_store(&m,o+0x10dau,roll>0 ? 1 : 2,2);pm_store(&m,o+0x10dcu,pitch>0 ? 1 : 2,2); }
    if ((entity==pm_load(&m,0x53e8b8u,4) && entity!=pm_load(&m,0x53b69cu,4)) || entity==pm_load(&m,0x53e8bcu,4)) pm_store(&m,o+0x107eu,300,4);
    if (m.error) return m.error;
    if (pm_load(&m,o+0xde5u,1)) return frame_feedback_collision(read,write,ctx,1,0,sink,sink_ctx);
    return m.error;
}
int frame_shatter_player_impulse(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t source,uint32_t mode,frame_effect_sink sink,void *sink_ctx)
{ return player_impulse(read,write,ctx,entity,source,0,mode,sink,sink_ctx); }
int frame_shatter_player_impulse_values(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const int32_t source[3],uint32_t mode,frame_effect_sink sink,void *sink_ctx)
{ if (!source) return -1;return player_impulse(read,write,ctx,entity,0,source,mode,sink,sink_ctx); }
int frame_shatter_activate(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t other,
                              const frame_entity_factory_services *e,const frame_shatter_services *s,uint32_t *out)
{
    shatter p={{read,write,ctx,0},e,s,0};physics_memory *m=&p.m;uint32_t o,tank,f,flags,angles[3],at,end,a,b,i,j,value,seed,axis,head,node,unused;
    int32_t incoming[3]={0,0,0},v[3]={0,0,0},matrix[9],amount;int status;
#define TRY(x) do { status=(x);if (status) return status; } while (0)
#define CHECK() do { if (m->error) return m->error; } while (0)
    if (!read || !write || !out || !e || !e->work_limit) return -1;
    o=object_of(m,entity);tank=object_of(m,other);f=pm_load(m,o+0x8cu,4);flags=pm_load(m,f+24u,4);CHECK();
    if (flags&1u) { *out=entity;return 0; }
    pm_store(m,f+22u,0,2);pm_store(m,f+24u,flags|0x401u,4);
    if (flags&64u) {
        incoming[2]=pm_trunc(magnitude(m,tank+80u),6);angles[0]=0;angles[1]=pm_load(m,f+44u,4);angles[2]=0;
        TRY(initial_matrix(&p,angles,matrix));transform(incoming,matrix);
    } else if (!(flags&2u)) for (i=0;i<3u;i++) incoming[i]=pm_trunc(pm_i32(m,tank+80u+i*4u),6);
    if (pm_load(m,f+24u,1)&4u) {
        for (i=0;i<9u;i++) {
            a=pm_load(m,0x53e8e0u+i*4u,4);if (!a) continue;b=object_of(m,a);
            if (pm_abs(pm_sub(pm_i32(m,b),pm_i32(m,o)))<20480 && pm_abs(pm_sub(pm_i32(m,b+8u),pm_i32(m,o+8u)))<20480 && pm_i32(m,b+0xdafu)<=0) {
                head=pm_load(m,0x545f14u,4);CHECK();
                TRY(frame_entity_dispatch_pair(read,write,ctx,a,head,o,4,s ? s->pair_method : 0,s ? s->pair_ctx : 0,&unused));
            }
        }
        /* Known40F720 is solely the omitted sound event; no RNG or gameplay. */
    }
    a=pm_load(m,f+40u,4);
    if (a) {
        for (i=0;i<3u;i++) { a=pm_load(m,f+40u,4);pm_write32(m,a+10u+i*4u,pm_add(pm_i32(m,f+i*4u),pm_i32(m,o+i*4u))); }
        seed=random_next(m);a=pm_load(m,f+40u,4);pm_store(m,a+46u,seed>>4,2);
        a=pm_load(m,f+40u,4);pm_store(m,a+30u,pm_load(m,a+54u,4),4);a=pm_load(m,f+40u,4);pm_store(m,a+34u,pm_load(m,a+54u,4),4);
        pm_store(m,pm_load(m,f+40u,4)+2u,0,2);pm_store(m,pm_load(m,f+40u,4),0,2);pm_store(m,f+24u,pm_load(m,f+24u,4)|8u,4);
    }
    at=pm_load(m,f+12u,4);end=at+(uint32_t)pm_mul(pm_i16(m,f+20u),104);
    for (;at<end;at+=104u) {
        TRY(progress(&p));flags=pm_load(m,f+24u,4);
        if (flags&2u) {
            for (i=0;i<3u;i++) { seed=random_next(m);v[i]=(int32_t)((seed>>4)&8191u)-(i==1u ? 0 : 4096); }
            for (i=0;i<3u;i++) pm_write32(m,at+i*4u,v[i]);
            if (pm_load(m,f+24u,4)&2048u) {
                pm_write32(m,at,pm_sar(pm_i32(m,at),1));pm_write32(m,at+4u,pm_sar(pm_mul(pm_i32(m,at+4u),3),2));pm_write32(m,at+8u,pm_sar(pm_i32(m,at+8u),1));
            }
        } else if (!(flags&256u)) {
            for (i=0;i<3u;i++) v[i]=pm_trunc(pm_mul(incoming[i],pm_i16(m,at+80u)),7);
            if (pm_load(m,f+24u,1)&64u) { angles[0]=((random_next(m)>>4)&16383u)+4096u;angles[1]=((random_next(m)>>4)&32767u)-16384u; }
            else { angles[0]=(random_next(m)>>4)&4095u;angles[1]=((random_next(m)>>4)&16383u)-8192u; }
            angles[2]=0;TRY(initial_matrix(&p,angles,matrix));transform(v,matrix);for (i=0;i<3u;i++) pm_write32(m,at+i*4u,v[i]);
        }
        if (!(pm_load(m,f+24u,4)&256u)) {
            for (i=0;i<3u;i++) pm_write32(m,at+12u+i*4u,pm_add(pm_add(pm_i32(m,at+12u+i*4u),pm_i32(m,at+i*4u)),pm_i32(m,f+i*4u)));
            seed=random_next(m);amount=pm_sar(pm_mul(pm_add((int32_t)((seed>>4)&127u),pm_i16(m,at+82u)),frame_collision_change_magnitude(v)),6);
            if (pm_load(m,f+24u,1)&128u) {
                seed=random_next(m);pm_write32(m,at+92u,(seed&16u) ? pm_add(pm_i32(m,at+92u),amount) : pm_sub(pm_i32(m,at+92u),amount));
            } else {
                axis=((random_next(m)>>4)&65535u)%6u;j=88u+(axis%3u)*4u;value=pm_load(m,at+j,4);
                pm_write32(m,at+j,axis<3u ? pm_add(pm_signed(value),amount) : pm_sub(pm_signed(value),amount));
            }
        }
        pm_store(m,at+84u,pm_load(m,at+84u,4)|1u,4);CHECK();
    }
    head=pm_load(m,0x54b48cu,4);CHECK();TRY(frame_list_remove_value(read,write,ctx,entity,head,e->release,e->system_ctx,e->work_limit));
    head=pm_load(m,0x54b488u,4);CHECK();TRY(frame_list_contains(read,ctx,entity,head,e->work_limit,&value));
    if (!value) TRY(frame_list_append(read,write,ctx,entity,head,e->allocate,e->release,e->system_ctx,e->work_limit,&node));
    *out=entity;return 0;
#undef TRY
#undef CHECK
}
static void approach(physics_memory *m,uint32_t at,int32_t amount)
{
    int32_t old=pm_i32(m,at),value;
    if (old<0) { value=pm_add(old,amount);pm_write32(m,at,value);if (value>0) pm_store(m,at,0,4); }
    else if (old>0) { value=pm_sub(old,amount);pm_write32(m,at,value);if (value<0) pm_store(m,at,0,4); }
}
int frame_shatter_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                          const frame_entity_factory_services *e,const frame_shatter_services *s,uint32_t *out)
{
    shatter p={{read,write,ctx,0},e,s,0};physics_memory *m=&p.m;uint32_t o,f,at,end,i,a,value,flags,angles[3],descriptor,unused;int status;
    int32_t friction,dt,radius,velocity[3],matrix[9],angle,amount,old;
#define TRY(x) do { status=(x);if (status) return status; } while (0)
#define CHECK() do { if (m->error) return m->error; } while (0)
    if (!read || !write || !out || !e || !e->work_limit) return -1;
    o=object_of(m,entity);f=pm_load(m,o+0x8cu,4);friction=pm_trunc(pm_mul(pm_i32(m,f+36u),pm_i32(m,0x5904fcu)),8);
    at=pm_load(m,f+12u,4);end=at+(uint32_t)pm_mul(pm_i16(m,f+20u),104);
    for (;at<end;at+=104u) {
        TRY(progress(&p));flags=pm_load(m,at+84u,4);if (flags&2u) continue;
        if (flags&1u) pm_write32(m,at+4u,pm_sub(pm_i32(m,at+4u),pm_trunc(pm_mul(pm_i32(m,f+32u),pm_i32(m,0x5904fcu)),8)));
        else if (!(flags&4u)) { approach(m,at,friction);approach(m,at+8u,friction);for (i=0;i<3u;i++) approach(m,at+88u+i*4u,friction); }
        for (i=0;i<3u;i++) pm_write32(m,at+12u+i*4u,pm_add(pm_i32(m,at+12u+i*4u),pm_trunc(pm_mul(pm_i32(m,at+i*4u),pm_i32(m,0x5904fcu)),8)));
        if (pm_load(m,pm_load(m,o+0x8cu,4)+24u,1)&128u) {
            pm_write32(m,at+100u,pm_add(pm_i32(m,at+100u),pm_trunc(pm_mul(pm_i32(m,at+92u),pm_i32(m,0x5904fcu)),8)));
            radius=pm_i32(m,pm_load(m,f+48u,4)+24u);
        } else {
            for (i=0;i<3u;i++) TRY(rotate(&p,at+28u,i,(uint32_t)pm_trunc(pm_mul(pm_i32(m,at+88u+i*4u),pm_i32(m,0x5904fcu)),8)));
            radius=(int32_t)pm_load(m,pm_load(m,at+24u,4)+12u,2)*8;
        }
        flags=pm_load(m,at+84u,4);
        if (flags&1u) {
            radius=pm_add(radius,pm_i32(m,f+28u));
            if (pm_i32(m,at+16u)<radius) {
                if (flags&8u) {
                    pm_store(m,at+8u,0,4);pm_store(m,at+4u,0,4);pm_store(m,at,0,4);pm_store(m,at+84u,(flags&~1u)|4u,4);
                } else {
                    pm_write32(m,at+16u,radius);value=(uint32_t)pm_abs(pm_i32(m,at+4u))&~1u;
                    if (pm_signed(value)<=2048) { pm_store(m,at+4u,0,4);pm_store(m,at+84u,pm_load(m,at+84u,4)&~1u,4); }
                    else {
                        pm_write32(m,at+4u,pm_sar(pm_abs(pm_i32(m,at+4u)),1));
                        for (i=0;i<3u;i++) { value=random_next(m);pm_store(m,at+88u+i*4u,((value>>4)&8191u)-4096u,4); }
                        angles[0]=0;angles[1]=((random_next(m)>>4)&4095u)-2048u;angles[2]=0;
                        TRY(initial_matrix(&p,angles,matrix));for (i=0;i<3u;i++) velocity[i]=pm_i32(m,at+i*4u);transform(velocity,matrix);
                        for (i=0;i<3u;i++) pm_write32(m,at+i*4u,velocity[i]);
                    }
                }
            }
        } else if (flags&4u) {
            if (!s || !s->trig || !s->trig->angle) return -5;
            TRY(physics_angle_lookup(pm_i32(m,at+56u),physics_length2_approx(pm_i32(m,at+52u),pm_i32(m,at+60u)),s->trig,&angle));
            old=pm_i32(m,at+76u);
            if (old==-1 || angle>=old) pm_write32(m,at+76u,angle);
            else {
                pm_store(m,at+84u,pm_load(m,at+84u,4)|2u,4);pm_store(m,f+22u,pm_load(m,f+22u,2)+1u,2);
                TRY(physics_angle_lookup(pm_i32(m,at+52u),pm_i32(m,at+60u),s->trig,&old));
                angles[0]=0xffffc004u;angles[1]=(uint32_t)pm_neg(old);angles[2]=0;TRY(matrix_store(&p,at+28u,angles));
            }
        } else {
            amount=magnitude(m,at);
            if (amount<=1024) { pm_store(m,f+22u,pm_load(m,f+22u,2)+1u,2);pm_store(m,at+84u,pm_load(m,at+84u,4)|2u,4); }
            else if ((entity&3u)==pm_load(m,0x590558u,4) && ((random_next(m)>>4)&65535u)%3u==0) {
                for (i=0;i<3u;i++) { value=random_next(m);pm_store(m,at+88u+i*4u,((value>>4)&8191u)-4096u,4); }
                angles[0]=0u-((random_next(m)>>4)&8191u);angles[1]=((random_next(m)>>4)&4095u)-2048u;angles[2]=0;
                TRY(initial_matrix(&p,angles,matrix));for (i=0;i<3u;i++) velocity[i]=pm_i32(m,at+i*4u);transform(velocity,matrix);
                for (i=0;i<3u;i++) pm_write32(m,at+i*4u,velocity[i]);
                pm_store(m,at+84u,pm_load(m,at+84u,4)|1u,4);
            }
        }
        CHECK();
    }
    if (pm_load(m,f+24u,1)&8u) {
        a=pm_load(m,f+40u,4);CHECK();TRY(frame_shatter_animation(read,write,ctx,a));a=pm_load(m,f+40u,4);
        if (pm_load(m,a+2u,2)==pm_load(m,a+4u,2)) pm_store(m,f+24u,pm_load(m,f+24u,4)&~8u,4);
        else {
            amount=pm_i16(m,a+52u);dt=pm_i32(m,0x5904fcu);
            if (amount>0) { value=(uint32_t)pm_sar(pm_mul(dt,amount),7);pm_write32(m,a+30u,pm_add(pm_i32(m,a+30u),pm_signed(value)));a=pm_load(m,f+40u,4);pm_write32(m,a+34u,pm_add(pm_i32(m,a+34u),pm_signed(value))); }
            else if (amount<0) { value=(uint32_t)pm_sar(pm_neg(pm_mul(dt,amount)),7);pm_write32(m,a+30u,pm_sub(pm_i32(m,a+30u),pm_signed(value)));a=pm_load(m,f+40u,4);pm_write32(m,a+34u,pm_sub(pm_i32(m,a+34u),pm_signed(value))); }
        }
    }
    if (pm_load(m,f+22u,2)==pm_load(m,f+20u,2) && !(pm_load(m,f+24u,1)&8u)) {
        descriptor=pm_load(m,0x545ed0u,4);CHECK();TRY(frame_entity_dispatch(read,write,ctx,entity,descriptor,0,0,e->method,e->method_ctx,&unused));
    }
    CHECK();*out=entity;return 0;
#undef TRY
#undef CHECK
}
int frame_shatter_pair_native(void *ctx,uint32_t target,uint32_t entity,uint32_t payload,uint32_t second,uint32_t *out)
{
    frame_shatter_native_context *c=(frame_shatter_native_context *)ctx;int status;
    if (!c || !out) return -1;
    if (target!=0x438540u) return -5;
    status=frame_shatter_player_impulse(c->base.base.read,c->base.base.write,c->base.base.data_ctx,entity,payload,second,c->shatter.feedback,c->shatter.feedback_ctx);
    if (!status) *out=0;
    return status; /* void original: output reserved */
}
int frame_shatter_native(void *ctx,uint32_t target,uint32_t entity,uint32_t has_payload,uint32_t payload,uint32_t *out)
{
    frame_shatter_native_context *c=(frame_shatter_native_context *)ctx;frame_shatter_services s;
    if (!c || !out) return -1;
    s=c->shatter;if (!s.pair_method) { s.pair_method=frame_shatter_pair_native;s.pair_ctx=c; }
    if (target==0x41cf80u) return frame_shatter_activate(c->base.base.read,c->base.base.write,c->base.base.data_ctx,entity,payload,&c->base.base.services,&s,out);
    if (target==0x41c980u) return frame_shatter_tick(c->base.base.read,c->base.base.write,c->base.base.data_ctx,entity,&c->base.base.services,&s,out);
    return frame_object_native(&c->base,target,entity,has_payload,payload,out);
}
