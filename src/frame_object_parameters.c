#include "frame_object_parameters.h"
#include "frame_text.h"
#include "physics_memory.h"
#include "physics_terrain.h"

typedef struct parameters {
    physics_memory m;uint32_t parser,object,temp,defined,used;
    const frame_entity_factory_services *e;const frame_object_parameters_services *s;
} parameters;
static int step(parameters *p) { return p->used++>=p->e->work_limit ? -3 : 0; }
static int find(parameters *p,uint32_t a,uint32_t b,uint32_t *found)
{
    int status;if (p->m.error) return p->m.error;
    status=frame_text_find(p->m.read,p->m.write,p->m.ctx,p->parser,a,p->e->work_limit,found);
    if (status || *found || !b) return status;
    return frame_text_find(p->m.read,p->m.write,p->m.ctx,p->parser,b,p->e->work_limit,found);
}
static int integer(parameters *p,uint32_t destination)
{
    uint32_t more;if (p->m.error) return p->m.error;
    return frame_text_read_i32(p->m.read,p->m.write,p->m.ctx,p->parser,destination,p->e->work_limit,&more);
}
static int native_integer(parameters *p,uint32_t *value,uint32_t *defined)
{
    uint32_t more;int status;if (p->m.error) return p->m.error;
    status=frame_text_read_value(p->m.read,p->m.write,p->m.ctx,p->parser,p->e->work_limit,value,&more);
    if (status) return status;
    if (more) *defined=1;
    return *defined ? 0 : -4;
}
static int token(parameters *p,unsigned char *text,uint32_t bytes)
{
    uint32_t more;if (p->m.error) return p->m.error;
    return frame_text_next(p->m.read,p->m.write,p->m.ctx,p->parser,text,bytes,p->e->work_limit,&more);
}
static int equal(parameters *p,const unsigned char *text,uint32_t bytes,uint32_t data,uint32_t *out)
{
    uint32_t i,a,b;
    for (i=0;i<bytes;i++) {
        a=text[i];b=pm_load(&p->m,data+i,1);if (p->m.error) return p->m.error;
        if (a>=128u || b>=128u) return -5;
        if (a>=65u && a<=90u) a+=32u;
        if (b>=65u && b<=90u) b+=32u;
        if (a!=b) { *out=0;return 0; }
        if (!a) { *out=1;return 0; }
    }
    return -3;
}
static int allocate(parameters *p,uint32_t bytes,uint32_t *out)
{ if (p->m.error) return p->m.error;return frame_pool_allocate(p->m.read,p->m.write,p->m.ctx,bytes,p->e->allocate,p->e->release,p->e->system_ctx,p->e->work_limit,out); }
static uint32_t fx(parameters *p) { return pm_load(&p->m,p->object+0x8cu,4); }
static uint32_t piece(parameters *p,uint32_t index) { return pm_load(&p->m,fx(p)+12u,4)+index*104u; }
static uint32_t anim(parameters *p) { return pm_load(&p->m,fx(p)+40u,4); }
static uint32_t sprite(parameters *p) { return pm_load(&p->m,fx(p)+48u,4); }
static uint32_t random_next(parameters *p)
{
    uint32_t value=pm_load(&p->m,0x55b6acu,4)*1664525u+1013904223u;pm_store(&p->m,0x55b6acu,value,4);return value;
}
static int material(parameters *p,const unsigned char *name,uint32_t capacity,uint32_t mode,uint32_t *out)
{
    uint32_t i;if (p->m.error) return p->m.error;
    if (!p->s || !p->s->material) return -5;
    for (i=0;i<capacity;i++) if (!name[i]) return p->s->material(p->s->material_ctx,name,i+1u,mode,out);
    return -3;
}
static int matrix(parameters *p,const uint32_t angles[3],uint32_t destination,int temporary)
{
    int32_t value[9];uint32_t i;physics_memory *m=&p->m;
    static const uint32_t zero_order[]={0,2,4,1,3,5,7,8,6};
    static const uint32_t roll_order[]={0,1,2,3,5,7,4,8,6};
    const uint32_t *order=angles[2] ? roll_order : zero_order;
    if (m->error) return m->error;
    if (!p->s || !p->s->trig || !p->s->trig->sine || !p->s->trig->cosine) return -5;
    physics_matrix_from_angles(value,angles,p->s->trig);
    if (temporary) {
        if (!p->s->matrix_tail) return -4;
        for (i=0;i<9u;i++) pm_write32(m,destination+i*4u,value[i]);
        for (i=0;i<3u;i++) pm_store(m,destination+36u+i*4u,p->s->matrix_tail[i],4);
    } else for (i=0;i<9u;i++) pm_write32(m,destination+order[i]*4u,value[order[i]]);
    return m->error;
}
static int basename(parameters *p,uint32_t shape,uint32_t destination)
{
    physics_memory *m=&p->m;uint32_t head,node,record,name,n,i,c;
    pm_store(m,destination,pm_load(m,0x534d00u,4),4);if (!shape) return m->error;
    head=pm_load(m,0x55ae54u,4);node=pm_load(m,head,4);
    while (node!=head) {
        if (m->error) return m->error;
        if (step(p)) return -3;
        record=pm_load(m,node+8u,4);
        if (pm_load(m,record,4)==shape) {
            name=record+4u;i=0;
            for (;;) {
                for (n=0;;n++) { if (step(p)) return -3;c=pm_load(m,name+n,1);if (m->error) return m->error;if (!c) break; }
                if (i>=n || pm_load(m,name+i,1)==46u) break;
                pm_store(m,destination+i,pm_load(m,name+i,1),1);i++;
            }
            if (i<8u) pm_store(m,destination+i,0,1);
            return m->error;
        }
        node=pm_load(m,node,4);
    }
    return m->error;
}
static int float_position(parameters *p,uint32_t *bits,uint32_t *out)
{
    uint32_t more,exponent,mantissa,shift,sign;uint64_t magnitude;int status;
    status=frame_text_read_f32_value(p->m.read,p->m.write,p->m.ctx,p->parser,p->e->work_limit,bits,&more);if (status) return status;
    exponent=(*bits>>23)&255u;mantissa=(*bits&0x7fffffu)|0x800000u;sign=*bits>>31;
    /* binary32 * exact1024, then x87 FISTP signed64 and EAX low32. */
    if (!exponent || exponent==255u || exponent<117u || exponent>=180u) { *out=0;return 0; }
    if (exponent>=140u) { shift=exponent-140u;magnitude=(uint64_t)mantissa<<shift; }
    else magnitude=mantissa>>(140u-exponent);
    if (magnitude>INT64_MAX && !(sign && magnitude==(UINT64_C(1)<<63))) magnitude=0;
    *out=sign ? 0u-(uint32_t)magnitude : (uint32_t)magnitude;return 0;
}
int frame_object_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,
                               const frame_entity_factory_services *e,const frame_object_parameters_services *s,uint32_t *out)
{
    parameters p={{read,write,ctx,0},parser,0,0,0,0,e,s};physics_memory *m=&p.m;
    uint32_t record,klass,unused,found,a,table,i,j,match,margin=0,vis=0,vis_defined=0,storage,vfp,bytes,capacity,at,value;
    uint32_t angles[3],float_bits=0,mode,more;int32_t height,angle;int status;unsigned char name[64],blend[8];
#define TRY(expression) do { status=(expression);if (status) return status; } while (0)
#define CHECK() do { if (m->error) return m->error; } while (0)
    if (!read || !write || !out || !e || !e->work_limit) return -1;
    record=pm_load(m,pm_load(m,0x55c694u,4)+entity*8u,4);p.object=record ? pm_load(m,record+4u,4) : 0;
    klass=pm_load(m,0x53e4b0u,4);CHECK();
    TRY(frame_class_dispatch_payload(read,ctx,klass,entity,parser,e->method,e->method_ctx,&unused));
    if (!p.object) { *out=entity;return 0; }
    TRY(find(&p,0x52ec38u,0,&found));
    if (found) {
        TRY(native_integer(&p,&vis,&vis_defined));a=pm_load(m,p.object+0x48u,4);pm_store(m,p.object+0x4cu,vis,2);if (a) pm_store(m,a+14u,vis,2);
    }
    if (pm_load(m,0x58e7acu,4)) TRY(basename(&p,pm_load(m,p.object+0x48u,4),p.object+0xa0u));
    TRY(find(&p,0x52e394u,0x52e38cu,&found));
    if (found) {
        table=pm_load(m,0x544f84u,4);TRY(token(&p,name,64));
        for (i=0;pm_signed(i)<pm_i32(m,0x544f88u);i++) {
            if (step(&p)) return -3;
            TRY(equal(&p,name,64,table+i*76u,&match));
            if (match) {
                a=table+i*76u;pm_write32(m,p.object+0x5cu,pm_i16(m,a+32u));
                if (pm_load(m,a+64u,1)) pm_store(m,p.object+0x84u,0,4);
                value=pm_load(m,a+62u,2);if (value) margin=value;break;
            }
        }
    }
    TRY(find(&p,0x530c3cu,0,&found));
    if (found) { value=1;TRY(native_integer(&p,&margin,&value)); }
    else if (!margin) margin=1536;
    pm_store(m,p.object+0xa8u,margin,2);a=pm_load(m,p.object+0x48u,4);
    if (a) for (i=0;i<6u;i++) pm_write32(m,p.object+0x60u+i*4u,pm_add(pm_mul(pm_i16(m,a+28u+i*2u),8),i<3u ? pm_neg(pm_signed(margin)) : pm_signed(margin)));
    TRY(find(&p,0x530c30u,0,&found));if (found) { TRY(token(&p,name,64));pm_store(m,p.object+0x84u,0,4); }
    TRY(find(&p,0x52e318u,0x52e30cu,&found));if (found) TRY(integer(&p,p.object+0x5cu));
    TRY(find(&p,0x530c20u,0x530c10u,&found));if (found) { TRY(integer(&p,p.object+0x94u));pm_store(m,p.object+0x94u,pm_load(m,p.object+0x94u,4)<<10,4); }
    TRY(find(&p,0x530c04u,0x530bf8u,&found));
    if (found) {
        static const uint32_t names[]={0x530bd8u,0x530bc8u,0x530bbcu,0x530bb0u,0x530ba0u};
        TRY(frame_object_auxiliary_create(read,write,ctx,entity,e));a=pm_load(m,p.object+0x88u,4);CHECK();
        TRY(frame_text_next_data(read,write,ctx,parser,a+4u,40,e->work_limit,&more));
        for (i=0;i<6u;i++) { a=pm_load(m,p.object+0x88u,4);pm_store(m,a+44u+i*4u,pm_load(m,0x530ad8u+i*4u,4),4); }
        TRY(find(&p,0x530be8u,0,&found));if (found) TRY(integer(&p,pm_load(m,p.object+0x88u,4)+44u));
        for (i=0;i<5u;i++) { TRY(find(&p,names[i],0,&found));if (found) TRY(frame_text_read_pair(read,write,ctx,parser,pm_load(m,p.object+0x88u,4)+48u+i*4u,e->work_limit)); }
    }
    TRY(find(&p,0x530b94u,0,&found));vfp=0;
    if (!found) { TRY(find(&p,0x530b84u,0,&found));vfp=1; }
    if (found) {
        TRY(allocate(&p,52,&storage));pm_store(m,p.object+0x8cu,storage,4);CHECK();if (!storage) return -4;pm_store(m,storage+24u,0,4);
        if (vfp) {
            TRY(allocate(&p,42,&storage));pm_store(m,fx(&p)+48u,storage,4);a=fx(&p);pm_store(m,a+24u,pm_load(m,a+24u,4)|128u,4);
            TRY(token(&p,name,64));TRY(material(&p,name,64,2,&value));a=sprite(&p);CHECK();if (!a) return -4;pm_store(m,a,value,4);
            pm_store(m,sprite(&p)+16u,UINT32_MAX,4);pm_store(m,sprite(&p)+20u,0,4);
            pm_store(m,sprite(&p)+33u,0,1);pm_store(m,sprite(&p)+32u,0,1);pm_store(m,sprite(&p)+35u,31,1);pm_store(m,sprite(&p)+34u,31,1);
            pm_store(m,sprite(&p)+38u,127,1);pm_store(m,sprite(&p)+37u,127,1);pm_store(m,sprite(&p)+36u,127,1);
            TRY(integer(&p,sprite(&p)+24u));a=sprite(&p);pm_store(m,a+28u,pm_load(m,a+24u,4),4);
        }
        TRY(native_integer(&p,&p.temp,&p.defined));pm_store(m,fx(&p)+20u,p.temp,2);value=pm_load(m,fx(&p)+20u,2);
        if (value) {
            bytes=(uint32_t)pm_mul(value>32767u ? (int32_t)value-65536 : (int32_t)value,104);pm_store(m,fx(&p)+16u,bytes,4);capacity=2;
            while (capacity<bytes) { if (step(&p)) return -3;capacity*=2u; }
            pm_store(m,fx(&p)+16u,capacity,4);TRY(allocate(&p,capacity,&storage));pm_store(m,fx(&p)+12u,storage,4);
        } else pm_store(m,fx(&p)+12u,0,4);
        for (i=0;pm_signed(i)<pm_i16(m,fx(&p)+20u);i++) {
            if (step(&p)) return -3;
            at=piece(&p,i);CHECK();if (!pm_load(m,fx(&p)+12u,4)) return -4;pm_store(m,at+84u,0,4);
            if (!(pm_load(m,fx(&p)+24u,1)&128u)) {
                TRY(token(&p,name,64));
                if (s && s->shapes) TRY(frame_shape_load_name(read,write,ctx,name,64,0,e,s->shapes,&value));
                else TRY(frame_shape_lookup_name(read,ctx,name,64,e->work_limit,&value));
                pm_store(m,piece(&p,i)+24u,value,4);
            }
            TRY(native_integer(&p,&p.temp,&p.defined));pm_store(m,piece(&p,i)+80u,p.temp,2);
            TRY(native_integer(&p,&p.temp,&p.defined));pm_store(m,piece(&p,i)+82u,p.temp,2);
            at=piece(&p,i)+12u;for (j=0;j<3u;j++) pm_store(m,at+j*4u,pm_load(m,p.object+j*4u,4),4);
            if (pm_load(m,fx(&p)+24u,1)&128u) {
                value=random_next(&p);pm_store(m,piece(&p,i)+100u,(value>>4)&65535u,4);
                value=random_next(&p);pm_store(m,piece(&p,i)+92u,((value>>4)&255u)-128u,4);
            } else {
                for (j=0;j<3u;j++) angles[j]=(random_next(&p)>>4)&65535u;
                TRY(matrix(&p,angles,piece(&p,i)+28u,1));
                for (j=0;j<3u;j++) { value=random_next(&p);pm_store(m,piece(&p,i)+88u+j*4u,((value>>4)&255u)-128u,4); }
            }
        }
        {
            static const uint32_t names[]={0x530b78u,0x530b70u,0x530b60u,0x530b58u};
            static const uint32_t masks[]={16,2,2048,4};
            for (i=0;i<4u;i++) { TRY(find(&p,names[i],0,&found));if (found) { a=fx(&p);pm_store(m,a+24u,pm_load(m,a+24u,4)|masks[i],4); } }
        }
        TRY(find(&p,0x530b50u,0,&found));
        if (found) {
            TRY(allocate(&p,58,&storage));pm_store(m,fx(&p)+40u,storage,4);TRY(token(&p,name+32u,32));TRY(token(&p,blend,8));mode=entity;
            for (i=0;i<3u;i++) { TRY(equal(&p,blend,8,0x52e1c8u-i*4u,&match));if (match) { mode=i-1u;break; } }
            TRY(material(&p,name+32u,32,1,&value));a=anim(&p);CHECK();if (!a) return -4;pm_store(m,a+6u,value,4);
            TRY(native_integer(&p,&p.temp,&p.defined));pm_store(m,anim(&p)+48u,p.temp,2);
            TRY(native_integer(&p,&p.temp,&p.defined));pm_store(m,anim(&p)+50u,p.temp,2);TRY(integer(&p,anim(&p)+54u));
            TRY(native_integer(&p,&p.temp,&p.defined));pm_store(m,anim(&p)+4u,p.temp,2);
            TRY(native_integer(&p,&p.temp,&p.defined));pm_store(m,anim(&p)+52u,p.temp,2);
            pm_store(m,anim(&p),0,2);pm_store(m,anim(&p)+22u,mode,4);pm_store(m,anim(&p)+26u,0,4);
            pm_store(m,anim(&p)+38u,0,1);pm_store(m,anim(&p)+39u,0,1);
            a=anim(&p);pm_store(m,a+40u,pm_load(m,a+50u,1),1);a=anim(&p);pm_store(m,a+41u,pm_load(m,a+50u,1),1);pm_store(m,anim(&p)+46u,0,2);
            at=anim(&p)+10u;for (j=0;j<3u;j++) pm_store(m,at+j*4u,pm_load(m,p.object+j*4u,4),4);
            for (j=0;j<3u;j++) pm_store(m,anim(&p)+42u+j,255,1);
        } else pm_store(m,fx(&p)+40u,0,4);
        TRY(find(&p,0x530b48u,0,&found));
        if (found) {
            a=fx(&p);pm_store(m,a+24u,pm_load(m,a+24u,4)|256u,4);
            for (i=0;pm_signed(i)<pm_i16(m,fx(&p)+20u);i++) {
                if (step(&p)) return -3;
                for (j=0;j<3u;j++) {
                    TRY(float_position(&p,&float_bits,&value));pm_store(m,piece(&p,i)+12u+j*4u,value,4);
                    if (j==1u && !value) { at=piece(&p,i);pm_store(m,at+84u,pm_load(m,at+84u,4)|8u,4); }
                }
                for (j=0;j<3u;j++) pm_store(m,piece(&p,i)+8u-j*4u,0,4);
                at=piece(&p,i);
                if (pm_load(m,at+84u,1)&8u) {
                    if (!s || !s->trig || !s->trig->angle) return -5;
                    TRY(physics_angle_lookup(pm_i32(m,at+12u),pm_i32(m,at+20u),s->trig,&angle));
                    angles[0]=0;angles[1]=(uint32_t)pm_sub(32760,angle);angles[2]=0;TRY(matrix(&p,angles,piece(&p,i)+28u,0));
                } else for (j=0;j<12u;j++) pm_store(m,piece(&p,i)+28u+j*4u,pm_load(m,p.object+24u+j*4u,4),4);
                at=piece(&p,i);pm_store(m,at+88u,(pm_load(m,at+84u,1)&8u) ? 8000 : 0,4);
                pm_store(m,piece(&p,i)+96u,0,4);pm_store(m,piece(&p,i)+92u,0,4);
                for (j=0;j<3u;j++) { at=piece(&p,i)+12u+j*4u;pm_write32(m,at,pm_add(pm_i32(m,at),pm_i32(m,p.object+j*4u))); }
                pm_store(m,piece(&p,i)+76u,UINT32_MAX,4);
            }
        }
        pm_store(m,fx(&p)+32u,4096,4);pm_store(m,fx(&p)+36u,2048,4);
        pm_store(m,fx(&p)+8u,0,4);pm_store(m,fx(&p)+4u,0,4);pm_store(m,fx(&p),0,4);
        TRY(find(&p,0x530b38u,0,&found));if (found) TRY(integer(&p,fx(&p)+32u));
        TRY(find(&p,0x530b28u,0,&found));if (found) {
            for (j=0;j<3u;j++) TRY(integer(&p,fx(&p)+j*4u));
            for (j=0;j<3u;j++) { a=fx(&p);pm_store(m,a+j*4u,pm_load(m,a+j*4u,4)<<10,4); }
        }
        TRY(find(&p,0x530b18u,0,&found));if (found) TRY(integer(&p,fx(&p)+36u));
        TRY(find(&p,0x530b0cu,0,&found));if (found) {
            TRY(integer(&p,fx(&p)+44u));a=fx(&p);pm_write32(m,a+44u,pm_mul(pm_i32(m,a+44u),182));
            while (pm_i32(m,fx(&p)+44u)>32760) { if (step(&p)) return -3;a=fx(&p);pm_write32(m,a+44u,pm_sub(pm_i32(m,a+44u),65520)); }
            while (pm_i32(m,fx(&p)+44u)<=-32760) { if (step(&p)) return -3;a=fx(&p);pm_write32(m,a+44u,pm_add(pm_i32(m,a+44u),65520)); }
            a=fx(&p);pm_store(m,a+24u,pm_load(m,a+24u,4)|64u,4);
        }
        TRY(find(&p,0x530afcu,0,&found));if (found) TRY(integer(&p,fx(&p)+28u));
        else {
            CHECK();status=physics_terrain_height(read,ctx,pm_load(m,0x55ae60u,4),pm_i32(m,p.object),pm_i32(m,p.object+8u),&height);
            if (status<0) return status;
            pm_write32(m,fx(&p)+28u,height);
        }
    }
    a=pm_load(m,0x545f4cu,4);CHECK();TRY(frame_entity_dispatch(read,write,ctx,entity,a,0,0,e->method,e->method_ctx,&unused));
    *out=entity;return 0;
#undef CHECK
#undef TRY
}
int frame_object_native(void *ctx,uint32_t target,uint32_t entity,uint32_t has_payload,uint32_t payload,uint32_t *out)
{
    frame_object_native_context *c=(frame_object_native_context *)ctx;int status;
    if (!c || !out) return -1;
    if (target==0x41e9a0u) return frame_object_construct(c->base.read,c->base.write,c->base.data_ctx,entity,payload,&c->base.services,&c->parameters,out);
    if (target==0x43f5d0u) return frame_world_object_construct(c->base.read,c->base.write,c->base.data_ctx,entity,payload,&c->base.services,out);
    if (target==0x410730u) {
        status=frame_lap_visibility_construct(c->base.read,c->base.write,c->base.data_ctx,entity,payload,&c->base.services);
        if (!status && out) *out=0;
        return status;
    }
    return frame_entity_native_base(&c->base,target,entity,has_payload,payload,out);
}
int frame_world_object_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,
                                     const frame_entity_factory_services *e,uint32_t *out)
{
    parameters p={{read,write,ctx,0},parser,0,entity,1,0,e,0};physics_memory *m=&p.m;uint32_t record,klass,unused,found,head,node;int status;
    if (!read || !write || !out || !e || !e->work_limit) return -1;
    record=pm_load(m,pm_load(m,0x55c694u,4)+entity*8u,4);p.object=record ? pm_load(m,record+4u,4) : 0;
    klass=pm_load(m,0x55b6c4u,4);if (m->error) return m->error;
    status=frame_class_dispatch_payload(read,ctx,klass,entity,parser,e->method,e->method_ctx,&unused);if (status) return status;
    if (p.object) {
        pm_store(m,p.object+76u,(uint32_t)pm_sar(pm_i32(m,0x5366dcu),10),2);
        if (m->error) return m->error;
        status=find(&p,0x53274cu,0,&found);if (status) return status;
        if (found) { status=native_integer(&p,&p.temp,&p.defined);if (status) return status;pm_store(m,p.object+78u,p.temp,2); }
        status=find(&p,0x52e318u,0x52e30cu,&found);if (status) return status;
        if (found) { status=integer(&p,p.object+92u);if (status) return status; }
        head=pm_load(m,0x54b48cu,4);if (m->error) return m->error;
        status=frame_list_append(read,write,ctx,entity,head,e->allocate,e->release,e->system_ctx,e->work_limit,&node);if (status) return status;
    }
    *out=entity;return 0;
}
int frame_lap_visibility_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,
                                       const frame_entity_factory_services *e)
{
    parameters p={{read,write,ctx,0},parser,0,entity,1,0,e,0};physics_memory *m=&p.m;uint32_t record,klass,unused,found,head,node,delayed=0;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    record=pm_load(m,pm_load(m,0x55c694u,4)+entity*8u,4);p.object=record ? pm_load(m,record+4u,4) : 0;
    klass=pm_load(m,0x5466e4u,4);if (m->error) return m->error;
    status=frame_class_dispatch_payload(read,ctx,klass,entity,parser,e->method,e->method_ctx,&unused);if (status) return status;
    head=pm_load(m,0x54b48cu,4);if (m->error) return m->error;
    status=frame_list_remove_value(read,write,ctx,entity,head,e->release,e->system_ctx,e->work_limit);if (status) return status;
    record=pm_load(m,pm_load(m,0x55c694u,4)+entity*8u,4);klass=record ? pm_load(m,record,4) : 0;
    if (klass==pm_load(m,0x545094u,4)) return m->error;
    status=find(&p,0x52fa04u,0,&found);if (status) return status;
    if (found) {
        status=native_integer(&p,&p.temp,&p.defined);if (status) return status;
        if (p.temp==(uint32_t)-99) p.temp=((random_next(&p)>>4)&65535u)%3u+1u;
        if (p.temp==2u || p.temp==3u) {
            pm_store(m,p.object+120u,p.temp-1u,2);head=pm_load(m,0x545f48u,4);if (m->error) return m->error;
            status=frame_entity_dispatch(read,write,ctx,entity,head,0,0,e->method,e->method_ctx,&unused);if (status) return status;
            head=pm_load(m,0x53e4b4u,4);if (m->error) return m->error;
            status=frame_list_append(read,write,ctx,entity,head,e->allocate,e->release,e->system_ctx,e->work_limit,&node);if (status) return status;delayed=1;
        }
    }
    if (!delayed) {
        head=pm_load(m,0x545f44u,4);if (m->error) return m->error;
        status=frame_entity_dispatch(read,write,ctx,entity,head,0,0,e->method,e->method_ctx,&unused);if (status) return status;
    }
    status=find(&p,0x52f9f4u,0,&found);if (status) return status;
    if (found) {
        status=native_integer(&p,&p.temp,&p.defined);if (status) return status;pm_store(m,p.object+122u,p.temp-1u,2);
        head=pm_load(m,0x53e4b4u,4);if (m->error) return m->error;
        status=frame_list_contains(read,ctx,entity,head,e->work_limit,&found);if (status) return status;
        if (!found) return frame_list_append(read,write,ctx,entity,head,e->allocate,e->release,e->system_ctx,e->work_limit,&node);
    }
    return m->error;
}
