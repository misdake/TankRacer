/* Host services shared by native and WebAssembly builds. */
#include "frame_host.h"
#include "frame_bootstrap.h"
#include "frame_native.h"
#include "frame_math.h"
#include "frame_tank_model.h"
#include "frame_bounce.h"
#include "frame_surface_config.h"
#include "frame_object_config.h"
#include "frame_powerup.h"
#include "frame_vehicle_scene.h"
#include "frame_mine.h"
#include "frame_blast_pool.h"
#include "frame_projectile.h"
#include "frame_ufo.h"
#include "frame_camera.h"
#include "frame_spatial.h"
#include "frame_environment.h"
#include "physics_terrain.h"
#include "frame_player_lifecycle.h"
#include "frame_race.h"
#include "frame_time.h"
#include "frame_io.h"
#include "frame_material.h"
#include "frame_surface.h"
#include "frame_texture.h"
#include "frame_session.h"
#include "frame_results.h"
#include "frame_game.h"
#include "frame_profile.h"
#include "frame_present.h"
#include "frame_vehicle_render.h"
#include "frame_hud.h"
#include "frame_overlay.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#endif
#define RAM_BYTES 0x800000u
typedef struct mounted { unsigned char *bytes;uint32_t length;const unsigned char *ss;uint32_t ss_length; } mounted;
typedef struct host_block { uint32_t at,size,live; } host_block;
typedef struct web_texture { struct web_texture *next;uint32_t token,width,height,sent,alpha;unsigned char *rgba; } web_texture;
typedef struct startup {
    unsigned char *ram;uint32_t next,fetches,materials,allocations,sprites;
    mounted packs[7];frame_native_context native;frame_shape_resources resources;
    host_block *blocks;uint32_t block_count,reuse,physics_frames,generations,cup,ai_deferred,vehicle,mode;
    frame_explosion_fog_host fog;frame_io_clock clock;
    frame_surface_info image;frame_surface_plan plan;frame_texture_services textures;
    const unsigned char *image_bytes;uint32_t image_length,pixels_length,texture_commands,texture_count,texture_rejections;unsigned char *pixels;
    uint32_t streaming,opponents,pause_items;web_texture *web_textures;
    /* Optional in-memory NWF1 sink; native sessions retain stdout transport. */
    int (*packet_write)(void *,const void *,uint32_t);void *packet_ctx;
} startup;
static void web_textures_clear(startup *s)
{ while (s->web_textures) { web_texture *t=s->web_textures;s->web_textures=t->next;free(t->rgba);free(t); } }
static int read_data(void *ctx,uint32_t at,void *dst,uint32_t n)
{ startup *s=(startup *)ctx;if (at<0x10000u || at>RAM_BYTES || n>RAM_BYTES-at) { fprintf(stderr,"undefined DATA %x/%u\n",at,n);return 0; }memcpy(dst,s->ram+at,n);return 1; }
static int write_data(void *ctx,uint32_t at,const void *src,uint32_t n)
{ startup *s=(startup *)ctx;if (at<0x10000u || at>RAM_BYTES || n>RAM_BYTES-at) { fprintf(stderr,"undefined DATA %x/%u\n",at,n);return 0; }memcpy(s->ram+at,src,n);return 1; }
static uint32_t get(startup *s,uint32_t at)
{ return (uint32_t)s->ram[at]|((uint32_t)s->ram[at+1u]<<8)|((uint32_t)s->ram[at+2u]<<16)|((uint32_t)s->ram[at+3u]<<24); }
static void set(startup *s,uint32_t at,uint32_t v)
{ unsigned i;for (i=0;i<4u;i++) s->ram[at+i]=(unsigned char)(v>>(i*8u)); }
static int allocate(void *ctx,uint32_t n,uint32_t *out)
{
    startup *s=(startup *)ctx;uint32_t size=(n+15u)&~15u;
    if (!n || n>0x100000u) return -3;
    if (s->reuse) { uint32_t i;for (i=0;i<s->block_count;i++) if (!s->blocks[i].live && s->blocks[i].size>=size) { s->blocks[i].live=1;*out=s->blocks[i].at;s->allocations++;return 0; } }
    if (s->block_count>=16384u || s->next>0x400000u-size) return -3;
    *out=s->next;s->next+=size;s->blocks[s->block_count++]=(host_block){*out,size,1};s->allocations++;return 0;
}
static int zero_allocate(void *ctx,uint32_t count,uint32_t bytes,uint32_t *out)
{
    startup *s=(startup *)ctx;int status;if (count && bytes>UINT32_MAX/count) return -3;
    status=allocate(ctx,count*bytes,out);if (!status) memset(s->ram+*out,0,count*bytes);return status;
}
static int release(void *ctx,uint32_t at)
{ startup *s=(startup *)ctx;uint32_t i;for (i=0;i<s->block_count;i++) if (s->blocks[i].at==at && s->blocks[i].live) { s->blocks[i].live=0;return 0; }return -5; }
static int fetch(void *ctx,uint32_t space,const unsigned char *name,uint32_t name_bytes,const unsigned char **bytes,uint32_t *length,uint32_t *found)
{
    startup *s=(startup *)ctx;uint32_t i,off,n,first,last;int status;char upper[16];
    if (!name_bytes || name_bytes>sizeof(upper) || name[name_bytes-1u]) return -1;
    s->fetches++;*found=0;
    if (space==6u) {
        for (i=0;i<name_bytes;i++) upper[i]=(char)(name[i]>=97u && name[i]<=122u ? name[i]-32u : name[i]);
        status=frame_host_archive_find(s->packs[5].bytes,s->packs[5].length,upper,name_bytes-1u,&off,&n);if (status==1) return 0;if (status) return status;*bytes=s->packs[5].bytes+off;*length=n;*found=1;return 0;
    }
    if (space==2u) {
        for (i=0;i<name_bytes;i++) upper[i]=(char)(name[i]>=97u && name[i]<=122u ? name[i]-32u : name[i]);
        status=frame_host_archive_find(s->packs[0].bytes,s->packs[0].length,upper,name_bytes-1u,&off,&n);
        if (status==1) return 0;
        if (status) return status;
        *bytes=s->packs[0].bytes+off;*length=n;*found=1;return 0;
    }
    if (space==5u && name_bytes>4u && name[name_bytes-5u]=='.' && (name[name_bytes-4u]=='d' || name[name_bytes-4u]=='D')) {
        for (i=0;i<name_bytes;i++) upper[i]=(char)(name[i]>=97u && name[i]<=122u ? name[i]-32u : name[i]);
        status=frame_host_archive_find(s->packs[5].bytes,s->packs[5].length,upper,name_bytes-1u,&off,&n);if (status==1) return 0;if (status) return status;
        *bytes=s->packs[5].bytes+off;*length=n;*found=1;return 0;
    }
    if (space!=1u && space!=3u && space!=5u) return -5;
    first=space==1u ? 1u : space==3u ? 4u : 5u;last=space==1u ? 4u : space==3u ? 5u : 6u;
    for (i=first;i<last;i++) {
        status=frame_host_shape_find(s->packs[i].ss,s->packs[i].ss_length,(const char *)name,name_bytes-1u,&off,&n);
        if (status==1) continue;
        if (status) return status;
        *bytes=s->packs[i].ss+off;*length=n;*found=1;return 0;
    }
    return 0;
}
static int texture_device(void *ctx,uint32_t operation,uint32_t object,uint32_t start,uint32_t count,uint32_t *out)
{
    startup *s=(startup *)ctx;(void)object;(void)start;(void)count;
    if (operation<FRAME_TEXTURE_PALETTE_CREATE || operation>FRAME_TEXTURE_PALETTE_UPLOAD) return -5;
    s->texture_commands++;*out=1;return 0;
}
static int texture_reload(void *ctx,uint32_t identity,uint32_t descriptor,uint32_t user,uint32_t *out)
{
    startup *s=(startup *)ctx;unsigned char palette[1024];uint32_t consumed;int status;(void)user;
    if (identity!=0x80000001u) return -5;
    status=frame_surface_decode(s->image_bytes,s->image_length,&s->image,s->plan.skip,s->pixels,s->pixels_length,palette,1024,&consumed);if (status) return status;
    if (s->streaming) {
        uint32_t n;web_texture *t=s->web_textures;
        while (t && t->token!=descriptor) t=t->next;
        if (!t) { t=(web_texture *)calloc(1,sizeof(*t));if (!t) return -3;t->next=s->web_textures;s->web_textures=t; }
        free(t->rgba);t->token=descriptor;t->width=s->image.width>>s->plan.skip;t->height=s->image.height>>s->plan.skip;t->sent=0;n=t->width*t->height;
        t->rgba=(unsigned char *)malloc(n*4u);if (!t->rgba) return -3;
        status=frame_surface_upload_rgba(&s->image,s->plan.flags,n,s->pixels,s->pixels_length,palette,1024,t->rgba,n*4u,&t->alpha);if (status) return status;
    }
    if (!s->image.kind) { memcpy(s->ram+0x80000u,palette,1024);status=frame_texture_palette(read_data,write_data,s,descriptor,0,256,0x80000u,&s->native.base.base.base.base.services,&s->textures);if (status) return status; }
    *out=1;return 0;
}
static int texture_load(void *ctx,uint32_t space,const unsigned char *name,uint32_t bytes,uint32_t flags,uint32_t *out)
{
    startup *s=(startup *)ctx;char upper[16];uint32_t i,first,last,off=0,n=0;frame_texture_parameters q;int status=1;
    if (!bytes || bytes>sizeof(upper) || name[bytes-1u]) return -1;
    for (i=0;i<bytes;i++) upper[i]=(char)(name[i]>=97u && name[i]<=122u ? name[i]-32u : name[i]);
    first=space==1u ? 1u : space==3u ? 4u : space==4u ? 0u : space==5u ? 5u : 6u;last=space==1u ? 4u : first+1u;if (first>=6u) return -5;
    for (i=first;i<last;i++) { status=frame_host_archive_find(s->packs[i].bytes,s->packs[i].length,upper,bytes-1u,&off,&n);if (!status) break;if (status!=1) return status; }
    if (status==1) { *out=0;return 0; }
    s->image_bytes=s->packs[i].bytes+off;s->image_length=n;
    status=frame_surface_open(s->image_bytes,n,flags,3,3,&s->image);if (status) return status;
    if (!((s->image.kind==0u && s->image.bits==8u) || (s->image.kind==2u && s->image.bits==32u)) || s->image.compression>2u) { s->texture_rejections++;*out=0;return 0; } /* explicit graphics resource failure; never a gameplay fallback */
    status=frame_surface_prepare(&s->image,flags,1,0,0,100000,&s->plan);if (status) return status;
    s->pixels_length=s->image.width*s->image.height*4u;s->pixels=(unsigned char *)malloc(s->pixels_length);if (!s->pixels) return -3;
    q.flags=s->plan.flags;q.width=s->plan.width;q.height=s->plan.height;q.mip_levels=s->plan.mip_levels;q.scale=s->plan.scale;q.reload=0x80000001u;q.user_data=0;
    status=frame_texture_create(read_data,write_data,s,&q,&s->native.base.base.base.base.services,&s->textures,out);free(s->pixels);s->pixels=0;if (!status) s->texture_count++;return status;
}
static int material_token(void *ctx,const unsigned char *name,uint32_t bytes,uint32_t mode,uint32_t *out)
{
    startup *s=(startup *)ctx;frame_material_services services={texture_load,s};int status=frame_material_resolve(read_data,write_data,s,name,bytes,mode,&s->native.base.base.base.base.services,&services,out);if (!status) s->materials++;return status;
}
static int material(void *ctx,uint32_t name,uint32_t mode,uint32_t *out)
{
    startup *s=(startup *)ctx;uint32_t n;unsigned char token[32];for (n=0;n<sizeof(token);n++) { token[n]=s->ram[name+n];if (!token[n]) break; }if (n==sizeof(token)) return -4;return material_token(ctx,token,n+1u,mode,out);
}
static int sprite_output(void *ctx,uint32_t descriptor)
{
    startup *s=(startup *)ctx;if (descriptor!=0x53d048u || get(s,0x55ae2cu)!=1u || !get(s,descriptor)) return -5;
    s->sprites++;return 0;
}

static int method(void *ctx,uint32_t target,uint32_t entity,uint32_t has_payload,uint32_t payload,uint32_t *out)
{
    frame_native_context *native=(frame_native_context *)ctx;startup *s=(startup *)native->base.base.base.base.data_ctx;
    /* Optional no-opponents setting; normal play dispatches the shared C AI. */
    if (target==0x401bd0u && !s->opponents) { s->ai_deferred++;*out=entity;return 0; }
    int status=frame_native_dispatch(ctx,target,entity,has_payload,payload,out);
    if (status) fprintf(stderr,"native method%x entity%u status%d\n",target,entity,status);
    return status;
}
static int video_quality(void *,uint32_t);
static int load_level(startup *s,const char *level)
{
    frame_session_devices d={0x81000u,video_quality,s};
    uint32_t vehicle=s->vehicle ? s->vehicle : s->cup*5u+1u;
    frame_game_options options={(const unsigned char *)level,(uint32_t)strlen(level)+1u,s->cup,vehicle,vehicle,get(s,0x52ee9cu),&d};uint32_t player;int status;
    s->native.actors.sprite=sprite_output;s->native.actors.sprite_ctx=s;
    status=s->opponents ? frame_game_load_opponents(&s->native,&options,1,&player) : frame_game_load(&s->native,&options,&player);if (!status) s->generations++;return status;
}
static int video_quality(void *ctx,uint32_t quality)
{ (void)ctx;return quality<=10u ? 0 : -5; }
static int step_level(startup *s,uint32_t *action)
{
    frame_session_devices d={0x81000u,video_quality,s};uint32_t result,paused=get(s,0x53e96cu);int status;
    if (get(s,0x52eed8u)!=1u) return -5;
    status=frame_hud_consume_events(read_data,write_data,s);if (status) return status;
    frame_io_clock_advance(&s->clock,16);status=frame_time_update(read_data,write_data,s,frame_io_clock_poll,&s->clock,1);if (status) return status;
    status=frame_time_physics_dt(read_data,write_data,s,&result);if (status) return status;
    status=frame_game_tick_menu_limit(&s->native,&d,s->pause_items,action);if (status) return status;if (!paused) s->physics_frames++;
    return *action ? 0 : frame_results_hud(read_data,write_data,s,0);
}
static void session_keys(startup *s,uint32_t offset)
{ memset(s->ram+0x74b600u,0,512);if (offset) s->ram[0x74b600u+get(s,0x592000u+offset)]=1; }
enum { HOST_RACE_COMPLETE=3 };
static int host_step(startup *s,uint32_t *action)
{
    uint32_t player,record,object;int status=step_level(s,action);if (status) return status;
    /*433D10 has two persistent outputs consumed by the next camera/HUD
     * update. Keep them in the logical frame, independent of GPU redraws. */
    player=get(s,0x53e8b8u);record=get(s,get(s,0x55c694u)+player*8u);object=record ? get(s,record+4u) : 0;
    return object ? frame_vehicle_view_feedback(read_data,write_data,s,object) : -4;
}
static int profile_feedback(void *ctx,uint32_t enabled,uint32_t *acquired)
{ (void)ctx;(void)enabled;*acquired=0;return 0; /* explicit unmounted FF device */ }
static int profile_load(startup *s,const char *path)
{
    unsigned char bytes[FRAME_PROFILE_BYTES];FILE *f;size_t n;int status,close_status;frame_profile_devices d={0x81000u,profile_feedback,s};
    if (!path) return 0;
    f=fopen(path,"rb");if (!f) return errno==ENOENT ? 0 : -1;
    n=fread(bytes,1,sizeof(bytes),f);status=ferror(f);close_status=fclose(f);if (status || close_status || n!=sizeof(bytes)) return -1;
    return frame_profile_decode(read_data,write_data,s,&d,bytes,sizeof(bytes));
}
static void wire_word(unsigned char *p,uint32_t v)
{ unsigned i;for (i=0;i<4u;i++) p[i]=(unsigned char)(v>>(i*8u)); }
static double web_milliseconds(void)
{
#ifdef __EMSCRIPTEN__
    return emscripten_get_now();
#elif defined(_WIN32)
    static LARGE_INTEGER frequency;LARGE_INTEGER now;
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);return 1000.0*(double)now.QuadPart/(double)frequency.QuadPart;
#else
    struct timespec now;timespec_get(&now,TIME_UTC);return (double)now.tv_sec*1000.0+(double)now.tv_nsec/1000000.0;
#endif
}
static int host_fog(void *ctx,uint32_t enabled,uint32_t near_distance,uint32_t far_distance,uint32_t rgba)
{
    startup *s=(startup *)ctx;int status=frame_explosion_fog_host_submit(&s->fog,enabled,near_distance,far_distance,rgba);
    return status;
}

/* Presentation resources never enter the game allocator or material lists. */
static int hud_resources(startup *s)
{
    uint32_t i,pack,offset,length,consumed,n;int status;
    for (i=0;i<FRAME_HUD_TEXTURES;i++) {
        const char *name=frame_hud_resource(i,get(s,0x52ed90u),&pack);frame_surface_info info;unsigned char palette[1024],*pixels;web_texture *t;uint32_t bytes=0;
        if (!name) return -4;
        while (name[bytes]) bytes++;
        status=frame_host_archive_find(s->packs[pack].bytes,s->packs[pack].length,name,bytes,&offset,&length);if (status) return status;
        status=frame_surface_open(s->packs[pack].bytes+offset,length,4,3,3,&info);if (status) return status;
        n=info.width*info.height;pixels=(unsigned char *)malloc(n*4u);if (!pixels) return -3;
        status=frame_surface_decode(s->packs[pack].bytes+offset,length,&info,0,pixels,n*4u,palette,1024,&consumed);
        if (status) { free(pixels);return status; }
        t=(web_texture *)calloc(1,sizeof(*t));if (!t) { free(pixels);return -3; }
        t->rgba=(unsigned char *)malloc(n*4u);if (!t->rgba) { free(t);free(pixels);return -3; }
        t->token=FRAME_HUD_TEXTURE_BASE+i+1u;t->width=info.width;t->height=info.height;
        status=frame_surface_upload_rgba(&info,info.flags,n,pixels,n*4u,palette,1024,t->rgba,n*4u,&t->alpha);if (status) { free(t->rgba);free(t);free(pixels);return status; }
        t->next=s->web_textures;s->web_textures=t;free(pixels);
    }return 0;
}
static int web_texture_alpha(void *ctx,uint32_t token)
{ startup *s=(startup *)ctx;web_texture *t;for (t=s->web_textures;t;t=t->next) if (t->token==token) return (int)t->alpha;return -4; }
static int stream_write(startup *s,const void *bytes,uint32_t n)
{ return s->packet_write ? s->packet_write(s->packet_ctx,bytes,n) : fwrite(bytes,1,n,stdout)==n ? 0 : -1; }
static int stream_frame(startup *s,frame_commands *commands,unsigned char *draws,unsigned char *vertices,unsigned char *wire,void *workspace,uint32_t sequence,uint32_t bits,uint32_t action,double simulation_ms)
{
    frame_present_fog_settings fog={s->fog.enabled,s->fog.near_distance,s->fog.far_distance,s->fog.rgba};
    frame_present_stats stats;frame_present_resources graphics={web_texture_alpha,s,&fog};uint32_t object,player,n,length,total,textures=0;unsigned char header[20];web_texture *t;char json[16384];uint32_t used,lap,total_time,checkpoint,first,record,waypoint,hud_i;int bytes,status;double begin=web_milliseconds(),render_ms;
    status=frame_commands_begin(commands,draws,192u*8192u,vertices,32u*196608u,640,480,sequence);if (status) return status;
    status=frame_present_scene_resources(read_data,s,commands,&graphics,workspace,frame_present_workspace_size(),&stats);if (status) return status;
    status=frame_hud_draw(frame_present_read,workspace,commands);if (status) return status;
    status=frame_overlay_draw(read_data,s,commands,0);if (status) return status;
    if (get(s,0x53e96cu) && !get(s,0x52ee9cu)) { status=frame_hud_pause_draw_limited(read_data,s,commands,s->pause_items);if (status) return status; }
    length=frame_commands_finish(commands,wire,8u*1024u*1024u);if (!length) return -3;wire_word(wire+40u,0xff839eb3u);
    render_ms=web_milliseconds()-begin;
    player=get(s,0x53e8b8u);object=get(s,get(s,get(s,0x55c694u)+player*8u)+4u);
    bytes=snprintf(json,sizeof(json),"{\"frame\":%u,\"graphics_caps\":%u,\"generation\":%u,\"position\":[%d,%d,%d],\"paused\":%u,\"countdown\":%u,\"rng\":%u,\"action\":%u,\"input_bits\":%u,\"elapsed_ms\":%u,\"steer\":%d,\"speed\":%d,\"simulation_ms\":%.4f,\"command_ms\":%.4f,\"meshes\":%u,\"triangles\":%u,\"result_rank\":%u,\"result_time\":%u,\"result_points\":%u}",sequence,get(s,0x5964d4u),s->generations,(int)get(s,object),(int)get(s,object+4u),(int)get(s,object+8u),get(s,0x53e96cu),get(s,0x53e4f0u),get(s,0x55b6acu),action,bits,sequence*16u,(int)get(s,object+0x94u),(int)get(s,object+0xecu),simulation_ms,render_ms,stats.meshes,stats.triangles,get(s,0x590330u),get(s,0x59032cu),get(s,0x59033cu));
    if (bytes<0 || (size_t)bytes>=sizeof(json)) return -3;
    lap=get(s,object+0xcb6u);total_time=get(s,object+0xcbau);for (hud_i=0;hud_i<lap && hud_i<3u;hud_i++) total_time+=get(s,object+0xcbeu+hud_i*7u);
    used=(uint32_t)bytes-1u;
    bytes=snprintf(json+used,sizeof(json)-used,",\"hud\":{\"time_ms\":%u,\"lap_ms\":%u,\"laps_done\":%u,\"rank\":%u,\"weapon\":%u,\"ammo\":%u,\"turret\":%d,\"heading\":[%d,%d],\"route\":[",total_time,get(s,object+0xcbau),lap,s->ram[object+0xddcu],get(s,object+0xd0fu),s->ram[object+0xd13u],(int)(int16_t)(get(s,object+0x762u)&65535u),(int)get(s,object+0x30u),(int)get(s,object+0x38u));
    if (bytes<0 || (size_t)bytes>=sizeof(json)-used) return -3;
    used+=(uint32_t)bytes;checkpoint=get(s,object+0x790u);first=checkpoint;
    for (hud_i=0;checkpoint && hud_i<256u;hud_i++) {
        record=get(s,get(s,0x55c694u)+checkpoint*8u);if (!record) break;waypoint=get(s,record+4u);if (!waypoint) break;
        bytes=snprintf(json+used,sizeof(json)-used,"%s[%d,%d]",hud_i ? "," : "",(int)get(s,waypoint),(int)get(s,waypoint+8u));
        if (bytes<0 || (size_t)bytes>=sizeof(json)-used) return -3;
        used+=(uint32_t)bytes;checkpoint=get(s,waypoint+0x78u);if (checkpoint==first) break;
    }
    if (sizeof(json)-used<4u) return -3;
    memcpy(json+used,"]},\"texture_alpha\":{",20);used+=20u;
    { uint32_t items=0;for (t=s->web_textures;t;t=t->next) if (!t->sent) { bytes=snprintf(json+used,sizeof(json)-used,"%s\"%u\":%u",items++ ? "," : "",t->token,t->alpha);if (bytes<0 || (size_t)bytes>=sizeof(json)-used) return -3;used+=(uint32_t)bytes; } }
    if (sizeof(json)-used<3u) return -3;
    memcpy(json+used,"}}",3);bytes=(int)used+2;

    total=20u+(uint32_t)bytes+length;
    for (t=s->web_textures;t;t=t->next) if (!t->sent) { textures++;total+=16u+t->width*t->height*4u; }
    memcpy(header,"NWF1",4);wire_word(header+4,total);wire_word(header+8,(uint32_t)bytes);wire_word(header+12,length);wire_word(header+16,textures);
    if (stream_write(s,header,20) || stream_write(s,json,(uint32_t)bytes) || stream_write(s,wire,length)) return -1;
    for (t=s->web_textures;t;t=t->next) if (!t->sent) { n=t->width*t->height*4u;wire_word(header,t->token);wire_word(header+4,t->width);wire_word(header+8,t->height);wire_word(header+12,n);if (stream_write(s,header,16) || stream_write(s,t->rgba,n)) return -1;t->sent=1; }
    return s->packet_write ? 0 : fflush(stdout) ? -1 : 0;
}
