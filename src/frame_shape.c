#include "frame_shape.h"
#include "physics_memory.h"

static int record_for(physics_memory *m,uint32_t shape,uint32_t limit,uint32_t *out)
{
    uint32_t head=pm_load(m,0x55ae54u,4),node=pm_load(m,head,4),record,found=0,used=0;
    while (node!=head) {
        if (m->error) return m->error;
        if (used++>=limit) return -3;
        record=pm_load(m,node+8u,4);if (pm_load(m,record,4)==shape) found=record;
        node=pm_load(m,node,4);
    }
    if (m->error) return m->error;
    *out=found;return 0;
}
static uint32_t fold(uint32_t c) { return c>=65u && c<=90u ? c+32u : c; }
static int lookup(frame_data_read read,void *ctx,uint32_t name,const unsigned char *native,uint32_t name_bytes,uint32_t limit,uint32_t *out)
{
    physics_memory m={read,0,ctx,0};uint32_t head,node,record,i,a,b,used=0;
    if (!read || !limit || !out) return -1;
    head=pm_load(&m,0x55ae54u,4);if (m.error) return m.error;
    if (!head) return 1;
    node=pm_load(&m,head,4);
    while (node!=head) {
        if (m.error) return m.error;
        if (used++>=limit) return -3;
        record=pm_load(&m,node+8u,4);
        for (i=0;;i++) {
            if (used++>=limit) return -3;
            a=pm_load(&m,record+4u+i,1);
            if (native) { if (i>=name_bytes) return -3;b=native[i]; }
            else b=pm_load(&m,name+i,1);
            if (m.error) return m.error;
            if (a>=128u || b>=128u) return -5;
            if (fold(a)!=fold(b)) break;
            if (!a) { a=pm_load(&m,record,4);if (m.error) return m.error;*out=a;return 0; }
        }
        node=pm_load(&m,node,4);
    }
    return m.error ? m.error : 1;
}
int frame_shape_lookup_cached(frame_data_read read,void *ctx,uint32_t name,uint32_t limit,uint32_t *out)
{ int status=lookup(read,ctx,name,0,0,limit,out);return status==1 ? -5 : status; }
int frame_shape_lookup_name(frame_data_read read,void *ctx,const unsigned char *name,uint32_t bytes,uint32_t limit,uint32_t *out)
{ int status;if (!name) return -1;status=lookup(read,ctx,0,name,bytes,limit,out);return status==1 ? -5 : status; }
int frame_shape_copy_name(frame_data_read read,frame_data_write write,void *ctx,const unsigned char *name,uint32_t name_bytes,
                             uint32_t size_destination,const frame_entity_factory_services *s,uint32_t *out)
{
    uint32_t source;int status;if (!s || !name) return -1;
    status=lookup(read,ctx,0,name,name_bytes,s->work_limit,&source);if (status) return status==1 ? -5 : status;
    return frame_shape_clone_loaded(read,write,ctx,source,size_destination,s,out);
}
int frame_shape_clone_loaded(frame_data_read read,frame_data_write write,void *ctx,uint32_t source,
                                uint32_t size_destination,const frame_entity_factory_services *s,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t record,bytes,copy,at,end,delta,i,value,used=0;int status;
    static const uint32_t always[]={0x28,0x2c,0x30,0x34,0x38,0x3c,0x40,0x44};
    static const uint32_t optional[]={0x10,0x14,0x18,0x48,0x4c,0x50};
    if (!read || !write || !out || !s || !s->work_limit) return -1;
    status=record_for(&m,source,s->work_limit,&record);if (status) return status;
    if (record && (pm_load(&m,record+0x12u,1)&1u)) return m.error ? m.error : -4;
    pm_store(&m,size_destination,0,4);
    if (!record) return m.error ? m.error : -4;
    if (pm_load(&m,0x55c688u,4)) pm_store(&m,size_destination,999999,4);
    else pm_store(&m,size_destination,(pm_load(&m,record+20u,4)+511u)&0xfffffe00u,4);
    bytes=pm_load(&m,record+20u,4);if (m.error) return m.error;
    if (pm_load(&m,0x55c688u,4)) status=frame_level_allocate(read,write,ctx,bytes,&copy);
    else status=frame_pool_allocate(read,write,ctx,(bytes+511u)&0xfffffe00u,s->allocate,s->release,s->system_ctx,s->work_limit,&copy);
    if (status) return status;
    if (copy) {
        end=source+(pm_load(&m,record+20u,4)&0xfffffffcu);delta=copy-source;
        for (at=source;at<end;at+=4u) {
            if (m.error) return m.error;
            if (used++>=s->work_limit) return -3;
            pm_store(&m,at+delta,pm_load(&m,at,4),4);
        }
        pm_store(&m,copy+4u,pm_load(&m,copy+4u,4)+delta,4);pm_store(&m,copy+8u,pm_load(&m,copy+8u,4)+delta,4);
        for (i=0;i<8u;i++) pm_store(&m,copy+always[i],pm_load(&m,source+always[i],4)+delta,4);
        for (i=0;i<6u;i++) {
            value=pm_load(&m,source+optional[i],4);if (value) pm_store(&m,copy+optional[i],value+delta,4);
        }
    }
    if (m.error) return m.error;
    *out=copy;return 0;
}
int frame_shape_copy_cached(frame_data_read read,frame_data_write write,void *ctx,uint32_t name,
                               uint32_t size_destination,const frame_entity_factory_services *s,uint32_t *out)
{
    uint32_t source;int status;if (!s) return -1;
    status=frame_shape_lookup_cached(read,ctx,name,s->work_limit,&source);if (status) return status;
    return frame_shape_clone_loaded(read,write,ctx,source,size_destination,s,out);
}
int frame_shape_release(frame_data_read read,frame_data_write write,void *ctx,uint32_t shape,uint32_t size_override,
                           const frame_entity_factory_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t record,bytes=0,protected_data=0;int status;
    if (!read || !write || !s || !s->work_limit) return -1;
    status=record_for(&m,shape,s->work_limit,&record);if (status) return status;
    if (record) {
        protected_data=pm_load(&m,record+0x12u,1)&4u;bytes=pm_load(&m,record+20u,4);if (m.error) return m.error;
        status=frame_pool_release(read,write,ctx,record,24,s->release,s->system_ctx);if (status) return status;
        status=frame_list_remove_value(read,write,ctx,record,pm_load(&m,0x55ae54u,4),s->release,s->system_ctx,s->work_limit);
        if (status) return status;
    }
    if (!protected_data) {
        if (size_override) bytes=size_override;
        if (!bytes) return -4;
        if (size_override!=999999u) return frame_pool_release(read,write,ctx,shape,bytes,s->release,s->system_ctx);
    }
    return m.error;
}
int frame_shape_relocate(frame_data_read read,frame_data_write write,void *ctx,uint32_t shape,
                            frame_shape_material_resolver resolver,void *resolver_ctx,uint32_t limit)
{
    physics_memory m={read,write,ctx,0};uint32_t i,offset,material,result,mode;int status;
    static const uint32_t always[]={8,4,0x28,0x2c,0x30,0x38,0x34,0x3c,0x40,0x44};
    static const uint32_t optional[]={0x48,0x4c,0x50,0x10,0x14,0x18};
    if (!read || !write || !limit) return -1;
    for (i=0;i<10u;i++) pm_store(&m,shape+always[i],pm_load(&m,shape+always[i],4)+shape,4);
    for (i=0;i<6u;i++) {
        offset=optional[i];material=pm_load(&m,shape+offset,4);if (material) pm_store(&m,shape+offset,material+shape,4);
    }
    for (i=0;i<pm_load(&m,shape+2u,2);i++) {
        if (m.error) return m.error;
        if (i>=limit) return -3;
        material=pm_load(&m,pm_load(&m,shape+8u,4)+i*24u+20u,4);
        if (material) {
            mode=pm_load(&m,0x55ae4cu,4) ? 1u : 2u;
            if (m.error) return m.error;
            if (!resolver) return -5;
            status=resolver(resolver_ctx,shape+material,mode,&result);if (status) return status;
            pm_store(&m,pm_load(&m,shape+8u,4)+i*24u+20u,result,4);
        }
    }
    return m.error;
}
static int load(frame_data_read read,frame_data_write write,void *ctx,uint32_t name,const unsigned char *native,
                    uint32_t native_bytes,uint32_t name_space,const frame_entity_factory_services *s,
                    const frame_shape_resources *r,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t head,shape=0,record,node,length=0,found=0,i,n,value,default_space;int status;
    const unsigned char *bytes=0;unsigned char token[256];
    if (!read || !write || !s || !s->work_limit || !out) return -1;
    head=pm_load(&m,0x55ae54u,4);if (m.error) return m.error;
    if (!head) {
        status=frame_list_create(read,write,ctx,s->allocate,s->release,s->system_ctx,s->work_limit,&head);if (status) return status;
        pm_store(&m,0x55ae54u,head,4);if (m.error) return m.error;
        if (!head) return -4;
    }
    status=lookup(read,ctx,name,native,native_bytes,s->work_limit,&shape);
    if (status!=1) { if (!status) *out=shape;return status; }
    if (!r || !r->fetch) return -5;
    /* No guessed stack address: this is the host's bounded filename argument. */
    for (n=0;;n++) {
        if (n>=sizeof(token) || n>=s->work_limit || (native && n>=native_bytes)) return -3;
        token[n]=native ? native[n] : (unsigned char)pm_load(&m,name+n,1);
        if (m.error) return m.error;
        if (!token[n]) { n++;break; }
    }
    default_space=pm_load(&m,0x55ae40u,4);if (m.error) return m.error;
    status=r->fetch(r->file_ctx,name_space ? name_space : default_space,token,n,&bytes,&length,&found);if (status) return status;
    if (!found) { *out=0;return 0; }
    if (length && !bytes) return -1;
    if (name_space && name_space!=pm_load(&m,0x55ae40u,4)) pm_store(&m,0x55ae4cu,name_space,4);
    if (m.error) return m.error;
    shape=0;
    if (length) {
        status=frame_pool_allocate(read,write,ctx,length,s->allocate,s->release,s->system_ctx,s->work_limit,&shape);if (status) return status;
        if (shape && !write(ctx,shape,bytes,length)) return -1;
    }
    if (shape) {
        status=frame_pool_allocate(read,write,ctx,24,s->allocate,s->release,s->system_ctx,s->work_limit,&record);if (status) return status;
        if (!record) return -4;
        pm_store(&m,record+18u,0,2);pm_store(&m,record,shape,4);pm_store(&m,record+20u,length,4);
        if (!native) {
            for (n=0;;n++) {
                if (n>=s->work_limit) return -3;
                value=pm_load(&m,name+n,1);if (m.error) return m.error;
                if (!value) { n++;break; }
            }
        }
        /* Original strcpy uses forward DWORDs followed by bytes, including NUL.
         * DATA-name reads occur here, after allocation, to preserve aliases. */
        for (i=0;i<(n&~3u);i+=4u) {
            if (native) value=(uint32_t)native[i]|((uint32_t)native[i+1u]<<8)|((uint32_t)native[i+2u]<<16)|((uint32_t)native[i+3u]<<24);
            else value=pm_load(&m,name+i,4);
            pm_store(&m,record+4u+i,value,4);
        }
        for (;i<n;i++) pm_store(&m,record+4u+i,native ? native[i] : pm_load(&m,name+i,1),1);
        head=pm_load(&m,0x55ae54u,4);if (m.error) return m.error;
        status=frame_list_prepend(read,write,ctx,record,head,s->allocate,s->release,s->system_ctx,s->work_limit,&node);if (status) return status;
        status=frame_shape_relocate(read,write,ctx,shape,r->material,r->material_ctx,s->work_limit);if (status) return status;
    }
    pm_store(&m,0x55ae4cu,0,4);if (m.error) return m.error;
    *out=shape;return 0;
}
int frame_shape_load(frame_data_read read,frame_data_write write,void *ctx,uint32_t name,uint32_t name_space,
                         const frame_entity_factory_services *s,const frame_shape_resources *r,uint32_t *out)
{ return load(read,write,ctx,name,0,0,name_space,s,r,out); }
int frame_shape_load_name(frame_data_read read,frame_data_write write,void *ctx,const unsigned char *name,uint32_t name_bytes,
                              uint32_t name_space,const frame_entity_factory_services *s,const frame_shape_resources *r,uint32_t *out)
{ if (!name) return -1;return load(read,write,ctx,0,name,name_bytes,name_space,s,r,out); }
int frame_shape_relocate_world_meshes(frame_data_read read,frame_data_write write,void *ctx,uint32_t base,
                                         frame_shape_material_resolver resolver,void *resolver_ctx,uint32_t limit)
{
    physics_memory m={read,write,ctx,0};uint32_t node=base,used=0,i,j,value,at,result,last_offset=0,last_material=0;int status;
    static const uint32_t optional[]={0x10,0x14,0x18};
    if (!read || !write || !limit) return -1;
    while (node) {
        if (used++>=limit) return -3;
        value=pm_load(&m,node+0x20u,4);if (value) pm_store(&m,node+0x20u,value+base,4);
        pm_store(&m,node+4u,pm_load(&m,node+4u,4)+base,4);pm_store(&m,node+8u,pm_load(&m,node+8u,4)+base,4);
        for (j=0;j<3u;j++) { value=pm_load(&m,node+optional[j],4);if (value) pm_store(&m,node+optional[j],value+base,4); }
        for (i=0;i<pm_load(&m,node+2u,2);i++) {
            if (m.error) return m.error;
            if (used++>=limit) return -3;
            at=pm_load(&m,node+8u,4)+i*24u+20u;value=pm_load(&m,at,4);if (m.error) return m.error;
            if (value) {
                if (value==last_offset) pm_store(&m,at,last_material,4);
                else {
                    if (!resolver) return -5;
                    status=resolver(resolver_ctx,base+value,2,&result);if (status) return status;
                    at=pm_load(&m,node+8u,4)+i*24u+20u;pm_store(&m,at,result,4);
                    last_material=pm_load(&m,pm_load(&m,node+8u,4)+i*24u+20u,4);last_offset=value;
                }
            }
        }
        node=pm_load(&m,node+0x20u,4);if (m.error) return m.error;
    }
    return m.error;
}
static int bind_nodes(physics_memory *m,uint32_t base,uint32_t node,int primary,uint32_t limit,uint32_t *used)
{
    uint32_t value;
    while (node) {
        if ((*used)++>=limit) return -3;
        value=pm_load(m,node,4);if (value) pm_store(m,node,value+base,4);
        pm_store(m,node+4u,pm_load(m,node+4u,4)+base,4);
        if (primary) {
            value=pm_load(m,node+12u,4);if (value) pm_store(m,node+12u,value+base,4);
            value=pm_load(m,node+164u,4);if (value) pm_store(m,node+164u,value+base,4);
        } else { value=pm_load(m,node+8u,4);if (value) pm_store(m,node+8u,value+base,4); }
        node=pm_load(m,node,4);if (m->error) return m->error;
    }
    return 0;
}
int frame_shape_bind_world(frame_data_read read,frame_data_write write,void *ctx,uint32_t base,
                              uint32_t primary,uint32_t secondary,uint32_t terrain,uint32_t limit)
{
    physics_memory m={read,write,ctx,0};uint32_t used=0,i,j,at,value,table0,table1;int status;
    if (!read || !write || !limit) return -1;
    pm_store(&m,0x55ae58u,primary,4);pm_store(&m,0x55ae5cu,secondary,4);pm_store(&m,0x55ae60u,terrain,4);
    if (m.error) return m.error;
    if (primary) { status=bind_nodes(&m,base,primary,1,limit,&used);if (status) return status;secondary=pm_load(&m,0x55ae5cu,4); }
    if (secondary) { status=bind_nodes(&m,base,secondary,0,limit,&used);if (status) return status; }
    if (terrain) {
        table1=pm_load(&m,terrain+4u,4)+base;table0=pm_load(&m,terrain,4)+base;
        pm_store(&m,terrain,table0,4);pm_store(&m,terrain+4u,table1,4);
        for (j=0;j<2u;j++) for (i=0;i<pm_load(&m,terrain+0x114u+j*2u,2);i++) {
            if (m.error) return m.error;
            if (used++>=limit) return -3;
            at=pm_load(&m,terrain+j*4u,4)+i*4u;value=pm_load(&m,at,4);if (value) pm_store(&m,at,value+base,4);
        }
    }
    return m.error;
}
int frame_shape_prepare_world(frame_data_read read,frame_data_write write,void *ctx,uint32_t size_destination,
                                 frame_shape_material_resolver resolver,void *resolver_ctx,uint32_t limit,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t bytes,primary,secondary,terrain;int status;
    if (!read || !write || !limit || !out) return -1;
    bytes=pm_load(&m,0x5dab40u,4)+20u;
    if (pm_load(&m,0x5dab44u,4)!=4u) return m.error ? m.error : -4;
    secondary=pm_load(&m,0x5dab4cu,4);primary=pm_load(&m,0x5dab48u,4);terrain=pm_load(&m,0x5dab50u,4);
    if (m.error) return m.error;
    if (bytes>0x91800u) return -4;
    pm_store(&m,size_destination,bytes,4);if (m.error) return m.error;
    if (primary) primary+=0x5dab54u;
    if (secondary) secondary+=0x5dab54u;
    if (terrain) terrain+=0x5dab54u;
    status=frame_shape_relocate_world_meshes(read,write,ctx,0x5dab54u,resolver,resolver_ctx,limit);if (status) return status;
    status=frame_shape_bind_world(read,write,ctx,0x5dab54u,primary,secondary,terrain,limit);if (status) return status;
    if (bytes<0x19000u) bytes=0x19000u;
    pm_store(&m,0x55ae50u,0x5dab40u+((bytes+3u)&~3u),4);if (m.error) return m.error;
    *out=0x5dab54u;return 0;
}
