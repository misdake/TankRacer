#include "frame_game.h"
#include "frame_bootstrap.h"
#include "frame_math.h"
#include "frame_surface_config.h"
#include "frame_object_config.h"
#include "frame_powerup.h"
#include "frame_vehicle_scene.h"
#include "frame_blast_pool.h"
#include "frame_projectile.h"
#include "frame_player_lifecycle.h"
#include "frame_ai.h"
#include "frame_spatial.h"
#include "frame_environment.h"
#include "frame_session.h"
#include "frame_results.h"
#include "frame_race.h"
#include "physics_memory.h"
#include <string.h>
typedef struct game { physics_memory m;frame_native_context *native;frame_entity_factory_services *e;frame_object_parameters_services *resources; } game;
static int file(game *g,uint32_t space,const unsigned char *name,uint32_t n,const unsigned char **bytes,uint32_t *length)
{
    uint32_t found=0;int status;if (!g->resources->shapes || !g->resources->shapes->fetch) return -5;
    status=g->resources->shapes->fetch(g->resources->shapes->file_ctx,space,name,n,bytes,length,&found);if (status) return status;return found ? 0 : -4;
}
static int background(game *g,const unsigned char *name,uint32_t n,uint32_t destination)
{
    const unsigned char *bytes=0;uint32_t length=0,found=0,p,i;int status;
    if (!g->resources->shapes || !g->resources->shapes->fetch) return -5;
    status=g->resources->shapes->fetch(g->resources->shapes->file_ctx,6,name,n,&bytes,&length,&found);if (status) return status;
    status=frame_bootstrap_background_span(g->m.read,g->m.write,g->m.ctx,found ? bytes : 0,found ? length : 0,g->e->work_limit,g->resources);if (status) return status;
    if (destination) { status=frame_level_allocate(g->m.read,g->m.write,g->m.ctx,320,&p);if (status) return status;for (i=0;i<80u;i++) pm_store(&g->m,p+i*4u,pm_load(&g->m,0x5367b0u+i*4u,4),4);pm_store(&g->m,destination,p,4); }return g->m.error;
}
static int material(void *ctx,uint32_t address,uint32_t mode,uint32_t *out)
{
    game *g=(game *)ctx;unsigned char name[32];uint32_t n;
    for (n=0;n<32u;n++) { name[n]=(unsigned char)pm_load(&g->m,address+n,1);if (!name[n]) break; }
    if (g->m.error) return g->m.error;
    if (n==32u) return -4;
    return g->resources->material ? g->resources->material(g->resources->material_ctx,name,n+1u,mode,out) : -5;
}
#define TRY(x) do { if (m->error) return m->error;status=(x);if (status) return status; } while (0)
static game context(frame_native_context *c)
{
    frame_entity_native_context *e=&c->base.base.base.base;game g={{e->read,e->write,e->data_ctx,0},c,&e->services,&c->base.base.base.parameters};return g;
}
int frame_game_initialize(frame_native_context *c)
{
    game g;physics_memory *m;const unsigned char *bytes;unsigned char name[16];uint32_t n,consumed,classes,h,i,j;int status;
    if (!c) return -1;
    g=context(c);m=&g.m;if (!m->read || !m->write || !g.e->work_limit) return -1;
    TRY(frame_bootstrap_static_data(m->write,m->ctx));TRY(frame_bootstrap_classes(m->read,m->write,m->ctx,g.e,&classes));TRY(frame_bootstrap_world_constants(m->write,m->ctx));TRY(frame_bootstrap_c_locale(m->write,m->ctx,0x10000));TRY(frame_math_initialize(m->write,m->ctx));TRY(frame_bootstrap_vehicle_constants(m->write,m->ctx));
    TRY(frame_results_constants(m->write,m->ctx));TRY(frame_results_defaults(m->write,m->ctx));TRY(frame_results_route_random(m->read,m->write,m->ctx));
    for (n=0;n<8u;n++) pm_store(m,0x530458u+n,(unsigned char)"---\0---"[n],1);
    pm_store(m,0x53e424u,1,4);pm_store(m,0x538820u,1,4);
    TRY(file(&g,2,(const unsigned char *)"SURFS.DAT",10,&bytes,&n));TRY(frame_surface_configure_span(m->read,m->write,m->ctx,bytes,n,g.e->work_limit,&consumed));
    TRY(file(&g,2,(const unsigned char *)"OBPARAMS.DAT",13,&bytes,&n));TRY(frame_object_configure_span(m->read,m->write,m->ctx,bytes,n,g.e,&consumed));
    TRY(frame_ai_constants(m->write,m->ctx));
    for (i=1;i<=15u;i++) {
        for (j=0;j<6u;j++) name[j]=(unsigned char)"AITANK"[j];
        j=6;if (i>=10u) name[j++]='1';name[j++]=(unsigned char)('0'+i%10u);memcpy(name+j,".DAT",5);j+=5u;
        TRY(file(&g,2,name,j,&bytes,&n));TRY(frame_ai_configure_span(m->read,m->write,m->ctx,i-1u,bytes,n,g.e->work_limit,&consumed));
    }
    TRY(frame_entity_table_initialize(m->read,m->write,m->ctx,128,g.e));pm_store(m,0x536cb4u,1,4);TRY(frame_scene_pools_initialize(m->write,m->ctx));
    TRY(frame_list_create(m->read,m->write,m->ctx,g.e->allocate,g.e->release,g.e->system_ctx,g.e->work_limit,&h));pm_store(m,0x54b488u,h,4);
    TRY(frame_list_create(m->read,m->write,m->ctx,g.e->allocate,g.e->release,g.e->system_ctx,g.e->work_limit,&h));pm_store(m,0x54b48cu,h,4);return m->error;
}
static int game_load(frame_native_context *c,const frame_game_options *o,uint32_t opponents,uint32_t seed,uint32_t *out)
{
    game g;physics_memory *m;const unsigned char *bytes;unsigned char name[32],upper[16],vehicle_name[16];uint32_t n,i,j,root,result,consumed,player,object,entry,number,digits=0;int status;
    static const uint32_t environment[]={0x438d60,0x42c550,0x4427c0},transients[]={0x426e30,0x42ac70,0x407830,0x415f60,0x430720},level_shapes[]={0x40ab60,0x413a40,0x404c10};
    if (!c || !o || !o->map || !out || o->map_bytes<2u || o->map_bytes>16u || o->map[o->map_bytes-1u] || o->cup>2u || !o->vehicle || o->vehicle>15u || o->mode>1u) return -1;
    g=context(c);m=&g.m;n=o->map_bytes-1u;
    for (i=0;i<n;i++) { unsigned char ch=o->map[i];if (ch>='a' && ch<='z') ch-=32;if (ch<'A' || ch>'Z') return -1;upper[i]=ch; }upper[n]=0;
    pm_store(m,0x52ee9cu,o->mode,4);TRY(frame_session_loading_reset(m->read,m->write,m->ctx));pm_store(m,0x53e98cu,0,4);
    pm_store(m,0x55ae40u,5,4);pm_store(m,0x53d184u,1,4);pm_store(m,0x53d188u,2,4);pm_store(m,0x55ae44u,4,4);pm_store(m,0x55ae48u,3,4);pm_store(m,0x5367a8u,0x20000,4);pm_store(m,0x53d18cu,o->cup,4);
    TRY(frame_bootstrap_level_select(m->read,m->write,m->ctx,upper,n+1u));if (pm_load(m,0x52ed90u,4)>7u) return -4;
    for (i=0;i<=n;i++) pm_store(m,0x590540u+i,upper[i] ? upper[i]+32u : 0,1);
    pm_store(m,0x55ae64u,pm_load(m,0x52ed90u,4)==5u,4);pm_store(m,0x54b4acu,0,4);
    for (i=0;i<n;i++) name[i]=upper[i];
    name[n]='.';name[n+1]='B';name[n+2]='S';name[n+3]='M';name[n+4]=0;
    TRY(file(&g,6,name,n+5u,&bytes,&result));if (result>0x91800u) return -4;if (!m->write(m->ctx,0x5dab40u,bytes,result)) return -1;
    TRY(frame_shape_prepare_world(m->read,m->write,m->ctx,0x53d180u,material,&g,g.e->work_limit,&root));pm_store(m,0x54b49cu,root,4);
    TRY(frame_spatial_initialize(m->read,m->write,m->ctx,g.e->allocate,g.e->release,g.e->system_ctx,g.e->work_limit));TRY(frame_waypoint_grid_initialize(m->read,m->write,m->ctx,g.e));
    if (pm_load(m,0x55ae64u,4)) { TRY(background(&g,(const unsigned char *)"BIOMOON.BGN",12,0x53e41cu)); }
    j=n;for (i=0;i<n;i++) name[i]=upper[i];entry=pm_load(m,0x52ed90u,4);
    if (o->cup==1u && entry==1u) name[j++]='S';
    if (o->cup==2u && (entry==0u || entry==3u || entry==6u)) name[j++]='G';
    name[j++]='.';name[j++]='B';name[j++]='G';name[j++]='N';name[j++]=0;TRY(background(&g,name,j,entry==5u ? 0x53e418u : 0u));pm_store(m,0x53e420u,0,4);
    pm_store(m,0x54b4acu,1,4);TRY(frame_projectile_materials(m->read,m->write,m->ctx,g.resources->material,g.resources->material_ctx));
    for (i=0;i<3u;i++) { TRY(frame_bootstrap_environment(m->read,m->write,m->ctx,environment[i],g.resources->material,g.resources->material_ctx)); }
    TRY(frame_bootstrap_environment(m->read,m->write,m->ctx,0x40d313u,g.resources->material,g.resources->material_ctx));
    TRY(frame_bootstrap_environment(m->read,m->write,m->ctx,0x40d34bu,g.resources->material,g.resources->material_ctx));
    TRY(frame_bootstrap_ingame_shapes(m->read,m->write,m->ctx,g.e,g.resources));
    pm_store(m,0x55ae40u,3,4);TRY(frame_powerup_load_resources(m->read,m->write,m->ctx,g.e,g.resources->shapes));pm_store(m,0x55ae40u,5,4);
    for (i=0;i<3u;i++) { TRY(frame_bootstrap_level_shapes(m->read,m->write,m->ctx,level_shapes[i],g.e,g.resources->shapes)); }
    pm_store(m,0x530ce0u,1,4);pm_store(m,0x54689cu,0,4);pm_store(m,0x5466ccu,0,4);
    for (i=0;i<4u;i++) vehicle_name[i]=(unsigned char)"tank"[i];
    number=o->vehicle;if (number>=10u) { vehicle_name[4]='1';digits=1;number-=10u; }vehicle_name[4u+digits]=(unsigned char)('0'+number);digits+=5u;
    for (i=0;i<5u;i++) vehicle_name[digits+i]=(unsigned char)".dat"[i];
    for (i=0;i<digits+5u;i++) pm_store(m,0x5303c0u+i,vehicle_name[i],1);
    pm_store(m,0x530440u,o->colour,4);
    for (i=0;i<3u;i++) name[i]=upper[i];
    name[3]=(unsigned char)"BSG"[o->cup];for (i=0;i<5u;i++) name[4u+i]=(unsigned char)".DAT"[i];
    TRY(file(&g,6,name,9,&bytes,&result));pm_store(m,0x55b680u,5,4);pm_store(m,0x55c688u,1,4);TRY(frame_world_load_span(m->read,m->write,m->ctx,77,0,bytes,result,g.e,&result,&consumed));
    TRY(frame_waypoint_link(m->read,m->write,m->ctx,g.e));TRY(frame_background_link(m->read,m->write,m->ctx,g.e));TRY(frame_explosion_fog(m->read,m->write,m->ctx,&c->explosion));
    for (i=0;i<2u;i++) { TRY(frame_bootstrap_transients(m->read,m->write,m->ctx,transients[i],g.resources->material,g.resources->material_ctx)); }
    TRY(frame_explosion_reset(m->read,m->write,m->ctx));TRY(frame_blast_pool_initialize(m->read,m->write,m->ctx,g.resources->material,g.resources->material_ctx));TRY(frame_entities_drain_queues(m->read,m->write,m->ctx,g.e->method,g.e->method_ctx,g.e->release,g.e->system_ctx,g.e->work_limit));
    TRY(frame_pickup_effect_initialize(m->read,m->write,m->ctx,g.resources->material,g.resources->material_ctx));TRY(frame_projectile_ground_initialize(m->read,m->write,m->ctx,g.resources->material,g.resources->material_ctx));
    for (i=2;i<5u;i++) { TRY(frame_bootstrap_transients(m->read,m->write,m->ctx,transients[i],g.resources->material,g.resources->material_ctx)); }
    pm_store(m,0x5461a8u,entry==4u || entry==5u,4);pm_store(m,0x5461acu,entry==7u,4);
    if (opponents) {
        TRY(frame_ai_selection_reset(m->read,m->write,m->ctx,seed));
        for (i=0;i<3u;i++) name[i]=upper[i];
        memcpy(name+3,"TANK.DAT",9);TRY(file(&g,6,name,12,&bytes,&result));
        TRY(frame_world_load_span(m->read,m->write,m->ctx,77,0,bytes,result,g.e,&result,&consumed));
    }
    player=pm_load(m,0x53e8b8u,4);if (!player) { TRY(frame_player_create(m->read,m->write,m->ctx,0,g.e,&player));pm_store(m,0x53e8b8u,player,4);TRY(frame_camera_create_chase(m->read,m->write,m->ctx,player,0,g.e));TRY(frame_native_dispatch(c,0x4350a0u,player,1,0x5303c0u,&result));TRY(frame_native_dispatch(c,0x437b00u,player,0,0,&result));object=pm_load(m,pm_load(m,0x55c694u,4)+player*8u,4);object=object ? pm_load(m,object+4u,4) : 0;if (object) pm_store(m,object+0xddfu,o->colour,1); }
    pm_store(m,0x53e8e0u,player,4);pm_store(m,0x53e8e4u,pm_load(m,0x53e8bcu,4),4);for (i=0;i<7u;i++) pm_store(m,0x53e8e8u+i*4u,pm_load(m,0x53e8c0u+i*4u,4),4);
    for (i=0;i<9u;i++) { object=pm_load(m,pm_load(m,0x55c694u,4)+pm_load(m,0x53e8e0u+i*4u,4)*8u,4);object=object ? pm_load(m,object+4u,4) : 0;if (object) pm_store(m,object+0xe07u,i,1); }
    TRY(frame_player_routes_initialize(m->read,m->write,m->ctx,g.e->work_limit));TRY(frame_player_finish_owner_create(m->read,m->write,m->ctx,g.e));pm_store(m,0x546720u,0,4);pm_store(m,0x546724u,0,4);pm_store(m,0x55c688u,0,4);
    TRY(frame_session_begin_with_devices(m->read,m->write,m->ctx,o->devices));pm_store(m,0x53e42cu,0x413dd0u,4);pm_store(m,0x74aed0u,0x411320u,4);if (m->error) return m->error;*out=player;return 0;
}
#undef TRY
int frame_game_load(frame_native_context *c,const frame_game_options *o,uint32_t *out)
{ return game_load(c,o,0,0,out); }
int frame_game_load_opponents(frame_native_context *c,const frame_game_options *o,uint32_t seed,uint32_t *out)
{ return game_load(c,o,1,seed,out); }
int frame_game_tick_menu_limit(frame_native_context *c,const frame_session_devices *d,uint32_t menu_limit,uint32_t *action)
{
    game g;physics_memory *m;uint32_t player,camera,result,request;int status;
    if (!c || !action) return -1;
    g=context(c);m=&g.m;
#define TICK(x) do { if (m->error) return m->error;status=(x);if (status) return status; } while (0)
    if (pm_load(m,0x52ee9cu,4)>1u || pm_load(m,0x58e7acu,4) || pm_load(m,0x54b480u,4) || pm_load(m,0x53e928u,4)) return -5;
    if (!pm_load(m,0x53e96cu,4)) {
        pm_store(m,0x54b4c4u,0,4);TICK(frame_player_finish_owner_activate(m->read,m->write,m->ctx,g.e));
        TICK(frame_ai_global_tick(m->read,m->write,m->ctx,g.resources->trig));
        TICK(frame_race_tick(m->read,m->write,m->ctx));TICK(frame_environment_weather(m->read,m->write,m->ctx,g.resources->trig,&c->explosion,g.e->work_limit));TICK(frame_entities_tick(m->read,m->write,m->ctx,g.e->method,g.e->method_ctx,g.e->work_limit));
        if (!pm_load(m,0x53e918u,4)) { if (pm_load(m,0x53e924u,4) || pm_load(m,0x5466ccu,4)) return -5;camera=pm_load(m,0x53cc38u,4);if (camera) { player=pm_load(m,0x53e8b8u,4);TICK(frame_entity_dispatch(m->read,m->write,m->ctx,camera,pm_load(m,0x545ec8u,4),1,player,g.e->method,g.e->method_ctx,&result)); } }
        TICK(frame_entities_drain_queues(m->read,m->write,m->ctx,g.e->method,g.e->method_ctx,g.e->release,g.e->system_ctx,g.e->work_limit));TICK(frame_session_idle(m->read,m->write,m->ctx,&request));if (request) { *action=4;return 0; }
        TICK(frame_session_results_wait(m->read,m->write,m->ctx,d,&request));if (request) { *action=3;return 0; }
    }
    TICK(frame_session_fades(m->read,m->write,m->ctx));TICK(frame_session_pause_key(m->read,m->write,m->ctx,d));
    if (pm_load(m,0x53e96cu,4) && !pm_load(m,0x53e928u,4)) { pm_store(m,0x54b4c4u,1,4);TICK(frame_session_menu_limited(m->read,m->write,m->ctx,d,menu_limit)); }
    TICK(frame_session_scoreboard(m->read,m->write,m->ctx));TICK(frame_session_action(m->read,m->write,m->ctx,action));return m->error;
#undef TICK
}

int frame_game_tick(frame_native_context *c,const frame_session_devices *d,uint32_t *action)
{ return frame_game_tick_menu_limit(c,d,0,action); }
