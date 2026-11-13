#include "frame_pool.h"
#include "physics_memory.h"

#define FREE_LISTS 0x54abb0u
typedef struct pool_work { physics_memory memory;uint32_t used,limit; } pool_work;
static int work(pool_work *w)
{
    if (w->memory.error) return 0;
    if (w->used++>=w->limit) { w->memory.error=-3;return 0; }
    return 1;
}
static void release_block(physics_memory *m,uint32_t pointer,uint32_t bytes,
                            frame_pool_system_free release,void *release_ctx)
{
    uint32_t rounded,index,flags;
    if (!bytes || m->error) return;
    rounded=(bytes+3u)&~3u;
    flags=pm_load(m,0x54b3d8u,1);
    pm_store(m,0x54b3e4u,pm_load(m,0x54b3e4u,4)-rounded,4);
    if (!(flags&1u) && rounded<2049u) {
        index=(bytes+3u)>>2;
        pm_store(m,pointer,pm_load(m,FREE_LISTS+index*4u,4),4);
        pm_store(m,FREE_LISTS+index*4u,pointer,4);
        pm_store(m,0x54b3e8u,pm_load(m,0x54b3e8u,4)+rounded,4);
    } else if (!m->error) m->error=release ? release(release_ctx,pointer) : -5;
}
int frame_pool_release(frame_data_read read,frame_data_write write,void *ctx,uint32_t pointer,uint32_t bytes,
                         frame_pool_system_free release,void *release_ctx)
{
    physics_memory m={read,write,ctx,0};
    if (!read || !write) return -1;
    release_block(&m,pointer,bytes,release,release_ctx);return m.error;
}
static uint32_t split(pool_work *w,int32_t bytes)
{
    physics_memory *m=&w->memory;
    int32_t index=512,requested=pm_sar(bytes,2);
    uint32_t pointer;
    pm_store(m,0x54b400u,pm_load(m,0x54b400u,4)+1u,4);
    if (requested<512) while (requested<index) {
        if (!work(w)) return 0;
        if (pm_load(m,FREE_LISTS+(uint32_t)index*4u,4)) break;
        index=pm_sub(index,1);
    }
    pointer=pm_load(m,FREE_LISTS+(uint32_t)index*4u,4);
    if (pointer) {
        uint32_t remainder=FREE_LISTS+(uint32_t)pm_sub(index,requested)*4u;
        pm_store(m,FREE_LISTS+(uint32_t)index*4u,pm_load(m,pointer,4),4);
        pm_store(m,pointer+(uint32_t)bytes,pm_load(m,remainder,4),4);
        pm_store(m,remainder,pointer+(uint32_t)bytes,4);
        pm_store(m,0x54b3e8u,pm_load(m,0x54b3e8u,4)-(uint32_t)bytes,4);
    }
    return pointer;
}
int frame_pool_split(frame_data_read read,frame_data_write write,void *ctx,int32_t bytes,
                       uint32_t work_limit,uint32_t *out)
{
    pool_work w={{read,write,ctx,0},0,work_limit};uint32_t pointer;
    if (!read || !write || !out || !work_limit) return -1;
    pointer=split(&w,bytes);
    if (w.memory.error) return w.memory.error;
    *out=pointer;return 0;
}
static uint32_t merge(pool_work *w,int32_t bytes,uint32_t pointer,int32_t other_class)
{
    physics_memory *m=&w->memory;
    uint32_t head=FREE_LISTS+(uint32_t)other_class*4u,node=pm_load(m,head,4),previous=0;
    while (node && work(w)) {
        if (node!=pointer && (node+(uint32_t)other_class*4u==pointer || node-(uint32_t)bytes==pointer)) {
            uint32_t result=node+(uint32_t)other_class*4u==pointer ? node : pointer;
            uint32_t combined=FREE_LISTS+(uint32_t)pm_add(pm_sar(bytes,2),other_class)*4u;
            pm_store(m,previous ? previous : head,pm_load(m,node,4),4);
            pm_store(m,result,pm_load(m,combined,4),4);pm_store(m,combined,result,4);
            return result;
        }
        previous=node;node=pm_load(m,node,4);
    }
    return 0;
}
int frame_pool_merge_adjacent(frame_data_read read,frame_data_write write,void *ctx,int32_t bytes,
                                uint32_t pointer,int32_t other_class,uint32_t work_limit,uint32_t *out)
{
    pool_work w={{read,write,ctx,0},0,work_limit};uint32_t result;
    if (!read || !write || !out || !work_limit) return -1;
    result=merge(&w,bytes,pointer,other_class);
    if (w.memory.error) return w.memory.error;
    *out=result;return 0;
}
static void compact(pool_work *w,int32_t index)
{
    physics_memory *m=&w->memory;
    uint32_t head=FREE_LISTS+(uint32_t)index*4u,node=pm_load(m,head,4),unmerged=0;
    while (node && work(w)) {
        int32_t other=index;
        uint32_t combined=0;
        pm_store(m,head,pm_load(m,node,4),4);
        while (pm_add(other,index)<512) {
            if (combined || !work(w)) break;
            combined=merge(w,pm_mul(index,4),node,other);other=pm_add(other,1);
        }
        if (!combined) { pm_store(m,node,unmerged,4);unmerged=node; }
        node=pm_load(m,head,4);
    }
    while (unmerged && work(w)) {
        node=pm_load(m,unmerged,4);pm_store(m,unmerged,pm_load(m,head,4),4);
        pm_store(m,head,unmerged,4);unmerged=node;
    }
}
int frame_pool_compact(frame_data_read read,frame_data_write write,void *ctx,int32_t index,uint32_t work_limit)
{
    pool_work w={{read,write,ctx,0},0,work_limit};
    if (!read || !write || !work_limit) return -1;
    compact(&w,index);return w.memory.error;
}
int frame_pool_compact_all(frame_data_read read,frame_data_write write,void *ctx,uint32_t work_limit)
{
    pool_work w={{read,write,ctx,0},0,work_limit};physics_memory *m=&w.memory;int32_t i;
    if (!read || !write || !work_limit) return -1;
    for (i=1;i<512 && !m->error;i++) compact(&w,i);
    pm_store(m,0x54b404u,pm_load(m,0x54b404u,4)+1u,4);return m->error;
}
int frame_pool_add_reserve(frame_data_read read,frame_data_write write,void *ctx,uint32_t pointer,uint32_t bytes)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;int32_t count;
    if (!read || !write) return -1;
    count=pm_i32(m,0x54b414u);
    if (count<8) {
        pm_store(m,0x54ab90u+(uint32_t)count*4u,pointer,4);
        pm_store(m,0x54b3b8u+(uint32_t)count*4u,bytes,4);
        pm_write32(m,0x54b414u,pm_add(count,1));
        if (pm_load(m,0x54b3f0u,4)<pointer+bytes) pm_store(m,0x54b3f0u,pointer+bytes,4);
        pm_store(m,0x54b3fcu,pm_load(m,0x54b3fcu,4)+bytes,4);
    }
    return m->error;
}
static uint32_t system_alloc(pool_work *w,frame_pool_system_alloc allocate,void *ctx,uint32_t bytes)
{
    uint32_t result=0;int status;
    if (!work(w)) return 0;
    if (!allocate) { w->memory.error=-5;return 0; }
    status=allocate(ctx,bytes,&result);
    if (status) w->memory.error=status;
    /* Complete 4F4690's invalid-pointer normalization around standard malloc. */
    return result==UINT32_MAX ? 0 : result;
}
static void statistics(physics_memory *m,uint32_t masked_end)
{
    uint32_t used=pm_load(m,0x54b3e4u,4),end;
    if (pm_load(m,0x54b3ecu,4)<used) pm_store(m,0x54b3ecu,used,4);
    end=pm_load(m,0x54b3f4u,4);
    if (end<masked_end) { end=masked_end;pm_store(m,0x54b3f4u,end,4); }
    if (pm_load(m,0x54b3f0u,4)<masked_end && masked_end<pm_load(m,0x534d24u,4))
        pm_store(m,0x534d24u,masked_end,4);
    pm_store(m,0x54b3f8u,end-pm_load(m,0x534d24u,4),4);
}
static uint32_t reserve_allocate(pool_work *w,uint32_t bytes,frame_pool_system_alloc allocate,
                                   frame_pool_system_free release,void *system_ctx)
{
    physics_memory *m=&w->memory;
    uint32_t rounded=(bytes+3u)&~3u,pointer,available,chunk=0,new_bytes=4096,step=4096;
    int32_t count,index,state=0;
    if (!bytes) return 0;
    if (pm_load(m,0x534d28u,4) && pm_signed(rounded)>pm_i32(m,0x534d2cu)) return 0;
    if (rounded>=4096u) {
        pointer=system_alloc(w,allocate,system_ctx,rounded);
        if (pointer) pm_store(m,0x534d2cu,pm_load(m,0x534d2cu,4)-rounded,4);
        return pointer;
    }
    pointer=pm_load(m,0x54b418u,4);available=pm_load(m,0x54b41cu,4);
    if (!pointer || available<rounded) {
        count=pm_i32(m,0x54b414u);
        if (count>0) {
            index=0;
            do {
                uint32_t candidate=pm_load(m,0x54b3b8u+(uint32_t)index*4u,4);
                if (!work(w)) return 0;
                if (rounded<=candidate) {
                    chunk=pm_load(m,0x54ab90u+(uint32_t)index*4u,4);count=pm_sub(count,1);new_bytes=candidate;
                    while (index<count && work(w)) {
                        pm_store(m,0x54ab90u+(uint32_t)index*4u,pm_load(m,0x54ab94u+(uint32_t)index*4u,4),4);
                        pm_store(m,0x54b3b8u+(uint32_t)index*4u,pm_load(m,0x54b3bcu+(uint32_t)index*4u,4),4);
                        index=pm_add(index,1);
                    }
                }
                index=pm_add(index,1);
            } while (index<count && !m->error);
            pm_write32(m,0x54b414u,count);
        }
        if (!chunk) {
            for (;;) {
                chunk=system_alloc(w,allocate,system_ctx,new_bytes);
                if (m->error) return 0;
                if (chunk) {
                    if (!state || pm_signed(step)<8) break;
                    step=(uint32_t)pm_sar(pm_signed(step),1);state=1;new_bytes+=step;
                    if (!release) { m->error=-5;return 0; }
                    m->error=release(system_ctx,chunk);
                    if (m->error) return 0;
                } else if (pm_signed(step)>8) {
                    step=(uint32_t)pm_sar(pm_signed(step),1);state=-1;new_bytes-=step;
                } else if (new_bytes>=8u) { state=-1;new_bytes-=step; }
                else break;
            }
            available=pm_load(m,0x54b41cu,4);pointer=pm_load(m,0x54b418u,4);
        }
        available&=~3u;pm_store(m,0x54b41cu,available,4);
        if (pointer && available) {
            if (available<=2048u) {
                uint32_t head=FREE_LISTS+(available>>2)*4u;
                pm_store(m,pointer,pm_load(m,head,4),4);pm_store(m,head,pm_load(m,0x54b418u,4),4);
                pm_store(m,0x54b3e8u,pm_load(m,0x54b3e8u,4)+pm_load(m,0x54b41cu,4),4);
            } else {
                pm_store(m,0x54b3fcu,pm_load(m,0x54b3fcu,4)-available,4);
                if (!m->error) m->error=frame_pool_add_reserve(m->read,m->write,m->ctx,pointer,available);
            }
        }
        pm_store(m,0x54b418u,chunk,4);pm_store(m,0x54b420u,new_bytes,4);pm_store(m,0x54b41cu,new_bytes,4);
        if (!chunk) return 0;
        statistics(m,(chunk+new_bytes)&0x7fffffffu);
        if (new_bytes<rounded) return 0;
        pm_store(m,0x54b424u,pm_load(m,0x54b424u,4)+1u,4);
        pointer=pm_load(m,0x54b418u,4);available=pm_load(m,0x54b41cu,4);
    }
    pm_store(m,0x54b41cu,available-rounded,4);
    pm_store(m,0x54b418u,pointer+rounded,4);pm_store(m,0x534d2cu,pm_load(m,0x534d2cu,4)-rounded,4);
    return pointer;
}
int frame_pool_reserve_allocate(frame_data_read read,frame_data_write write,void *ctx,uint32_t bytes,
                                  frame_pool_system_alloc allocate,frame_pool_system_free release,
                                  void *system_ctx,uint32_t work_limit,uint32_t *out)
{
    pool_work w={{read,write,ctx,0},0,work_limit};uint32_t result;
    if (!read || !write || !out || !work_limit) return -1;
    result=reserve_allocate(&w,bytes,allocate,release,system_ctx);
    if (w.memory.error) return w.memory.error;
    *out=result;return 0;
}
int frame_pool_allocate(frame_data_read read,frame_data_write write,void *ctx,uint32_t bytes,
                          frame_pool_system_alloc allocate,frame_pool_system_free release,
                          void *system_ctx,uint32_t work_limit,uint32_t *out)
{
    pool_work w={{read,write,ctx,0},0,work_limit};physics_memory *m=&w.memory;
    uint32_t rounded=(bytes+3u)&~3u,result=0,index,masked,end;
    int32_t i;
    if (!read || !write || !out || !work_limit) return -1;
    if (bytes) {
        if (!(pm_load(m,0x54b3d8u,1)&1u) && rounded<=2048u) {
            index=rounded>>2;result=pm_load(m,FREE_LISTS+index*4u,4);
            if (result) {
                pm_store(m,FREE_LISTS+index*4u,pm_load(m,result,4),4);
                pm_store(m,0x54b3e8u,pm_load(m,0x54b3e8u,4)-rounded,4);
            } else {
                result=reserve_allocate(&w,rounded,allocate,release,system_ctx);
                if (!result && !m->error) result=split(&w,pm_signed(rounded));
                if (!result && !m->error) {
                    for (i=1;i<512 && !m->error;i++) compact(&w,i);
                    pm_store(m,0x54b404u,pm_load(m,0x54b404u,4)+1u,4);
                    result=pm_load(m,FREE_LISTS+index*4u,4);
                    if (result) {
                        pm_store(m,FREE_LISTS+index*4u,pm_load(m,result,4),4);
                        pm_store(m,0x54b3e8u,pm_load(m,0x54b3e8u,4)-rounded,4);
                    } else result=split(&w,pm_signed(rounded));
                }
            }
        } else result=system_alloc(&w,allocate,system_ctx,rounded);
        if (result && !m->error) {
            masked=result&0x7fffffffu;
            pm_store(m,0x54b3e4u,pm_load(m,0x54b3e4u,4)+rounded,4);
            if (pm_load(m,0x54b3ecu,4)<pm_load(m,0x54b3e4u,4)) pm_store(m,0x54b3ecu,pm_load(m,0x54b3e4u,4),4);
            end=pm_load(m,0x54b3f4u,4);
            if (end<masked+rounded) { end=masked+rounded;pm_store(m,0x54b3f4u,end,4); }
            if (pm_load(m,0x54b3f0u,4)<masked && masked<pm_load(m,0x534d24u,4)) pm_store(m,0x534d24u,masked,4);
            pm_store(m,0x54b3f8u,end-pm_load(m,0x534d24u,4),4);
        }
    }
    if (m->error) return m->error;
    *out=result;return 0;
}
static void recycle(physics_memory *m,uint32_t node,int unlink,
                       frame_pool_system_free release,void *release_ctx)
{
    if (!node || m->error) return;
    if (unlink) {
        pm_store(m,pm_load(m,node+4u,4),pm_load(m,node,4),4);
        pm_store(m,pm_load(m,node,4)+4u,pm_load(m,node+4u,4),4);
    }
    pm_store(m,0x54ab68u,pm_load(m,0x54ab68u,4)-1u,4);pm_store(m,node+8u,0,4);
    release_block(m,node,12,release,release_ctx);
}
int frame_pool_allocate_zero(frame_data_read read,frame_data_write write,void *ctx,uint32_t bytes,
                               frame_pool_system_alloc allocate,frame_pool_system_free release,
                               void *system_ctx,uint32_t work_limit,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t pointer,i=0,used=0;int status;
    if (!out) return -1;
    status=frame_pool_allocate(read,write,ctx,bytes,allocate,release,system_ctx,work_limit,&pointer);if (status) return status;
    if (bytes && !pointer) return -4;
    for (;i<(bytes&~3u);i+=4u) {
        if (used++>=work_limit) return -3;
        pm_store(&m,pointer+i,0,4);if (m.error) return m.error;
    }
    for (;i<bytes;i++) {
        if (used++>=work_limit) return -3;
        pm_store(&m,pointer+i,0,1);if (m.error) return m.error;
    }
    *out=pointer;return 0;
}
int frame_list_remove(frame_data_read read,frame_data_write write,void *ctx,uint32_t node,
                        frame_pool_system_free release,void *release_ctx)
{
    physics_memory m={read,write,ctx,0};
    if (!read || !write) return -1;
    recycle(&m,node,1,release,release_ctx);return m.error;
}
int frame_list_recycle(frame_data_read read,frame_data_write write,void *ctx,uint32_t node,
                         frame_pool_system_free release,void *release_ctx)
{
    physics_memory m={read,write,ctx,0};
    if (!read || !write) return -1;
    recycle(&m,node,0,release,release_ctx);return m.error;
}
int frame_list_pop(frame_data_read read,frame_data_write write,void *ctx,uint32_t head,
                     frame_pool_system_free release,void *release_ctx,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t node,result=0;
    if (!read || !write || !out) return -1;
    node=pm_load(&m,head,4);
    if (node!=head) { result=pm_load(&m,node+8u,4);recycle(&m,node,1,release,release_ctx); }
    if (m.error) return m.error;
    *out=result;return 0;
}
int frame_list_new(frame_data_read read,frame_data_write write,void *ctx,
                     frame_pool_system_alloc allocate,frame_pool_system_free release,
                     void *system_ctx,uint32_t work_limit,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t node;int status;
    if (!read || !write || !out || !work_limit) return -1;
    pm_store(&m,0x54ab68u,pm_load(&m,0x54ab68u,4)+1u,4);
    if (m.error) return m.error;
    status=frame_pool_allocate(read,write,ctx,12,allocate,release,system_ctx,work_limit,&node);
    if (status) return status;
    if (node) pm_store(&m,node+8u,0,4);
    if (m.error) return m.error;
    *out=node;return 0;
}
int frame_list_create(frame_data_read read,frame_data_write write,void *ctx,
                        frame_pool_system_alloc allocate,frame_pool_system_free release,
                        void *system_ctx,uint32_t work_limit,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t node;int status;
    if (!out) return -1;
    status=frame_list_new(read,write,ctx,allocate,release,system_ctx,work_limit,&node);if (status) return status;
    if (node) { pm_store(&m,node+8u,node,4);pm_store(&m,node,node,4);pm_store(&m,node+4u,node,4); }
    if (m.error) return m.error;
    *out=node;return 0;
}
int frame_list_append(frame_data_read read,frame_data_write write,void *ctx,uint32_t value,uint32_t head,
                        frame_pool_system_alloc allocate,frame_pool_system_free release,
                        void *system_ctx,uint32_t work_limit,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t node;int status;
    if (!read || !write || !out || !work_limit) return -1;
    status=frame_list_new(read,write,ctx,allocate,release,system_ctx,work_limit,&node);
    if (status) return status;
    if (node) {
        pm_store(&m,node+8u,value,4);
        pm_store(&m,pm_load(&m,head+4u,4),node,4);
        pm_store(&m,node,head,4);pm_store(&m,node+4u,pm_load(&m,head+4u,4),4);pm_store(&m,head+4u,node,4);
    }
    if (m.error) return m.error;
    *out=node;return 0;
}
int frame_list_prepend(frame_data_read read,frame_data_write write,void *ctx,uint32_t value,uint32_t head,
                         frame_pool_system_alloc allocate,frame_pool_system_free release,
                         void *system_ctx,uint32_t work_limit,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t node;int status;
    if (!read || !write || !out || !work_limit) return -1;
    status=frame_list_new(read,write,ctx,allocate,release,system_ctx,work_limit,&node);
    if (status) return status;
    if (node) {
        pm_store(&m,node+8u,value,4);pm_store(&m,pm_load(&m,head,4)+4u,node,4);
        pm_store(&m,node,pm_load(&m,head,4),4);pm_store(&m,node+4u,head,4);pm_store(&m,head,node,4);
    }
    if (m.error) return m.error;
    *out=node;return 0;
}
int frame_list_remove_value(frame_data_read read,frame_data_write write,void *ctx,uint32_t value,uint32_t head,
                              frame_pool_system_free release,void *release_ctx,uint32_t work_limit)
{
    physics_memory m={read,write,ctx,0};uint32_t node,work=0;
    if (!read || !write || !work_limit) return -1;
    node=pm_load(&m,head,4);
    for (;;) {
        uint32_t current=pm_load(&m,node+8u,4);
        if (m.error) return m.error;
        if (work++>=work_limit) return -3;
        if (current==value) {
            if (node!=head) recycle(&m,node,1,release,release_ctx);
            return m.error;
        }
        if (node==head) return 0;
        node=pm_load(&m,node,4);
    }
}
int frame_list_contains(frame_data_read read,void *ctx,uint32_t value,uint32_t head,uint32_t work_limit,uint32_t *out)
{
    physics_memory m={read,0,ctx,0};uint32_t node,work=0;
    if (!read || !out || !work_limit) return -1;
    node=pm_load(&m,head,4);
    for (;;) {
        uint32_t current=pm_load(&m,node+8u,4);
        if (m.error) return m.error;
        if (work++>=work_limit) return -3;
        if (current==value || node==head) { *out=node!=head;return 0; }
        node=pm_load(&m,node,4);
    }
}
int frame_list_prepend_unique(frame_data_read read,frame_data_write write,void *ctx,uint32_t value,uint32_t head,
                                frame_pool_system_alloc allocate,frame_pool_system_free release,
                                void *system_ctx,uint32_t work_limit)
{
    uint32_t found,node;int status;
    if (!write) return -1;
    status=frame_list_contains(read,ctx,value,head,work_limit,&found);if (status || found) return status;
    return frame_list_prepend(read,write,ctx,value,head,allocate,release,system_ctx,work_limit,&node);
}
int frame_list_clear(frame_data_read read,frame_data_write write,void *ctx,uint32_t head,
                       frame_pool_system_free release,void *release_ctx,uint32_t work_limit)
{
    physics_memory m={read,write,ctx,0};uint32_t node,work=0;int status;
    if (!read || !write || !work_limit) return -1;
    if (pm_load(&m,head+8u,4)!=head) return m.error;
    node=pm_load(&m,head,4);
    while (node!=head) {
        uint32_t old=node;node=pm_load(&m,node,4);
        if (m.error) return m.error;
        if (work++>=work_limit) return -3;
        status=frame_list_remove(read,write,ctx,old,release,release_ctx);if (status) return status;
    }
    return m.error;
}
int frame_list_destroy(frame_data_read read,frame_data_write write,void *ctx,uint32_t head,
                         frame_pool_system_free release,void *release_ctx,uint32_t work_limit)
{
    physics_memory m={read,write,ctx,0};int status;
    if (!read || !write || !work_limit) return -1;
    if (pm_load(&m,head+8u,4)!=head) return m.error;
    status=frame_list_clear(read,write,ctx,head,release,release_ctx,work_limit);if (status) return status;
    return frame_list_recycle(read,write,ctx,head,release,release_ctx);
}
