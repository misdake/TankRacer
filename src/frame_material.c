#include "frame_material.h"
#include "physics_memory.h"
static uint32_t fold(uint32_t c) { return c>=65u && c<=90u ? c+32u : c; }
int frame_material_resolve(frame_data_read read,frame_data_write write,void *ctx,const unsigned char *name,uint32_t name_bytes,uint32_t mode,
                               const frame_entity_factory_services *e,const frame_material_services *s,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t head,node,record=0,list,space,i,j,n,found=0,used=0,value,texture=0;int status,equal;
    unsigned char filename[16];
    if (!read || !write || !name || !name_bytes || !out || !e || !e->work_limit) return -1;
#define TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
#define WORK() do { if (m.error) return m.error;if (used++>=e->work_limit) return -3; } while (0)
    for (i=0;i<4u;i++) if (!pm_load(&m,0x55ae30u+i*4u,4)) {
        TRY(frame_list_create(read,write,ctx,e->allocate,e->release,e->system_ctx,e->work_limit,&head));pm_store(&m,0x55ae30u+i*4u,head,4);
    }
    list=mode==0u ? pm_load(&m,0x55ae30u,4) : mode==1u ? pm_load(&m,0x55ae38u,4) : mode==2u ? pm_load(&m,0x55ae3cu,4) : 0;
    space=pm_load(&m,mode==1u ? 0x55ae48u : mode==2u ? 0x55ae40u : 0x55ae44u,4);
    if (pm_load(&m,0x55ae4cu,4)) { space=pm_load(&m,0x55ae4cu,4);list=pm_load(&m,0x55ae38u,4); }
    head=pm_load(&m,0x55ae68u,4);
    if (!head) { TRY(frame_list_create(read,write,ctx,e->allocate,e->release,e->system_ctx,e->work_limit,&head));pm_store(&m,0x55ae68u,head,4); }
    node=pm_load(&m,head,4);
    while (node!=head) {
        WORK();record=pm_load(&m,node+8u,4);equal=1;
        for (i=0;;i++) {
            WORK();if (i>=name_bytes) return -3;value=pm_load(&m,record+i,1);
            if (value>=128u || name[i]>=128u) return -5;
            if (fold(value)!=fold(name[i])) { equal=0;break; }if (!value) break;
        }
        node=pm_load(&m,node,4);if (equal) { found=1;break; }
    }
    if (found) {
        value=pm_load(&m,record+16u,4);if (m.error) return m.error;
        *out=value ? record : 0;return 0; /* zero texture is NOT retried */
    }
    TRY(frame_pool_allocate(read,write,ctx,20,e->allocate,e->release,e->system_ctx,e->work_limit,&record));
    if (!record) { *out=0;return 0; }
    TRY(frame_list_append(read,write,ctx,record,head,e->allocate,e->release,e->system_ctx,e->work_limit,&node));
    if (list) { TRY(frame_list_append(read,write,ctx,record,list,e->allocate,e->release,e->system_ctx,e->work_limit,&node)); }
    for (n=0;;n++) { WORK();if (n>=name_bytes) return -3;if (!name[n]) break; }
    if (n>=16u) return -4;
    for (i=0;i+4u<=n+1u;i+=4u) { value=0;for (j=0;j<4u;j++) value|=(uint32_t)name[i+j]<<(j*8u);pm_store(&m,record+i,value,4); }
    for (;i<n+1u;i++) pm_store(&m,record+i,name[i],1);
    for (i=0;i<n;i++) filename[i]=name[i];
    for (j=0;;j++) {
        WORK();value=pm_load(&m,0x536738u+j,1);if (n+j>=16u) return -4;
        filename[n+j]=(unsigned char)value;if (!value) break;
    }
    if (m.error) return m.error;
    if (!s || !s->texture) return -5;
    status=s->texture(s->ctx,space,filename,n+j+1u,4,&texture);if (status) return status;
    pm_store(&m,record+16u,texture,4);if (m.error) return m.error;
    *out=texture ? record : 0;return 0;
#undef WORK
#undef TRY
}
