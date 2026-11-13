#include "frame_texture.h"
#include "physics_memory.h"
typedef struct texture_context { physics_memory m;const frame_entity_factory_services *e;const frame_texture_services *s; } texture_context;
static int device(texture_context *p,uint32_t operation,uint32_t object,uint32_t start,uint32_t count,uint32_t *out)
{
    if (p->m.error) return p->m.error;
    return p->s && p->s->device ? p->s->device(p->s->device_ctx,operation,object,start,count,out) : -5;
}
static int release(texture_context *p,uint32_t at,uint32_t bytes)
{
    if (p->m.error) return p->m.error;
    return frame_pool_release(p->m.read,p->m.write,p->m.ctx,at,bytes,p->e->release,p->e->system_ctx);
}
static int palette_release(texture_context *p,uint32_t palette)
{
    physics_memory *m=&p->m;uint32_t colors,unused;int status;
    if (!palette || palette==0x596980u) return m->error;
    status=device(p,FRAME_TEXTURE_PALETTE_RELEASE,palette,0,0,&unused);if (status) return status;
    colors=pm_load(m,palette+4u,4);
    if (colors && colors!=pm_load(m,0x596984u,4)) { status=release(p,colors,pm_load(m,palette,2)*4u);if (status) return status; }
    return release(p,palette,20);
}
int frame_texture_destroy(frame_data_read read,frame_data_write write,void *ctx,uint32_t descriptor,
                              const frame_entity_factory_services *e,const frame_texture_services *s)
{
    texture_context p={{read,write,ctx,0},e,s};physics_memory *m=&p.m;uint32_t value,unused;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    if (!descriptor || descriptor==pm_load(m,0x5329ecu,4)) return m->error;
    status=device(&p,FRAME_TEXTURE_RELEASE,descriptor,0,0,&unused);if (status) return status;
    value=pm_load(m,descriptor+14u,4);status=palette_release(&p,value);if (status) return status;
    value=pm_load(m,descriptor+50u,4);if (m->error) return m->error;
    if (value) { status=frame_list_destroy(read,write,ctx,value,e->release,e->system_ctx,e->work_limit);if (status) return status; }
    status=release(&p,descriptor,58);if (status) return status;
    value=pm_load(m,0x594160u,4);if (m->error) return m->error;
    if (value) return frame_list_remove_value(read,write,ctx,descriptor,value,e->release,e->system_ctx,e->work_limit);
    return 0;
}
int frame_texture_create(frame_data_read read,frame_data_write write,void *ctx,const frame_texture_parameters *q,
                             const frame_entity_factory_services *e,const frame_texture_services *s,uint32_t *out)
{
    texture_context p={{read,write,ctx,0},e,s};physics_memory *m=&p.m;uint32_t descriptor,palette,list,value,unused,v,shift=0;int status;
    if (!read || !write || !e || !e->work_limit || !q || !out) return -1;
#define TRY(x) do { if (m->error) return m->error;status=(x);if (status) return status; } while (0)
    TRY(frame_pool_allocate_zero(read,write,ctx,58,e->allocate,e->release,e->system_ctx,e->work_limit,&descriptor));
    if (!descriptor) return -4;
    pm_store(m,descriptor,q->flags,2);pm_store(m,descriptor+2u,q->width,2);pm_store(m,descriptor+4u,q->height,2);
    pm_store(m,descriptor+6u,q->reload,4);pm_store(m,descriptor+10u,q->user_data,4);
    TRY(frame_list_create(read,write,ctx,e->allocate,e->release,e->system_ctx,e->work_limit,&list));pm_store(m,descriptor+50u,list,4);
    TRY(frame_pool_allocate_zero(read,write,ctx,20,e->allocate,e->release,e->system_ctx,e->work_limit,&palette));
    if (!palette) pm_store(m,descriptor+14u,0,4);
    else {
        pm_store(m,palette,256,2);pm_store(m,palette+4u,pm_load(m,0x596984u,4),4);pm_store(m,palette+12u,descriptor,4);
        TRY(device(&p,FRAME_TEXTURE_PALETTE_CREATE,palette,0,0,&unused));pm_store(m,descriptor+14u,palette,4);
    }
    if (!pm_load(m,descriptor+14u,4)) return m->error ? m->error : -4;
    if (pm_load(m,descriptor,1)&4u) {
        v=2;while (pm_signed(v)<=pm_signed(q->width) && pm_signed(q->width)>1) { v*=2u;shift++;if (!v) return -3; }
        pm_store(m,descriptor+18u,shift,2);pm_store(m,descriptor+20u,q->mip_levels,2);pm_store(m,descriptor+22u,q->scale,4);
    }
    TRY(device(&p,FRAME_TEXTURE_CREATE,descriptor,0,0,&value));
    if (!value) {
        TRY(palette_release(&p,pm_load(m,descriptor+14u,4)));TRY(release(&p,descriptor,58));*out=0;return 0;
    }
    value=pm_load(m,descriptor+6u,4);
    if (value) {
        unused=pm_load(m,descriptor+10u,4);if (m->error) return m->error;
        if (!s || !s->reload) return -5;
        status=s->reload(s->reload_ctx,value,descriptor,unused,&value);if (status) return status;
        if (!value) { TRY(frame_texture_destroy(read,write,ctx,descriptor,e,s));*out=0;return 0; }
    }
    list=pm_load(m,0x594160u,4);
    if (list) { TRY(frame_list_prepend(read,write,ctx,descriptor,list,e->allocate,e->release,e->system_ctx,e->work_limit,&unused)); }
    if (m->error) return m->error;
    *out=descriptor;return 0;
#undef TRY
}
int frame_texture_palette(frame_data_read read,frame_data_write write,void *ctx,uint32_t descriptor,uint32_t start,uint32_t count,uint32_t colors,
                              const frame_entity_factory_services *e,const frame_texture_services *s)
{
    texture_context p={{read,write,ctx,0},e,s};physics_memory *m=&p.m;uint32_t palette,dest,n,i,value,unused;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    palette=pm_load(m,descriptor+14u,4);n=pm_load(m,palette,2);
    if (m->error) return m->error;
    if (n<=start || n<count+start) return -4;
    palette=pm_load(m,descriptor+14u,4);
    if (palette!=0x596980u) {
        value=pm_load(m,palette+4u,4);
        if (!value || value==pm_load(m,0x596984u,4)) {
            if (m->error) return m->error;
            status=frame_pool_allocate(read,write,ctx,pm_load(m,palette,2)*4u,e->allocate,e->release,e->system_ctx,e->work_limit,&dest);if (status) return status;
            pm_store(m,palette+4u,dest,4);
            if (value) {
                palette=pm_load(m,descriptor+14u,4);n=pm_load(m,palette,2);dest=pm_load(m,palette+4u,4);value=pm_load(m,0x596984u,4);
                for (i=0;i<n;i++) { if (i>=e->work_limit) return -3;pm_store(m,dest+i*4u,pm_load(m,value+i*4u,4),4); }
            }
        }
    }
    dest=pm_load(m,pm_load(m,descriptor+14u,4)+4u,4)+start*4u;
    n=count&0x3fffffffu;for (i=0;i<n;i++) { if (i>=e->work_limit) return -3;pm_store(m,dest+i*4u,pm_load(m,colors+i*4u,4),4); }
    palette=pm_load(m,descriptor+14u,4);
    if (pm_load(m,0x596508u,4)) { status=device(&p,FRAME_TEXTURE_PALETTE_SET,palette,start,count,&unused);if (status) return status; }
    value=pm_load(m,palette+12u,4);
    if (value && (pm_load(m,value,1)&4u)) { status=device(&p,FRAME_TEXTURE_PALETTE_UPLOAD,value,start,count,&unused);if (status) return status; }
    return m->error;
}
