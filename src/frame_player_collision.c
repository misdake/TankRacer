#include "frame_player_collision.h"
#include "physics_memory.h"

int32_t frame_collision_change_magnitude(const int32_t delta[3])
{
    uint32_t x=(uint32_t)pm_abs(delta[0]),y=(uint32_t)pm_abs(delta[1]),z=(uint32_t)pm_abs(delta[2]);
    if (y<x && z<x) return pm_signed(x+(y>>2)+(z>>2));
    if (y>=x && z<y) return pm_signed(y+(x>>2)+(z>>2));
    return pm_signed(z+(x>>2)+(y>>2));
}

static int32_t vector_change(physics_memory *m,uint32_t address)
{
    int32_t delta[3];
    unsigned i;
    for (i=0;i<3;i++) delta[i]=pm_i32(m,address+i*4u);
    return frame_collision_change_magnitude(delta);
}

static void add_change(physics_memory *m,uint32_t object,int32_t magnitude)
{
    if (magnitude>30000) {
        int32_t increment=pm_sar(pm_sub(magnitude,30000),11);
        pm_write32(m,object+0x107eu,pm_add(pm_i32(m,object+0x107eu),increment));
    }
}

int frame_player_collision_state(frame_data_read read,frame_data_write write,
                                  void *ctx,uint32_t object)
{
    physics_memory m={read,write,ctx,0};
    int32_t value,dt;
    if (!read || !write) return -1;
    if (!pm_load(&m,0x53e3fcu,4)) return m.error;
    value=pm_i32(&m,object+0x107eu);
    if (value) {
        value=pm_sub(value,pm_i32(&m,0x5904fcu));
        pm_write32(&m,object+0x107eu,value);
        if (value<0) pm_write32(&m,object+0x107eu,0);
    }
    /* Both vector helpers are pure; the original re-reads a vector after the
     * threshold succeeds. No DATA changes occur between those two calls. */
    add_change(&m,object,vector_change(&m,object+0xfcu));
    add_change(&m,object,vector_change(&m,object+0x108u));
    /* +114 already describes an obstacle-response velocity change; taking abs
     * or substituting vehicle speed would change this third contribution. */
    add_change(&m,object,pm_i32(&m,object+0x114u));
    if (pm_i32(&m,object+0x107eu)>1250) pm_write32(&m,object+0x107eu,1250);
    value=pm_i32(&m,object+0x118u);
    if (value) {
        dt=pm_i32(&m,0x5904fcu);
        pm_write32(&m,object+0x118u,value<dt ? 0 : pm_sub(value,dt));
    }
    return m.error;
}
