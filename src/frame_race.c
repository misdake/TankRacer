#include "frame_race.h"
#include "physics_memory.h"

enum {
    RACE_COUNTDOWN=0x53e4f0, RELEASE_COUNTDOWN=0x53e4f4,
    RACER_IDS_BEGIN=0x53e8e0, RACER_IDS_END=0x53e904,
    OBJECT_TABLE=0x55c694, COUNTDOWN_DISPLAY=0x5904e0, RAW_ELAPSED=0x590550
};

static uint32_t racer_object(physics_memory *m, uint32_t id)
{
    uint32_t descriptor=pm_load(m,pm_load(m,OBJECT_TABLE,4)+id*8u,4);
    return descriptor ? pm_load(m,descriptor+4u,4) : 0;
}
int frame_race_initialize(frame_data_write write,void *ctx)
{
    physics_memory m={0,write,ctx,0};if (!write) return -1;
    pm_store(&m,0x53e4f0u,3000,4);pm_store(&m,0x53e4f4u,0,4);pm_store(&m,0x52fa28u,UINT32_MAX,4);pm_store(&m,0x52fa2cu,UINT32_MAX,4);pm_store(&m,0x5904e0u,1,4);return m.error;
}
int frame_race_reset_laps(frame_data_read read,frame_data_write write,void *ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t i,id;if (!read || !write) return -1;
    for (i=0;i<9u;i++) { id=pm_load(&m,0x53e8e0u+i*4u,4);if (id) pm_store(&m,racer_object(&m,id)+0xddau,0,2); }return m.error;
}

int frame_race_tick(frame_data_read read, frame_data_write write, void *ctx)
{
    physics_memory m={read,write,ctx,0};
    int32_t countdown,remaining;
    uint32_t slot;
    if (!read || !write) return -1;
    countdown=pm_i32(&m,RACE_COUNTDOWN);
    if (!countdown) {
        remaining=pm_i32(&m,RELEASE_COUNTDOWN);
        if (remaining) {
            remaining=pm_sub(remaining,pm_i32(&m,RAW_ELAPSED));
            pm_write32(&m,RELEASE_COUNTDOWN,remaining);
            if (remaining<0) pm_write32(&m,RELEASE_COUNTDOWN,0);
        }
        for (slot=RACER_IDS_BEGIN;slot<RACER_IDS_END;slot+=4u) {
            uint32_t id=pm_load(&m,slot,4);
            if (id) {
                uint32_t object=racer_object(&m,id);
                pm_write32(&m,object+0xcbau,pm_add(pm_i32(&m,object+0xcbau),pm_i32(&m,RAW_ELAPSED)));
            }
        }
        return m.error;
    }
    countdown=pm_sub(countdown,pm_i32(&m,RAW_ELAPSED));
    pm_write32(&m,RACE_COUNTDOWN,countdown);
    if (countdown<1) {
        pm_write32(&m,RACE_COUNTDOWN,0);
        pm_write32(&m,RELEASE_COUNTDOWN,1000);
        pm_write32(&m,COUNTDOWN_DISPLAY,4);
        for (slot=RACER_IDS_BEGIN;slot<RACER_IDS_END;slot+=4u) {
            uint32_t id=pm_load(&m,slot,4);
            if (id) pm_store(&m,racer_object(&m,id)+0xdeau,1,1);
        }
    } else {
        pm_write32(&m,COUNTDOWN_DISPLAY,countdown>1500 ? 1 : 2+(countdown<1001));
    }
    return m.error;
}
