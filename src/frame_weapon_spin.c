#include "frame_weapon_spin.h"
#include "physics_memory.h"

int frame_scalar_approach(frame_data_read read,frame_data_write write,void *ctx,uint32_t address,int32_t target,uint32_t packed)
{
    physics_memory m={read,write,ctx,0};uint32_t low,factor;int32_t amount,delta,decay;
    if (!read || !write) return -1;
    low=(packed*pm_load(&m,0x5904fcu,2))&65535u;amount=low>32767u ? (int32_t)low-65536 : (int32_t)low;
    factor=(((packed>>16)&255u)*pm_load(&m,0x5904fcu,1))&255u;
    delta=pm_sub(pm_i32(&m,address),target);
    if (delta>amount) delta=pm_sub(delta,amount);
    else if (delta<pm_neg(amount)) delta=pm_add(delta,amount);
    else delta=0;
    if (delta) {
        int32_t before=delta;decay=pm_trunc(pm_mul(delta,(int32_t)factor),packed>>24);delta=pm_sub(delta,decay);
        if ((before<0 && delta>0) || (before>0 && delta<0)) delta=0;
    }
    pm_write32(&m,address,pm_add(delta,target));return m.error;
}
static uint32_t random_next(physics_memory *m)
{
    uint32_t seed=pm_load(m,0x55b6acu,4)*1664525u+1013904223u;pm_store(m,0x55b6acu,seed,4);return seed;
}
int frame_weapon_spin_start(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t record,o,color,roll;int32_t mode;
    if (!read || !write || !out) return -1;
    record=pm_load(&m,pm_load(&m,0x55c694u,4)+entity*8u,4);o=record ? pm_load(&m,record+4u,4) : 0;
    if (!pm_load(&m,o+0xdafu,4)) pm_store(&m,0x545de0u,pm_load(&m,0x545de0u,1)+1u,1);
    mode=pm_i32(&m,0x52ee9cu);
    pm_store(&m,o+0xd9bu,(!pm_load(&m,o+0xde5u,1) || mode==4 || mode==5) ? 0u : 4000u,4);
    color=pm_load(&m,o+0xda3u,4);pm_store(&m,o+0xda3u,color&0xffff003fu,4);
    pm_store(&m,o+0xdafu,1,4);pm_store(&m,o+0xda3u,color&0xffff0001u,4);pm_store(&m,o+0xd9au,1,1);
    roll=(random_next(&m)>>4)&65535u;color=pm_load(&m,o+0xda7u,4);
    pm_store(&m,o+0xda7u,(((roll%12u*2u+6u)^color)&0x3eu)^color,4);
    roll=(random_next(&m)>>4)&65535u;color=pm_load(&m,o+0xda7u,4);
    pm_store(&m,o+0xda7u,((((roll%12u+3u)*64u)^color)&0x7c0u)^color,4);
    roll=(random_next(&m)>>4)&65535u;pm_store(&m,o+0xd99u,1,1);color=pm_load(&m,o+0xda7u,4);
    pm_store(&m,o+0xda7u,((((roll%12u+3u)*2048u)^color)&0xf800u)^color,4);pm_store(&m,0x545de4u,0,1);
    if (m.error) return m.error;
    *out=roll/12u;return 0;
}
static void chase_channel(physics_memory *m,uint32_t address,uint32_t target,uint32_t mask,uint32_t base,uint32_t up,uint32_t down)
{
    uint32_t value=pm_load(m,address,4),goal=target&mask;
    if (!((value^target)&mask)) return;
    if ((value&mask)<goal) {
        value=((((value&base)+up)^value)&mask)^value;pm_store(m,address,value,4);
        if ((value&mask)>goal) pm_store(m,address,((value^target)&mask)^value,4);
    }
    value=pm_load(m,address,4);
    if ((value&mask)>goal) {
        value=((((value&base)-down)^value)&mask)^value;pm_store(m,address,value,4);
        if ((value&mask)<goal) pm_store(m,address,((value^target)&mask)^value,4);
    }
}
int frame_weapon_spin_update(frame_data_read read,frame_data_write write,void *ctx,uint32_t o,uint32_t work_limit)
{
    physics_memory m={read,write,ctx,0};uint32_t flags,packed,work=0,color,target,roll;int32_t goal,angle,timer;int status;
    if (!read || !write || !work_limit) return -1;
    if (!pm_load(&m,o+0xde4u,1)) pm_store(&m,o+0xde4u,1,1);
    flags=pm_load(&m,o+0xd99u,1);packed=0;goal=0;
    if (flags&1u) { packed=pm_load(&m,0x531808u,4);goal=pm_i32(&m,0x5317fcu); }
    else if (flags&2u) {
        goal=pm_i32(&m,0x531800u);
        if (pm_i32(&m,o+0xdafu)==goal) {
            timer=pm_sub(pm_i32(&m,o+0xd9bu),pm_i32(&m,0x5904fcu));pm_write32(&m,o+0xd9bu,timer);
            if (timer<0) pm_store(&m,o+0xd99u,4,1);
        } else packed=pm_load(&m,0x53180cu,4);
    } else if ((flags&4u) && pm_i32(&m,o+0xdafu)>pm_i32(&m,0x531804u)) {
        packed=pm_load(&m,0x531808u,4);goal=pm_i32(&m,0x531804u);
    }
    /* Packed0 is a valid approach configuration: do not use it as a call gate. */
    if ((flags&1u) || ((flags&2u) && pm_i32(&m,o+0xdafu)!=goal) ||
        (!(flags&3u) && (flags&4u) && pm_i32(&m,o+0xdafu)>pm_i32(&m,0x531804u))) {
        if (m.error) return m.error;
        status=frame_scalar_approach(read,write,ctx,o+0xdafu,goal,packed);if (status) return status;
        if ((flags&1u) && pm_i32(&m,o+0xdafu)==pm_i32(&m,0x5317fcu)) pm_store(&m,o+0xd99u,2,1);
    }
    angle=pm_add(pm_i32(&m,o+0xdabu),pm_sar(pm_mul(pm_i32(&m,0x5904fcu),pm_i32(&m,o+0xdafu)),8));
    pm_write32(&m,o+0xdabu,angle);
    while (pm_i32(&m,o+0xdabu)>32760) {
        if (m.error) return m.error;
        if (work++>=work_limit) return -3;
        pm_write32(&m,o+0xdabu,pm_sub(pm_i32(&m,o+0xdabu),65520));
    }
    while (pm_i32(&m,o+0xdabu)<-32759) {
        if (m.error) return m.error;
        if (work++>=work_limit) return -3;
        pm_write32(&m,o+0xdabu,pm_add(pm_i32(&m,o+0xdabu),65520));
    }
    timer=pm_add(pm_i32(&m,o+0xd9fu),pm_i32(&m,0x5904fcu));pm_write32(&m,o+0xd9fu,timer);
    if (timer<=60) { pm_store(&m,0x545de4u,0,1);return m.error; }
    pm_store(&m,o+0xd9fu,0,4);pm_store(&m,0x545de4u,1,1);
    if ((pm_load(&m,o+0xd99u,1)&4u) && pm_i32(&m,o+0xdafu)<=pm_i32(&m,0x531804u)) {
        packed=pm_load(&m,0x531814u,4);if (m.error) return m.error;
        status=frame_scalar_approach(read,write,ctx,o+0xdafu,0,packed);if (status) return status;
        if (!pm_load(&m,o+0xdafu,4)) pm_store(&m,o+0xdafu,1,4);
        color=pm_load(&m,o+0xda3u,4);
        if (color&0x3eu) pm_store(&m,o+0xda3u,((((color&0xfffffffeu)-5u)^color)&0x3eu)^color,4);
        color=pm_load(&m,o+0xda3u,4);
        if (color&0x7c0u) pm_store(&m,o+0xda3u,((((color&0xffffffc0u)-0x81u)^color)&0x7c0u)^color,4);
        color=pm_load(&m,o+0xda3u,4);
        if (color&0xf800u) pm_store(&m,o+0xda3u,((((color&0xfffff800u)-0x1001u)^color)&0xf800u)^color,4);
        color=pm_load(&m,o+0xda3u,4);
        if (!(color&0xfffeu) && pm_i32(&m,o+0xdafu)==1) {
            if (pm_load(&m,o+0xd99u,1)&8u) pm_store(&m,o+0xda3u,(color&0xffffffe1u)|0x20u,4);
            else {
                pm_store(&m,o+0xdafu,0,4);pm_store(&m,o+0xde4u,0,1);
                pm_store(&m,0x545de0u,pm_load(&m,0x545de0u,1)-1u,1);
            }
        }
    } else {
        target=pm_load(&m,o+0xda7u,4);
        chase_channel(&m,o+0xda3u,target,0x3e,0xfffffffeu,6,5);
        chase_channel(&m,o+0xda3u,target,0x7c0,0xffffffc0u,0xc0,0x81);
        chase_channel(&m,o+0xda3u,target,0xf800,0xfffff800u,0x1800,0x1001);
        color=pm_load(&m,o+0xda3u,4);
        if (!((color^target)&0xfffeu)) {
            roll=(random_next(&m)>>4)&65535u;color=pm_load(&m,o+0xda7u,4);
            pm_store(&m,o+0xda7u,(((roll%12u*2u+6u)^color)&0x3eu)^color,4);
            roll=(random_next(&m)>>4)&65535u;color=pm_load(&m,o+0xda7u,4);
            pm_store(&m,o+0xda7u,((((roll%12u+3u)*64u)^color)&0x7c0u)^color,4);
            roll=(random_next(&m)>>4)&65535u;color=pm_load(&m,o+0xda7u,4);
            pm_store(&m,o+0xda7u,((((roll%12u+3u)*2048u)^color)&0xf800u)^color,4);
        }
    }
    return m.error;
}
