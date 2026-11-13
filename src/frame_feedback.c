#include "frame_feedback.h"
#include "physics_memory.h"
#include <string.h>

static int emit(physics_memory *m,frame_effect_sink sink,void *ctx,const frame_effect_command *command)
{
    if (m->error) return m->error;
    return sink ? sink(ctx,command) : -5;
}
int frame_feedback_mine(frame_data_read read,frame_data_write write,void *ctx,frame_effect_sink sink,void *sink_ctx)
{
    physics_memory m={read,write,ctx,0};frame_effect_command command={0};uint32_t effect;
    if (!read || !write) return -1;
    if (!pm_load(&m,0x58e7ccu,4)) return m.error;
    effect=pm_load(&m,0x58e7dcu,4);if (!effect || pm_load(&m,0x53e928u,4)) return m.error;
    command.operation=FRAME_EFFECT_START;command.effect=effect;command.argument0=1;return emit(&m,sink,sink_ctx,&command);
}
int frame_feedback_fire(frame_data_read read,frame_data_write write,void *ctx,uint32_t weapon,frame_effect_sink sink,void *sink_ctx)
{
    physics_memory m={read,write,ctx,0};frame_effect_command command={0};uint32_t at,effect,r,record,o;int32_t angle,converted,direction;int status;
    if (!read || !write) return -1;
    if (!pm_load(&m,0x58e7ccu,4) || pm_load(&m,0x53e928u,4)) return m.error;
    switch (weapon) { case 0:case 4:case 9:case 10:at=0x58e7d0;break;case 3:at=0x58e7d8;break;case 5:at=0x58e7d4;break;default:return m.error; }
    effect=pm_load(&m,at,4);if (!effect) return m.error;
    record=pm_load(&m,pm_load(&m,0x55c694u,4)+pm_load(&m,0x53e8b8u,4)*8u,4);o=record ? pm_load(&m,record+4u,4) : 0;angle=pm_i16(&m,o+0x762u);
    if (angle!=pm_i32(&m,0x58e7f0u)) {
        converted=pm_mul(angle,100)/182;direction=pm_add(converted,18000);if (direction>35999) direction=pm_sub(converted,18000);else if (direction<0) direction=pm_add(converted,54000);
        direction=direction<18000 ? pm_sub(18000,direction) : pm_sub(54000,direction);
        if (direction==36000) { r=pm_load(&m,0x55b6acu,4)*1664525u+1013904223u;pm_store(&m,0x55b6acu,r,4);direction=(r&16u) ? 35999 : 1; }
        command.operation=FRAME_EFFECT_SET_DIRECTION;command.effect=effect;command.flags=0x40;command.parameter_count=2;command.parameters[0]=(uint32_t)direction;
        status=emit(&m,sink,sink_ctx,&command);if (status) return status;pm_write32(&m,0x58e7f0u,pm_i16(&m,o+0x762u));
    }
    memset(&command,0,sizeof(command));command.operation=FRAME_EFFECT_START;command.effect=effect;command.argument0=1;return emit(&m,sink,sink_ctx,&command);
}
int frame_feedback_impact(frame_data_read read,frame_data_write write,void *ctx,
                            uint32_t strength,frame_effect_sink sink,void *sink_ctx)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    uint32_t effect,magnitude;
    frame_effect_command command={0};
    int status;
    if (!read || !write) return -1;
    if (!pm_load(m,0x58e7ccu,4)) return m->error;
    effect=pm_load(m,0x58e7ecu,4);
    if (!effect || pm_load(m,0x53e928u,4) || !pm_load(m,0x58e7c8u,4)) return m->error;
    if (pm_load(m,0x58e7f4u,4)!=strength) {
        magnitude=strength*5u;
        if (magnitude<2500u) magnitude=2500;
        if (magnitude>10000u) magnitude=10000;
        pm_store(m,0x58e7f4u,strength,4);
        command.operation=FRAME_EFFECT_SET_PERIODIC;command.effect=effect;command.flags=0x100;
        command.parameter_count=4;command.parameters[0]=magnitude;command.parameters[3]=40000;
        status=emit(m,sink,sink_ctx,&command);if (status) return status;
    }
    memset(&command,0,sizeof(command));
    command.operation=FRAME_EFFECT_START;command.effect=effect;command.argument0=1;
    return emit(m,sink,sink_ctx,&command);
}
int frame_feedback_surface(frame_data_read read,frame_data_write write,void *ctx,
                             frame_effect_sink sink,void *sink_ctx)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    uint32_t effect,table,slot,object;
    int32_t kind,coefficient;
    frame_effect_command command={0};
    if (!read || !write) return -1;
    if (!pm_load(m,0x58e7ccu,4)) return m->error;
    effect=pm_load(m,0x58e7e0u,4);
    if (!effect) return m->error;
    table=pm_load(m,0x55c694u,4);slot=pm_load(m,table+pm_load(m,0x53e8b8u,4)*8u,4);
    object=slot ? pm_load(m,slot+4u,4) : 0;
    kind=pm_i32(m,object+0x716u);
    if (kind!=pm_i32(m,0x538818u) && kind==pm_i32(m,object+0x71au)) {
        switch (kind) {
        case 2:case 5:case 6:coefficient=500;break;
        case 3:coefficient=4000;break;
        case 4:coefficient=350;break;
        case 7:coefficient=-3500;break;
        case 9:coefficient=0;break;
        default:coefficient=1000;break;
        }
        pm_write32(m,0x538818u,kind);
        command.operation=FRAME_EFFECT_SET_CONDITION;command.effect=effect;command.flags=0x100;
        command.parameter_count=6;command.parameters[1]=(uint32_t)coefficient;
        command.parameters[2]=(uint32_t)coefficient;command.parameters[3]=10000;command.parameters[4]=10000;
        return emit(m,sink,sink_ctx,&command);
    }
    return m->error;
}
void frame_effect_host_init(frame_effect_host *host)
{
    if (host) memset(host,0,sizeof(*host));
}
int frame_feedback_collision(frame_data_read read,frame_data_write write,void *ctx,uint32_t kind,uint32_t alternate,
                                frame_effect_sink sink,void *sink_ctx)
{
    physics_memory m={read,write,ctx,0};frame_effect_command command={0};uint32_t effect,previous,level=0,magnitude=0;int status;
    if (!read || !write) return -1;
    if (!pm_load(&m,0x58e7ccu,4) || pm_load(&m,0x53e928u,4)) return m.error;
    effect=pm_load(&m,0x58e7e8u,4);if (!effect) return m.error;
    previous=pm_load(&m,0x53881cu,4);
    if (alternate) { if (previous!=10000u && kind<=1u) { magnitude=6500;level=10000; } }
    else switch (kind) {
    case 0:case 3:case 5:if (previous!=6000u) { magnitude=2500;level=6000; }break;
    case 1:if (previous!=10000u) { magnitude=10000;level=10000; }break;
    case 2:case 4:if (previous!=10000u) { magnitude=4000;level=10000; }break;
    default:break;
    }
    if (level) {
        pm_store(&m,0x53881cu,level,4);command.operation=FRAME_EFFECT_SET_COLLISION_PERIODIC;command.effect=effect;command.flags=0x180;
        command.parameter_count=6;command.parameters[0]=magnitude;command.parameters[1]=40000;command.parameters[2]=level;
        command.parameters[3]=100000;command.parameters[5]=110000;
        status=emit(&m,sink,sink_ctx,&command);if (status) return status;
    }
    memset(&command,0,sizeof(command));command.operation=FRAME_EFFECT_START;command.effect=effect;command.argument0=1;
    return emit(&m,sink,sink_ctx,&command);
}
int frame_effect_host_mount(frame_effect_host *host,uint32_t effect,uint32_t type)
{
    unsigned i;
    if (!host || !effect || (type!=FRAME_EFFECT_SET_PERIODIC && type!=FRAME_EFFECT_SET_CONDITION && type!=FRAME_EFFECT_SET_COLLISION_PERIODIC)) return -1;
    for (i=0;i<FRAME_EFFECT_CAPACITY;i++) if (host->effects[i].mounted && host->effects[i].effect==effect) return -1;
    for (i=0;i<FRAME_EFFECT_CAPACITY;i++) if (!host->effects[i].mounted) {
        host->effects[i].mounted=1;host->effects[i].effect=effect;host->effects[i].type=type;return 0;
    }
    return -3;
}
int frame_effect_host_submit(void *ctx,const frame_effect_command *command)
{
    frame_effect_host *host=(frame_effect_host *)ctx;
    unsigned i,j;
    if (!host || !command) return -1;
    for (i=0;i<FRAME_EFFECT_CAPACITY;i++) if (host->effects[i].mounted && host->effects[i].effect==command->effect) {
        frame_effect_state *s=&host->effects[i];
        if (command->operation==FRAME_EFFECT_START) {
            if (command->flags || command->argument0!=1 || command->argument1 || command->parameter_count) return -1;
            s->active=1;s->starts++;return 0;
        }
        if (command->operation==FRAME_EFFECT_SET_DIRECTION) {
            if (s->type!=FRAME_EFFECT_SET_PERIODIC || command->flags!=0x40u || command->argument0 || command->argument1 || command->parameter_count!=2u || command->parameters[1]) return -1;
            s->parameters[4]=command->parameters[0];s->parameters[5]=0;s->updates++;return 0;
        }
        if (command->operation!=s->type || command->flags!=(s->type==FRAME_EFFECT_SET_COLLISION_PERIODIC ? 0x180u : 0x100u) || command->argument0 || command->argument1 ||
            command->parameter_count!=(s->type==FRAME_EFFECT_SET_PERIODIC ? 4u : 6u)) return -1;
        for (j=0;j<6;j++) s->parameters[j]=j<command->parameter_count ? command->parameters[j] : 0;
        s->updates++;return 0;
    }
    return -5;
}
