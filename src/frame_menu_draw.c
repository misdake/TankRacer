#include "frame_menu.h"
#include "physics_memory.h"
#include <string.h>
#include "frame_menu_tables.h"
static uint32_t string_length(const char *s) { uint32_t n=0;while (s[n]) n++;return n; }
static int string_equal(const char *a,const char *b) { while (*a && *a==*b) { a++;b++; }return *a==*b; }
typedef struct painter { physics_memory m;frame_menu_draws *out;uint32_t layer,blend,pulse,browser; } painter;
uint32_t frame_menu_resources(void) { return (uint32_t)(sizeof(menu_resources)/sizeof(menu_resources[0])); }
const unsigned char *frame_menu_characters(void) { return menu_name_characters; }
const char *frame_menu_resource(uint32_t i,uint32_t *identity)
{ if (i>=frame_menu_resources()) return 0;if (identity) *identity=menu_resources[i].identity;return menu_resources[i].name; }
int frame_menu_bootstrap(frame_data_write write,void *ctx)
{
    physics_memory m={0,write,ctx,0};uint32_t i,j;
    if (!write) return -1;
    for (i=0;i<80u;i++) pm_store(&m,0x537ee0u+i*4u,menu_text_pointers[i],4);
    for (i=0;i<256u;i++) for (j=0;j<5u;j++) pm_store(&m,0x74ec00u+i*20u+j*4u,menu_font[i][j],4);
    pm_store(&m,0x5376b0u,1,4);pm_store(&m,0x5376b4u,UINT32_MAX,4);pm_store(&m,0x562b90u,2,4);pm_store(&m,0x562bb4u,0,4);pm_store(&m,0x5884c0u,(uint32_t)-640,4);pm_store(&m,0x5884c4u,(uint32_t)-416,4);pm_store(&m,0x537ed8u,1,4);pm_store(&m,0x588508u,4096,4);
    for (i=0;i<4u;i++) pm_store(&m,0x562a58u+i*4u,0x562a58u+i*4u,4);
    for (i=0;i<25u;i++) pm_store(&m,0x592000u+i*4u,16u+i,4);
    if (!write(ctx,0x63c5c0u,menu_credits,sizeof(menu_credits))) return -2;
    pm_store(&m,0x754004u,0x63c5c0u,4);pm_store(&m,0x538820u,1,4);
    return m.error;
}
static void quad(painter *p,uint32_t texture,int32_t x,int32_t y,int32_t w,int32_t h,int32_t u,int32_t v,int32_t u1,int32_t v1,uint32_t r,uint32_t g,uint32_t b,uint32_t scaled)
{
    frame_menu_quad *q;if (p->out->count==p->out->capacity) { p->out->error=-3;return; }
    q=&p->out->quads[p->out->count++];*q=(frame_menu_quad){texture,p->layer,p->blend,x,y,w,h,u,v,u1,v1,r,g,b,scaled,{0,0,0,0}};
}
static void image(painter *p,uint32_t texture,int32_t x,int32_t y,int32_t w,int32_t h,int32_t u,int32_t v,uint32_t grey)
{ quad(p,texture,x,y,w,h,u,v,u+w,v+h,grey,grey,grey,0); }
static int width(const unsigned char *s)
{ int n=0;for (;*s;s++) n+=menu_font[*s][0] ? (int)menu_font[*s][4] : 11;return n; }
static void string_at(painter *p,uint32_t at,unsigned char *s,uint32_t cap)
{ uint32_t i;for (i=0;i+1u<cap;i++) { s[i]=(unsigned char)pm_load(&p->m,at+i,1);if (!s[i]) return; }s[cap-1u]=0; }
static void text(painter *p,const unsigned char *s,int32_t x,int32_t y,uint32_t r,uint32_t g,uint32_t b)
{
    for (;*s;s++) { const uint32_t *f=menu_font[*s];if (f[0]) { uint32_t t=f[1]==0u ? 0x562a6cu : f[1]==2u ? 0x562a74u : 0x562a70u;quad(p,t,x,y,16,16,(int32_t)f[2],(int32_t)f[3],(int32_t)f[2]+16,(int32_t)f[3]+16,r,g,b,0);x+=(int32_t)f[4]; }else x+=11; }p->blend=0;
}
static void centered(painter *p,const unsigned char *s,int32_t x,int32_t y,uint32_t grey)
{ text(p,s,x-(width(s)>>1),y,grey,grey,grey); }
static void center_data(painter *p,uint32_t at,int32_t x,int32_t y,uint32_t grey)
{ unsigned char s[256];string_at(p,at,s,sizeof(s));centered(p,s,x,y,grey); }
static uint32_t button(painter *p,uint32_t s,uint32_t i) { return pm_load(&p->m,s+28u,4)+i*64u; }
static int shifted(painter *p,uint32_t s,uint32_t b)
{ uint32_t cup=pm_load(&p->m,0x53d18cu,4);return pm_load(&p->m,b+39u,1) && pm_load(&p->m,0x53d194u+cup*2u,2) && !pm_load(&p->m,s+8u,4) && pm_load(&p->m,s+12u,4)!=14u; }
static void car_statistics(painter *p,uint32_t car,int32_t x,int32_t y)
{
    uint32_t i,j;int32_t longest=0,start;unsigned char s[80];
    for (i=0;i<3u;i++) { string_at(p,0x757580u+i*80u,s,sizeof(s));if (width(s)>longest) longest=width(s); }
    centered(p,(const unsigned char *)menu_car_names[car],x,y,128);start=x-((longest+80)>>1);y+=32;
    for (i=0;i<4u;i++) { string_at(p,i==3u ? 0x757ad0u : 0x757580u+i*80u,s,sizeof(s));text(p,s,start,y+(int32_t)i*16,96,96,96); }
    for (i=0;i<4u;i++) if (car%5u==4u) text(p,(const unsigned char *)"???",start+longest+16,y+(int32_t)i*16,96,96,96);
    else for (j=0;j<menu_car_stats[car*4u+i];j++) image(p,0x562b6cu,start+longest+17+(int32_t)j*16,y+1+(int32_t)i*16,14,14,0,0,128);
}
static void vehicle_icons(painter *p,uint32_t s,int32_t x,int32_t y,uint32_t selected)
{
    uint32_t i,b,car,grey;int32_t bx,by;
    if (!pm_load(&p->m,s+24u,4) || !pm_load(&p->m,button(p,s,0)+39u,1)) return;
    p->layer=3;
    for (i=0;i<pm_load(&p->m,s+24u,4);i++) {
        b=button(p,s,i);car=pm_load(&p->m,b+39u,1);if (!car || !pm_load(&p->m,b+37u,1)) continue;car--;bx=x+pm_i32(&p->m,b+28u)-(shifted(p,s,b) ? 64 : 0);by=y+pm_i32(&p->m,b+32u);grey=i==selected ? 128u : 72u;
        image(p,0x562af0u+car%5u*4u,bx-32,by-32,64,64,0,0,grey);
        if (i==selected) {
            if (car%5u==4u) center_data(p,pm_load(&p->m,0x537f2cu,4),bx,by+60,128);else centered(p,(const unsigned char *)menu_car_makes[car%5u],bx,by+60,128);
            car_statistics(p,car,x,y-80);
            quad(p,0x562b28u+car*4u,x-64,y-208,128,128,0,0,63,63,128,128,128,1);
        }
    }
}
static uint32_t decimal(unsigned char *s,uint32_t v,uint32_t digits)
{ unsigned char reverse[10];uint32_t n=0,i;do { reverse[n++]=(unsigned char)('0'+v%10u);v/=10u; }while ((v||n<digits)&&n<10u);for (i=0;i<n;i++) s[i]=reverse[n-i-1u];return n; }
static void time_text(painter *p,uint32_t ms,int32_t x,int32_t y)
{ unsigned char s[32];uint32_t n=decimal(s,ms/60000u,2);s[n++]=':';n+=decimal(s+n,ms/1000u%60u,2);s[n++]='.';n+=decimal(s+n,ms/10u%100u,2);s[n]=0;text(p,s,x,y,128,128,128); }
static void number(painter *p,uint32_t v,int32_t x,int32_t y,int tied)
{ unsigned char s[24];uint32_t n=decimal(s,v,1);if (tied) { s[n++]=tied==2 ? '=' : '.'; }s[n]=0;text(p,s,x,y,128,128,128); }
static void player_name(painter *p,unsigned char *name)
{
    uint32_t n;string_at(p,pm_load(&p->m,0x537f10u,4),name,80);
    if (pm_load(&p->m,0x590338u,4)) { n=(uint32_t)string_length((char *)name);name[n++]=' ';name[n++]='(';string_at(p,0x530458u,name+n,8);n+=(uint32_t)string_length((char *)name+n);name[n++]=')';name[n]=0; }
}
static void race_results(painter *p,int32_t x,int32_t y,uint32_t points)
{
    uint32_t i,at,rank,car,cup=pm_load(&p->m,0x5903f0u,4);int32_t row;unsigned char name[96];
    p->layer=3;center_data(p,pm_load(&p->m,points ? 0x537f08u : 0x537f04u,4),x,y-160,128);
    /*511B90/511F10 submit the player first, then the seven actual AI slots.
     * Rows use native ranks; no JS sorting or guessed race results. */
    for (i=0;i<8u;i++) {
        at=0x590360u+(i ? i-1u : 0u)*20u;rank=pm_load(&p->m,i ? at+4u : 0x590330u,4);car=pm_load(&p->m,i ? at+8u : 0x590334u,4);if (!car || car>15u || !rank || rank>8u) continue;
        row=y-144+(int32_t)rank*32;number(p,rank,x-256,row,1);
        if (i) text(p,(const unsigned char *)menu_car_names[cup*5u+(car-1u)%5u],x-224,row,128,128,128);else { player_name(p,name);text(p,name,x-224,row,128,128,128); }
        if (points) number(p,pm_load(&p->m,i ? at+16u : 0x59033cu,4),x+128,row,0);else time_text(p,pm_load(&p->m,i ? at : 0x59032cu,4),x+96,row);
        image(p,0x562b08u+(car-1u)%5u*4u,x+(points ? 192 : 208),row-4,24,24,0,0,128);if (!i) image(p,0x562b1cu,x-280,row-4,32,32,0,0,128);
    }
}
static void cup_results(painter *p,int32_t x,int32_t y)
{
    uint32_t rank,category,member,count,car=pm_load(&p->m,0x590334u,4),cup=pm_load(&p->m,0x5903f0u,4),player_category=(car-1u)%5u,place,value,player,i;int32_t row=y-112;unsigned char name[96],line[256],temp[32];
    if (!car || car>15u || cup>2u) { p->out->error=-4;return; }
    if (player_category==4u) player_category=pm_load(&p->m,0x52f2d8u+cup*4u,4);
    p->layer=3;center_data(p,pm_load(&p->m,0x537f0cu,4),x,y-160,128);
    for (rank=1;rank<=8u;rank++) {
        count=0;for (category=0;category<4u;category++) for (member=0;member<2u;member++) { player=category==player_category && !member;place=pm_load(&p->m,player ? 0x53e3acu : 0x53e3d8u+(category*2u+member)*4u,4);if (place==rank) count++; }
        if (!count) continue;
        for (category=0;category<4u;category++) for (member=0;member<2u;member++) {
            player=category==player_category && !member;place=pm_load(&p->m,player ? 0x53e3acu : 0x53e3d8u+(category*2u+member)*4u,4);if (place!=rank) continue;
            value=pm_load(&p->m,player ? 0x53e3a8u : 0x53e3b8u+(category*2u+member)*4u,4);
            number(p,rank,x-256,row,count>1u ? 2 : 1);if (player) { player_name(p,name);text(p,name,x-224,row,128,128,128); }else text(p,(const unsigned char *)menu_car_names[cup*5u+category],x-224,row,128,128,128);
            number(p,value,x+128,row,0);image(p,player && (car-1u)%5u==4u ? 0x562b18u : 0x562b08u+category*4u,x+192,row-4,24,24,0,0,128);if (player) image(p,0x562b1cu,x-280,row-4,32,32,0,0,128);row+=32;
        }
    }
    if (pm_load(&p->m,0x590408u,4)) center_data(p,pm_load(&p->m,pm_load(&p->m,0x53e3acu,4)==1u ? 0x537f24u : 0x537f28u,4),x,y+144,128);
    else {
        uint32_t n,used=0,stage=pm_load(&p->m,0x5903f8u,4),cost=pm_load(&p->m,0x52eea0u+stage*4u,4);
        string_at(p,pm_load(&p->m,0x537f14u,4),line,128);used=(uint32_t)string_length((char *)line);line[used++]=' ';n=decimal(temp,cost,1);memcpy(line+used,temp,n);used+=n;line[used++]=' ';string_at(p,pm_load(&p->m,0x537f18u,4),line+used,96);centered(p,line,x,y+144,128);
        i=pm_load(&p->m,0x53e3a8u,4)<cost ? 0x537f20u : 0x537f1cu;center_data(p,pm_load(&p->m,i,4),x,y+164,128);
    }
}
static void award(painter *p,int32_t x,int32_t y)
{
    uint32_t which=pm_load(&p->m,0x59040cu,4),at,i;unsigned char s[192];p->layer=3;center_data(p,pm_load(&p->m,0x537f30u,4),x,y-36,128);string_at(p,pm_load(&p->m,0x537f34u,4),s,sizeof(s));
    for (i=1;s[i] && s[i+1];i++) if ((s[i-1]==' ' && (s[i+1]==' '||s[i+1]=='-') && s[i]=='x') || s[i]=='X') s[i]=(unsigned char)('0'+pm_load(&p->m,0x53e3b0u,4));
    centered(p,s,x,y-12,128);center_data(p,pm_load(&p->m,0x537f38u,4),x,y+12,128);
    at=which==13u ? 0x755be0u+pm_load(&p->m,0x75ef44u,4)*80u : which==14u ? 0x755c30u+pm_load(&p->m,0x75ef44u,4)*80u : 0x756e00u+which*80u;center_data(p,at,x,y+36,128);
}
static void name_entry(painter *p,int32_t x,int32_t y)
{
    uint32_t cup=pm_load(&p->m,0x562bd8u,4),round=pm_load(&p->m,0x562bd4u,4),map,position=pm_load(&p->m,0x562be4u,4),editing=pm_load(&p->m,0x562bdcu,4),kind,i,record,flag,car;int32_t row,left;unsigned char s[8];
    if (cup>2u || round>=6u) { p->out->error=-4;return; }map=pm_load(&p->m,0x52ee88u+cup*6u+round,1);if (map>=8u) { p->out->error=-4;return; }
    p->layer=2;
    if (editing && position<3u) {
        quad(p,0,x+pm_i32(&p->m,0x562c34u)-72,y+pm_i32(&p->m,0x562c38u)-176,2,2,0,0,0,0,255,0,0,0);
        for (i=0;i<3u;i++) if ((pm_load(&p->m,0x5884d0u,4)&511u)<256u || i!=position) { s[0]=(unsigned char)pm_load(&p->m,0x530458u+i,1);s[1]=0;text(p,s,x-24+(int32_t)i*16,y-160,128,128,128); }
        image(p,0x562ab0u,x-32,y-168,64,32,0,0,128);image(p,0x562ab4u,x-62,y-168,32,32,0,0,128);
    }
    quad(p,0x754820u+map*4u,x+112,y-192,127,63,0,0,127,127,128,128,128,1);
    { unsigned char title[80];string_at(p,pm_load(&p->m,0x537fa8u+cup*4u,4),title,sizeof(title));text(p,title,x-256,y-144,128,128,128);string_at(p,pm_load(&p->m,0x537fb8u+map*4u,4),title,sizeof(title));text(p,title,x-256,y-124,128,128,128); }
    for (kind=0;kind<2u;kind++) {
        center_data(p,pm_load(&p->m,kind ? 0x537f98u : 0x537f94u,4),x+(kind ? 148 : -140),y-80,128);left=x+(kind ? 48 : -240);
        for (i=0;i<6u;i++) {
            row=y-48+(int32_t)i*32;record=(kind ? 0x53daa8u : 0x53d1a8u)+((cup*16u+map)*6u+i)*8u;number(p,i+1u,left,row,1);string_at(p,record+4u,s,4);flag=pm_load(&p->m,(kind ? 0x590520u : 0x590500u)+i*4u,4);
            text(p,s,left+32,row,editing && flag==1u ? 96u : 128u,editing && flag==1u ? 32u : 128u,editing && flag==1u ? 32u : 128u);time_text(p,pm_load(&p->m,record,4),left+88,row);
            car=pm_load(&p->m,record+7u,1);if (car) image(p,0x562b08u+(car-1u)%5u*4u,left+184,row-4,24,24,0,0,128);
        }
    }
}
static void congratulations(painter *p,int32_t x,int32_t y)
{
    /*5117D0's literal GUI string IDs and exact baselines. */
    static const uint32_t ids[2][5]={{153,154,155,159,0},{153,156,157,158,160}};
    static const int32_t offsets[2][5]={{-52,-20,0,32,0},{-62,-30,-10,10,42}};
    uint32_t cup=pm_load(&p->m,0x5903f0u,4),i;p->layer=2;if (cup>1u) return;
    for (i=0;i<(cup ? 5u : 4u);i++) center_data(p,0x754830u+ids[cup][i]*80u,x,y+offsets[cup][i],128);
}
static void credits(painter *p,int32_t x,int32_t y)
{
    uint32_t at=pm_load(&p->m,0x754004u,4),n,ch;unsigned char line[128];p->layer=2;y+=pm_sar(pm_i32(&p->m,0x562c40u),4);
    /*5116A0: CR/LF rows,24px spacing, centered original DATA text. */
    while (pm_load(&p->m,at,1)) {
        n=0;while ((ch=pm_load(&p->m,at,1)) && ch!=10u && ch!=13u) { if (n+1u<sizeof(line)) line[n++]=(unsigned char)ch;at++; }line[n]=0;
        if (ch==13u) { at++;ch=pm_load(&p->m,at,1); }if (ch==10u) at++;
        if (y>-24 && y<480 && n) centered(p,line,x,y,128);
        y+=24;
    }
}
static void route_details(painter *p,uint32_t s,int32_t x,int32_t y,uint32_t selected)
{
    uint32_t b,i,j,control,map=UINT32_MAX,record,rank=0,cup=pm_load(&p->m,0x53d18cu,4),stage=pm_load(&p->m,0x53e3f8u,4);unsigned char name[16],label[192];int32_t cx,cy;
    if (selected==UINT32_MAX) return;
    b=button(p,s,selected);string_at(p,b+16u,name,sizeof(name));for (i=0;i<8u;i++) if (string_equal((char *)name,menu_map_names[i])) map=i;if (map==UINT32_MAX) return;
    p->layer=3;
    for (i=0;i<pm_load(&p->m,s+32u,4);i++) {
        control=pm_load(&p->m,s+36u,4)+i*56u;if (pm_load(&p->m,control,4)!=7u) continue;cx=x+pm_i32(&p->m,control+4u);cy=y+pm_i32(&p->m,control+8u);
        quad(p,0x754820u+map*4u,cx-240,cy-64,254,127,0,0,127,127,128,128,128,1);
        for (j=0;j<2u;j++) {
            int32_t row=cy-58+(int32_t)j*52;record=(j ? 0x53daa8u : 0x53d1a8u)+(cup*16u+map)*48u;
            center_data(p,pm_load(&p->m,j ? 0x537fa0u : 0x537f9cu,4),cx+176,row,128);string_at(p,record+4u,name,4);text(p,name,cx+92,row+20,128,128,128);time_text(p,pm_load(&p->m,record,4),cx+148,row+20);
            { uint32_t car=pm_load(&p->m,record+7u,1);if (car) image(p,0x562b08u+(car-1u)%5u*4u,cx+244,row+16,24,24,0,0,128); }
        }
        if (stage>(cup==0u ? 2u : cup==1u ? 3u : 4u)) string_at(p,0x758930u,label,sizeof(label));
        else {
            int32_t need=pm_i32(&p->m,0x52eea0u+stage*4u)-pm_i32(&p->m,0x53e3a8u);uint32_t used;
            for (j=0;j<8u;j++) if (need<=pm_i32(&p->m,0x52eeb8u+j*4u)) rank=j;
            string_at(p,0x7588e0u,label,sizeof(label));used=(uint32_t)string_length((char *)label);label[used++]=' ';for (j=0;menu_qualify_names[rank][j] && used+1u<sizeof(label);j++) label[used++]=(unsigned char)menu_qualify_names[rank][j];label[used]=0;
        }
        centered(p,label,cx+176,cy+46,128);
    }
}
static void buttons(painter *p,uint32_t s,int32_t x,int32_t y,uint32_t selected)
{
    uint32_t i,b,texture,enabled,visible,type,grey;int32_t bx,by,ly;unsigned char line[80];
    p->layer=4;p->blend=4;
    if (selected!=UINT32_MAX && pm_load(&p->m,0x5376b4u,4)==UINT32_MAX) {
        uint32_t pulse=(p->pulse+pm_load(&p->m,0x5904fcu,4))&1023u;int32_t c=(int32_t)(pulse>512u ? 1024u-pulse : pulse)>>2;p->pulse=pulse;b=button(p,s,selected);
        if (!pm_load(&p->m,b+42u,1)) image(p,0x562a84u,x+pm_i32(&p->m,b+28u)-64-(shifted(p,s,b) ? 64 : 0),y+pm_i32(&p->m,b+32u)-64,128,128,0,0,(uint32_t)c);
    }
    p->blend=0;vehicle_icons(p,s,x,y,selected);p->layer=3;
    if (pm_load(&p->m,0x5376b0u,4)==10u && selected!=UINT32_MAX) {
        uint32_t choice=pm_load(&p->m,0x74d7c0u,4),yes=choice==1u ? 1u : choice==3u ? 2u : 0u,no=choice==0u ? 1u : choice==2u ? 2u : 0u;b=button(p,s,selected);bx=x+pm_i32(&p->m,b+28u)-64-(shifted(p,s,b) ? 64 : 0);by=y+pm_i32(&p->m,b+32u)-64;
        if (choice<2u) center_data(p,0x74b9e0u,bx+64,by-16,128);
        image(p,0x562ac0u+yes*4u,bx,by,64,64,0,0,128);image(p,0x562ad0u+no*4u,bx+64,by,64,64,0,0,128);
    }
    for (i=0;i<pm_load(&p->m,s+24u,4);i++) {
        b=button(p,s,i);type=pm_load(&p->m,b,4);visible=pm_load(&p->m,b+37u,1);enabled=pm_load(&p->m,b+36u,1);if (!visible || type>2u) continue;
        bx=x+pm_i32(&p->m,b+28u)-64-(shifted(p,s,b) ? 64 : 0);by=y+pm_i32(&p->m,b+32u)-64;
        texture=pm_load(&p->m,b+39u,1)||pm_load(&p->m,button(p,s,0)+43u,1) ? 0x562a7cu : 0x562a78u;
        if (!enabled) texture=0x562a80u;
        grey=enabled && i!=selected ? 72u : 128u;
        image(p,texture,bx,by,128,pm_load(&p->m,b+42u,1) ? 48 : 128,0,0,grey);
    }
    route_details(p,s,x,y,selected);p->layer=2;
    for (i=0;i<pm_load(&p->m,s+24u,4);i++) {
        uint32_t r=128,g=128,bl=128,cls;b=button(p,s,i);if (!pm_load(&p->m,b+37u,1)) continue;bx=x+pm_i32(&p->m,b+28u);by=y+pm_i32(&p->m,b+32u);
        if (pm_load(&p->m,b+24u,4) && (pm_load(&p->m,b,4)==1u || pm_load(&p->m,b,4)==2u)) {
            cls=pm_load(&p->m,b+60u,4);if (cls==0u) { r=199;g=108;bl=36; }else if (cls==1u) r=g=bl=256;else if (cls==2u) { r=241;g=199;bl=26; }
            grey=pm_load(&p->m,b+36u,1) && i==selected ? 128u : 72u;
            quad(p,pm_load(&p->m,b+24u,4),bx-16,by+14,32,32,0,0,32,32,(r*grey)>>8,(g*grey)>>8,(bl*grey)>>8,0);
        }
    }
    for (i=0;i<pm_load(&p->m,s+24u,4);i++) {
        b=button(p,s,i);if (!pm_load(&p->m,b+37u,1)) continue;bx=x+pm_i32(&p->m,b+28u);by=y+pm_i32(&p->m,b+32u);
        if (pm_load(&p->m,b+4u,4)) {
            grey=i==selected ? 128u : 0u;ly=by-64+(pm_load(&p->m,b+12u,4) ? 40 : 55);
            if (pm_load(&p->m,b+42u,1)) ly=by-32+(pm_load(&p->m,b+8u,4) ? 14 : 26);
            string_at(p,pm_load(&p->m,b+4u,4),line,17);centered(p,line,bx,ly,grey);
            if (pm_load(&p->m,b+8u,4)) { ly+=pm_load(&p->m,b+12u,4) ? 19 : pm_load(&p->m,b+42u,1) ? 20 : 24;string_at(p,pm_load(&p->m,b+8u,4),line,17);centered(p,line,bx,ly,grey);if (pm_load(&p->m,b+12u,4)) { string_at(p,pm_load(&p->m,b+12u,4),line,17);centered(p,line,bx,ly+21,grey); } }
        }
    }
}
static void header(painter *p)
{
    uint32_t i;p->layer=1;image(p,0x562a68u,160,32,96,24,0,40,128);image(p,0x562a68u,64,24,96,32,0,0,128);image(p,0x562a68u,16,24,48,32,0,96,128);
    for (i=0;i<640;i+=64) image(p,0x562a70u,(int32_t)i,59,64,10,0,54,128);
    if (p->browser) {
        uint32_t phase=pm_load(&p->m,0x5376b0u,4);const unsigned char *a=(const unsigned char *)(phase==9u ? "TYPE INITIALS  A/D: CHANGE" : "WASD / ARROWS: MOVE"),*b=(const unsigned char *)(phase==9u ? "ENTER/SPACE: OK  ESC: BACK" : phase==12u || phase==14u ? "ENTER / SPACE: CONTINUE" : "ENTER/SPACE: OK  ESC: BACK");
        text(p,a,608-width(a),14,128,128,128);text(p,b,608-width(b),36,128,128,128);
        quad(p,0,0,0,640,64,0,0,0,0,190,175,22,0);return;
    }
    /*50EC30 uses the source page's title from language-specific GUI text. */
    { uint32_t phase=pm_load(&p->m,0x5376b0u,4),editing=pm_load(&p->m,0x562bdcu,4)&&pm_load(&p->m,0x562be4u,4)<3u,pointer=phase==12u||phase==14u||(phase==9u&&!editing) ? 0x537f00u : 0x537ee0u;unsigned char hint[128];
      if (phase!=11u) { string_at(p,pm_load(&p->m,pointer,4),hint,sizeof(hint));text(p,hint,608-width(hint),36,128,128,128); }
      if (pm_load(&p->m,0x74ba60u,1)) { string_at(p,0x74ba60u,hint,sizeof(hint));text(p,hint,608-width(hint),16,128,128,128); }
    }
    quad(p,0,0,0,640,64,0,0,0,0,190,175,22,0);
}
/*50E9E0/50F8E0 default wallpaper: the four original RGB cycle tables,
 * sampled a quarter-cycle apart at the four untextured quad corners. */
static uint32_t wallpaper_colour(uint32_t t)
{
    uint32_t block=(t&4095u)>>10,step=((t&1023u)*54u)>>10,down=((t&1023u)*54u+1023u)>>10,r,g,b;
    r=block==0u ? 105u : block==1u ? 105u-down : block==2u ? 51u : 51u+step;
    g=block==0u ? 51u+step : block==1u ? 105u : block==2u ? 105u-down : 51u;
    b=block<2u ? 51u : block==2u ? 51u+step : 105u-down;
    return 0xff000000u|(r<<16)|(g<<8)|b;
}
static int draw_menu(frame_data_read read,void *ctx,frame_menu_draws *out,uint32_t browser)
{
    painter p;uint32_t phase,page,i,s,selected;int32_t x,y;
    if (!read || !out || !out->quads) return -1;
    memset(&p,0,sizeof(p));p.m=(physics_memory){read,0,ctx,0};p.out=out;p.browser=browser;p.pulse=pm_load(&p.m,0x588514u,4);out->count=0;out->error=0;
    p.layer=5;for (i=0;i<4u;i++) { uint32_t j,k;if (!pm_load(&p.m,0x562a58u+i*4u,4)) continue;for (j=0;j<5u;j++) for (k=0;k<6u;k++) image(&p,0x562a58u+i*4u,-((pm_i32(&p.m,0x5884c0u)>>1)&127)+(int32_t)(i%2u)*64+(int32_t)k*128,64-((pm_i32(&p.m,0x5884c4u)>>1)&127)+(int32_t)(i/2u)*64+(int32_t)j*128,64,64,0,0,128); }
    if (pm_load(&p.m,0x537ed8u,4)==1u) { quad(&p,0,0,64,640,416,0,0,0,0,0,0,0,0);for (i=0;i<4u;i++) out->quads[out->count-1u].corners[i]=wallpaper_colour(pm_load(&p.m,0x588504u,4)+i*1024u); }
    phase=pm_load(&p.m,0x5376b0u,4);page=pm_load(&p.m,0x562b90u,4);
    for (i=0;i<(phase==3u ? 2u : 1u);i++) {
        uint32_t center_y=0;s=0x75dec0u+(i ? pm_load(&p.m,0x562bc8u,4) : page)*44u;selected=pm_load(&p.m,i ? 0x562bccu : 0x562bb4u,4);
        x=pm_i32(&p.m,s+16u)*640-pm_i32(&p.m,0x5884c0u)+320;y=pm_i32(&p.m,s+20u)*416-pm_i32(&p.m,0x5884c4u)+272;
        if (pm_load(&p.m,s+40u,4)) center_y=0; /* CUP pages have no center flag. */
        if (phase==13u) credits(&p,x,y);
        else if (phase==14u) congratulations(&p,x,y);
        else if (phase==9u) name_entry(&p,x,y);
        else if (phase==12u) { uint32_t result=pm_load(&p.m,0x562bfcu,4);if (result<3u) race_results(&p,x,y,result==2u);else if (result==3u) cup_results(&p,x,y);else if (result==4u) award(&p,x,y); }
        else {
            if (!i && pm_load(&p.m,s+12u,4)==4u) { p.layer=3;image(&p,0x562adcu,x+pm_i32(&p.m,0x562becu),y+pm_i32(&p.m,0x562bf0u),32,32,0,0,128);p.blend=4;image(&p,0x562ae0u,x+pm_i32(&p.m,0x562becu),y+pm_i32(&p.m,0x562bf0u),32,32,0,0,128);p.blend=0; }
            buttons(&p,s,x,y-(int32_t)center_y,selected);
        }
    }
    if (phase==13u) { uint32_t count=out->count,j=0;for (i=0;i<count;i++) if (out->quads[i].layer!=5u) out->quads[j++]=out->quads[i];out->count=j; }
    else header(&p);
    /*50F530 runs after the ordinary layered GUI flush. */
    p.layer=0;p.blend=2;for (i=0x74ebccu;i>=0x74d7e0u;i-=20u) if (pm_load(&p.m,i+18u,2)) quad(&p,0,pm_sar(pm_i32(&p.m,i),8),pm_sar(pm_i32(&p.m,i+4u),8),4,4,0,0,0,0,pm_load(&p.m,i+12u,2)>>8,pm_load(&p.m,i+14u,2)>>8,pm_load(&p.m,i+16u,2)>>8,0);
    out->pulse=p.pulse;return p.m.error ? p.m.error : out->error;
}
int frame_menu_draw(frame_data_read read,void *ctx,frame_menu_draws *out)
{ return draw_menu(read,ctx,out,0); }
int frame_menu_draw_browser(frame_data_read read,void *ctx,frame_menu_draws *out)
{ return draw_menu(read,ctx,out,1); }
static void put32(unsigned char *p,uint32_t v) { p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24); }
static void real(unsigned char *p,float v) { uint32_t bits;memcpy(&bits,&v,4);put32(p,bits); }
int frame_menu_submit(const frame_menu_draws *draws,const uint32_t *dimensions,frame_commands *commands)
{
    int layer;uint32_t i,j,k,index,dim,r,g,b,alpha;int status;unsigned char tl[128];
    if (!draws || !dimensions || !commands) return -1;
    frame_commands_state(commands,7,0);frame_commands_state(commands,14,0);frame_commands_state(commands,15,0);frame_commands_state(commands,22,1);frame_commands_state(commands,21,2);frame_commands_state(commands,4,1);frame_commands_state(commands,41,1);frame_commands_state(commands,29,0);frame_commands_state(commands,28,0);
    /*50F470 traverses descending layers, each linked list in LIFO order. */
    for (layer=7;layer>=0;layer--) for (i=draws->count;i;i--) {
        const frame_menu_quad *q=&draws->quads[i-1u];float x0=(float)q->x,y0=(float)q->y,x1=(float)(q->x+q->width),y1=(float)(q->y+q->height),u0,u1,v0,v1;uint32_t texture=0;
        if (q->layer!=(uint32_t)layer) continue;
        index=UINT32_MAX;for (k=0;k<frame_menu_resources();k++) if (menu_resources[k].identity==q->texture) { index=k;break; }
        dim=index==UINT32_MAX ? 1u : dimensions[index];if (q->texture && (!dim || index==UINT32_MAX)) return -4;
        texture=q->texture ? FRAME_MENU_TEXTURE_BASE+index+1u : 0;u0=(float)q->u0/(float)dim+(q->scaled ? .5f : (float)dim/2048.0f)/(float)dim;u1=(float)q->u1/(float)dim+(q->scaled ? .5f : (float)dim/2048.0f)/(float)dim;v0=(float)q->v0/(float)dim+(q->scaled ? .5f : (float)dim/2048.0f)/(float)dim;v1=(float)q->v1/(float)dim+(q->scaled ? .5f : (float)dim/2048.0f)/(float)dim;
        if (x1<0 || y1<0 || x0>640 || y0>480) continue;
        if (x0<0) { u0+=(u1-u0)*(-x0)/(x1-x0);x0=0; }if (x1>640) { u1=u0+(u1-u0)*(640-x0)/(x1-x0);x1=640; }if (y0<0) { v0+=(v1-v0)*(-y0)/(y1-y0);y0=0; }if (y1>480) { v1=v0+(v1-v0)*(480-y0)/(y1-y0);y1=480; }
        r=q->texture ? q->red<128u ? q->red*2u : 255u : q->red;g=q->texture ? q->green<128u ? q->green*2u : 255u : q->green;b=q->texture ? q->blue<128u ? q->blue*2u : 255u : q->blue;alpha=q->blend==1u ? 128u : q->blend==4u ? 64u : 255u;
        for (j=0;j<4u;j++) { unsigned char *v=tl+j*32u;real(v,j==0u||j==3u ? x0 : x1);real(v+4,j<2u ? y0 : y1);real(v+8,0);real(v+12,1);put32(v+16,q->corners[j] ? q->corners[j] : (alpha<<24)|(r<<16)|(g<<8)|b);put32(v+20,0xff000000u);real(v+24,j==0u||j==3u ? u0 : u1);real(v+28,j<2u ? v0 : v1); }
        frame_commands_state(commands,27,q->blend!=0u);frame_commands_state(commands,19,q->blend ? 5u : 2u);frame_commands_state(commands,20,q->blend==2u||q->blend==4u ? 2u : q->blend ? 6u : 1u);
        status=frame_commands_submit(commands,6,texture,0,tl,4,0);if (status) return status;
    }
    return 0;
}
