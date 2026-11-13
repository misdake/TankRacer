#include "frame_hud.h"
#include "physics_memory.h"
#include <string.h>
#include "frame_hud_tables.h"
typedef struct hud { physics_memory m;frame_commands *c;int32_t x[9],y[9]; } hud;
int frame_hud_consume_events(frame_data_read read,frame_data_write write,void *ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t player,record,o;
    if (!read || !write) return -1;
    player=pm_load(&m,0x53e8b8u,4);
    if (player==pm_load(&m,0x53b69cu,4)) return m.error;
    record=pm_load(&m,pm_load(&m,0x55c694u,4)+player*8u,4);
    o=record ? pm_load(&m,record+4u,4) : 0;
    if (!o) return m.error ? m.error : -4;
    if (pm_load(&m,o+0x1104u,1)) pm_store(&m,o+0x1104u,0,1);
    return m.error;
}
const char *frame_hud_resource(uint32_t i,uint32_t map,uint32_t *pack)
{
    static const char *names[]={"CFONT1.SUR","CFONT2.SUR","CFONT3.SUR","DOTDIAL.SUR","SPEEDO.SUR",0,"P-MINE.SUR","P-EMINE.SUR","P-HOMER.SUR","P-UFO.SUR","P-CONC.SUR","P-CONC.SUR","P-TURBO.SUR","P-SHIELD.SUR","P-SHELL.SUR","P-WARP.SUR","SHOOT.SUR","GOG.SUR","GOO.SUR","GOEX.SUR","WARNING.SUR"};
    static const char *maps[]={"ENGMAP.SUR","MEXMAP.SUR","SWAMAP.SUR","EASMAP.SUR","THEMAP.SUR","BIOMAP.SUR","MONMAP.SUR","CIRMAP.SUR"};
    if (!pack || i>=FRAME_HUD_TEXTURES || map>=8u) return 0;
    *pack=i<3u || (i>=17u && i<=19u) ? 6u : i==5u ? 5u : 4u;return i==5u ? maps[map] : names[i];
}
static void put(unsigned char *p,uint32_t v) { unsigned i;for (i=0;i<4u;i++) p[i]=(unsigned char)(v>>(i*8u)); }
static void real(unsigned char *p,float v) { uint32_t bits;memcpy(&bits,&v,4);put(p,bits); }
/*4FD3E0/4FD520/4FD680: retain quarter-pixel integer rounding and the
 * original4-vertex fan. kind0=opaque keyed UI, kind1=alpha128 countdown. */
static int quad(hud *h,uint32_t resource,int32_t x,int32_t y,int32_t x0,int32_t y0,int32_t x1,int32_t y1,int32_t u0,int32_t v0,int32_t u1,int32_t v1,uint32_t rgb,uint32_t angle,int rotated,uint32_t kind)
{
    unsigned char vertices[128];uint32_t i;int32_t c=4096,s=0;float dim=resource==2u ? 128.0f : resource>=17u && resource<=19u ? 32.0f : 64.0f;
    if (rotated) { s=pm_i32(&h->m,0x597380u+(angle&65532u));c=pm_i32(&h->m,0x59b380u+(angle&65532u)); }
    for (i=0;i<4u;i++) {
        int32_t a=(i==0u || i==3u) ? x0 : x1,b=i<2u ? y0 : y1;
        float px=(float)x+(rotated ? (float)pm_sar(pm_sub(pm_mul(c,a),pm_mul(s,b)),10)/4.0f : (float)a);
        float py=(float)y+(rotated ? (float)pm_sar(pm_add(pm_mul(s,a),pm_mul(c,b)),10)/4.0f : (float)b);unsigned char *out=vertices+i*32u;
        real(out,px);real(out+4,py);real(out+8,0);real(out+12,1);put(out+16,(kind ? 0x80000000u : 0xff000000u)|rgb);put(out+20,0xff000000u);
        real(out+24,(float)((i==0u || i==3u) ? u0 : u1)/dim);real(out+28,(float)(i<2u ? v0 : v1)/dim);
    }
    frame_commands_state(h->c,27,kind!=0u);frame_commands_state(h->c,19,kind ? 5u : 2u);frame_commands_state(h->c,20,kind ? 6u : 1u);
    return frame_commands_submit(h->c,6,FRAME_HUD_TEXTURE_BASE+resource+1u,0,vertices,4,0);
}
static int rect(hud *h,uint32_t resource,int32_t x,int32_t y,int32_t w,int32_t height,int32_t u0,int32_t v0,int32_t u1,int32_t v1,uint32_t rgb)
{ return quad(h,resource,x,y,0,0,w,height,u0,v0,u1,v1,rgb,0,0,0); }
static int glyph(hud *h,uint32_t ch,int32_t x,int32_t y,int large,uint32_t rgb)
{
    uint32_t index,resource;int32_t u,v;
    if (large) {
        if (ch>=48u && ch<=57u) { index=ch-48u;return rect(h,2,x,y,31,32,(int32_t)(index&3u)*32,(int32_t)(index>>2)*32,(int32_t)(index&3u)*32+31,(int32_t)(index>>2)*32+32,rgb); }
        if (ch==180u || ch==181u) return rect(h,2,x,y,31,16,32,ch==180u ? 96 : 112,63,ch==180u ? 112 : 128,rgb);
        return 0;
    }
    if (ch>=48u && ch<=57u) { index=ch-48u;return rect(h,2,x,y,16,16,(int32_t)(index&3u)*16+64,(int32_t)(index>>2)*16+65,(int32_t)(index&3u)*16+80,(int32_t)(index>>2)*16+81,rgb); }
    if (ch>=160u && ch<=163u) { static const unsigned char uv[]={32,32,48,32,0,48,16,48};index=ch-160u;return rect(h,2,x,y,16,16,uv[index*2u]+64,uv[index*2u+1u]+65,uv[index*2u]+80,uv[index*2u+1u]+81,rgb); }
    if (ch>=97u && ch<=122u) ch-=32u;
    if (ch>=65u && ch<=80u) { index=ch-65u;resource=0;u=(int32_t)(index&3u)*16;v=(int32_t)(index>>2)*16; }
    else if (ch>=81u && ch<=90u) { index=ch-81u;resource=1;u=(int32_t)(index&3u)*16;v=(int32_t)(index>>2)*16; }
    else if (ch==58u) { resource=1;u=v=32; }
    else if (ch==47u) { resource=1;u=48;v=32; }
    else if (ch==45u) { resource=1;u=0;v=48; }
    else if (ch==63u) { resource=1;u=16;v=48; }
    else return 0;
    return rect(h,resource,x,y,16,16,u,v+1,u+16,v+17,rgb);
}
static int text(hud *h,const unsigned char *s,int32_t x,int32_t y,uint32_t rgb)
{ uint32_t i;int status;for (i=0;s[i];i++) { status=glyph(h,s[i],x,y,0,rgb);if (status) return status;x+=glyph_width[s[i]]; }return h->m.error; }
static int centered(hud *h,const unsigned char *s,int32_t y,uint32_t rgb)
{ uint32_t n=0,i;for (i=0;s[i];i++) n+=glyph_width[s[i]];return text(h,s,320-(int32_t)(n>>1),y,rgb); }
static uint32_t decimal(unsigned char *s,uint32_t v,uint32_t digits)
{
    unsigned char reverse[10];uint32_t n=0,i;do { reverse[n++]=(unsigned char)(48u+v%10u);v/=10u; } while ((v || n<digits) && n<10u);
    for (i=0;i<n;i++) s[i]=reverse[n-1u-i];
    return n;
}
static int lap_time(hud *h,int32_t ms,int32_t x,int32_t y,int blink)
{
    unsigned char s[16];uint32_t minutes=(uint32_t)(ms/60000)&255u,n;if (minutes>99u) minutes=99;
    n=decimal(s,minutes,1);s[n++]=58;n+=decimal(s+n,(uint32_t)((ms/1000)%60),2);s[n++]=blink && ((ms/500)&1) ? 32u : 58u;n+=decimal(s+n,(uint32_t)((ms/10)%100),2);s[n]=0;
    return text(h,s,x+(minutes<10u ? 16 : 0),y,0xd2c31eu);
}
static int lap_record(hud *h,uint32_t at,int32_t x,int32_t y)
{
    unsigned char s[16];uint32_t minutes=pm_load(&h->m,at+4u,1),n;if (minutes>99u) minutes=99;
    n=decimal(s,minutes,1);s[n++]=58;n+=decimal(s+n,pm_load(&h->m,at+5u,1),2);s[n++]=58;n+=decimal(s+n,pm_load(&h->m,at+6u,1),2);s[n]=0;
    return text(h,s,x+(minutes<10u ? 16 : 0),y,0xd2c31eu);
}
static uint32_t object_of(physics_memory *m,uint32_t id)
{ uint32_t r=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);return r ? pm_load(m,r+4u,4) : 0; }
#define TRY(expression) do { status=(expression);if (status) return status; } while (0)
int frame_hud_pause_draw_limited(frame_data_read read,void *ctx,frame_commands *commands,uint32_t limit)
{
    static const unsigned char *labels[]={(const unsigned char *)"RESUME",(const unsigned char *)"QUIT RACE",(const unsigned char *)"SOUND FX VOLUME",(const unsigned char *)"MUSIC VOLUME",(const unsigned char *)"MUSIC TRACK"};
    hud h;physics_memory *m=&h.m;uint32_t i,j,w=144,setting,selection,count=5;int32_t x,y=200,cx=320;int status;unsigned char vertices[128];
    if (!read || !commands || commands->width!=640u || commands->height!=480u) return -1;
    memset(&h,0,sizeof(h));h.m=(physics_memory){read,0,ctx,0};h.c=commands;
    if (!pm_load(m,0x53e96cu,4)) return m->error;
    if (pm_load(m,0x52ee9cu,4)!=0u || pm_load(m,0x54b480u,4)) return -5;
    if (limit && limit<count) count=limit;
    setting=pm_load(m,0x53e978u,4);selection=pm_load(m,0x53e970u,4);if (selection>=5u || setting>3u) return -4;
    if (setting!=3u) for (i=0;i<count;i++) { uint32_t width=0;for (j=0;labels[i][j];j++) width+=glyph_width[labels[i][j]];if (width>w) w=width; }
    else { uint32_t first=pm_load(m,0x53e504u,4),last=pm_load(m,0x53e508u,4);if (last>10u) return -4;count=last-first+3u;if (pm_load(m,0x56021cu,4)<2u) count=3;for (i=first;i<=last;i++) { uint32_t width=0;for (j=0;pause_music_tracks[i][j];j++) width+=glyph_width[(unsigned char)pause_music_tracks[i][j]];if (width>w) w=width; } }
    if (pm_load(m,0x53e974u,4) && count<4u) count=4;
    y=240-(int32_t)count*8;x=cx-(int32_t)(w>>1);
    for (i=0;i<4u;i++) { unsigned char *v=vertices+i*32u;real(v,(float)(i==0u||i==3u ? x-8 : x+(int32_t)w+8));real(v+4,(float)(i<2u ? y-8 : y-8+(int32_t)(count+1u)*16));real(v+8,0);real(v+12,1);put(v+16,0x80000080u);put(v+20,0xff000000u);real(v+24,0);real(v+28,0); }
    frame_commands_state(commands,7,0);frame_commands_state(commands,14,0);frame_commands_state(commands,15,0);frame_commands_state(commands,22,1);frame_commands_state(commands,27,1);frame_commands_state(commands,19,5);frame_commands_state(commands,20,6);frame_commands_state(commands,41,1);
    TRY(frame_commands_submit(commands,6,0,0,vertices,4,0));
    if (!setting) {
        if (pm_load(m,0x53e974u,4)) {
            TRY(text(&h,(const unsigned char *)"ARE YOU SURE?",x,y,0xdccd28u));TRY(text(&h,(const unsigned char *)"NO",x,y+32,pm_load(m,0x53e4f8u,4) ? 0x968700u : 0xff1e1eu));TRY(text(&h,(const unsigned char *)"YES",x,y+48,pm_load(m,0x53e4f8u,4) ? 0xff1e1eu : 0x968700u));
        } else for (i=0;i<count;i++) TRY(text(&h,labels[i],x,y+(int32_t)i*16,i==selection ? 0xff1e1eu : 0x968700u));
    } else {
        TRY(text(&h,labels[selection],x,y,0xdccd28u));
        if (setting!=3u) {
            TRY(rect(&h,3,cx-74,y+32,10,12,31,51,41,63,0xbeaf00u));for (i=0;i<16u;i++) TRY(rect(&h,3,cx-64+(int32_t)i*8,y+32,8,12,44,51,52,63,0xffffffu));TRY(rect(&h,3,cx+64,y+32,10,12,53,51,63,63,0xbeaf00u));TRY(rect(&h,3,cx-66+pm_sar(pm_i32(m,0x53e4fcu),4),y+30,4,16,0,35,4,53,0xffffffu));
        } else if (pm_load(m,0x56021cu,4)>1u) for (i=pm_load(m,0x53e504u,4);i<=pm_load(m,0x53e508u,4);i++) TRY(text(&h,(const unsigned char *)pause_music_tracks[i],x,y+32+(int32_t)(i-pm_load(m,0x53e504u,4))*16,i==pm_load(m,0x53e500u,4) ? 0xff1e1eu : 0x968700u));
    }
    return m->error;
}
int frame_hud_pause_draw(frame_data_read r,void *ctx,frame_commands *c)
{ return frame_hud_pause_draw_limited(r,ctx,c,0); }
int frame_hud_draw(frame_data_read read,void *ctx,frame_commands *commands)
{
    hud h;physics_memory *m=&h.m;uint32_t player,map,i,weapon,texture,lap,rank,ammo,countdown,go;int32_t speed,needle;int status;
    static const uint32_t white=0xffffffu,gold=0xd2c31eu;
    if (!read || !commands || commands->width!=640u || commands->height!=480u) return -1;
    memset(&h,0,sizeof(h));h.m=(physics_memory){read,0,ctx,0};h.c=commands;
    if (pm_load(m,0x53e928u,4) || pm_load(m,0x54b480u,4) || pm_load(m,0x52ee9cu,4)>1u) return -5;
    player=object_of(m,pm_load(m,0x53e8b8u,4));map=pm_load(m,0x52ed90u,4);if (!player || map>=8u) return -4;
    for (i=0;i<9u;i++) {
        const struct widget_definition *w=&widgets[i];
        h.x[i]=w->anchor==100u || w->anchor==103u ? 16+w->x : w->anchor==101u || w->anchor==104u ? 624-w->x-w->width : 320+w->x-(w->width>>1);
        h.y[i]=w->anchor<103u ? 16+w->y : 464-w->y-w->height;
    }
    frame_commands_state(commands,4,1);frame_commands_state(commands,7,0);frame_commands_state(commands,14,0);frame_commands_state(commands,15,0);frame_commands_state(commands,41,1);frame_commands_state(commands,21,2);frame_commands_state(commands,22,1);frame_commands_state(commands,29,0);frame_commands_state(commands,28,0);
    weapon=pm_load(m,player+0xd0fu,4);if (weapon>10u) return -4;
    /*4113B0: nine50-byte controls, in table order; NULL controls have no image. */
    for (i=0;i<9u;i++) {
        texture=i==1u && weapon ? weapon+5u : i==2u ? 4u : i==3u ? 5u : i==8u && pm_load(m,player+0xdecu,1) ? 16u : UINT32_MAX;
        if (texture!=UINT32_MAX) TRY(rect(&h,texture,h.x[i],h.y[i],64,64,0,0,64,64,white));
    }
    needle=pm_i32(m,player+(pm_load(m,player+0xdeau,1) ? 0xb0u : 0xa8u));needle=pm_trunc(pm_mul(pm_abs(needle),120),10);if (needle>32768) needle=32768;
    TRY(quad(&h,3,h.x[2]+32,h.y[2]+38,-2,-23,2,11,0,0,5,35,white,(uint32_t)(needle-16384),1,0));
    if (pm_load(m,player+0xdecu,1)) TRY(quad(&h,3,h.x[8]+31,h.y[8]+31,-7,-23,7,9,49,0,63,32,white,(uint32_t)-pm_i16(m,player+0x762u),1,0));
    /*41157D: map calibration is independent of race waypoints. Draw competitors
     * in reverse slot order; player flag selects the native marker's UV cell. */
    for (i=9;i;i--) {
        uint32_t o=object_of(m,pm_load(m,0x53e8e0u+(i-1u)*4u,4)),scale=map_scale[map]&65535u,shift=map_scale[map]>>16;int32_t x,z;
        if (!o) continue;
        x=pm_trunc(pm_mul(pm_sub(pm_i32(m,o),map_origin[map][0]),(int32_t)scale),shift)+28;
        z=pm_trunc(pm_mul(pm_sub(map_origin[map][1],pm_i32(m,o+8u)),(int32_t)scale),shift)+28;
        texture=pm_load(m,o+0xde5u,1) ? 0u : 8u;TRY(rect(&h,3,h.x[3]+x,h.y[3]+z,8,8,(int32_t)texture,56,(int32_t)texture+8,64,white));
    }
    countdown=pm_load(m,0x53e4f0u,4);go=pm_load(m,0x53e4f4u,4);
    if (countdown) {
        uint32_t digit=countdown>2000u ? 3u : countdown>1000u ? 2u : 1u,index=digit;int32_t size=(int32_t)((countdown-(digit-1u)*1000u)>>3)+32;
        TRY(quad(&h,2,320-(size>>1),240-(size>>1),0,0,size,size,(int32_t)(index&3u)*32,(int32_t)(index>>2)*32,(int32_t)(index&3u)*32+31,(int32_t)(index>>2)*32+32,white,0,0,1));
    }
    if (go) {
        int32_t size=(int32_t)(go>>3)+32,x=320-(((size>>1)+size*2)>>1),y=240-(size>>1);
        for (i=0;i<3u;i++) TRY(quad(&h,17u+i,x+(int32_t)i*size,y,0,0,size,size,0,0,31,32,white,0,0,1));
    }
    lap=pm_load(m,player+0xcb6u,4);
    TRY(glyph(&h,48u+(lap<3u ? lap+1u : 3u),h.x[4],h.y[4],1,gold));
    TRY(text(&h,(const unsigned char *)"/3",h.x[4]+32,h.y[4]+16,gold));TRY(text(&h,(const unsigned char *)"LAP",h.x[4]+10,h.y[4]+32,gold));
    for (i=0;i<lap && i<3u;i++) TRY(lap_record(&h,player+0xcbeu+i*7u,h.x[5],h.y[5]+(int32_t)i*16));
    if (lap<3u) TRY(lap_time(&h,pm_i32(m,player+0xcbau),h.x[5],h.y[5]+(int32_t)lap*16,1));
    rank=pm_load(m,player+0xdddu,1);
    if (!pm_load(m,player+0xde6u,1) && pm_load(m,0x53e3fcu,4)) {
        rank=1;for (i=0;i<9u;i++) { uint32_t o=object_of(m,pm_load(m,0x53e8e0u+i*4u,4));if (o && o!=player && pm_i32(m,player+0xd18u)<pm_i32(m,o+0xd18u)) rank++; }
    }
    TRY(glyph(&h,rank+48u,h.x[0],h.y[0],1,gold));TRY(glyph(&h,(rank<4u ? rank : 4u)+159u,h.x[0]+32,h.y[0],0,gold));
    if (pm_load(m,0x53e99cu,4)) { unsigned char message[128];for (i=0;i<127u;i++) { message[i]=(unsigned char)pm_load(m,0x5904a0u+i,1);if (!message[i]) break; }message[127]=0;TRY(centered(&h,message,216,gold)); }
    if (!pm_load(m,0x53e3fcu,4)) { unsigned char message[16]="YOU CAME ";uint32_t place=pm_load(m,player+0xdddu,1);if (place>8u) return -4;message[9]=(unsigned char)(48u+place);message[10]=(unsigned char)(159u+(place<4u ? place : 4u));message[11]=0;TRY(centered(&h,message,232,gold)); }
    ammo=pm_load(m,player+0xd13u,1);if (pm_signed(ammo<<24)>0 && ammo>1u) TRY(glyph(&h,ammo+48u,h.x[6],h.y[6],0,gold));
    speed=pm_sar(pm_mul(pm_sar(pm_abs(pm_i32(m,player+0xecu)),6),1000),10);speed=pm_trunc(pm_mul(speed,3600)/1000,8);
    { unsigned char s[12];uint32_t n=decimal(s,(uint32_t)speed,1);s[n]=0;TRY(text(&h,s,h.x[7]+(speed<10 ? 32 : speed<100 ? 16 : 0),h.y[7],gold)); }
    TRY(glyph(&h,pm_load(m,0x53044cu,4)==1u ? 181u : 180u,h.x[7]+48,h.y[7],1,gold));
    if (pm_load(m,player+0xde5u,1) && !pm_load(m,player+0xde6u,1) && pm_load(m,player+0x1105u,1)==2u) TRY(centered(&h,(const unsigned char *)"WRONG WAY",232,gold));
    if (pm_load(m,0x53e8b8u,4)!=pm_load(m,0x53b69cu,4) && pm_load(m,player+0x1104u,1)) TRY(rect(&h,20,560,305,64,64,0,0,63,63,white));
    return m->error;
}
