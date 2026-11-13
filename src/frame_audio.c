#include "frame_audio.h"
#include "physics_memory.h"
#include <string.h>
#define AUDIO_COMMANDS 1024u
#define AUDIO_VOICES 128u
typedef struct voice { uint32_t key,object,sample,active,frequency; } voice;
typedef struct car_state { uint32_t object,edges;int32_t reverse_ms; } car_state;
typedef struct audio {
    physics_memory m;uint32_t cup,count,stage,serial,overflow;
    voice voices[AUDIO_VOICES];frame_audio_command commands[AUDIO_COMMANDS];
    car_state cars[16];
    uint32_t ambience[64];
} audio;
size_t frame_audio_size(void) { return sizeof(audio); }
void frame_audio_initialize(void *ctx,frame_data_read read,void *data,uint32_t cup)
{ audio *a=(audio *)ctx;memset(a,0,sizeof(*a));a->m=(physics_memory){read,0,data,0};a->cup=cup;a->stage=UINT32_MAX; }
void frame_audio_begin(void *ctx) { audio *a=(audio *)ctx;a->count=0;a->overflow=0;a->m.error=0; }
static void emit(audio *a,uint32_t operation,uint32_t key,uint32_t sample,uint32_t flags,uint32_t gain,uint32_t frequency,uint32_t o,const int32_t *position)
{
    frame_audio_command *c;uint32_t i;if (a->count>=AUDIO_COMMANDS) { a->overflow=1;return; }
    c=&a->commands[a->count++];*c=(frame_audio_command){operation,key,sample,flags,gain,frequency,{0,0,0}};
    for (i=0;i<3u;i++) c->position[i]=position ? position[i] : o ? pm_i32(&a->m,o+i*4u) : 0;
}
static void loop(audio *a,uint32_t o,uint32_t lane,uint32_t sample,uint32_t gain,uint32_t frequency,uint32_t flags,int enabled)
{
    uint32_t i,key=o*8u+lane;voice *v=0;
    for (i=0;i<AUDIO_VOICES;i++) if (a->voices[i].key==key) { v=&a->voices[i];break; }
    if (!v && enabled) for (i=0;i<AUDIO_VOICES;i++) if (!a->voices[i].active) { v=&a->voices[i];*v=(voice){key,o,sample,0,frequency};break; }
    if (!v) return;
    if (!enabled) { if (v->active) emit(a,0,key,0,0,0,0,0,0);v->active=0;return; }
    v->active=1;v->frequency=frequency;v->sample=sample;emit(a,2,key,sample,flags|1u,gain,frequency,o,0);
}
static uint32_t clamp_gain(int32_t x) { return x<0 ? 0u : x>65535 ? 65535u : (uint32_t)x; }
void frame_audio_gui(void *ctx,uint32_t cue,uint32_t variant)
{
    audio *a=(audio *)ctx;const char *name=0;uint32_t sample=2166136261u,i,frequency=0;
    /*5179C0: the selected button index changes pitch by1000Hz. */
    if (cue==0x5179c0u) { name="select15";frequency=(22050u+variant*1000u)&65535u; }
    else if (cue==0x5187e0u) name="select7";
    else if (cue==0x517990u) name="select8";
    else if (cue==0x5186c0u) name="select12";
    else if (cue==0x519990u) name="select9";
    else if (cue==0x51a930u) name="select13";
    else if (cue==0x516820u) name="select14";
    else if (cue==0x517a20u) name="engine1";
    else if (cue==0x517a50u) { emit(a,0,0xf0000001u,0,0,0,0,0,0);return; }
    if (!name) return;
    for (i=0;name[i];i++) sample=(sample^(uint32_t)(unsigned char)name[i])*16777619u;
    emit(a,cue==0x517a20u ? 2u : 1u,cue==0x517a20u ? 0xf0000001u : 0u,sample,0x100u|(cue==0x517a20u),65535u,frequency,0,0);
}
static int32_t length(physics_memory *m,uint32_t at)
{
    int32_t x=pm_abs(pm_i32(m,at)),y=pm_abs(pm_i32(m,at+4u)),z=pm_abs(pm_i32(m,at+8u));
    if (y>x && y>z) return pm_add(y,pm_add(pm_sar(x,2),pm_sar(z,2)));
    if (x>=y && x>z) return pm_add(x,pm_add(pm_sar(y,2),pm_sar(z,2)));
    return pm_add(z,pm_add(pm_sar(x,2),pm_sar(y,2)));
}
static void edge(audio *a,car_state *s,uint32_t bit,uint32_t sample,uint32_t gain,int on)
{ if (on && !(s->edges&bit)) emit(a,1,0,sample,2u,gain,0,s->object,0);if (on) s->edges|=bit;else s->edges&=~bit; }
static void vehicle(audio *a,uint32_t o)
{
    physics_memory *m=&a->m;int32_t rpm,v,slip,frequency,impact;int active;uint32_t flags,i;car_state *s=0;
    for (i=0;i<16u;i++) if (a->cars[i].object==o) { s=&a->cars[i];break; }
    if (!s) for (i=0;i<16u;i++) if (!a->cars[i].object) { s=&a->cars[i];s->object=o;break; }
    /*40F910..40FA62: abs(B0), or idle A8 for inactive first200 frames;
     * exact x86 integer pitch, not speed mapped by the browser. */
    rpm=pm_abs(pm_i32(m,o+(!pm_load(m,o+0xdeau,1) && pm_load(m,0x590564u,4)<=200u ? 0xa8u : 0xb0u)));
    frequency=pm_add(12000,pm_sar(pm_mul(rpm,1700),14));
    flags=pm_load(m,o+0xde3u,1)>=2u ? 0x1fu : 0x17u;
    loop(a,o,0,a->cup+1u,65535u,(uint32_t)frequency&65535u,flags,1);
    active=pm_load(m,o+0xdedu,1)!=0;
    /*40FC5F..40FD25: water / flagged terrain. */
    loop(a,o,1,0x53e458u,clamp_gain(pm_sar(rpm,2)),0,3u,active&&(pm_load(m,o+0xdeeu,1)||pm_i32(m,o+0x716u)==3||pm_i32(m,o+0x71au)==3));
    /*40FD25..40FDF5 shield spin. The original voice starts at22050Hz. */
    frequency=22050;
    { uint32_t i;for (i=0;i<AUDIO_VOICES;i++) if (a->voices[i].key==o*8u+2u && a->voices[i].active) { frequency=(int32_t)a->voices[i].frequency;break; } }
    if ((pm_load(m,o+0xd99u,1)&4u) && frequency>15000) frequency-=50;
    else if ((pm_load(m,o+0xd99u,1)&1u) && frequency<30000) frequency+=50;
    loop(a,o,2,0x53e474u,65535u,(uint32_t)frequency,0x13u,active&&pm_i32(m,o+0xdafu)>0);
    loop(a,o,3,0x5904f8u,65535u,0,3u,active&&pm_load(m,o+0xd0au,1)!=0u);
    /*40FE8B..40FFDC: road skid magnitude starts above50000. */
    v=pm_i32(m,o+0xc0u);slip=pm_abs(v);
    loop(a,o,4,0x53e448u,clamp_gain((slip-50000)/2),0,3u,active&&v&&(pm_i32(m,o+0x716u)==1||pm_i32(m,o+0x71au)==1)&&!(pm_load(m,0x53e414u,4)&&pm_load(m,o+0xde5u,1)));
    if (!s || !active) return;
    /*40FAE4..40FB78: one shot on the horn's rising edge, not a loop.
     *40CCD6 selects horn((player vehicle-1)%5+1); in single player the
     *second/rival resource reuses that same name. Sound handles stay private. */
    {
        uint32_t down=pm_load(m,o+0xd03u,1),sample=2166136261u;
        unsigned char name[6]={'h','o','r','n','1',0};
        name[4]=(unsigned char)('1'+pm_sub(pm_i32(m,0x530440u),1)%5);
        if (pm_load(m,0x53e410u,4)) { const unsigned char *alternate=(const unsigned char *)"humhorn";for (i=0;alternate[i];i++) sample=(sample^alternate[i])*16777619u; }
        else for (i=0;name[i];i++) sample=(sample^name[i])*16777619u;
        if (down && !(s->edges&64u)) emit(a,1,0x80000000u|o,sample,pm_i8(m,o+0xde3u)>=2 ? 10u : 2u,65535u,0,o,0);
        if (down) s->edges|=64u;else s->edges&=~64u;
    }
    /*40FFDC..410226: contact/obstacle and tank-impact entry latches.
     * These private latches replace only audio D05..D08, never game fields. */
    impact=pm_i32(m,o+0xf0u);edge(a,s,16u,0x53e478u,clamp_gain(pm_sar(impact-6000000,6)),impact>6000000);
    impact=length(m,o+0xfcu)-100000;if (impact<0) impact=0;
    edge(a,s,1u,0x53e47cu,clamp_gain(pm_sar(impact,3)),impact>0);
    edge(a,s,2u,0x53e484u,clamp_gain(impact/16),impact>300000);
    impact=length(m,o+0x108u)-100000;if (impact<0) impact=0;
    edge(a,s,4u,0x53e480u,clamp_gain(pm_sar(impact,2)),impact>0);
    edge(a,s,8u,0x53e488u,clamp_gain(pm_mul(impact,3)/16),impact>200000);
    edge(a,s,32u,0x53e490u,clamp_gain(pm_sar(rpm,2)),pm_i32(m,o+0x716u)==8||pm_i32(m,o+0x71au)==8);
    if (pm_i32(m,o+0x94u)<0 && !pm_i32(m,o+0x98u)) {
        if (s->reverse_ms<pm_i32(m,0x5904fcu)) { emit(a,1,o*8u+5u,0x53e494u,2u,65535u,0,o,0);s->reverse_ms=1000; }
        else s->reverse_ms-=pm_i32(m,0x5904fcu);
    } else s->reverse_ms=0;
}
void frame_audio_cue(void *ctx,uint32_t cue,uint32_t o,uint32_t variant,const int32_t *position,const unsigned char *name)
{
    audio *a=(audio *)ctx;uint32_t sample=0,key=0;
    if (cue==0x41b740u) {
        uint32_t hash=2166136261u,i,ch;if (variant>=64u || !name) return;
        for (i=0;i<128u && name[i] && name[i]!='.';i++) { ch=name[i];if (ch>='A' && ch<='Z') ch+=32;hash=(hash^ch)*16777619u; }
        a->ambience[variant]=hash;return;
    }
    if (cue==0x41b870u) {
        int32_t index=pm_i32(&a->m,o+0x7cu);uint32_t count=a->count,i;
        sample=index>=0 && index<64 ? a->ambience[index] : 0;
        loop(a,o,0,sample,65535u,0,3u,sample!=0);
        if (sample && a->count>count) for (i=0;i<3u;i++) a->commands[a->count-1u].position[i]=pm_i32(&a->m,0x544e18u+(uint32_t)index*20u+i*4u);
        return;
    }
    if (cue==0x40f8f0u) { vehicle(a,o);return; }
    if (cue==0x40f5b0u) {
        /* Original jump table40F67C.8 returns without submitting a voice. */
        if (variant==8u) return;
        sample=variant==1u||variant==2u ? 0x53e464u : variant==3u ? 0x53e46cu : variant==5u ? 0x53e468u : variant==7u ? 0x53e470u : 0x53e450u;
        if (o && variant!=1u && variant!=7u) key=o*8u+7u;
    } else if (cue==0x40f6a0u) { sample=0x5904f0u;key=o*8u+6u; }
    else if (cue==0x40f720u) sample=0x53e454u;
    if (sample) emit(a,1,key,sample,2u,65535u,0,o,position);
}
int frame_audio_tick(void *ctx)
{
    audio *a=(audio *)ctx;physics_memory *m=&a->m;uint32_t stage,countdown=pm_load(m,0x53e4f0u,4);int32_t position[3];uint32_t i;
    if (!pm_load(m,0x53e96cu,4)) {
        stage=countdown>2000u ? 3u : countdown>1000u ? 2u : countdown ? 1u : 0u;
        if (stage!=a->stage) { emit(a,1,0,stage ? 0x53e45cu : 0x53e460u,0,65535u,0,0,0);a->stage=stage; }
    }
    for (i=0;i<3u;i++) position[i]=pm_mul(pm_i32(m,0x74aee0u+i*4u),8);
    emit(a,3,0,0,0,0,0,0,position);
    /* Listener orientation is sent separately in the host's float-free API. */
    for (i=0;i<3u;i++) position[i]=pm_i32(m,0x74af18u+i*4u);
    emit(a,4,0,0,0,0,0,0,position);
    return m->error ? m->error : a->overflow ? -3 : 0;
}
uint32_t frame_audio_count(void *ctx) { return ((audio *)ctx)->count; }
const frame_audio_command *frame_audio_commands(void *ctx) { return ((audio *)ctx)->commands; }
