#include "frame_session.h"
#include "frame_controls.h"
#include "frame_race.h"
#include "frame_shape.h"
#include "frame_scene_effects.h"
#include "frame_player_lifecycle.h"
#include "frame_overlay.h"
#include "physics_memory.h"
#define TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
static int key(physics_memory *m,uint32_t offset,uint32_t *pressed)
{
    uint32_t code=pm_load(m,0x592000u+offset,4),value;int status;
    if (m->error) return m->error;
    status=frame_control_key(m->read,m->ctx,code,&value);if (status) return status;
    *pressed=value==1u;if (*pressed) return 0;
    if (pm_load(m,0x53ccdcu,4)) { code=pm_load(m,0x592900u+offset,4);if (code) { status=frame_control_key(m->read,m->ctx,code,&value);if (status) return status;*pressed=value==1u; } }
    return m->error;
}
static int audio_restore(physics_memory *m,const frame_session_devices *d)
{ if (!d || !d->audio_settings) return -5;pm_store(m,d->audio_settings+4u,pm_load(m,0x53e50cu,2),2);return m->error; }
/*411210/411280: slot selects configured joystick, not key-map offset.
 * Menu helper calls receive pause_state-1 as their slot argument. */
static int direction(physics_memory *m,uint32_t slot,uint32_t action,uint32_t *pressed)
{
    int32_t value;int status;*pressed=0;
    if (!pm_load(m,0x53cca8u+slot*4u,4)) return m->error;
    status=frame_control_axis(m->read,m->ctx,(action==0x11u || action==0x12u) ? 0u : 3u,&value);if (status) return status;
    *pressed=(action==0x11u || action==0x13u) ? value<-16384 : value>16384;return m->error;
}
static int menu_input(physics_memory *m,uint32_t slot,uint32_t offset,uint32_t axis_action,uint32_t *pressed)
{
    int status;*pressed=0;
    if (!slot) { status=key(m,offset,pressed);if (status || *pressed) return status; }
    if (!axis_action) return m->error;
    return direction(m,slot,axis_action,pressed);
}
static int accepted(physics_memory *m,uint32_t slot,uint32_t *pressed)
{
    int status=menu_input(m,slot,0x54u,0,pressed);if (status || *pressed) return status;
    return menu_input(m,slot,0x5cu,0,pressed);
}
int frame_session_pause_key(frame_data_read read,frame_data_write write,void *ctx,const frame_session_devices *d)
{
    physics_memory m={read,write,ctx,0};uint32_t pressed,pause,mode;int status;
    if (!read || !write) return -1;
    TRY(key(&m,0x40u,&pressed));if (!pressed) { pm_store(&m,0x53e968u,0,4);return m.error; }
    if (pm_load(&m,0x53e968u,4)) return m.error;
    pm_store(&m,0x53e968u,1,4);
    if (pm_load(&m,0x53e928u,4)) { pm_store(&m,0x53e928u,0,4);pm_store(&m,0x53e924u,0,4);if (!pm_load(&m,0x53e908u,4)) pm_store(&m,0x53e984u,1,4);else { pm_store(&m,0x53e96cu,0,4);pm_store(&m,0x53e988u,1,4); }return m.error; }
    pause=pm_load(&m,0x53e96cu,4);
    if (pause) { pm_store(&m,0x53e96cu,0,4);TRY(audio_restore(&m,d)); }
    else if (!pm_load(&m,0x53e944u,4) && !pm_load(&m,0x53e940u,4) && !pm_load(&m,0x54b4a0u,4) && (!pm_load(&m,0x54b480u,4) || !pm_load(&m,0x54b4a4u,4))) {
        /* Same key is reread by the original before choosing keyboard/player2. */
        TRY(key(&m,0x40u,&pressed));pm_store(&m,0x53e96cu,pressed ? 1u : 2u,4);
        if (!d || !d->audio_settings) return -5;
        pm_store(&m,0x53e50cu,pm_load(&m,d->audio_settings+4u,2),4);mode=pm_load(&m,0x52ee9cu,4);if (mode!=4u && mode!=5u) pm_store(&m,d->audio_settings+4u,0,2);
    } else pm_store(&m,0x53e96cu,0,4);
    pm_store(&m,0x53e970u,0,4);pm_store(&m,0x53e974u,0,4);pm_store(&m,0x53e978u,0,4);return m.error;
}
int frame_session_menu_limited(frame_data_read read,frame_data_write write,void *ctx,const frame_session_devices *d,uint32_t limit)
{
    physics_memory m={read,write,ctx,0};uint32_t pause,slot,mode,setting,selection,max_items,pressed,changed=0,value,volume;int status;
    if (!read || !write) return -1;
    pause=pm_load(&m,0x53e96cu,4);slot=pause-1u;mode=pm_load(&m,0x52ee9cu,4);max_items=(mode==0u || mode==4u || mode==5u) ? 5u : 7u;setting=pm_load(&m,0x53e978u,4);
    if (limit && limit<max_items) max_items=limit;
    if (setting) {
        if (setting==3u) {
            if (pm_load(&m,0x53e500u,4)>pm_load(&m,0x53e504u,4)) { TRY(menu_input(&m,slot,0x4cu,0x13u,&pressed));if (pressed) { if (!pm_load(&m,0x53e554u,4)) { pm_store(&m,0x53e554u,1,4);pm_store(&m,0x53e500u,pm_load(&m,0x53e500u,4)-1u,4);changed=1; } }else pm_store(&m,0x53e554u,0,4); }
            if (pm_load(&m,0x53e500u,4)<pm_load(&m,0x53e508u,4)) { TRY(menu_input(&m,slot,0x50u,0x14u,&pressed));if (pressed) { if (!pm_load(&m,0x53e558u,4)) { pm_store(&m,0x53e558u,1,4);pm_store(&m,0x53e500u,pm_load(&m,0x53e500u,4)+1u,4);changed=1; } }else pm_store(&m,0x53e558u,0,4); }
            if (changed) { value=pm_load(&m,0x53e500u,4);pm_store(&m,0x53e980u,value,4);if (!d || !d->video) return -5;TRY(d->video(d->video_ctx,value));pm_store(&m,0x54b434u,pm_load(&m,0x53e980u,4),4); }
        } else {
            if (pm_i32(&m,0x53e4fcu)>0) { TRY(menu_input(&m,slot,0x44u,0x11u,&pressed));if (pressed) { value=pm_load(&m,0x53e4fcu,4)-pm_load(&m,0x5904fcu,4);pm_store(&m,0x53e4fcu,value,4);changed=1;if (pm_signed(value)<0) pm_store(&m,0x53e4fcu,0,4); } }
            if (pm_i32(&m,0x53e4fcu)<2048) { TRY(menu_input(&m,slot,0x48u,0x12u,&pressed));if (pressed) { value=pm_load(&m,0x53e4fcu,4)+pm_load(&m,0x5904fcu,4);pm_store(&m,0x53e4fcu,value,4);changed=1;if (pm_signed(value)>2048) pm_store(&m,0x53e4fcu,2048,4); } }
            if (changed && (setting==1u || setting==2u)) { if (!d || !d->audio_settings) return -5;volume=(uint32_t)(pm_mul(pm_sar(pm_i32(&m,0x53e4fcu),4),65535)/128);if (setting==1u) { pm_store(&m,d->audio_settings+4u,volume,2);pm_store(&m,0x53e50cu,volume&65535u,4); }else pm_store(&m,d->audio_settings+6u,volume,2); }
        }
        TRY(accepted(&m,slot,&pressed));if (!pressed) { pm_store(&m,0x53e55cu,0,4);return m.error; }if (pm_load(&m,0x53e55cu,4)) return m.error;
        pm_store(&m,0x53e55cu,1,4);if (setting==1u) { if (!d || !d->audio_settings) return -5;if (mode!=4u && mode!=5u) pm_store(&m,d->audio_settings+4u,0,2); }pm_store(&m,0x53e978u,0,4);return m.error;
    }
    if (pm_load(&m,0x53e974u,4)) {
        TRY(menu_input(&m,slot,0x4cu,0x13u,&pressed));if (pressed) pm_store(&m,0x53e4f8u,0,4);
        TRY(menu_input(&m,slot,0x50u,0x14u,&pressed));if (pressed) pm_store(&m,0x53e4f8u,1,4);
        TRY(accepted(&m,slot,&pressed));if (!pressed) { pm_store(&m,0x53e55cu,0,4);return m.error; }if (pm_load(&m,0x53e55cu,4)) return m.error;
        pm_store(&m,0x53e55cu,1,4);pm_store(&m,0x53e974u,0,4);if (!pm_load(&m,0x53e4f8u,4)) return m.error;
        selection=pm_load(&m,0x53e970u,4);
        if (selection==1u && (mode==0u || mode==4u || mode==5u)) { pm_store(&m,0x53e96cu,0,4);pm_store(&m,0x53e984u,1,4);if (pm_load(&m,0x58e7acu,4)) return -5; }
        else if (selection==1u || selection==3u) { if (pm_load(&m,0x58e6f8u,4)) { pm_store(&m,0x53e428u,1,4); }else { if (selection==1u) pm_store(&m,0x53e95cu,1,4);pm_store(&m,0x53e96cu,0,4);pm_store(&m,0x53e984u,1,4); } }
        else if (selection==2u) { if (pm_load(&m,0x58e6f8u,4)) pm_store(&m,0x53e428u,1,4);else { pm_store(&m,0x53e96cu,0,4);pm_store(&m,mode==3u ? 0x53e984u : 0x53e988u,1,4); } }
        else return m.error;
        TRY(audio_restore(&m,d));return m.error;
    }
    selection=pm_load(&m,0x53e970u,4);
    if (selection) { TRY(menu_input(&m,slot,0x4cu,0x13u,&pressed));if (pressed) { if (!pm_load(&m,0x53e554u,4)) { pm_store(&m,0x53e554u,1,4);pm_store(&m,0x53e970u,selection-1u,4); } }else pm_store(&m,0x53e554u,0,4); }
    if (pm_load(&m,0x53e970u,4)<max_items-1u) { TRY(menu_input(&m,slot,0x50u,0x14u,&pressed));if (pressed) { if (!pm_load(&m,0x53e558u,4)) { pm_store(&m,0x53e558u,1,4);pm_store(&m,0x53e970u,pm_load(&m,0x53e970u,4)+1u,4); } }else pm_store(&m,0x53e558u,0,4); }
    TRY(accepted(&m,slot,&pressed));if (!pressed) { pm_store(&m,0x53e55cu,0,4);return m.error; }if (pm_load(&m,0x53e55cu,4)) return m.error;
    pm_store(&m,0x53e55cu,1,4);selection=pm_load(&m,0x53e970u,4);
    if (!selection) { pm_store(&m,0x53e96cu,0,4);TRY(audio_restore(&m,d)); }
    else if ((mode==0u || mode==4u || mode==5u) ? selection==1u : selection<=((mode==3u) ? 2u : 3u)) { pm_store(&m,0x53e974u,1,4);pm_store(&m,0x53e4f8u,0,4); }
    else if (mode==3u && selection==3u) pm_store(&m,0x530c50u,!pm_load(&m,0x530c50u,4),4);
    else if (selection<max_items) pm_store(&m,0x53e978u,selection-((mode==0u || mode==4u || mode==5u) ? 1u : 3u),4);
    setting=pm_load(&m,0x53e978u,4);if (setting==1u) { TRY(audio_restore(&m,d)); }
    if (!setting) return m.error;
    if (setting==1u || setting==2u) { if (!d || !d->audio_settings) return -5;volume=pm_load(&m,d->audio_settings+(setting==1u ? 4u : 6u),2);pm_store(&m,0x53e4fcu,(volume*2048u)/65535u,4); }
    else if (setting==3u) { pm_store(&m,0x53e500u,pm_load(&m,0x53e980u,4),4);value=pm_load(&m,0x56021cu,4);pm_store(&m,0x53e508u,value-(pm_load(&m,0x53e97cu,4)!=0u),4);if (pm_load(&m,0x53e508u,4)>10u) pm_store(&m,0x53e508u,10,4);pm_store(&m,0x53e504u,(value!=2u)+2u,4); }
    return m.error;
}
int frame_session_menu(frame_data_read r,frame_data_write w,void *ctx,const frame_session_devices *d)
{ return frame_session_menu_limited(r,w,ctx,d,0); }
int frame_session_action(frame_data_read read,frame_data_write write,void *ctx,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t action=0;if (!read || !write || !out) return -1;
    pm_store(&m,0x53e99cu,0,4);
    if (pm_load(&m,0x53e984u,4)) { pm_store(&m,0x53e984u,0,4);if (pm_load(&m,0x58e7acu,4)) return -5;if (pm_load(&m,0x53e908u,4)) pm_store(&m,0x53e428u,1,4);else action=FRAME_SESSION_EXIT; }
    else if (pm_load(&m,0x53e988u,4)) { pm_store(&m,0x53e988u,0,4);action=FRAME_SESSION_RESTART; }
    if (m.error) return m.error;
    *out=action;return 0;
}
int frame_session_fades(frame_data_read read,frame_data_write write,void *ctx)
{
    physics_memory m={read,write,ctx,0};frame_overlay_params p={0,0,0,0,0};uint32_t value,i;int status;
    if (!read || !write) return -1;
    if (!pm_load(&m,0x53e96cu,4) && pm_load(&m,0x53e940u,4)) {
        value=pm_load(&m,0x53e940u,4)-pm_load(&m,0x5904fcu,4);pm_store(&m,0x53e940u,value,4);
        if (pm_signed(value)<=0) { pm_store(&m,0x53e940u,0,4);p.blend_mode=1;for (i=0;i<2u;i++) { TRY(frame_overlay_set(read,write,ctx,0,i,&p)); } }
        else { p.alpha=(uint32_t)pm_sar(pm_signed(value),2);for (i=0;i<2u;i++) { TRY(frame_overlay_set(read,write,ctx,1,i,&p)); } }
    }
    if (pm_load(&m,0x53e944u,4)) {
        value=pm_load(&m,0x53e944u,4)-pm_load(&m,0x5904fcu,4);pm_store(&m,0x53e944u,value,4);
        if (pm_signed(value)<=0) { pm_store(&m,0x53e944u,0,4);p.alpha=255; }
        else p.alpha=(uint32_t)pm_sar(pm_signed(2047u-value),3);
        p.blend_mode=0;for (i=0;i<2u;i++) { TRY(frame_overlay_set(read,write,ctx,1,i,&p)); }
    }
    return m.error;
}
int frame_session_clock_reset(frame_data_write write,void *ctx)
{
    physics_memory m={0,write,ctx,0};if (!write) return -1;
    pm_store(&m,0x59055cu,1,4);pm_store(&m,0x59054cu,10,4);pm_store(&m,0x590560u,1,4);pm_store(&m,0x590550u,10,4);pm_store(&m,0x590554u,100,4);
    pm_store(&m,0x590564u,0,4);pm_store(&m,0x590558u,0,4);pm_store(&m,0x590568u,0,4);
    pm_store(&m,0x53d16cu,10,4);pm_store(&m,0x53d168u,10,4);pm_store(&m,0x53d164u,10,4);pm_store(&m,0x53d160u,10,4);pm_store(&m,0x53d158u,0,4);pm_store(&m,0x53d174u,0,4);return m.error;
}
int frame_session_video_initialize(frame_data_read read,frame_data_write write,void *ctx,const frame_session_devices *d)
{
    physics_memory m={read,write,ctx,0};uint32_t count,range,value;int status;if (!read || !write) return -1;
    pm_store(&m,0x53e980u,0,4);count=pm_load(&m,0x56021cu,4);
    if (count>1u) { pm_store(&m,0x53e97cu,count>5u,4);if (count==2u) pm_store(&m,0x53e980u,2,4);
        else { range=(count>10u ? 10u : count)-(count<=5u ? 2u : 3u);if (range) { value=pm_load(&m,0x538820u,4)*214013u+2531011u;pm_store(&m,0x538820u,value,4);pm_store(&m,0x53e980u,((value>>16)&32767u)%range+3u,4); } }
    }
    value=pm_load(&m,0x53e980u,4);if (value>1u) { if (!d || !d->video) return -5;TRY(d->video(d->video_ctx,value)); }
    pm_store(&m,0x54b434u,pm_load(&m,0x53e980u,4),4);return m.error;
}
int frame_session_begin(frame_data_read read,frame_data_write write,void *ctx)
{ return frame_session_begin_with_devices(read,write,ctx,0); }
int frame_session_begin_with_devices(frame_data_read read,frame_data_write write,void *ctx,const frame_session_devices *d)
{
    physics_memory m={read,write,ctx,0};int status;
    if (!read || !write) return -1;
    pm_store(&m,0x5904ecu,UINT32_MAX,4);pm_store(&m,0x5904e8u,0,4);pm_store(&m,0x5904e4u,0,4);pm_store(&m,0x53e990u,0,4);
    pm_store(&m,0x53e944u,0,4);pm_store(&m,0x53e940u,1023,4);
    { frame_overlay_params p={0,255,0,0,0};TRY(frame_overlay_set(read,write,ctx,1,0,&p));TRY(frame_overlay_set(read,write,ctx,1,1,&p)); }
    TRY(frame_session_video_initialize(read,write,ctx,d));
    TRY(frame_session_clock_reset(write,ctx));TRY(frame_race_initialize(write,ctx));TRY(frame_race_reset_laps(read,write,ctx));pm_store(&m,0x53e3fcu,1,4);pm_store(&m,0x52eed8u,1,4);return m.error;
}
int frame_session_loading_reset(frame_data_read read,frame_data_write write,void *ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t mode;if (!read || !write) return -1;
    mode=pm_load(&m,0x52ee9cu,4);pm_store(&m,0x53e984u,0,4);pm_store(&m,0x53e8b4u,0,1);pm_store(&m,0x53e8b0u,0,1);pm_store(&m,0x53e99cu,0,4);pm_store(&m,0x53e904u,0,4);pm_store(&m,0x53e948u,0,4);
    if (mode==4u || mode==5u) pm_store(&m,0x53e414u,0,4);
    return m.error;
}
int frame_session_scoreboard(frame_data_read read,frame_data_write write,void *ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t down;int status;if (!read || !write) return -1;
    TRY(key(&m,0x1cu,&down));if (down) pm_store(&m,0x53e9a4u,1,4);
    else if (pm_load(&m,0x53e9a4u,4)) { pm_store(&m,0x53e9a4u,0,4);if (pm_load(&m,0x53e908u,4)) pm_store(&m,0x53e428u,1,4); }
    return m.error;
}
int frame_session_results_wait(frame_data_read read,frame_data_write write,void *ctx,const frame_session_devices *d,uint32_t *collect)
{
    physics_memory m={read,write,ctx,0};uint32_t limit,skip,elapsed,mode,pressed,ready=0;int status;
    if (!read || !write || !collect) return -1;
    if (pm_load(&m,0x53e3fcu,4)) { *collect=0;return m.error; }
    mode=pm_load(&m,0x52ee9cu,4);limit=pm_load(&m,0x53e928u,4) ? 4000u : mode==2u ? 7000u : 10000u;
    if (pm_load(&m,0x58e7acu,4)) return -5;
    skip=(mode==2u || mode==3u) ? 5000u : 2000u;elapsed=pm_load(&m,0x53e98cu,4)+pm_load(&m,0x590550u,4);pm_store(&m,0x53e98cu,elapsed,4);
    if (pm_signed(limit)<pm_signed(elapsed)) ready=1;
    else if (pm_signed(skip)<pm_signed(elapsed)) { TRY(key(&m,0x30u,&pressed));ready=pressed;if (!ready) { TRY(key(&m,0x14u,&pressed));ready=pressed; } }
    if (ready) { pm_store(&m,0x53e98cu,0,4);if (pm_load(&m,0x53e928u,4)) return -5; }
    else if (!pm_load(&m,0x53e944u,4) && pm_signed(limit-elapsed)<2048 && mode!=4u && mode!=5u) {
        pm_store(&m,0x53e944u,2047,4);if (pm_load(&m,0x53e96cu,4)) { pm_store(&m,0x53e96cu,0,4);TRY(audio_restore(&m,d)); }
    }
    if (m.error) return m.error;
    *collect=ready;return 0;
}
int frame_session_idle(frame_data_read read,frame_data_write write,void *ctx,uint32_t *request)
{
    physics_memory m={read,write,ctx,0};uint32_t i,pressed,active=0,elapsed,index;int32_t axis;int status;
    if (!read || !write || !request) return -1;
    *request=0;if (pm_load(&m,0x58e7acu,4) || pm_load(&m,0x53e928u,4) || pm_load(&m,0x54b480u,4)) return m.error;
    for (i=4u;i<48u && !active;i+=4u) { TRY(key(&m,i,&pressed));active=pressed; }
    if (!active && pm_load(&m,0x53cca8u,4)) for (i=0;i<4u;i++) { TRY(frame_control_axis(read,ctx,i,&axis));if (axis) active=1; }
    for (i=0;i<2u;i++) { TRY(frame_control_axis(read,ctx,i ? 3u : 0u,&axis));if (pm_abs(axis)>2000) active=1; }
    for (i=0x10au;i<0x116u;i++) { TRY(frame_control_key(read,ctx,i,&pressed));if (pressed==1u) active=1; }
    if (active) pm_store(&m,0x53e948u,0,4);
    else {
        elapsed=pm_load(&m,0x53e948u,4)+pm_load(&m,0x5904fcu,4);pm_store(&m,0x53e948u,elapsed,4);
        if (pm_signed(elapsed)>180000) {
            pm_store(&m,0x53e948u,0,4);index=pm_load(&m,0x53e92cu,4);
            /* Original inline literal copies retain their DWORD/WORD/BYTE
             * widths. Bytes after the terminator belong to prior DATA. */
            if (!index) { pm_store(&m,0x530340u,0x6c676e65u,4);pm_store(&m,0x530344u,0x00646e61u,4); }
            else if (index==1u) { pm_store(&m,0x530340u,0x6978656du,4);pm_store(&m,0x530344u,0x6f63u,2);pm_store(&m,0x530346u,0,1); }
            else if (index==2u) { pm_store(&m,0x530340u,0x6d617773u,4);pm_store(&m,0x530344u,0x70u,2); }
            pm_store(&m,0x53e92cu,index+1u,4);if (index+1u==3u) pm_store(&m,0x53e92cu,0,4);
            pm_store(&m,0x53d18cu,0,4);pm_store(&m,0x52ee9cu,1,4);pm_store(&m,0x530440u,1,4);
            for (i=0;i<10u;i++) pm_store(&m,0x5303c0u+i,(unsigned char)"tank1.dat"[i],1);
            pm_store(&m,0x53e928u,1,4);pm_store(&m,0x53e924u,1,4);pm_store(&m,0x53e96cu,0,4);*request=1;
        }
    }
    return m.error;
}
int frame_session_stop_entities(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t head,node,id,used=0,i;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    head=pm_load(&m,0x55b6a8u,4);node=pm_load(&m,head,4);
    while (node!=head) { if (used++>=e->work_limit) return -3;id=pm_load(&m,node+8u,4);TRY(frame_entity_queue(read,write,ctx,id,1,e->allocate,e->release,e->system_ctx,e->work_limit));node=pm_load(&m,node,4); }
    TRY(frame_entities_drain_queues(read,write,ctx,e->method,e->method_ctx,e->release,e->system_ctx,e->work_limit));
    for (i=0;i<3u;i++) { head=pm_load(&m,(uint32_t[]){0x55b6a8u,0x55b6b8u,0x55b6bcu}[i],4);TRY(frame_list_clear(read,write,ctx,head,e->release,e->system_ctx,e->work_limit)); }return m.error;
}
int frame_session_clear_entities(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t id,first,table,capacity,used=0;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    first=pm_load(&m,0x536cb4u,4);id=first;
    while (pm_signed(id)<pm_i32(&m,0x55c690u)) { if (used++>=e->work_limit) return -3;table=pm_load(&m,0x55c694u,4);if (pm_load(&m,table+id*8u,4)) { TRY(frame_entity_destroy(read,write,ctx,id,e->method,e->method_ctx,e->release,e->system_ctx)); }id++; }
    table=pm_load(&m,0x55c694u,4);if (table) { id=first;capacity=pm_load(&m,0x55c690u,4);pm_store(&m,0x55c68cu,first,4);while (pm_signed(id)<pm_signed(capacity-1u)) { if (used++>=e->work_limit) return -3;pm_store(&m,table+4u+id*8u,id+1u,4);id++; }pm_store(&m,table+capacity*8u-4u,0,4);pm_store(&m,0x536cbcu,first,4);pm_store(&m,0x536cc0u,first,4); }return m.error;
}
static int clear_materials(frame_data_read read,frame_data_write write,void *ctx,uint32_t mode,const frame_entity_factory_services *e,const frame_texture_services *textures)
{
    physics_memory m={read,write,ctx,0};uint32_t head,node,record,texture,used=0;int status;
    head=pm_load(&m,mode==0u ? 0x55ae30u : mode==1u ? 0x55ae38u : 0x55ae3cu,4);
    if (!head) return m.error;
    node=pm_load(&m,head,4);
    while (node!=head) {
        if (used++>=e->work_limit) return -3;
        record=pm_load(&m,node+8u,4);texture=pm_load(&m,record+16u,4);
        if (texture) { uint32_t image=pm_load(&m,texture+10u,4);if (image) { TRY(frame_pool_release(read,write,ctx,image,32,e->release,e->system_ctx)); }TRY(frame_texture_destroy(read,write,ctx,texture,e,textures)); }
        TRY(frame_pool_release(read,write,ctx,record,20,e->release,e->system_ctx));
        TRY(frame_list_remove_value(read,write,ctx,record,pm_load(&m,0x55ae68u,4),e->release,e->system_ctx,e->work_limit));
        node=pm_load(&m,node,4);
    }
    TRY(frame_list_clear(read,write,ctx,head,e->release,e->system_ctx,e->work_limit));return m.error;
}
int frame_session_unload(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *e,const frame_texture_services *textures)
{
    physics_memory m={read,write,ctx,0};uint32_t i,j,head,node,next,record,table,used=0;int status;
    static const uint32_t lists[]={0x54b48cu,0x54b488u,0x5450b4u,0x544f90u,0x53e9b8u,0x592e7cu,0x53d148u,0x5468a0u,0x5466c8u,0x53cd18u,0x53e4b4u,0x53e4b8u,0x53e4bcu};
    if (!read || !write || !e || !e->work_limit) return -1;
    if (pm_load(&m,0x58e7acu,4) || pm_load(&m,0x54b480u,4)) return -5;
    pm_store(&m,0x53e904u,0,4);
    if (pm_load(&m,0x53e418u,4)) pm_store(&m,0x53e418u,0,4);
    if (pm_load(&m,0x53e41cu,4)) pm_store(&m,0x53e41cu,0,4);
    { frame_overlay_params p={1,0,0,0,0};TRY(frame_overlay_set(read,write,ctx,0,0,&p));TRY(frame_overlay_set(read,write,ctx,0,1,&p)); }
    TRY(frame_scene_pools_initialize(write,ctx));
    if (pm_load(&m,0x54b49cu,4)) { pm_store(&m,0x55ae58u,0,4);pm_store(&m,0x55ae5cu,0,4);pm_store(&m,0x55ae60u,0,4);pm_store(&m,0x54b49cu,0,4); }
    head=pm_load(&m,0x55ae54u,4);
    if (head) {
        node=pm_load(&m,head,4);
        while (node!=head) { if (used++>=e->work_limit) return -3;next=pm_load(&m,node,4);record=pm_load(&m,node+8u,4);if (!(pm_load(&m,record+18u,1)&2u)) { TRY(frame_shape_release(read,write,ctx,pm_load(&m,record,4),0,e)); }node=next; }
    }
    for (i=3u;i;i--) { TRY(clear_materials(read,write,ctx,i-1u,e,textures)); }
    TRY(frame_session_stop_entities(read,write,ctx,e));TRY(frame_session_clear_entities(read,write,ctx,e));
    /*4F42F0 +40DD50: entity references become the declared nil value; class
     * descriptors and interned names survive reload. Each bucket is a chain. */
    table=pm_load(&m,0x534cfcu,4);
    for (i=0;i<131u;i++) {
        node=pm_load(&m,table+i*4u,4);
        while (node) { if (used++>=e->work_limit) return -3;record=pm_load(&m,node+8u,4);if (pm_load(&m,record,4)==128u) { pm_store(&m,record,pm_load(&m,0x5b3388u,4),4);pm_store(&m,record+4u,pm_load(&m,0x5b338cu,4),4); }node=pm_load(&m,node,4); }
    }
    for (i=0;i<4u;i++) { head=pm_load(&m,lists[i],4);if (head) { TRY(frame_list_clear(read,write,ctx,head,e->release,e->system_ctx,e->work_limit)); } }
    for (j=0;j<2u;j++) for (i=0;i<64u;i++) { uint32_t at=(j ? 0x546770u : 0x544e70u)+i*4u;head=pm_load(&m,at,4);if (head) { TRY(frame_list_destroy(read,write,ctx,head,e->release,e->system_ctx,e->work_limit)); }pm_store(&m,at,0,4); }
    for (i=4u;i<13u;i++) {
        if (i==6u) { head=pm_load(&m,0x53e568u,4);if (head) { TRY(frame_list_destroy(read,write,ctx,head,e->release,e->system_ctx,e->work_limit));pm_store(&m,0x53e568u,0,4); } }
        head=pm_load(&m,lists[i],4);if (head) { TRY(frame_list_clear(read,write,ctx,head,e->release,e->system_ctx,e->work_limit)); }if (i==4u) pm_store(&m,0x53e9bcu,0,4);
    }
    pm_store(&m,0x53e8b8u,0,4);for (i=0;i<7u;i++) pm_store(&m,0x53e8c0u+i*4u,0,4);for (i=0;i<9u;i++) pm_store(&m,0x53e8e0u+i*4u,0,4);
    pm_store(&m,0x53e8bcu,0,4);pm_store(&m,0x53e8dcu,0,4);pm_store(&m,0x53cc38u,0,4);pm_store(&m,0x53cc3cu,0,4);pm_store(&m,0x590410u,0,4);return m.error;
}
int frame_session_end(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *e,const frame_texture_services *textures,const frame_session_devices *d)
{
    physics_memory m={read,write,ctx,0};int status;
    if (!read || !write) return -1;
    pm_store(&m,0x53e984u,0,4);
    if (pm_load(&m,0x53e96cu,4)) { pm_store(&m,0x53e96cu,0,4);TRY(audio_restore(&m,d)); }
    pm_store(&m,0x74aed0u,0,4);
    if (pm_load(&m,0x52eed8u,4)==1u) { TRY(frame_session_unload(read,write,ctx,e,textures)); }
    pm_store(&m,0x52eed8u,0,4);return m.error;
}
int frame_session_prepare_restart(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *e,const frame_texture_services *textures,const frame_session_devices *d)
{
    physics_memory m={read,write,ctx,0};int status;
    if (!read || !write) return -1;
    if (pm_load(&m,0x53e96cu,4)) { pm_store(&m,0x53e96cu,0,4);TRY(audio_restore(&m,d)); }
    TRY(frame_session_unload(read,write,ctx,e,textures));return m.error;
}
#undef TRY
