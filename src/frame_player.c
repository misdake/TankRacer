#include "frame_player.h"
#include "frame_player_lifecycle.h"
#include "frame_player_input.h"
#include "frame_player_collision.h"
#include "frame_player_state.h"
#include "frame_turret.h"
#include "frame_environment.h"
#include "frame_camera.h"
#include "physics_memory.h"
static uint32_t object_of(physics_memory *m,uint32_t entity)
{
    uint32_t record=pm_load(m,pm_load(m,0x55c694u,4)+entity*8u,4);return record ? pm_load(m,record+4u,4) : 0;
}
#define TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
int frame_player_begin_death(frame_data_read read,frame_data_write write,void *ctx,uint32_t o)
{
    physics_memory m={read,write,ctx,0};uint32_t i,j,record,at,id;int status;
    if (!read || !write) return -1;
    pm_store(&m,o+0x10deu,1,1);pm_store(&m,o+0x10dfu,pm_load(&m,0x59054cu,4),4);pm_store(&m,o+0x10e3u,500,2);
    if (pm_i16(&m,o+0xdd0u)) { id=(uint32_t)pm_i16(&m,o+0xdceu);pm_store(&m,o+0xdd0u,0,2);TRY(frame_temp_effects_expire(read,write,ctx,id)); }
    for (i=0;i<2u;i++) {
        record=0x58faa0u+i*44u;
        if (pm_load(&m,record+4u,4)!=o || !pm_load(&m,record,1)) continue;
        for (j=0;j<10u;j++) { at=0x545158u+i*840u+j*84u;id=pm_load(&m,at,4);if (id!=0xfeb4024du) { TRY(frame_light_release(read,write,ctx,id));pm_store(&m,at,0xfeb4024du,4); } }
        for (j=0;j<10u;j++) { at=0x545820u+i*680u+j*68u;id=pm_load(&m,at,4);if (id!=0xfeb4024du) { TRY(frame_light_release(read,write,ctx,id));pm_store(&m,at,0xfeb4024du,4); } }
        pm_store(&m,record,0,1);pm_store(&m,record+4u,0,4);
    }
    for (i=0;i<2u;i++) if (pm_load(&m,o+0xe18u+i,1)) { id=pm_load(&m,o+0xe1au+i*4u,4);TRY(frame_light_release(read,write,ctx,id));pm_store(&m,o+0xe18u+i,0,1); }
    return m.error;
}
int frame_player_death_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                               const frame_entity_factory_services *e,const frame_explosion_services *effects,const uint32_t words[25])
{
    physics_memory m={read,write,ctx,0};uint32_t o,i,contact[25],unused,seed;int status;
    static const int32_t offsets[4][2]={{2000,2000},{-2000,-2000},{-2000,2000},{2000,-2000}};
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,entity);if (!o) return m.error;
    if (!pm_load(&m,o+0x10f0u,1)) {
        if (pm_load(&m,0x59054cu,4)-pm_load(&m,o+0x10dfu,4)<=500u) return m.error;
        if (!words) return m.error ? m.error : -4;
        for (i=0;i<25u;i++) contact[i]=words[i];
        contact[15]=contact[16]=contact[17]=0;contact[18]=entity;
        TRY(frame_explosion_spawn_values(read,write,ctx,o,contact,1,e,effects,&unused));
        for (i=0;i<4u;i++) {
            contact[15]=(uint32_t)offsets[i][0];contact[17]=(uint32_t)offsets[i][1];seed=pm_load(&m,0x55b6acu,4)*1664525u+1013904223u;pm_store(&m,0x55b6acu,seed,4);contact[16]=((seed>>4)&65535u)%500u-250u;
            TRY(frame_explosion_spawn_values(read,write,ctx,o,contact,0,e,effects,&unused));
        }
        TRY(frame_list_remove_value(read,write,ctx,entity,pm_load(&m,0x54b488u,4),e->release,e->system_ctx,e->work_limit));
        pm_store(&m,0x53e8e0u+pm_load(&m,o+0xe07u,1)*4u,0,4);pm_store(&m,o+0x10f0u,1,1);
    } else if (entity==pm_load(&m,0x53e8b8u,4) && !pm_load(&m,o+0x10f1u,1) && pm_load(&m,0x59054cu,4)-pm_load(&m,o+0x10dfu,4)>11500u) {
        pm_store(&m,o+0x10f1u,1,1);pm_store(&m,0x5466ccu,1,4);
    }
    return m.error;
}
int frame_player_environment(frame_data_read read,frame_data_write write,void *ctx,uint32_t o,uint32_t entity,const frame_explosion_services *effects,uint32_t limit)
{
    physics_memory m={read,write,ctx,0};uint32_t frame,flags=0,count,style,previous,i;int status;
    if (!read || !write || !limit) return -1;
    frame=pm_load(&m,0x590564u,4);if (((entity^frame)&15u) && frame!=1u) return m.error;
    if (pm_load(&m,0x52ee9cu,4)==2u) { if (pm_load(&m,0x52ed90u,4)==10u && pm_load(&m,0x53d18cu,4)!=2u && !pm_load(&m,o+0xde3u,1)) flags=17; }
    else { TRY(frame_environment_at(read,ctx,frame==1u ? 0u : pm_load(&m,o+0xd18u,4),&flags)); }
    previous=pm_load(&m,o+0x107au,4);if (flags==UINT32_MAX || (flags==previous && pm_load(&m,0x590564u,4)!=1u)) return m.error;
    count=50;style=0;
    if (flags&1u) { style=1;if (flags&16u) count=100;else if (flags&32u) count=200; }
    else if (flags&2u) { style=2;count=flags&16u ? 200u : flags&32u ? 300u : 75u; }
    TRY(frame_environment_config(read,write,ctx,style,count,!previous));
    if (flags&4u) pm_store(&m,0x545ea4u,1,4);
    else if (pm_load(&m,0x545ea4u,4)) {
        pm_store(&m,0x545ea4u,0,4);
        if (pm_load(&m,0x545eb0u,4)) {
            pm_store(&m,0x545eb0u,0,4);
            for (i=0;i<pm_load(&m,0x545eb4u,4);i++) { if (i>=limit) return -3;TRY(frame_line_release(read,write,ctx,(uint32_t)pm_i8(&m,0x545e28u+i))); }
        }
        TRY(frame_explosion_fog(read,write,ctx,effects));pm_store(&m,0x545ea8u,0,4);pm_store(&m,0x545eacu,0,4);pm_store(&m,0x531918u,2000,4);
    }
    pm_store(&m,o+0x107au,flags,4);return m.error;
}
int frame_player_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,
                         const frame_entity_factory_services *e,const physics_trig *trig,const frame_player_lap_services *lap,
                         const frame_explosion_services *effects,const uint32_t death_words[25])
{
    physics_memory m={read,write,ctx,0};frame_player_input_rates rates;frame_player_input_result input;uint32_t o,parent,unused,other;int status,takeover=0;
    if (!read || !write || !e || !e->work_limit || !lap) return -1;
    o=object_of(&m,entity);rates.throttle_return=(uint16_t)pm_load(&m,0x530c60u,2);rates.brake_return=(uint16_t)pm_load(&m,0x530c64u,2);
    if (pm_load(&m,0x52ee9cu,4)==4 && pm_load(&m,o+0x10deu,1)) TRY(frame_player_death_tick(read,write,ctx,entity,e,effects,death_words));
    if (pm_i16(&m,o+0xddau)==3 && pm_load(&m,0x52ee9cu,4)!=4) {
        if (pm_load(&m,0x52ee9cu,4)!=3) pm_store(&m,0x53e3fcu,0,4);
        if (pm_load(&m,o+0xdeau,1)) {
            if (pm_load(&m,0x52ee9cu,4)==0 || pm_load(&m,0x52ee9cu,4)==1) {
                TRY(frame_player_takeover(read,write,ctx,e));TRY(frame_camera_create_finish(read,write,ctx,pm_load(&m,0x53b69cu,4),(uint32_t)pm_i8(&m,o+0xdf0u),e));takeover=1;
            } else TRY(frame_camera_create_finish(read,write,ctx,entity,(uint32_t)pm_i8(&m,o+0xdf0u),e));
            pm_store(&m,o+0xdeau,0,1);
        }
        pm_store(&m,o+0x98u,pm_abs(pm_i32(&m,o+0xecu))<1000 ? 0u : 65535u,4);pm_store(&m,o+0x94u,0,4);pm_store(&m,o+0x9cu,0,4);
        TRY(frame_player_restore_overrides(read,write,ctx,o));
        if (!takeover) { parent=pm_load(&m,0x545fdcu,4);TRY(frame_class_dispatch(read,ctx,parent,entity,e->method,e->method_ctx,&unused)); }
        TRY(frame_player_horn(read,write,ctx,entity));frame_entity_sound(e,0x40f8f0u,o,entity,0);return 0;
    }
    if (pm_load(&m,0x52ee9cu,4)==2 && pm_i32(&m,o+0x10c2u)<1 && pm_load(&m,0x53e3fcu,4)) {
        pm_store(&m,0x53e3fcu,0,4);TRY(frame_camera_create_finish(read,write,ctx,entity,(uint32_t)pm_i8(&m,o+0xdf0u),e));
        other=pm_load(&m,0x53e8bcu-(uint32_t)pm_i8(&m,o+0xdf0u)*4u,4);TRY(frame_camera_create_finish(read,write,ctx,other,1u-(uint32_t)pm_i8(&m,o+0xdf0u),e));
    }
    if (pm_load(&m,0x52ee9cu,4)==4 && pm_i32(&m,o+0x10c2u)<1 && pm_load(&m,0x53e3fcu,4)) {
        TRY(frame_network_race_eliminated(read,write,ctx,lap->network,lap->network_ctx,lap->workspace,lap->capacity,lap->work_limit));
        pm_store(&m,0x53e3fcu,0,4);TRY(frame_player_begin_death(read,write,ctx,o));
    }
    if (!pm_load(&m,0x53e3fcu,4)) pm_store(&m,o+0xdeau,0,1);
    TRY(frame_player_input_step(read,write,ctx,o,&rates,&input));
    if (input.driving) {
        TRY(frame_player_collision_state(read,write,ctx,o));TRY(frame_player_lap_update(read,write,ctx,o,entity,lap));
        TRY(frame_lap_flush_deferred(read,write,ctx,e->method,e->method_ctx,e->release,e->system_ctx,e->work_limit));
        if (pm_load(&m,0x53e918u,4)) TRY(frame_camera_player_view(read,write,ctx,o,trig));
        TRY(frame_turret_input(read,write,ctx,o));TRY(frame_player_fire(read,write,ctx,o,entity,e->method,e->method_ctx));
        TRY(frame_player_aux_brake(read,write,ctx,o,input.auxiliary_down));
    }
    TRY(frame_player_route_update(read,write,ctx,o,e->work_limit));TRY(frame_player_progress_state(read,write,ctx,o,entity));
    TRY(frame_player_environment(read,write,ctx,o,entity,effects,e->work_limit));TRY(frame_player_palette_update(read,write,ctx,o));
    if (pm_load(&m,0x53e918u,4)) TRY(frame_camera_player_view(read,write,ctx,o,trig));
    parent=pm_load(&m,0x545fdcu,4);TRY(frame_class_dispatch(read,ctx,parent,entity,e->method,e->method_ctx,&unused));
    TRY(frame_player_horn(read,write,ctx,entity));frame_entity_sound(e,0x40f8f0u,o,entity,0);return m.error;
}
#undef TRY
