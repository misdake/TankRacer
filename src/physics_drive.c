#include "physics_drive.h"
#include "physics_memory.h"

static int32_t coefficient(physics_memory *m,int32_t value,uint32_t address)
{ return pm_trunc(pm_mul(value,(int32_t)pm_load(m,address,2)),pm_load(m,address+2,1)); }
static int32_t surface_average(physics_memory *m,int32_t value,int32_t a,int32_t b,unsigned offset)
{ return pm_sar(pm_add(coefficient(m,value,UINT32_C(0x546018)+(uint32_t)a*40u+offset),coefficient(m,value,UINT32_C(0x546018)+(uint32_t)b*40u+offset)),1); }
static uint32_t packed_force(physics_memory *m,int32_t force,uint32_t object_config)
{ return ((uint32_t)force&65535u)|(pm_load(m,object_config,2)<<16); }
static int any_fallback(physics_memory *m,uint32_t o)
{ unsigned i;for (i=0;i<4;++i) if (pm_i16(m,o+0x134+i*70u)==4) return 1;return 0; }
static int gear_update(physics_memory *m,uint32_t o,int32_t dt)
{
    int32_t remaining=pm_abs(pm_i32(m,o+0xa4)),gear=0,target;unsigned count=0;
    if (remaining>pm_i32(m,0x531b80)) do {
        if (gear>=5) break;
        remaining=pm_sub(remaining,pm_i32(m,UINT32_C(0x531b80)+(uint32_t)gear*4u));
        if (gear<4) ++gear;
        if (++count>65536u) return -3;
    } while (remaining>pm_i32(m,UINT32_C(0x531b80)+(uint32_t)gear*4u));
    pm_store(m,o+0xdde,(uint32_t)gear,1);
    target=pm_add(remaining,pm_i32(m,UINT32_C(0x531b68)+(uint32_t)gear*4u));
    pm_write32(m,o+0xb0,pm_add(target,pm_damp(pm_sub(pm_i32(m,o+0xb0),target),pm_load(m,0x5319d4,4),dt)));
    return 0;
}
static int32_t approach_target(int32_t value,int32_t target,uint32_t packed,int32_t dt)
{
    uint32_t scaled=((packed>>16)&255u)*(uint32_t)dt;int32_t force;
    uint32_t low=((packed&65535u)*(uint32_t)dt)&65535u;
    int32_t d=pm_sub(value,target),v;
    force=low>32767u ? (int32_t)low-65536 : (int32_t)low;
    v=d>force ? pm_sub(d,force) : d<pm_neg(force) ? pm_add(d,force) : 0;
    d=pm_sub(v,pm_trunc(pm_mul(v,(int32_t)(scaled&255u)),packed>>24));
    if ((v<0 && d>0) || (v>0 && d<0)) d=0;
    return pm_add(d,target);
}
int physics_vehicle_drive(physics_world_read read,physics_world_write write,void *ctx,uint32_t o,uint32_t id)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    int32_t dt=pm_i32(m,0x5904fc),a=pm_i32(m,o+0x716),b=pm_i32(m,o+0x71a),old_x=pm_i32(m,o+0xe4),old_z=pm_i32(m,o+0xec);
    int32_t lateral,force,v,base,budget,applied,delta,gear,cap,throttle,airborne,drive,term;
    uint32_t packed,flags=pm_load(m,o+0x107a,4);int player=pm_load(m,o+0xde5,1)!=0,alternate=pm_load(m,o+0x10bc,1)!=0;
    int mode=pm_i32(m,0x52ee9c),active=pm_load(m,o+0xdea,1)!=0;
    /* Lateral damping selector. Action bits select distinct surface descriptors. */
    if (player && alternate) lateral=pm_sar(pm_mul(pm_i16(m,o+0x6ca),pm_i16(m,o+0x10b6)),pm_load(m,UINT32_C(0x546022)+(uint32_t)a*40u,1));
    else {
        unsigned offset=8;
        if (player && (flags&3u)) {
            if (!(flags&56u)) return -4; /* Original would consume undeclared caller-stack coefficients. */
            offset=(flags&1u) ? 12u : 24u;
            offset+=(flags&8u) ? 0u : (flags&16u) ? 4u : 8u;
        }
        lateral=surface_average(m,pm_i16(m,o+0x6ca),a,b,offset);
    }
    force=pm_trunc(pm_mul(pm_mul(lateral,(int32_t)pm_load(m,o+0x6a6,2)),pm_i32(m,o+0xf4)),pm_load(m,o+0x6a8,1));
    packed=packed_force(m,force,o+0x6cc);pm_write32(m,o+0xe4,pm_damp(old_x,packed,dt));
    pm_write32(m,o+0xe8,pm_rate_step(pm_i32(m,o+0xe8),pm_i16(m,o+0x6be),dt));
    force=surface_average(m,pm_i16(m,o+0x6ae),a,b,0);packed=packed_force(m,force,o+0x6b0);
    pm_write32(m,o+0xec,pm_damp(pm_i32(m,o+0xec),packed,dt));
    pm_write32(m,o+0xb4,pm_damp(pm_i32(m,o+0xb4),pm_load(m,o+0x6c6,4),dt));
    pm_write32(m,o+0xbc,pm_damp(pm_i32(m,o+0xbc),pm_load(m,o+0x6c6,4),dt));
    /* Alternate controller reuses the prior longitudinal descriptor's high
     * bytes (6B0/6B1), rather than loading 6C2's damping bytes. */
    if (!alternate) packed=pm_load(m,o+0x6c2,4);
    force=alternate ? coefficient(m,pm_i16(m,o+0x10b8),UINT32_C(0x54601c)+(uint32_t)a*40u) : surface_average(m,pm_i16(m,o+0x6c2),a,b,4);
    airborne=pm_i8(m,o+0xde1);force=pm_trunc(pm_mul(pm_sub(4,airborne),force),2);
    packed=(packed&UINT32_C(0xffff0000))|((uint32_t)force&65535u);pm_write32(m,o+0xb8,pm_damp(pm_i32(m,o+0xb8),packed,dt));
    pm_write32(m,o+0xc0,pm_i32(m,o+0xe4));
    if (active && !pm_i32(m,o+0x94) && !pm_i32(m,o+0x98)) {
        term=pm_sub(pm_add(pm_i32(m,o+0x172),pm_i32(m,o+0x1fe)),pm_add(pm_i32(m,o+0x12c),pm_i32(m,o+0x1b8)));
        term=coefficient(m,term,o+0x67e);pm_write32(m,o+0xec,pm_add(pm_i32(m,o+0xec),term));
    }
    if (airborne==4) packed=pm_load(m,o+0x6b6,4);
    else if (pm_i32(m,o+0xa0)) packed=pm_load(m,o+0x6ba,4);
    else packed=packed_force(m,surface_average(m,pm_i16(m,o+0x6b2),a,b,0),o+0x6b4);
    pm_write32(m,o+0xa4,pm_decay(pm_i32(m,o+0xa4),packed,dt));
    if (pm_load(m,o+0xdeb,1)!=(uint32_t)active) {
        pm_store(m,o+0xdeb,(uint32_t)active,1);
        if (active) {
            uint32_t link=pm_load(m,pm_load(m,0x55c694,4)+id*8u,4),cls=link ? pm_load(m,link,4) : 0;
            if (cls==pm_load(m,0x544f98,4) && pm_load(m,o+0x1107,1)) {
                pm_write32(m,o+0xa4,314488);pm_write32(m,o+0xac,314488);pm_write32(m,o+0xec,19687);
            }
        }
    }
    budget=surface_average(m,pm_mul(pm_i32(m,o+0xac) ? 20000 : pm_i32(m,o+0x696),dt),a,b,4);
    if (pm_i32(m,o+0xac)) {
        if (pm_i32(m,o+0xa4)<pm_i32(m,o+0xac)) pm_write32(m,o+0xa4,approach_target(pm_i32(m,o+0xa4),pm_i32(m,o+0xac),pm_load(m,0x5319d8,4),dt));
        pm_write32(m,o+0xac,pm_damp(pm_i32(m,o+0xac),pm_load(m,0x5319dc,4),dt));
        v=pm_i32(m,o+0xec);
        if (v<314488) { v=pm_add(v,pm_mul(dt,512));if (v>314488) v=314488;pm_write32(m,o+0xec,v); }
    }
    v=pm_i32(m,o+0xec);drive=pm_i32(m,o+0xa4);
    if (v>0 && v>drive) drive=pm_add(drive,pm_sub(v,drive)<budget ? pm_sub(v,drive) : budget);
    else if (v<0 && pm_neg(v)>pm_neg(drive)) { term=pm_sub(drive,v);drive=pm_sub(drive,term<budget ? term : budget); }
    pm_write32(m,o+0xa4,drive);if (gear_update(m,o,dt)<0) return -3;
    term=pm_sar(drive,12);term=pm_mul(term,term);
    cap=pm_sub(pm_i32(m,o+0x66e),pm_mul(pm_i32(m,o+(pm_i16(m,o+0xdd6)<2501 ? 0x672u : 0x712u)),term));
    if (pm_i16(m,o+0xdd6)>=2501) cap=pm_add(cap,pm_i32(m,o+0xc9e));
    if (cap<0) cap=0;
    if (cap>pm_i32(m,o+0x666)) cap=pm_i32(m,o+0x666);
    throttle=pm_i32(m,o+0x94);gear=pm_i8(m,o+0xdde);
    term=pm_mul(pm_mul(pm_sar(cap,8),pm_sar(throttle,8)),dt);
    if (airborne==4) term=pm_mul(term,4);
    term=coefficient(m,pm_sar(term,15),UINT32_C(0x5319e0)+(uint32_t)gear*4u);drive=pm_add(pm_i32(m,o+0xa4),term);
    base=pm_abs(throttle)<1024 ? pm_mul(dt,16) : 0;
    term=pm_trunc(pm_mul(pm_mul(pm_mul(pm_sub(4,airborne),pm_sar(pm_i32(m,o+0x676),23)),pm_sar(pm_i32(m,o+0x98),8)),dt),2);
    if (drive>0) { drive=pm_sub(drive,pm_add(base,term));if (drive<0) drive=0; }
    else { drive=pm_add(drive,pm_add(base,term));if (drive>0) drive=0; }
    cap=pm_i32(m,o+0x662);if (drive<pm_neg(cap)) drive=pm_neg(cap);if (drive>cap) drive=cap;pm_write32(m,o+0xa4,drive);
    delta=airborne==4 ? 0 : pm_sub(drive,pm_i32(m,o+0xec));
    budget=surface_average(m,pm_mul(((mode==4 || mode==5) && any_fallback(m,o)) ? 400 : pm_i32(m,o+0x68e),dt),a,b,4);
    if (player && (mode==0 || mode==1 || mode==5) && pm_abs(pm_i32(m,o+0xec))<300000 && !pm_i32(m,o+0x98) && !pm_i32(m,o+0xac) && !any_fallback(m,o)) {
        if (delta>budget) { applied=pm_mul(budget,2);pm_write32(m,o+0xa0,pm_sub(delta,applied)); }
        else if (delta<pm_neg(budget)) { applied=pm_neg(pm_mul(budget,2));pm_write32(m,o+0xa0,pm_sub(delta,applied)); }
        else { applied=delta;pm_write32(m,o+0xa0,0); }
    } else {
        applied=delta>budget ? budget : delta<pm_neg(budget) ? pm_neg(budget) : delta;
        pm_write32(m,o+0xa0,pm_sub(delta,applied));
    }
    if (active) applied=pm_trunc(pm_mul(pm_sub(4,airborne),pm_add(applied,pm_i32(m,o+0xcaa))),2);
    pm_write32(m,o+0xec,pm_add(pm_i32(m,o+0xec),applied));pm_write32(m,o+0xc4,pm_sub(pm_i32(m,o+0xe4),old_x));pm_write32(m,o+0xcc,pm_sub(pm_i32(m,o+0xec),old_z));
    base=pm_trunc(pm_i32(m,o+0xf4),8);pm_write32(m,o+0xf4,base);
    term=pm_trunc(pm_mul(pm_i32(m,o+0x9c),pm_i32(m,o+0x66a)),8);
    term=pm_trunc(pm_mul(pm_mul(term,(int32_t)pm_load(m,o+0x67a,2)),base),pm_load(m,o+0x67c,1));
    v=pm_sub(pm_i32(m,o+0xb8),pm_mul(dt,term));term=coefficient(m,pm_neg(pm_i32(m,o+0xe4)),o+0x682);
    pm_write32(m,o+0xb8,pm_add(v,term));return m->error;
}
