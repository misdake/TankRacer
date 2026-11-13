#include "frame_time.h"
#include "physics_memory.h"

static int system_tick(physics_memory *m,frame_time_poll poll,void *ctx,uint32_t *tick)
{
    if (m->error) return 0;
    if (!poll || !poll(ctx,tick)) { m->error=-4;return 0; }
    return 1;
}

int frame_time_update(frame_data_read read,frame_data_write write,void *ctx,
                       frame_time_poll poll,void *poll_ctx,uint32_t max_polls)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    uint32_t tick=0,anchor,raw=0,phase,acc_ten,i;
    if (!read || !write || !max_polls) return -1;
    if (pm_load(m,0x53d158u,4)) return m->error;
    pm_store(m,0x590550u,0,4);
    for (i=0;i<max_polls;i++) {
        if (!system_tick(m,poll,poll_ctx,&tick)) return m->error;
        anchor=pm_load(m,0x53d174u,4);
        pm_store(m,0x53d15cu,tick,4);
        if (!anchor) { anchor=tick-10u;pm_store(m,0x53d174u,anchor,4); }
        raw=tick-anchor;
        pm_store(m,0x590550u,raw,4);
        if (m->error) return m->error;
        if (raw>=10u) break;
    }
    if (raw<10u) return 1;
    if (raw>1000u) { raw=1000u;pm_store(m,0x590550u,raw,4); }
    pm_store(m,0x590564u,pm_load(m,0x590564u,4)+1u,4);
    phase=pm_load(m,0x590558u,4)+1u;
    pm_store(m,0x590558u,phase,4);
    if (phase>3u) pm_store(m,0x590558u,0,4);
    phase=pm_load(m,0x590568u,4)+1u;
    pm_store(m,0x590568u,phase,4);
    if (phase>15u) pm_store(m,0x590568u,0,4);
    acc_ten=pm_load(m,0x59055cu,4);
    pm_store(m,0x53d174u,tick,4);
    pm_store(m,0x590560u,raw/10u,4);
    pm_store(m,0x59054cu,pm_load(m,0x59054cu,4)+raw,4);
    pm_store(m,0x59055cu,acc_ten+raw/10u,4);
    pm_store(m,0x590554u,1000u/raw,4);
    return m->error;
}

int frame_time_suspend(frame_data_read read,frame_data_write write,void *ctx,
                        uint32_t suspend,frame_time_poll poll,void *poll_ctx)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    uint32_t depth,tick;
    if (!read || !write) return -1;
    depth=pm_load(m,0x53d158u,4);
    if (suspend) {
        pm_store(m,0x53d158u,depth+1u,4);
        if (!depth && system_tick(m,poll,poll_ctx,&tick)) pm_store(m,0x53d170u,tick,4);
    } else if (depth) {
        depth=pm_load(m,0x53d158u,4)-1u;
        pm_store(m,0x53d158u,depth,4);
        if (!depth && system_tick(m,poll,poll_ctx,&tick))
            pm_store(m,0x53d174u,pm_load(m,0x53d174u,4)+tick-pm_load(m,0x53d170u,4),4);
    }
    return m->error;
}

int frame_time_physics_dt(frame_data_read read,frame_data_write write,void *ctx,uint32_t *continuation)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    int32_t raw;
    uint32_t next;
    if (!read || !write || !continuation) return -1;
    raw=pm_i32(m,0x590550u);
    pm_write32(m,0x5904fcu,raw);
    if (raw<10) pm_store(m,0x5904fcu,10,4);
    else if (raw>100) pm_store(m,0x5904fcu,100,4);
    next=pm_load(m,0x53e430u,4);
    if (m->error) return m->error;
    *continuation=next;
    return 0;
}
