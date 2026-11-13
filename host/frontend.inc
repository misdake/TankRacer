/* Browser I/O adapter for the readable original GUI. All menu state and
 * geometry are produced by frame_menu; JS only forwards keys and pixels. */
#include "frame_menu.h"
typedef struct frontend_host {
    startup s;unsigned char *pack,*draws,*vertices,*wire,*packet;
    uint32_t pack_bytes,packet_bytes,packet_capacity,dimensions[128],sequence,ready;
    frame_menu_quad *quads;
    void *audio;
} frontend_host;
static frontend_host frontend;
static void menu_sound(void *ctx,uint32_t cue,uint32_t variant) { frame_audio_gui(ctx,cue,variant); }
static int menu_is_active(void) { return frontend.ready!=0; }
static int menu_output(void *ctx,const void *p,uint32_t bytes)
{
    frontend_host *f=(frontend_host *)ctx;unsigned char *next;uint32_t capacity;
    if (bytes>16u*1024u*1024u-f->packet_bytes) return -3;
    if (f->packet_bytes+bytes>f->packet_capacity) { capacity=f->packet_bytes+bytes+1048576u;next=(unsigned char *)realloc(f->packet,capacity);if (!next) return -3;f->packet=next;f->packet_capacity=capacity; }
    memcpy(f->packet+f->packet_bytes,p,bytes);f->packet_bytes+=bytes;return 0;
}
static void menu_profile_to_game(startup *s)
{
    if (!frontend.ready) return;
    /* Exactly the original profile's0x486 words; CUP points are session data. */
    memcpy(s->ram+0x53d190u,frontend.s.ram+0x53d190u,0x486u*4u);
    memcpy(s->ram+0x530458u,frontend.s.ram+0x530458u,8);
}
static int menu_textures(frontend_host *f)
{
    uint32_t i,identity,off,n,count,consumed;int status;startup *s=&f->s;
    if (frame_menu_resources()>128u) return -4;
    for (i=0;i<frame_menu_resources();i++) {
        const char *name=frame_menu_resource(i,&identity);frame_surface_info info;unsigned char palette[1024],*pixels;web_texture *t;
        status=frame_host_archive_find(f->pack,f->pack_bytes,name,(uint32_t)strlen(name),&off,&n);if (status) return status;
        status=frame_surface_open(f->pack+off,n,4,3,3,&info);if (status) return status;count=info.width*info.height;
        pixels=(unsigned char *)malloc(count*4u);t=(web_texture *)calloc(1,sizeof(*t));if (!pixels || !t) { free(pixels);free(t);return -3; }
        t->rgba=(unsigned char *)malloc(count*4u);if (!t->rgba) { free(t);free(pixels);return -3; }
        status=frame_surface_decode(f->pack+off,n,&info,0,pixels,count*4u,palette,1024,&consumed);
        if (!status) status=frame_surface_upload_rgba(&info,info.flags,count,pixels,count*4u,palette,1024,t->rgba,count*4u,&t->alpha);
        free(pixels);if (status) { free(t->rgba);free(t);return status; }
        t->token=FRAME_MENU_TEXTURE_BASE+i+1u;t->width=info.width;t->height=info.height;f->dimensions[i]=info.width;t->next=s->web_textures;s->web_textures=t;
    }
    return 0;
}
int tank_menu_create(const unsigned char *bytes,uint32_t length)
{
    frontend_host *f=&frontend;uint32_t off,n;int status;if (f->ready) return 0;
    if (!bytes || length<28u) return -1;
    f->s.ram=(unsigned char *)calloc(RAM_BYTES,1);f->pack=(unsigned char *)malloc(length);f->draws=(unsigned char *)malloc(192u*8192u);f->vertices=(unsigned char *)malloc(32u*32768u);f->wire=(unsigned char *)malloc(4u*1024u*1024u);f->quads=(frame_menu_quad *)malloc(sizeof(frame_menu_quad)*8192u);
    if (!f->s.ram || !f->pack || !f->draws || !f->vertices || !f->wire || !f->quads) return -3;
    memcpy(f->pack,bytes,length);f->pack_bytes=length;
    status=frame_host_archive_find(bytes,length,"GUIPCENG.GUI",12,&off,&n);if (status) return status;
    status=frame_menu_load(read_data,write_data,&f->s,bytes+off,n);if (status) return status;
    status=frame_menu_bootstrap(write_data,&f->s);if (status) return status;
    status=frame_results_constants(write_data,&f->s);if (status) return status;status=frame_results_defaults(write_data,&f->s);if (status) return status;
    set(&f->s,0x53044cu,1);set(&f->s,0x53e424u,1);set(&f->s,0x530458u,0x002d2d2du);set(&f->s,0x53045cu,0x002d2d2du);
    status=menu_textures(f);if (status) return status;f->audio=malloc(frame_audio_size());if (!f->audio) return -3;frame_audio_initialize(f->audio,read_data,&f->s,0);frame_io_clock_init(&f->s.clock,1);set(&f->s,0x53d174u,1);f->ready=1;return frame_menu_refresh(read_data,write_data,&f->s,2);
}
int tank_menu_open(void)
{
    frontend_host *f=&frontend;web_texture *t;if (!f->ready) return -1;
    set(&f->s,0x5376b0u,1);set(&f->s,0x562b90u,2);set(&f->s,0x562bb4u,0);set(&f->s,0x5884c0u,(uint32_t)-640);set(&f->s,0x5884c4u,(uint32_t)-416);set(&f->s,0x53e94cu,0);memset(f->s.ram+0x588dd4u,0,0xd4);
    for (t=f->s.web_textures;t;t=t->next) t->sent=0;
    return frame_menu_refresh(read_data,write_data,&f->s,2);
}
int tank_menu_finish(void)
{
    frontend_host *f=&frontend;startup *s=&browser.s;web_texture *t;if (!f->ready || !browser.loaded || browser.action!=HOST_RACE_COMPLETE) return -1;
    memcpy(f->s.ram+0x53d190u,s->ram+0x53d190u,0x53e404u-0x53d190u);memcpy(f->s.ram+0x59032cu,s->ram+0x59032cu,0x59053cu-0x59032cu);memcpy(f->s.ram+0x530458u,s->ram+0x530458u,8);set(&f->s,0x53d18cu,s->cup);set(&f->s,0x530440u,s->vehicle);
    set(&f->s,0x53e954u,s->cup==0u ? 4u : s->cup==1u ? 27u : 29u);
    for (t=f->s.web_textures;t;t=t->next) t->sent=0;
    return frame_menu_results_begin(read_data,write_data,&f->s);
}
int tank_menu_unlock(void)
{
    frontend_host *f=&frontend;uint32_t i;int status;if (!f->ready) return -1;
    /* Explicit browser action: unlock the three CUP classes and their fifth
     * vehicles. Other bonus bits also change gameplay; do not enable those. */
    set(&f->s,0x53d190u,2);
    for (i=0;i<3u;i++) { f->s.ram[0x53d194u+i*2u]=1;f->s.ram[0x53d195u+i*2u]=0; }
    set(&f->s,0x53e400u,1);
    for (i=0;i<3u;i++) { status=frame_menu_refresh(read_data,write_data,&f->s,i==0u ? 3u : i==1u ? 26u : 28u);if (status) return status; }
    return frame_menu_refresh(read_data,write_data,&f->s,2);
}
int tank_menu_step(uint32_t bits,uint32_t dt,int32_t typed)
{
    frontend_host *f=&frontend;startup *s=&f->s;frame_commands commands={0};frame_menu_draws plan={f->quads,0,8192,0,0};web_texture *t;uint32_t event,length,n,textures=0,total,used,map,cup;unsigned char header[20];char json[4096];int status,written;
    if (!f->ready || (dt && dt<10u) || dt>1000u) return -1;
    if (dt) {
        uint32_t continuation;frame_io_clock_advance(&s->clock,dt);
        status=frame_time_update(read_data,write_data,s,frame_io_clock_poll,&s->clock,1);if (status) return status;
        status=frame_time_physics_dt(read_data,write_data,s,&continuation);if (status) return status;dt=get(s,0x5904fcu);
    }
    status=frame_menu_sample_input(read_data,write_data,s,bits,&event);if (status) return status;
    frame_audio_begin(f->audio);
    status=frame_menu_update_audio(read_data,write_data,s,event,dt,typed,menu_sound,f->audio);if (status) return status;
    status=frame_menu_draw_browser(read_data,s,&plan);if (status) return status;
    set(s,0x588514u,plan.pulse);
    status=frame_commands_begin(&commands,f->draws,192u*8192u,f->vertices,32u*32768u,640,480,++f->sequence);if (status) return status;
    status=frame_menu_submit(&plan,f->dimensions,&commands);if (status) return status;
    length=frame_commands_finish(&commands,f->wire,4u*1024u*1024u);if (!length) return -3;wire_word(f->wire+40u,0xff000000u);
    cup=get(s,0x53d18cu);map=s->ram[0x52ee88u+cup*6u+get(s,0x53e3f8u)];
    written=snprintf(json,sizeof(json),"{\"frontend\":true,\"frame\":%u,\"generation\":0,\"page\":%u,\"phase\":%u,\"selection\":%u,\"cup\":%u,\"vehicle\":%u,\"map\":%u,\"start\":%u,\"continuation\":%u,\"texture_alpha\":{",f->sequence,get(s,0x562b90u),get(s,0x5376b0u),get(s,0x562bb4u),cup,get(s,0x530440u),map,get(s,0x53e94cu),get(s,0x53e3f8u)>0u);
    if (written<0 || (size_t)written>=sizeof(json)) return -3;
    used=(uint32_t)written;
    for (t=s->web_textures;t;t=t->next) if (!t->sent) { written=snprintf(json+used,sizeof(json)-used,"%s\"%u\":%u",textures++ ? "," : "",t->token,t->alpha);if (written<0 || (size_t)written>=sizeof(json)-used) return -3;used+=(uint32_t)written; }
    if (used+3u>sizeof(json)) return -3;
    written=snprintf(json+used,sizeof(json)-used,"},\"unlocked\":%u}",get(s,0x53d190u)==2u && s->ram[0x53d194u] && s->ram[0x53d196u] && s->ram[0x53d198u]);if (written<0 || (size_t)written>=sizeof(json)-used) return -3;used+=(uint32_t)written;total=20u+used+length;
    for (t=s->web_textures;t;t=t->next) if (!t->sent) total+=16u+t->width*t->height*4u;
    f->packet_bytes=0;memcpy(header,"NWF1",4);wire_word(header+4,total);wire_word(header+8,used);wire_word(header+12,length);wire_word(header+16,textures);
    if (menu_output(f,header,20) || menu_output(f,json,used) || menu_output(f,f->wire,length)) return -3;
    for (t=s->web_textures;t;t=t->next) if (!t->sent) { n=t->width*t->height*4u;wire_word(header,t->token);wire_word(header+4,t->width);wire_word(header+8,t->height);wire_word(header+12,n);if (menu_output(f,header,16) || menu_output(f,t->rgba,n)) return -3;t->sent=1; }
    return 0;
}
const unsigned char *tank_menu_packet(void) { return frontend.packet; }
uint32_t tank_menu_packet_size(void) { return frontend.packet_bytes; }
const unsigned char *tank_menu_data(void) { return frontend.s.ram; }
const frame_audio_command *tank_menu_audio(void) { return frame_audio_commands(frontend.audio); }
uint32_t tank_menu_audio_count(void) { return frame_audio_count(frontend.audio); }
