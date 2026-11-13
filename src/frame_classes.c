#include "frame_classes.h"
#include "physics_memory.h"
typedef struct classes { physics_memory m;const frame_entity_factory_services *e;uint32_t used;const unsigned char *name;uint32_t name_bytes; } classes;
/* Native lexer tokens are not emulated stack addresses. Only the input name
 * uses this bounded span; hash keys and allocated copies still live in DATA. */
static uint32_t character(classes *c,uint32_t address,uint32_t i)
{ if (!c->name) return pm_load(&c->m,address+i,1);if (i>=c->name_bytes) { c->m.error=-4;return 0; }return c->name[i]; }
static int step(classes *c) { if (c->m.error) return c->m.error;return c->used++>=c->e->work_limit ? -3 : 0; }
static int allocate(classes *c,uint32_t bytes,uint32_t *out)
{ if (c->m.error) return c->m.error;return frame_pool_allocate(c->m.read,c->m.write,c->m.ctx,bytes,c->e->allocate,c->e->release,c->e->system_ctx,c->e->work_limit,out); }
static int length(classes *c,uint32_t name,uint32_t *out)
{
    uint32_t n=0;int status;for (;;) { status=step(c);if (status) return status;if (!character(c,name,n)) break;n++; }
    if (c->m.error) return c->m.error;
    *out=n+1u;return 0;
}
static uint32_t fold(classes *c,uint32_t x)
{ if (x>=128u) { c->m.error=-5;return 0; }return x>=65u && x<=90u ? x+32u : x; }
static int hash(classes *c,uint32_t name,uint32_t *out)
{
    uint32_t i=0,sum=0,value;int status;
    for (;;) { status=step(c);if (status) return status;value=character(c,name,i++);if (!value) break;sum+=fold(c,value); }
    if (c->m.error) return c->m.error;
    *out=(uint32_t)(pm_signed(sum)%131);return 0;
}
static int equal(classes *c,uint32_t a,uint32_t b,uint32_t *out)
{
    uint32_t i,x,y;int status;
    for (i=0;i<32u;i++) {
        status=step(c);if (status) return status;x=character(c,a,i);y=pm_load(&c->m,b+i,1);
        if (!x || !y) break;
        if (fold(c,x)!=fold(c,y)) { if (c->m.error) return c->m.error;*out=0;return 0; }
    }
    x=character(c,a,i);y=pm_load(&c->m,b+i,1);if (c->m.error) return c->m.error;
    *out=!x && !y;return 0;
}
static int duplicate(classes *c,uint32_t name,uint32_t *out)
{
    physics_memory *m=&c->m;uint32_t n,copy,i,j,word;int status;
    status=length(c,name,&n);if (status) return status;status=allocate(c,n,&copy);if (status) return status;
    status=length(c,name,&n);if (status) return status;pm_store(m,0x54ab7cu,pm_load(m,0x54ab7cu,4)+n,4);
    status=length(c,name,&n);if (status) return status;
    for (i=0;i<(n&~3u);i+=4u) { status=step(c);if (status) return status;word=0;for (j=0;j<4u;j++) word|=character(c,name,i+j)<<(j*8u);pm_store(m,copy+i,word,4); }
    for (;i<n;i++) { status=step(c);if (status) return status;pm_store(m,copy+i,character(c,name,i),1); }
    if (m->error) return m->error;
    *out=copy;return 0;
}
int frame_symbol_create(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *e,uint32_t *out)
{
    classes c={{read,write,ctx,0},e,0,0,0};physics_memory *m=&c.m;uint32_t symbol,i;int status;
    if (!read || !write || !e || !e->work_limit || !out) return -1;
    status=allocate(&c,36,&symbol);if (status) return status;
    if (symbol) {
        pm_store(m,symbol+32u,0,4);
        for (i=0;i<3u;i++) { pm_store(m,symbol+i*8u,pm_load(m,0x5b3388u,4),4);pm_store(m,symbol+i*8u+4u,pm_load(m,0x5b338cu,4),4); }
        pm_store(m,symbol+24u,1,4);pm_store(m,symbol+28u,0,4);
    }
    if (m->error) return m->error;
    *out=symbol;return 0;
}
int frame_interfaces_initialize(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *e)
{
    static const uint32_t globals[]={
        0x545eb8,0x545ebc,0x545ec0,0x545ec4,0x545ec8,0x545ecc,0x545ed0,0x545ed4,0x545ed8,0x545edc,
        0x545ee0,0x545ee4,0x545ee8,0x545eec,0x545ef0,0x545ef4,0x545ef8,0x545efc,0x545f00,0x545f04,
        0x545f0c,0x545f10,0x545f44,0x545f48,0x545f4c,0x545f50,0x545f08,0x545f14,0x545f1c,0x545f18,
        0x545f20,0x545f24,0x545f54,0x545f58,0x545f5c,0x545f60,0x545f64,0x545f68,0x545f28,0x545f34,
        0x545f38,0x545f3c,0x545f40,0x545f2c,0x545f30,0x545f6c,0x545f70,0x545f74,0x545f78,0x545f7c
    };
    physics_memory m={read,write,ctx,0};uint32_t i,symbol;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    for (i=0;i<50u;i++) {
        status=frame_symbol_create(read,write,ctx,e,&symbol);if (status) return status;
        pm_store(&m,globals[i],symbol,4);if (m.error) return m.error;
    }
    return 0;
}
static int insert(classes *c,uint32_t name,uint32_t symbol)
{
    physics_memory *m=&c->m;uint32_t table=pm_load(m,0x534cfcu,4),bucket,node,key,found;int status;
    status=hash(c,name,&bucket);if (status) return status;node=pm_load(m,table+bucket*4u,4);
    while (node) {
        status=step(c);if (status) return status;status=equal(c,name,pm_load(m,node+4u,4),&found);if (status) return status;
        if (found) return m->error;
        node=pm_load(m,node,4);
    }
    status=allocate(c,12,&node);if (status) return status;
    pm_store(m,node,0,4);pm_store(m,node+4u,0,4);pm_store(m,node+8u,0,4);
    if (pm_load(m,table+bucket*4u,4)) pm_store(m,node,pm_load(m,table+bucket*4u,4),4);
    pm_store(m,table+bucket*4u,node,4);status=duplicate(c,name,&key);if (status) return status;
    pm_store(m,node+4u,key,4);pm_store(m,node+8u,symbol,4);return m->error;
}
int frame_symbols_initialize(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *e)
{
    classes c={{read,write,ctx,0},e,0,0,0};physics_memory *m=&c.m;uint32_t i,symbol,key;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    for (i=0;i<131u;i++) { status=step(&c);if (status) return status;pm_store(m,pm_load(m,0x534cfcu,4)+i*4u,0,4); }
    if (m->error) return m->error;
    status=frame_symbol_create(read,write,ctx,e,&symbol);if (status || !symbol) return status;
    pm_store(m,0x5b338cu,symbol,4);pm_store(m,0x5b3388u,8,4);pm_store(m,symbol,8,4);pm_store(m,symbol+4u,symbol,4);
    pm_store(m,symbol+8u,pm_load(m,0x5b3388u,4),4);pm_store(m,symbol+12u,pm_load(m,0x5b338cu,4),4);
    pm_store(m,symbol+16u,pm_load(m,0x5b3388u,4),4);key=pm_load(m,0x5b338cu,4);
    pm_store(m,symbol+24u,1,4);pm_store(m,symbol+28u,0,4);pm_store(m,symbol+20u,key,4);
    status=duplicate(&c,0x534d00u,&key);if (status) return status;pm_store(m,symbol+32u,key,4);
    status=insert(&c,0x534d00u,symbol);if (status) return status;
    pm_store(m,0x5b35acu,symbol,4);return m->error;
}
static int intern(classes *c,uint32_t name,uint32_t *out)
{
    physics_memory *m=&c->m;uint32_t table=pm_load(m,0x534cfcu,4),bucket,node,symbol=0,key,i,found;int status;
    status=hash(c,name,&bucket);if (status) return status;
    node=pm_load(m,table+bucket*4u,4);
    while (node) {
        status=step(c);if (status) return status;status=equal(c,name,pm_load(m,node+4u,4),&found);if (status) return status;
        if (found) { symbol=pm_load(m,node+8u,4);break; }node=pm_load(m,node,4);
    }
    if (!symbol) {
        status=allocate(c,36,&symbol);if (status) return status;
        if (symbol) {
            pm_store(m,symbol+32u,0,4);
            for (i=0;i<3u;i++) { pm_store(m,symbol+i*8u,pm_load(m,0x5b3388u,4),4);pm_store(m,symbol+i*8u+4u,pm_load(m,0x5b338cu,4),4); }
            pm_store(m,symbol+24u,1,4);pm_store(m,symbol+28u,0,4);
        }
        status=duplicate(c,name,&key);if (status) return status;pm_store(m,symbol+32u,key,4);
        /* Hash insertion does its own lookup; equal key does not overwrite. */
        status=insert(c,name,symbol);if (status) return status;
    }
    if (m->error) return m->error;
    *out=symbol;return 0;
}
int frame_symbol_intern(frame_data_read read,frame_data_write write,void *ctx,uint32_t name,const frame_entity_factory_services *e,uint32_t *out)
{ classes c={{read,write,ctx,0},e,0,0,0};if (!read || !write || !out || !e || !e->work_limit) return -1;return intern(&c,name,out); }
int frame_symbol_intern_name(frame_data_read read,frame_data_write write,void *ctx,const unsigned char *name,uint32_t bytes,const frame_entity_factory_services *e,uint32_t *out)
{ classes c={{read,write,ctx,0},e,0,name,bytes};if (!read || !write || !name || !out || !e || !e->work_limit) return -1;return intern(&c,0,out); }
int frame_class_grow(frame_data_read read,frame_data_write write,void *ctx,uint32_t klass,int32_t capacity,const frame_entity_factory_services *e)
{
    classes c={{read,write,ctx,0},e,0,0,0};physics_memory *m=&c.m;uint32_t copy,i,old,bytes;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    if (capacity%25) capacity=pm_mul(pm_add(capacity/25,1),25);
    status=allocate(&c,(uint32_t)capacity*4u,&copy);if (status) return status;
    for (i=0;pm_signed(i)<pm_i32(m,klass+32u);i++) { status=step(&c);if (status) return status;pm_store(m,copy+i*4u,pm_load(m,pm_load(m,klass+36u,4)+i*4u,4),4); }
    old=pm_load(m,klass+32u,4);
    for (i=old;pm_signed(i)<capacity;i++) { status=step(&c);if (status) return status;pm_store(m,copy+i*4u,0,4); }
    old=pm_load(m,klass+36u,4);bytes=pm_load(m,klass+32u,4)*4u;if (m->error) return m->error;
    if (old) { status=frame_pool_release(read,write,ctx,old,bytes,e->release,e->system_ctx);if (status) return status; }
    pm_store(m,klass+36u,copy,4);pm_store(m,klass+32u,(uint32_t)capacity,4);return m->error;
}
static int copy_name(classes *c,uint32_t name,uint32_t dest)
{
    uint32_t i=0,j,value,source=name,remaining=16,zero=0;physics_memory *m=&c->m;
    /* Original strncpy aligns the SOURCE, then copies DWORDs; after early NUL
     * in alignment bytes it aligns the destination for zero-fill. */
    while ((source&3u) && remaining) {
        value=zero ? 0 : pm_load(m,source,1);pm_store(m,dest+i,value,1);source++;i++;remaining--;if (!value) { zero=1;break; }
    }
    if (zero) while (((dest+i)&3u) && remaining) { pm_store(m,dest+i,0,1);i++;remaining--; }
    while (remaining>=4u) {
        value=0;for (j=0;j<4u;j++) { uint32_t ch=zero ? 0 : pm_load(m,source+j,1);value|=ch<<(j*8u);if (!ch) zero=1; }
        pm_store(m,dest+i,value,4);source+=4u;i+=4u;remaining-=4u;
    }
    while (remaining) { value=zero ? 0 : pm_load(m,source++,1);if (!value) zero=1;pm_store(m,dest+i++,value,1);remaining--; }
    return m->error;
}
int frame_class_create(frame_data_read read,frame_data_write write,void *ctx,uint32_t class_bytes,uint32_t name,uint32_t object_bytes,uint32_t parent,
                            const frame_entity_factory_services *e,uint32_t *out)
{
    classes c={{read,write,ctx,0},e,0,0,0};physics_memory *m=&c.m;uint32_t klass,n,i,symbol;int status;
    if (!read || !write || !e || !e->work_limit || !out) return -1;
#define TRY(x) do { if (m->error) return m->error;status=(x);if (status) return status; } while (0)
    TRY(allocate(&c,class_bytes,&klass));if (!klass) { *out=0;return 0; }
    if (parent) for (i=0;i<pm_load(m,parent+16u,4);i++) { TRY(step(&c));pm_store(m,klass+i,pm_load(m,parent+i,1),1); }
    TRY(copy_name(&c,name,klass));pm_store(m,klass+20u,object_bytes,4);pm_store(m,klass+16u,class_bytes,4);pm_store(m,klass+24u,parent,4);
    pm_store(m,klass+28u,0,4);pm_store(m,klass+32u,0,4);pm_store(m,klass+36u,0,4);
    if (parent) {
        pm_store(m,klass+28u,pm_load(m,parent+28u,4),4);TRY(frame_class_grow(read,write,ctx,klass,pm_i32(m,parent+32u),e));
        for (i=0;pm_signed(i)<pm_i32(m,parent+32u);i++) { TRY(step(&c));pm_store(m,pm_load(m,klass+36u,4)+i*4u,pm_load(m,pm_load(m,parent+36u,4)+i*4u,4),4); }
    } else {
        TRY(frame_class_grow(read,write,ctx,klass,25,e));
        for (i=0;pm_signed(i)<pm_i32(m,klass+32u);i++) { TRY(step(&c));pm_store(m,pm_load(m,klass+36u,4)+i*4u,0,4); }
    }
    pm_store(m,klass+40u,0,4);n=pm_load(m,0x536cb8u,4);pm_store(m,0x74af60u+n*4u,klass,4);pm_store(m,0x536cb8u,n+1u,4);
    TRY(intern(&c,name,&symbol));pm_store(m,symbol+8u,4096,4);pm_store(m,symbol+12u,klass,4);pm_store(m,0x55c67cu,klass,4);
    if (m->error) return m->error;
    *out=klass;return 0;
#undef TRY
}
int frame_class_bind(frame_data_read read,frame_data_write write,void *ctx,uint32_t klass,uint32_t interface,uint32_t arguments,uint32_t method,
                          const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t slot,max;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    if (!pm_load(&m,interface+28u,4)) {
        slot=pm_load(&m,0x536cc4u,4);pm_store(&m,interface+24u,1,4);pm_store(&m,interface+28u,slot,4);
        pm_store(&m,0x536cc4u,pm_load(&m,0x536cc4u,4)+1u,4);pm_store(&m,0x55be98u+slot*4u,arguments,4);
    }
    slot=pm_load(&m,interface+28u,4);
    if (pm_signed(slot)>=pm_i32(&m,klass+32u)) {
        if (m.error) return m.error;
        status=frame_class_grow(read,write,ctx,klass,pm_add(pm_signed(slot),1),e);if (status) return status;
    }
    pm_store(&m,pm_load(&m,klass+36u,4)+slot*4u,method,4);max=pm_load(&m,klass+28u,4);if (pm_signed(max)<pm_signed(slot)) max=slot;
    pm_store(&m,klass+28u,max,4);pm_store(&m,0x55b6c8u+slot*4u,interface,4);return m.error;
}
int frame_class_lookup(frame_data_read read,frame_data_write write,void *ctx,uint32_t name,const frame_entity_factory_services *e,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t symbol,value=0;int status;
    if (!out) return -1;
    status=frame_symbol_intern(read,write,ctx,name,e,&symbol);if (status) return status;
    if (pm_load(&m,symbol+8u,4)==4096u) value=pm_load(&m,symbol+12u,4);
    if (m.error) return m.error;
    *out=value;return 0;
}
int frame_class_lookup_name(frame_data_read read,frame_data_write write,void *ctx,const unsigned char *name,uint32_t bytes,const frame_entity_factory_services *e,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t symbol,value=0;int status;
    if (!out) return -1;
    status=frame_symbol_intern_name(read,write,ctx,name,bytes,e,&symbol);if (status) return status;
    if (pm_load(&m,symbol+8u,4)==4096u) value=pm_load(&m,symbol+12u,4);
    if (m.error) return m.error;
    *out=value;return 0;
}
