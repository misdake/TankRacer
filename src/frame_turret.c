#include "frame_turret.h"
#include "frame_controls.h"
#include "physics_memory.h"

static int key(physics_memory *m,uint32_t code)
{
    uint32_t value=0;
    int status;
    if (m->error) return 0;
    status=frame_control_key(m->read,m->ctx,code,&value);
    if (status) m->error=status;
    return value==1;
}
static int binding(physics_memory *m,int32_t player,uint32_t action)
{
    uint32_t code;
    if (player || m->error) return 0;
    if (key(m,pm_load(m,0x592000u+action*4u,4))) return 1;
    if (!pm_load(m,0x53ccdcu,4)) return 0;
    code=pm_load(m,0x592900u+(uint32_t)player*100u+action*4u,4);
    return code && key(m,code+(uint32_t)player*12u);
}
static void integrate(physics_memory *m,uint32_t object)
{
    int32_t v=pm_damp(pm_i32(m,object+0xe0fu),pm_load(m,0x531ba0u,4),pm_i32(m,0x5904fcu));
    pm_write32(m,object+0xe0fu,v); /* complete 441300, one final velocity write */
    pm_store(m,object+0x762u,(uint32_t)pm_add(pm_i16(m,object+0x762u),
             pm_sar(pm_mul(pm_i32(m,0x5904fcu),pm_i32(m,object+0xe0fu)),6)),2);
}
int frame_turret_input(frame_data_read read,frame_data_write write,void *ctx,uint32_t object)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    int32_t player,dt,angle;
    uint32_t cooldown,latch;
    if (!read || !write) return -1;
    dt=pm_i32(m,0x5904fcu);
    pm_store(m,0x544facu,0,1);pm_store(m,0x544fb0u,0,1);
    player=pm_i8(m,object+0xdf0u);
    cooldown=0x544fa0u+(uint32_t)player*4u;latch=0x544fa8u+(uint32_t)player;
    pm_write32(m,cooldown,pm_sub(pm_i32(m,cooldown),dt));
    if (binding(m,player,10) && pm_i32(m,cooldown)<0) {
        pm_write32(m,object+0xe0fu,pm_add(pm_i32(m,object+0xe0fu),pm_mul(pm_i32(m,0x530c6cu),pm_i32(m,0x5904fcu))));
        pm_store(m,0x544facu,1,1);pm_store(m,latch,0,1);
    }
    if (binding(m,player,11) && pm_i32(m,cooldown)<0) {
        pm_write32(m,object+0xe0fu,pm_sub(pm_i32(m,object+0xe0fu),pm_mul(pm_i32(m,0x530c6cu),pm_i32(m,0x5904fcu))));
        pm_store(m,0x544fb0u,1,1);pm_store(m,latch,0,1);
    }
    if (pm_load(m,0x544facu,1) && pm_load(m,0x544fb0u,1) && !pm_load(m,latch,1) &&
        pm_i16(m,object+0x762u)!=0 && pm_i16(m,object+0xe0bu)<1) {
        pm_store(m,latch,1,1);pm_write32(m,cooldown,600);
    }
    angle=pm_i16(m,object+0x762u);
    if (!pm_load(m,latch,1)) integrate(m,object);
    else if (angle) {
        int32_t force=pm_mul(pm_mul(pm_i32(m,0x530c6cu),2),pm_i32(m,0x5904fcu));
        pm_write32(m,object+0xe0fu,angle<0 ? pm_add(pm_i32(m,object+0xe0fu),force) : pm_sub(pm_i32(m,object+0xe0fu),force));
        integrate(m,object);
        if ((angle<0 && pm_i16(m,object+0x762u)>0) || (angle>0 && pm_i16(m,object+0x762u)<0)) {
            pm_store(m,object+0x762u,0,2);pm_write32(m,object+0xe0fu,0);pm_store(m,latch,0,1);
        }
    }
    pm_store(m,object+0xd0au,pm_i32(m,object+0xe0fu)!=0,1);
    return m->error;
}
