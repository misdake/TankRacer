#include "frame_fall.h"
#include "frame_classes.h"
#include "frame_spatial.h"
#include "frame_text.h"
#include "physics_terrain.h"
#include "physics_memory.h"
static uint32_t object_of(physics_memory *m,uint32_t entity)
{ uint32_t r=pm_load(m,pm_load(m,0x55c694u,4)+entity*8u,4);return r ? pm_load(m,r+4u,4) : 0; }
static int dispatch(physics_memory *m,uint32_t id,uint32_t global,const frame_entity_factory_services *e)
{ uint32_t descriptor=pm_load(m,global,4),unused;if (m->error) return m->error;return frame_entity_dispatch(m->read,m->write,m->ctx,id,descriptor,0,0,e->method,e->method_ctx,&unused); }
#define TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
int frame_fall_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *e,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,shape,special,parent,unused,list,found,more,n,i,symbol;
    unsigned char name[1024];int status;
    static const unsigned offsets[]={8,0,20,4,32,12,16,24,28};
    if (!read || !write || !e || !e->work_limit || !out) return -1;
    o=object_of(&m,entity);shape=pm_load(&m,o+0x48u,4);special=shape==pm_load(&m,0x53d140u,4) || shape==pm_load(&m,0x53d144u,4);
    if (pm_load(&m,0x58e7acu,4) && !special) {
        TRY(dispatch(&m,entity,0x545f48u,e));TRY(dispatch(&m,entity,0x545ed0u,e));*out=entity;return 0;
    }
    parent=pm_load(&m,0x544f80u,4);TRY(frame_class_dispatch_payload(read,ctx,parent,entity,parser,e->method,e->method_ctx,&unused));
    if (pm_load(&m,0x58e7acu,4)) {
        TRY(dispatch(&m,entity,0x545f48u,e));list=pm_load(&m,0x54b48cu,4);
        TRY(frame_list_append(read,write,ctx,entity,list,e->allocate,e->release,e->system_ctx,e->work_limit,&unused));*out=entity;return 0;
    }
    pm_store(&m,o+0x80u,0,4);shape=pm_load(&m,o+0x48u,4);
    if (shape==pm_load(&m,0x53d140u,4) || shape==pm_load(&m,0x53d144u,4)) {
        for (i=0;i<9u;i++) pm_store(&m,o+0xaau+offsets[i],pm_load(&m,o+0x18u+offsets[i],4),4);
        pm_store(&m,o+0xeau,pm_load(&m,o+0xeau,4)|32u,4);list=pm_load(&m,0x53d148u,4);
        TRY(frame_list_append(read,write,ctx,entity,list,e->allocate,e->release,e->system_ctx,e->work_limit,&unused));
    }
    TRY(frame_text_find_name(read,write,ctx,parser,(const unsigned char *)":attached_object",17,e->work_limit,&found));
    if (found) {
        TRY(frame_list_create(read,write,ctx,e->allocate,e->release,e->system_ctx,e->work_limit,&list));pm_store(&m,o+0xfau,list,4);
        n=0;TRY(frame_text_read_value(read,write,ctx,parser,e->work_limit,&n,&more));if (!more) return -4;
        for (i=0;pm_signed(i)<pm_signed(n);i++) {
            if (i>=e->work_limit) return -3;
            TRY(frame_text_next(read,write,ctx,parser,name,1024,e->work_limit,&more));TRY(frame_symbol_intern_name(read,write,ctx,name,1024,e,&symbol));list=pm_load(&m,o+0xfau,4);
            TRY(frame_list_append(read,write,ctx,symbol,list,e->allocate,e->release,e->system_ctx,e->work_limit,&unused));
        }
    }
    *out=0;return m.error;
}
int frame_fall_destroy(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o,list,parent,unused;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,entity);list=pm_load(&m,o+0xfau,4);
    if (list) TRY(frame_list_destroy(read,write,ctx,list,e->release,e->system_ctx,e->work_limit));
    parent=pm_load(&m,0x544f80u,4);TRY(frame_class_dispatch(read,ctx,parent,entity,e->method,e->method_ctx,&unused));return m.error;
}
int frame_fall_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t o,tank,flags,matrix,mesh,aux,fx,i,j;int32_t dt,speed,delta,old,current,limit,bounce,angle,threshold,height,width,x,z,v[3];int turn,flip,status;
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,entity);tank=object_of(&m,pm_load(&m,o+0xdau,4));
    if (pm_load(&m,o+0x80u,4)) {
        flags=pm_load(&m,o+0xeau,4);dt=pm_i32(&m,0x5904fcu);matrix=pm_load(&m,o+0x14u,4);
        if (flags&2u) {
            for (i=0;i<3u;i++) v[i]=pm_i32(&m,tank+0x18u+i*4u);
            /*442ED0 uses the original vector for all three outputs. */
            if (matrix) { int32_t local[3];for (i=0;i<3u;i++) { int32_t sum=0;for (j=0;j<3u;j++) sum=pm_add(sum,pm_mul(v[j],pm_i32(&m,matrix+(i*3u+j)*4u)));local[i]=pm_sar(sum,12); }for (i=0;i<3u;i++) v[i]=local[i]; }
            if (!trig || !trig->angle) return -5;
            TRY(physics_angle_lookup(v[2],v[0],trig,&angle));threshold=pm_i32(&m,0x52ec9cu);turn=0;flip=0;
            if (angle>=0 && angle<16384) { if (angle<threshold) flip=1;else turn=1; }
            else if (angle<=-16384 && angle>-32768) { if (pm_abs(pm_add(angle,32768))<threshold) flip=2;else turn=1; }
            else if (angle>=16384 && angle<=32768) { if (pm_abs(pm_sub(angle,32768))<threshold) flip=2;else turn=-1; }
            else if (angle<0 && angle>-16384) { if (pm_neg(angle)<threshold) flip=1;else turn=-1; }
            if (flip) pm_store(&m,o+0xeau,(pm_load(&m,o+0xeau,4)&~2u)|(flip==2 ? 4u : 0u),4);
            if (turn) { delta=pm_trunc(pm_mul(pm_i32(&m,o+0xe2u),dt),8);if (turn<0) delta=pm_neg(delta);TRY(frame_matrix_rotate(read,write,ctx,matrix,1,(uint32_t)delta,trig)); }
            flags=pm_load(&m,o+0xeau,4);
            if (!(flags&2u)) {
                mesh=pm_load(&m,o+0x48u,4);width=pm_mul(pm_sub(pm_i16(&m,mesh+0x24u),pm_i16(&m,mesh+0x1eu)),8);
                x=pm_trunc(pm_mul(pm_i32(&m,o+0x30u),width),12);z=pm_trunc(pm_mul(pm_i32(&m,o+0x38u),width),12);
                if (!!(flags&8u)!=!!(flags&4u)) { x=pm_neg(x);z=pm_neg(z); }
                status=physics_terrain_height(read,ctx,pm_load(&m,0x55ae60u,4),pm_add(pm_i32(&m,o),x),pm_add(pm_i32(&m,o+8u),z),&height);if (status<0) return status;
                if (height==-2147483647) pm_store(&m,o+0xf6u,0,4);
                else { TRY(physics_angle_lookup(pm_sub(pm_i32(&m,o+4u),height),width,trig,&angle));pm_write32(&m,o+0xf6u,angle); }
            }
        } else {
            speed=pm_i32(&m,o+0xeeu);delta=pm_trunc(pm_mul(dt,speed),8);old=pm_i32(&m,o+0xf2u);
            if (flags&8u) { pm_write32(&m,o+0xeeu,pm_add(speed,pm_mul(pm_i32(&m,0x530accu),dt)));current=pm_sub(old,delta); }
            else { pm_write32(&m,o+0xeeu,pm_sub(speed,pm_mul(pm_i32(&m,0x530accu),dt)));current=pm_add(old,delta); }
            limit=pm_i32(&m,o+0xf6u);
            if (pm_abs(current)>pm_add(limit,16380)) {
                delta=delta>0 ? pm_add(pm_sub(limit,pm_abs(old)),16384) : pm_sub(pm_sub(pm_abs(old),limit),16384);
                pm_write32(&m,o+0xf2u,pm_sub(-16380,limit));bounce=pm_mul(pm_i32(&m,0x530ad0u),pm_i32(&m,o+0xeeu));
                bounce=pm_neg(bounce)>=0 ? pm_sar(pm_neg(bounce),pm_load(&m,0x530ad4u,4)) : pm_neg(pm_sar(bounce,pm_load(&m,0x530ad4u,4)));
                pm_write32(&m,o+0xeeu,bounce);
                if (pm_abs(bounce)<200) {
                    pm_store(&m,o+0x80u,0,4);fx=pm_load(&m,o+0x8cu,4);
                    if (!fx || !(pm_load(&m,fx+24u,1)&1u)) {
                        TRY(frame_spatial_remove(read,write,ctx,entity,pm_load(&m,o+0x90u,4),e->release,e->system_ctx,e->work_limit));
                        TRY(frame_entity_queue(read,write,ctx,entity,0,e->allocate,e->release,e->system_ctx,e->work_limit));
                    }
                }
            } else pm_write32(&m,o+0xf2u,current);
            if (pm_load(&m,o+0xeau,1)&4u) delta=pm_neg(delta);
            TRY(frame_matrix_rotate(read,write,ctx,matrix,0,(uint32_t)delta,trig));
        }
    }
    aux=pm_load(&m,o+0x88u,4);if (aux && pm_i16(&m,aux+34u)) TRY(dispatch(&m,entity,0x545ee8u,e));
    fx=pm_load(&m,o+0x8cu,4);if (fx && (pm_load(&m,fx+24u,1)&1u)) TRY(dispatch(&m,entity,0x545ef0u,e));return m.error;
}
#undef TRY
