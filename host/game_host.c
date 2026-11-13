/* Browser host: shared native host services, full game tick and NWF1/TCB1.
 * All assets are explicit immutable memory mounts; no original PE/dump needed.
 * This wrapper also builds natively for same-input transport parity tests. */
#include "game_host.h"
#include "runtime.inc"

typedef struct browser_host {
    startup s;
    unsigned char *draws,*vertices,*wire,*packet;
    void *workspace;
    uint32_t length,capacity,sequence,bits,action,map,loaded;
    void *audio;
} browser_host;
static browser_host browser;
static const char *browser_maps[]={"ENGLAND","MEXICO","SWAMP","EAST","THEME","BIO","MONACO","CIRCUIT"};
static const uint32_t browser_tail[3]={0,0,0},browser_mine[4]={0,0,0,0};
static const int32_t browser_miss[3]={0,0,0};
static void menu_profile_to_game(startup *s);
static int menu_is_active(void);

static int browser_write(void *ctx,const void *bytes,uint32_t n)
{
    browser_host *b=(browser_host *)ctx;uint32_t capacity;unsigned char *p;
    if (n>64u*1024u*1024u-b->length) return -3;
    if (b->length+n>b->capacity) {
        capacity=b->capacity ? b->capacity : 1048576u;
        while (capacity<b->length+n) capacity*=2u;
        p=(unsigned char *)realloc(b->packet,capacity);if (!p) return -3;b->packet=p;b->capacity=capacity;
    }
    memcpy(b->packet+b->length,bytes,n);b->length+=n;return 0;
}
void tank_destroy(void)
{
    browser_host *b=&browser;startup *s=&b->s;uint32_t i;
    if (b->loaded) { frame_session_devices d={0x81000u,video_quality,s};(void)frame_session_end(read_data,write_data,s,&s->native.base.base.base.base.services,&s->textures,&d); }
    web_textures_clear(s);for (i=0;i<7u;i++) free(s->packs[i].bytes);
    free(s->blocks);free(s->ram);free(b->draws);free(b->vertices);free(b->wire);free(b->packet);free(b->workspace);free(b->audio);memset(b,0,sizeof(*b));
}
int tank_create(uint32_t map,uint32_t cup,uint32_t vehicle,uint32_t mode,uint32_t opponents)
{
    browser_host *b=&browser;startup *s=&b->s;tank_destroy();
    if (map>=8u || cup>2u || vehicle>15u || mode>1u || opponents>1u) return -1;
    b->map=map;s->pause_items=menu_is_active() ? 3u : 0u;s->cup=cup;s->vehicle=vehicle;s->mode=mode;s->opponents=opponents;s->streaming=1;s->next=0x100000u;
    s->blocks=(host_block *)calloc(16384,sizeof(host_block));s->ram=(unsigned char *)calloc(RAM_BYTES,1);
    b->draws=(unsigned char *)malloc(192u*8192u);b->vertices=(unsigned char *)malloc(32u*196608u);b->wire=(unsigned char *)malloc(8u*1024u*1024u);b->workspace=malloc(frame_present_workspace_size());b->audio=malloc(frame_audio_size());
    if (!s->blocks || !s->ram || !b->draws || !b->vertices || !b->wire || !b->workspace || !b->audio) { tank_destroy();return -3; }
    frame_audio_initialize(b->audio,read_data,s,cup);
    s->packet_write=browser_write;s->packet_ctx=b;return 0;
}
int tank_mount(uint32_t slot,const unsigned char *bytes,uint32_t length)
{
    browser_host *b=&browser;mounted *m;uint32_t off,n;const char *bundle=0;char level_bundle[32];int status;
    static const char *bundles[]={0,"TANKB.SS","TANKS.SS","TANKG.SS","INGAME.SS"};
    if (!b->s.ram || b->loaded || slot>=7u || length<28u || length>128u*1024u*1024u) return -1;
    m=&b->s.packs[slot];if (m->bytes) return -1;
    if (slot<5u) bundle=bundles[slot];else if (slot==5u) { snprintf(level_bundle,sizeof(level_bundle),"%s.SS",browser_maps[b->map]);bundle=level_bundle; }
    if (bundle) { status=frame_host_archive_find(bytes,length,bundle,(uint32_t)strlen(bundle),&off,&n);if (status) return status; }
    m->bytes=(unsigned char *)malloc(length);if (!m->bytes) return -3;memcpy(m->bytes,bytes,length);m->length=length;
    if (bundle) { m->ss=m->bytes+off;m->ss_length=n; }return 0;
}
int tank_start(void)
{
    browser_host *b=&browser;startup *s=&b->s;frame_entity_factory_services *e;uint32_t i;int status;
    if (!s->ram || b->loaded) return -1;
    for (i=0;i<7u;i++) if (!s->packs[i].bytes) return -1;
    s->textures.device=texture_device;s->textures.device_ctx=s;s->textures.reload=texture_reload;s->textures.reload_ctx=s;memcpy(s->ram+0x536738u,".sur",5);
    s->resources.fetch=fetch;s->resources.file_ctx=s;s->resources.material=material;s->resources.material_ctx=s;
    e=&s->native.base.base.base.base.services;e->allocate=allocate;e->zero_allocate=zero_allocate;e->release=release;e->system_ctx=s;e->work_limit=1000000;
    e->sound=frame_audio_cue;e->sound_ctx=b->audio;
    s->native.base.base.base.base.read=read_data;s->native.base.base.base.base.write=write_data;s->native.base.base.base.base.data_ctx=s;
    s->native.base.base.base.parameters.trig=frame_math_trig();s->native.base.base.base.parameters.shapes=&s->resources;s->native.base.base.base.parameters.material=material_token;s->native.base.base.base.parameters.material_ctx=s;s->native.base.base.base.parameters.matrix_tail=browser_tail;s->native.base.base.shatter.trig=frame_math_trig();s->native.camera_matrix_tail=browser_tail;
    s->native.explosion.fog=host_fog;s->native.explosion.fog_ctx=s;s->native.explosion.mine_collision_words=browser_mine;s->native.inactive_miss_words=browser_miss;
    status=frame_native_bind(&s->native);if (status) return status;e->method=method;
    set(s,0x54b3d8u,1);set(s,0x534d24u,UINT32_MAX);set(s,0x534d2cu,10000000);set(s,0x56021cu,2);
    status=frame_game_initialize(&s->native);if (status) return status;menu_profile_to_game(s);set(s,0x5964d4u,0xecffu);
    for (i=0;i<25u;i++) set(s,0x592000u+i*4u,i+16u);
    set(s,0x81004u,32767);status=profile_load(s,0);if (status) return status;set(s,0x52ee9cu,s->mode);
    status=load_level(s,browser_maps[b->map]);if (status) return status;b->loaded=1;frame_io_clock_init(&s->clock,1);
    status=hud_resources(s);if (status) return status;return frame_present_workspace_initialize(b->workspace,frame_present_workspace_size());
}
int tank_step(uint32_t frames,uint32_t bits,uint32_t focused)
{
    browser_host *b=&browser;startup *s=&b->s;uint32_t i,j,code;int status;double begin,simulation_ms=0;
    static const uint32_t offsets[]={0x14,0x18,4,8,0x30,0x40,0x54,0x4c,0x50,0x5c,0,0,0x28,0x2c,0x34,0x38,0x3c,0x24};
    if (!b->loaded || frames>8u || bits>262143u || focused>1u) return -1;
    frame_audio_begin(b->audio);
    /* The native stream publishes an action before performing its lifecycle. */
    if (b->action==FRAME_SESSION_RESTART) {
        frame_session_devices d={0x81000u,video_quality,s};status=frame_session_prepare_restart(read_data,write_data,s,&s->native.base.base.base.base.services,&s->textures,&d);if (status) return status;
        web_textures_clear(s);frame_audio_initialize(b->audio,read_data,s,s->cup);s->reuse=1;status=load_level(s,browser_maps[b->map]);if (status) return status;
        status=hud_resources(s);if (status) return status;status=frame_present_workspace_initialize(b->workspace,frame_present_workspace_size());if (status) return status;
    } else if (b->action) return -5;
    b->action=0;if (!focused) bits&=3072u;b->bits=bits;session_keys(s,0);
    for (j=0;j<18u;j++) if (offsets[j] && (bits&(1u<<j))) { code=get(s,0x592000u+(get(s,0x53e96cu) && j<2u ? 0x44u+j*4u : offsets[j]));if (code>=512u) return -4;s->ram[0x74b600u+code]=1; }
    for (i=0;i<frames;i++) {
        if (!i && (bits&1024u)) set(s,0x53e988u,1);
        if (!i && (bits&2048u)) set(s,0x53e984u,1);
        uint32_t old_pause=get(s,0x53e96cu),old_selection=get(s,0x53e970u),old_setting=get(s,0x53e978u),old_confirm=get(s,0x53e974u);
        begin=web_milliseconds();status=host_step(s,&b->action);simulation_ms+=web_milliseconds()-begin;if (status) return status;b->sequence++;status=frame_audio_tick(b->audio);if (status) return status;
        if (menu_is_active()) { if (old_pause && old_selection!=get(s,0x53e970u)) frame_audio_gui(b->audio,0x5179c0u,get(s,0x53e970u));if (old_setting!=get(s,0x53e978u) || old_confirm!=get(s,0x53e974u) || old_pause!=get(s,0x53e96cu)) frame_audio_gui(b->audio,0x5187e0u,0); }
        if (b->action==HOST_RACE_COMPLETE) { uint32_t result;status=frame_results_collect(read_data,write_data,s,&result);if (status) return status;if (!menu_is_active()) { status=frame_results_initials(read_data,write_data,s,0);if (status) return status; } }
        if (b->action) break;
    }
    b->length=0;return stream_frame(s,&(frame_commands){0},b->draws,b->vertices,b->wire,b->workspace,b->sequence,bits,b->action,simulation_ms);
}
const unsigned char *tank_packet(void) { return browser.packet; }
uint32_t tank_packet_size(void) { return browser.length; }
const unsigned char *tank_data(void) { return browser.s.ram; }
uint32_t tank_audio_gain(void) { startup *s=&browser.s;return get(s,0x53e96cu) ? get(s,0x53e50cu)&65535u : get(s,0x81004u)&65535u; }
const frame_audio_command *tank_audio(void) { return frame_audio_commands(browser.audio); }
uint32_t tank_audio_count(void) { return frame_audio_count(browser.audio); }
int tank_advance(uint32_t map,const unsigned char *bytes,uint32_t length,uint32_t opponents)
{
    browser_host *b=&browser;startup *s=&b->s;frame_session_devices d={0x81000u,video_quality,s};uint32_t next;int status;
    if (!b->loaded || b->action!=HOST_RACE_COMPLETE || map>=8u || opponents>1u || get(s,0x590408u) || !get(s,0x5903fcu)) return -1;
    next=get(s,0x53e3f8u);if (next>=6u || s->ram[0x52ee88u+s->cup*6u+next]!=map) return -1;
    status=frame_session_prepare_restart(read_data,write_data,s,&s->native.base.base.base.base.services,&s->textures,&d);if (status) return status;
    b->loaded=0;web_textures_clear(s);free(s->packs[5].bytes);memset(&s->packs[5],0,sizeof(s->packs[5]));b->map=map;s->opponents=opponents;
    status=tank_mount(5,bytes,length);if (status) return status;menu_profile_to_game(s);frame_audio_initialize(b->audio,read_data,s,s->cup);s->reuse=1;status=load_level(s,browser_maps[map]);if (status) return status;
    b->loaded=1;b->action=0;status=hud_resources(s);if (status) return status;return frame_present_workspace_initialize(b->workspace,frame_present_workspace_size());
}
#include "frontend.inc"
