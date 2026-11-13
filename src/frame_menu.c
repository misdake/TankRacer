#include "frame_menu.h"
#include "physics_memory.h"
#include "frame_results.h"
#include <string.h>
static uint32_t word(const unsigned char *b) { return b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24); }
static uint32_t screen_at(uint32_t n) { return 0x75dec0u+n*44u; }
int frame_menu_sample_input(frame_data_read read,frame_data_write write,void *ctx,uint32_t bits,uint32_t *event)
{
    physics_memory m={read,write,ctx,0};uint32_t i,count,tail,head,phase,action;
    if (!read || !write || !event || bits>255u) return -1;
    /*517CB0 edge latches and the original eight-entry event ring. Key codes
     * are external; held keys must not get browser auto-repeat semantics. */
    for (i=0;i<8u;i++) {
        uint32_t code=pm_load(&m,0x592000u+(17u+i)*4u,4);if (code>=512u) return -4;pm_store(&m,0x74b600u+code,(bits>>i)&1u,1);
        if (bits&(1u<<i)) { if (!pm_load(&m,0x588dd4u+i*4u,4)) { pm_store(&m,0x588dd4u+i*4u,1,4);count=pm_load(&m,0x588e58u,4);if (count<8u) { tail=pm_load(&m,0x588ea0u,4);pm_store(&m,0x588e60u+tail*4u,17u+i,4);pm_store(&m,0x588e80u+tail*4u,0,4);pm_store(&m,0x588ea0u,(tail+1u)&7u,4);pm_store(&m,0x588e58u,count+1u,4); } } }
        else pm_store(&m,0x588dd4u+i*4u,0,4);
    }
    *event=0;phase=pm_load(&m,0x5376b0u,4);count=pm_load(&m,0x588e58u,4);
    if ((phase==1u || phase==12u || phase==10u || phase==13u || phase==14u) && count) {
        head=pm_load(&m,0x588ea4u,4);action=pm_load(&m,0x588e60u+head*4u,4);pm_store(&m,0x588ea4u,(head+1u)&7u,4);pm_store(&m,0x588e58u,count-1u,4);
        if (action<=22u) *event=action-16u;else if (action==23u) *event=5;
    }
    return m.error;
}
static void copy_to(physics_memory *m,uint32_t at,const unsigned char *b,uint32_t n)
{ if (!m->error && !m->write(m->ctx,at,b,n)) m->error=-2; }
int frame_menu_load(frame_data_read read,frame_data_write write,void *ctx,const unsigned char *b,uint32_t n)
{
    physics_memory m={read,write,ctx,0};uint32_t sizes[5],at=24,i,s,button,type,text,base,k;static const uint32_t destinations[]={0x75dec0u,0x750000u,0x74bb40u,0x562658u,0x754880u};
    if (!read || !write || !b || n<24u) return -1;
    for (i=0;i<5u;i++) { sizes[i]=word(b+i*4u);if (i==4u) { if (sizes[i]>512u) return -4;sizes[i]*=80u; }if (sizes[i]>n-at) return -4;at+=sizes[i]; }
    if (at!=n || sizes[0]%44u || sizes[1]%64u || sizes[2]%56u || sizes[0]>4224u || sizes[1]>16384u || sizes[2]>12288u) return -4;
    at=24;for (i=0;i<5u;i++) { copy_to(&m,destinations[i],b+at,sizes[i]);at+=sizes[i]; }
    pm_store(&m,0x75ef44u,word(b+20u),4);
    /*50D7D0 relocates array indices, discovers special pages, and converts
     * special page tags to the actual ordinary-page renderer. */
    for (i=0;i<sizes[0]/44u;i++) {
        s=screen_at(i);base=pm_load(&m,s+28u,4);k=pm_load(&m,s+24u,4);if (base>sizes[1]/64u || k>sizes[1]/64u-base) return -4;
        pm_store(&m,s+28u,0x750000u+base*64u,4);base=pm_load(&m,s+36u,4);k=pm_load(&m,s+32u,4);if (base>sizes[2]/56u || k>sizes[2]/56u-base) return -4;pm_store(&m,s+36u,0x74bb40u+base*56u,4);
        type=pm_load(&m,s+12u,4);
        if (type==3u) { pm_store(&m,0x562b94u,i,4);type=0; }
        if (type==22u) { pm_store(&m,0x562b98u,i,4);type=0; }
        if (type==1u) pm_store(&m,0x562b9cu,i,4);
        if (type==5u) { pm_store(&m,0x562ba0u,i,4);type=0; }
        if (type==6u) { pm_store(&m,0x562ba4u,i,4);type=0; }
        pm_store(&m,s+12u,type,4);
    }
    for (i=0;i<sizes[1]/64u;i++) {
        button=0x750000u+i*64u;text=pm_load(&m,button+24u,4);if (text) pm_store(&m,button+24u,0x562657u+text,4);text=pm_load(&m,button+4u,4);
        if (text) { if (text>sizes[4]/80u) return -4;base=0x754880u+(text-1u)*80u;pm_store(&m,button+4u,base,4);if (pm_load(&m,button+8u,4)) { pm_store(&m,button+8u,base+pm_load(&m,button+8u,4),4);if (pm_load(&m,button+12u,4)) pm_store(&m,button+12u,base+pm_load(&m,button+12u,4),4); } }
    }
    for (i=0;i<sizes[2]/56u;i++) { at=0x74bb40u+i*56u+52u;text=pm_load(&m,at,4);if (text) pm_store(&m,at,0x754830u+text*80u,4); }
    return m.error;
}
int frame_menu_refresh(frame_data_read read,frame_data_write write,void *ctx,uint32_t screen)
{
    physics_memory m={read,write,ctx,0};uint32_t s=screen_at(screen),i,b,car,cup=pm_load(&m,0x53d18cu,4);int32_t limit;
    if (screen>=61u || cup>2u) return -1;
    /*515A40 CUP subset: original hidden fifth vehicle, class locks and the
     * already-reached stage. No unrelated network/controller predicates. */
    for (i=0;i<pm_load(&m,s+24u,4);i++) {
        b=pm_load(&m,s+28u,4)+i*64u;car=pm_load(&m,b+39u,1);
        if (car && (car-1u)%5u==4u) { uint32_t unlocked=pm_load(&m,0x53d194u+(car-1u)/5u*2u,2)!=0;pm_store(&m,b+36u,unlocked,1);pm_store(&m,b+37u,unlocked,1); }
        if (pm_load(&m,s+12u,4)==4u) pm_store(&m,b+36u,i<=pm_load(&m,0x53e3f8u,4),1);
        limit=pm_i32(&m,b+60u);if (limit!=-1) pm_store(&m,b+36u,pm_i32(&m,0x53d190u)>=limit,1);
    }
    return m.error;
}
static int valid(physics_memory *m,uint32_t b) { return pm_load(m,b+36u,1) && pm_load(m,b+37u,1); }
int frame_menu_direction(frame_data_read read,frame_data_write write,void *ctx,uint32_t direction,uint32_t player)
{
    physics_memory m={read,write,ctx,0};uint32_t s=screen_at(pm_load(&m,0x562b90u,4)),selected=0x562bb4u+player*4u,index=pm_load(&m,selected,4),base=pm_load(&m,s+28u,4),count=pm_load(&m,s+24u,4),i,b,best=UINT32_MAX;int32_t dx,dy,distance,minimum=10000000;
    if (direction>3u || player>2u || index==UINT32_MAX) return 0;
    if (index>=count) return -4;
    for (i=0;i<count;i++) {
        b=base+i*64u;if (!valid(&m,b)) continue;dx=pm_sub(pm_i32(&m,b+28u),pm_i32(&m,base+index*64u+28u));dy=pm_sub(pm_i32(&m,b+32u),pm_i32(&m,base+index*64u+32u));
        distance=direction==0u ? -dx : direction==1u ? dx : direction==2u ? -dy : dy;
        if (distance>0 && (direction<2u ? pm_abs(dy)<(pm_load(&m,base+42u,1) ? 16 : 100) : pm_abs(dx)<100) && distance<minimum) { minimum=distance;best=i; }
    }
    /*519A90/519C10 use a second search only for vertical movement. */
    if (best==UINT32_MAX && direction>=2u) for (i=0;i<count;i++) {
        b=base+i*64u;if (!valid(&m,b)) continue;dx=pm_sub(pm_i32(&m,b+28u),pm_i32(&m,base+index*64u+28u));dy=pm_sub(pm_i32(&m,b+32u),pm_i32(&m,base+index*64u+32u));distance=pm_add(pm_mul(dx,dx),pm_mul(dy,dy));
        if ((direction==2u ? dy<0 : dy>0) && distance<minimum) { minimum=distance;best=i; }
    }
    if (best!=UINT32_MAX) pm_store(&m,selected,best,4);
    return m.error;
}
static uint32_t first(physics_memory *m,uint32_t s,uint32_t selected)
{
    uint32_t i,base=pm_load(m,s+28u,4),count=pm_load(m,s+24u,4);if (selected<count && valid(m,base+selected*64u)) return selected;
    for (i=0;i<count;i++) if (valid(m,base+i*64u)) return i;
    return UINT32_MAX;
}
static int menu_key(physics_memory *m,uint32_t offset)
{ uint32_t code=pm_load(m,0x592000u+offset,4);if (code>=512u) { m->error=-4;return 0; }return pm_load(m,0x74b600u+code,1)==1u; }
static uint32_t character_index(uint32_t ch)
{ const unsigned char *s=frame_menu_characters();uint32_t i;for (i=0;s[i];i++) if (s[i]==ch) return i;return 0; }
static uint32_t menu_random(physics_memory *m)
{ uint32_t v=pm_load(m,0x538820u,4)*214013u+2531011u;pm_store(m,0x538820u,v,4);return (v>>16)&32767u; }
static void particles_create(physics_memory *m,int32_t x,int32_t y,uint32_t count,uint32_t spread,int32_t scale)
{
    uint32_t at=0x74d7e0u,i=0;while (i<count && at<0x74ebe0u) {
        if (!pm_load(m,at+18u,2)) {
            pm_store(m,at,pm_mul(x,256),4);pm_store(m,at+4u,pm_mul(y,256),4);
            pm_store(m,at+12u,menu_random(m),2);pm_store(m,at+14u,menu_random(m),2);pm_store(m,at+16u,0,2);
            pm_store(m,at+8u,pm_mul((int32_t)(menu_random(m)%(spread*2u))-(int32_t)spread,scale),2);
            pm_store(m,at+10u,pm_mul((int32_t)(menu_random(m)%(spread*2u))-(int32_t)spread,scale),2);pm_store(m,at+18u,1024,2);i++;
        } else at+=20u;
    }
}
static void particles_update(physics_memory *m,uint32_t dt)
{
    uint32_t at,j,v,fade;int32_t life;for (at=0x74d7e0u;at<0x74ebe0u;at+=20u) if (pm_load(m,at+18u,2)) {
        for (j=0;j<2u;j++) pm_store(m,at+j*4u,pm_add(pm_i32(m,at+j*4u),pm_mul((int16_t)pm_load(m,at+8u+j*2u,2),(int32_t)dt)),4);
        for (j=0;j<3u;j++) { v=pm_load(m,at+12u+j*2u,2);fade=dt*(j==2u ? 64u : 32u);pm_store(m,at+12u+j*2u,v>fade ? v-fade : 0u,2); }
        life=(int16_t)pm_load(m,at+18u,2)-(int32_t)dt;pm_store(m,at+18u,life<0 ? 0u : (uint32_t)life,2);
    }
}
static void initials_animation(physics_memory *m,uint32_t dt)
{
    int32_t x=128,y=48,t=pm_i32(m,0x588d70u);uint32_t a,b,elapsed;
    /*51BA20's rectangle perimeter cycle and100ms spark emission. */
    a=(uint32_t)x*2000u/((uint32_t)(x+y)*2u);b=(uint32_t)y*2000u/((uint32_t)(x+y)*2u);
    if (t<(int32_t)(b+a*2u)) {
        if (t<(int32_t)(b+a)) { if (t<(int32_t)a) { y=0;x=x*t/(int32_t)a; }else y=(t-(int32_t)a)*y/(int32_t)b; }
        else x-=((t-(int32_t)a-(int32_t)b)*x)/(int32_t)a;
    } else { x=0;y-=((t-(int32_t)(a*2u)-(int32_t)b)*y)/(int32_t)b; }
    pm_store(m,0x562c34u,x,4);pm_store(m,0x562c38u,y,4);elapsed=(uint32_t)t+dt;while (elapsed>2000u) elapsed-=2000u;pm_store(m,0x588d70u,elapsed,4);
    elapsed=pm_load(m,0x588d74u,4)+dt;if (elapsed>99u) { elapsed%=100u;particles_create(m,x+248,y+96,12,10,1); }pm_store(m,0x588d74u,elapsed,4);
}
static int initials(physics_memory *m,uint32_t dt,int32_t typed,frame_menu_sound_sink sound,void *sound_ctx)
{
    const unsigned char *alphabet=frame_menu_characters();uint32_t previous_char=pm_load(m,0x530458u+pm_load(m,0x562be4u,4),1),n=0,index=pm_load(m,0x562be4u,4),i,editing=index<3u&&pm_load(m,0x562bdcu,4),accept=menu_key(m,0x54u)||menu_key(m,0x5cu);int32_t position=pm_i32(m,0x588ee8u),velocity=pm_i32(m,0x588ee0u);int status=0;
    while (alphabet[n]) n++;
    if (editing) initials_animation(m,dt);
    pm_store(m,0x588ea4u,pm_load(m,0x588ea0u,4),4);pm_store(m,0x588e58u,0,4);
    if (editing) {
        if (menu_key(m,0x44u)) {
            if (!pm_load(m,0x588ef0u,4)) { velocity=0;position=(int32_t)((uint32_t)position&0xffff0000u)-32768; }
            else { velocity-=(int32_t)dt;if (velocity<-1000) velocity=-1000;position=pm_add(position,pm_mul(velocity,dt)); }
            while (position<0) position=pm_add(position,(int32_t)n*65536);
            pm_store(m,0x588ef0u,1,4);pm_store(m,0x530458u+index,alphabet[(uint32_t)position>>16],1);
        } else {
            pm_store(m,0x588ef0u,0,4);
            if (menu_key(m,0x48u)) {
                if (!pm_load(m,0x588ef8u,4)) { velocity=0;position=(int32_t)((uint32_t)position&0xffff0000u)+98304; }
                else { velocity+=(int32_t)dt;if (velocity>1000) velocity=1000;position=pm_add(position,pm_mul(velocity,dt)); }
                while (position>=(int32_t)n*65536) position=pm_sub(position,(int32_t)n*65536);
                pm_store(m,0x588ef8u,1,4);pm_store(m,0x530458u+index,alphabet[(uint32_t)position>>16],1);
            } else { velocity=0;position=(int32_t)((uint32_t)position&0xffff0000u)+32768;pm_store(m,0x588ef8u,0,4); }
        }
        pm_store(m,0x588ee0u,velocity,4);pm_store(m,0x588ee8u,position,4);
        if (sound && previous_char!=pm_load(m,0x530458u+index,1)) sound(sound_ctx,0x5179c0u,(uint32_t)pm_sar(position,18));
        if (accept) {
            if (!pm_load(m,0x588f00u,4)) { particles_create(m,((int32_t)index+19)*16,120,64,32,2);index++;pm_store(m,0x562be4u,index,4);if (index<3u) pm_store(m,0x588ee8u,character_index(pm_load(m,0x530458u+index,1))*65536u+32768u,4);else status=frame_results_initials(m->read,m->write,m->ctx,0); }
            pm_store(m,0x588f00u,1,4);
        } else {
            if (typed>='a' && typed<='z') typed-=32;
            for (i=0;i<n;i++) if ((int32_t)alphabet[i]==typed) { pm_store(m,0x530458u+index,(uint32_t)typed,1);if (sound) sound(sound_ctx,0x516820u,0);index++;pm_store(m,0x562be4u,index,4);particles_create(m,((int32_t)index+19)*16,120,64,32,2);if (index<3u) pm_store(m,0x588ee8u,character_index(pm_load(m,0x530458u+index,1))*65536u+32768u,4);else status=frame_results_initials(m->read,m->write,m->ctx,0);break; }
            pm_store(m,0x588f00u,0,4);
        }
        if (menu_key(m,0x58u)) { if (!pm_load(m,0x588f08u,4) && index>0u) { if (sound) sound(sound_ctx,0x51a930u,0);index--;pm_store(m,0x562be4u,index,4);pm_store(m,0x588ee8u,character_index(pm_load(m,0x530458u+index,1))*65536u+32768u,4); }pm_store(m,0x588f08u,1,4); }else pm_store(m,0x588f08u,0,4);
    } else {
        if (accept) { if (!pm_load(m,0x588f00u,4)) { pm_store(m,0x5376b0u,12,4);pm_store(m,0x562bfcu,1,4);pm_store(m,0x562bdcu,0,4);pm_store(m,0x562be0u,0,4); }pm_store(m,0x588f00u,1,4); }else { pm_store(m,0x588f00u,0,4);pm_store(m,0x588f04u,0,4); }
    }
    if (editing) { unsigned char hint[256];uint32_t at=pm_load(m,0x537ef8u,4);for (i=0;i<255u;i++) { hint[i]=(unsigned char)pm_load(m,at+i,1);if (!hint[i]) break; }hint[i]=0;copy_to(m,0x74ba60u,hint,i+1u); }
    return status ? status : m->error;
}
int frame_menu_enter(frame_data_read read,frame_data_write write,void *ctx,uint32_t screen,uint32_t fresh)
{
    physics_memory m={read,write,ctx,0};uint32_t s=screen_at(screen),selected;int status;if (screen>=61u || fresh>1u) return -1;
    status=frame_menu_refresh(read,write,ctx,screen);if (status) return status;
    selected=first(&m,s,fresh ? 0u : pm_load(&m,0x590420u+screen,1));
    if (screen!=pm_load(&m,0x562b90u,4)) {
        pm_store(&m,0x562bc8u,screen,4);pm_store(&m,0x562bccu,selected,4);pm_store(&m,0x588b48u,pm_mul(pm_i32(&m,0x5884c4u),256),4);pm_store(&m,0x588b4cu,pm_mul(pm_i32(&m,0x5884c0u),256),4);
        pm_store(&m,0x588718u,pm_mul(pm_i32(&m,s+16u),640*256),4);pm_store(&m,0x58871cu,pm_mul(pm_i32(&m,s+20u),416*256),4);
        pm_store(&m,0x588720u,pm_sub(pm_i32(&m,0x588718u),pm_i32(&m,0x588b4cu))/375,4);pm_store(&m,0x588928u,pm_sub(pm_i32(&m,0x58871cu),pm_i32(&m,0x588b48u))/375,4);pm_store(&m,0x58892cu,pm_load(&m,0x588720u,4),4);pm_store(&m,0x588930u,pm_load(&m,0x588928u,4),4);pm_store(&m,0x588934u,0,4);pm_store(&m,0x588940u,0,4);pm_store(&m,0x5376b0u,3,4);
    }
    if (pm_load(&m,s+12u,4)==4u) {
        if (fresh) { uint32_t i;pm_store(&m,0x53e3a8u,0,4);pm_store(&m,0x53e3b0u,0,4);for (i=0;i<8u;i++) pm_store(&m,0x53e3b8u+i*4u,0,4);pm_store(&m,0x53e3f8u,0,4);pm_store(&m,0x562bccu,UINT32_MAX,4); }
        else pm_store(&m,0x562bccu,pm_load(&m,0x53e3f8u,4)-1u,4);
        uint32_t route=pm_load(&m,s+28u,4),stage=pm_load(&m,0x53e3f8u,4);
        pm_store(&m,0x562bf4u,pm_i32(&m,route+stage*64u+28u)-16,4);pm_store(&m,0x562bf8u,pm_i32(&m,route+stage*64u+32u)+48,4);
        pm_store(&m,0x562becu,fresh ? (uint32_t)-352 : (uint32_t)(pm_i32(&m,route+(stage-1u)*64u+28u)-16),4);
        pm_store(&m,0x562bf0u,fresh ? pm_load(&m,0x562bf8u,4) : (uint32_t)(pm_i32(&m,route+(stage-1u)*64u+32u)+48),4);
        pm_store(&m,0x588d68u,pm_load(&m,0x562becu,4),4);pm_store(&m,0x588d6cu,0,4);pm_store(&m,0x588d84u,1,4);
        return frame_menu_refresh(read,write,ctx,screen);
    }
    return m.error;
}
int frame_menu_update_audio(frame_data_read read,frame_data_write write,void *ctx,uint32_t event,uint32_t dt,int32_t typed,frame_menu_sound_sink sound,void *sound_ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t s=screen_at(pm_load(&m,0x562b90u,4)),phase=pm_load(&m,0x5376b0u,4),b,selected;int32_t old,next,target;int status=0;
    if (event>6u || dt>1000u) return -1;
    particles_update(&m,dt);pm_store(&m,0x74ba60u,0,1);
    pm_store(&m,0x5904fcu,dt,4);
    pm_store(&m,0x588504u,(pm_load(&m,0x588504u,4)+dt)&4095u,4);
    pm_store(&m,0x5884d0u,(pm_load(&m,0x5884d0u,4)+dt)%65535u,4);
    if (phase==3u) {
        uint32_t i;for (i=0;i<2u;i++) { uint32_t at=i ? 0x588b48u : 0x588b4cu;old=pm_i32(&m,at);target=pm_i32(&m,0x588718u+i*4u);next=old==target ? old : pm_add(old,pm_mul(pm_i32(&m,0x58892cu+i*4u),dt));if ((old<target && next>target)||(old>target && next<target)) next=target;pm_store(&m,at,next,4); }
        pm_store(&m,0x5884c0u,pm_sar(pm_i32(&m,0x588b4cu),8),4);pm_store(&m,0x5884c4u,pm_sar(pm_i32(&m,0x588b48u),8),4);
        if (pm_load(&m,0x588b4cu,4)==pm_load(&m,0x588718u,4) && pm_load(&m,0x588b48u,4)==pm_load(&m,0x58871cu,4)) { uint32_t page=pm_load(&m,0x562bc8u,4);pm_store(&m,0x562b90u,page,4);pm_store(&m,0x562bb4u,pm_load(&m,0x562bccu,4),4);pm_store(&m,0x5376b0u,pm_load(&m,screen_at(page)+12u,4)==4u ? 11u : 1u,4);if (sound && pm_load(&m,0x5376b0u,4)==11u) sound(sound_ctx,0x517a20u,0); }
    } else if (phase==11u) {
        uint32_t walk=pm_load(&m,0x588d84u,4);int32_t t=pm_i32(&m,0x588d6cu)+(int32_t)dt;pm_store(&m,0x588d6cu,t,4);
        if (walk==0u) {
            next=pm_add(pm_i32(&m,0x588d68u),pm_mul(pm_mul(t,t),32)/90000);pm_store(&m,0x562becu,next,4);
            if (t>=300) { pm_store(&m,0x588d68u,next,4);pm_store(&m,0x588d6cu,t-300,4);pm_store(&m,0x588d84u,1,4); }
        } else if (walk==1u) {
            next=pm_add(pm_i32(&m,0x588d68u),pm_mul(t,64)/300);pm_store(&m,0x562becu,next,4);
            if (pm_load(&m,0x562bf0u,4)!=pm_load(&m,0x562bf8u,4)) { if (next>=320) { pm_store(&m,0x562bf0u,pm_load(&m,0x562bf8u,4),4);pm_store(&m,0x562becu,(uint32_t)-352,4);pm_store(&m,0x588d68u,(uint32_t)-352,4);pm_store(&m,0x588d6cu,0,4); } }
            else if (next>=pm_i32(&m,0x562bf4u)-32) { pm_store(&m,0x588d6cu,0,4);pm_store(&m,0x588d84u,2,4); }
        } else if (walk==2u) {
            next=pm_add(pm_i32(&m,0x562bf4u),pm_mul(pm_mul(t-300,300-t),32)/90000);pm_store(&m,0x562becu,next,4);
            if (t>=300) { pm_store(&m,0x562becu,pm_load(&m,0x562bf4u,4),4);pm_store(&m,0x5376b0u,1,4);pm_store(&m,0x562bb4u,pm_load(&m,0x53e3f8u,4),4);if (sound) sound(sound_ctx,0x517a50u,0); }
        }
    } else if (phase==9u) status=initials(&m,dt,typed,sound,sound_ctx);
    else if (phase==10u) {
        uint32_t choice=pm_load(&m,0x74d7c0u,4);pm_store(&m,0x588ea4u,pm_load(&m,0x588ea0u,4),4);pm_store(&m,0x588e58u,0,4);
        if (choice<2u) {
            if (menu_key(&m,0x44u)) { if (sound && choice!=1u) sound(sound_ctx,0x5179c0u,1);choice=1; }
            if (menu_key(&m,0x48u)) { if (sound && choice!=0u) sound(sound_ctx,0x5179c0u,0);choice=0; }
            pm_store(&m,0x74d7c0u,choice,4);
            if (menu_key(&m,0x54u)||menu_key(&m,0x5cu)) { if (pm_load(&m,0x588d54u,4)) { if (sound) sound(sound_ctx,0x5179c0u,choice);pm_store(&m,0x74d7c0u,choice+2u,4);pm_store(&m,0x588938u,500,4); } }else pm_store(&m,0x588d54u,1,4);
            if (menu_key(&m,0x58u)) { if (pm_load(&m,0x588d58u,4)) pm_store(&m,0x5376b0u,1,4); }else pm_store(&m,0x588d58u,1,4);
        } else {
            int32_t remaining=pm_i32(&m,0x588938u)-(int32_t)dt;pm_store(&m,0x588938u,remaining>0 ? (uint32_t)remaining : 0u,4);
            if (remaining<=0) { if (choice==3u) status=frame_menu_enter(read,write,ctx,pm_load(&m,s+4u,4),0);else pm_store(&m,0x5376b0u,1,4); }
        }
    } else if (phase==12u && event==5u) {
        uint32_t result=pm_load(&m,0x562bfcu,4)+1u;pm_store(&m,0x562bfcu,result,4);
        if (result==4u && !pm_load(&m,0x59040cu,4)) { result=5;pm_store(&m,0x562bfcu,5,4); }
        if (result==5u && pm_load(&m,0x5903fcu,4) && !pm_load(&m,0x590400u,4)) {
            uint32_t page=pm_load(&m,0x53e954u,4);status=frame_menu_enter(read,write,ctx,page,0);if (status) return status;s=screen_at(page);
            pm_store(&m,0x562b90u,page,4);pm_store(&m,0x562bb4u,pm_load(&m,0x53e3f8u,4)-1u,4);pm_store(&m,0x5884c0u,pm_mul(pm_i32(&m,s+16u),640),4);pm_store(&m,0x5884c4u,pm_mul(pm_i32(&m,s+20u),416),4);pm_store(&m,0x5376b0u,11,4);if (sound) sound(sound_ctx,0x517a20u,0);
        } else if (result>=6u || (result==5u && pm_load(&m,0x590400u,4))) {
            /*51BE70/5175BD return to the front end. CUP-only policy changes
             * just the destination from original root0 to original CUP page2.
             * The external CD/video player is unmounted in the browser. */
            uint32_t next_phase=result==5u ? (pm_load(&m,0x590404u,4) ? 13u : 14u) : 1u;
            pm_store(&m,0x562b90u,2,4);pm_store(&m,0x562bb4u,0,4);pm_store(&m,0x5884c0u,(uint32_t)-640,4);pm_store(&m,0x5884c4u,(uint32_t)-416,4);pm_store(&m,0x5376b0u,next_phase,4);status=frame_menu_refresh(read,write,ctx,2);
            if (next_phase==13u) { pm_store(&m,0x562c40u,4096,4);pm_store(&m,0x562c3cu,0,4);pm_store(&m,0x588d7cu,0,4); }
        }
    } else if (phase==13u) {
        uint32_t at=pm_load(&m,0x754004u,4),ch;int32_t bottom=272+pm_sar(pm_i32(&m,0x562c40u),4);
        while (pm_load(&m,at,1)) { while ((ch=pm_load(&m,at,1)) && ch!=10u && ch!=13u) at++;if (ch==13u) { at++;ch=pm_load(&m,at,1); }if (ch==10u) at++;bottom+=24; }
        pm_store(&m,0x562c40u,pm_sub(pm_i32(&m,0x562c40u),(int32_t)dt),4);
        if (event==5u || bottom<-24) { pm_store(&m,0x5376b0u,1,4);status=frame_menu_refresh(read,write,ctx,2); }
    } else if (phase==14u) {
        if (!(pm_load(&m,0x590564u,4)%3u) && menu_random(&m)%100u<10u) { uint32_t count=menu_random(&m)&31u;int32_t y=(int32_t)(menu_random(&m)%480u),x=(int32_t)(menu_random(&m)%640u);particles_create(&m,x,y,count,10,2); }
        if (event==5u) {
        /*516AE0 case14: Enter/Space dismisses the original congratulations. */
        pm_store(&m,0x5376b0u,1,4);status=frame_menu_refresh(read,write,ctx,2);
        }
    } else if (phase==1u) {
        status=frame_menu_refresh(read,write,ctx,pm_load(&m,0x562b90u,4));if (status) return status;
        selected=first(&m,s,pm_load(&m,0x562bb4u,4));pm_store(&m,0x562bb4u,selected,4);
        if (event>=1u && event<=4u) { status=frame_menu_direction(read,write,ctx,event-1u,0);if (sound && selected!=pm_load(&m,0x562bb4u,4)) sound(sound_ctx,0x5179c0u,pm_load(&m,0x562bb4u,4)); }
        else if (event==6u) {
            if (pm_load(&m,s+12u,4)==4u) { unsigned char question[256];uint32_t at=pm_load(&m,0x537f84u,4),i;if (sound) sound(sound_ctx,0x519990u,0);for (i=0;i<255u;i++) { question[i]=(unsigned char)pm_load(&m,at+i,1);if (!question[i]) break; }question[i]=0;copy_to(&m,0x74b9e0u,question,i+1u);pm_store(&m,0x74d7c0u,0,4);pm_store(&m,0x588d54u,0,4);pm_store(&m,0x588d58u,0,4);pm_store(&m,0x58893cu,3,4);pm_store(&m,0x5376b0u,10,4); }
            else if (pm_load(&m,0x562b90u,4)!=2u) { if (sound) sound(sound_ctx,0x517990u,0);status=frame_menu_enter(read,write,ctx,pm_load(&m,s+4u,4),0); }
        }
        else if (event==5u && selected!=UINT32_MAX) {
            b=pm_load(&m,s+28u,4)+selected*64u;if (sound) sound(sound_ctx,pm_load(&m,b+44u,4)==1u ? 0x5186c0u : 0x5187e0u,0);
            if (pm_i32(&m,b+60u)!=-1) pm_store(&m,0x53d18cu,pm_load(&m,b+60u,4),4);
            if (pm_load(&m,b+39u,1)) { char name[16]="tank1.dat";uint32_t car=pm_load(&m,b+39u,1);name[4]=(char)('0'+car);if (car>9u) { name[4]='1';name[5]=(char)('0'+car-10u);memcpy(name+6,".dat",5); }copy_to(&m,0x5303c0u,(const unsigned char *)name,car>9u ? 11u : 10u);pm_store(&m,0x530440u,car,4); }
            if (pm_load(&m,b+16u,1)) { unsigned char map[9]={0};uint32_t i;for (i=0;i<8u;i++) map[i]=(unsigned char)pm_load(&m,b+16u+i,1);copy_to(&m,0x530340u,map,9);pm_store(&m,0x53e954u,pm_load(&m,0x562b90u,4),4); }
            if (pm_load(&m,b+38u,1)) status=frame_menu_enter(read,write,ctx,pm_load(&m,b+38u,1),1);
            if (pm_load(&m,b+44u,4)==1u) { pm_store(&m,0x52ee9cu,0,4);pm_store(&m,0x53e94cu,1,4); }
        }
        pm_store(&m,0x590420u+pm_load(&m,0x562b90u,4),pm_load(&m,0x562bb4u,4),1);
    }
    return status ? status : m.error;
}
int frame_menu_update_typed(frame_data_read r,frame_data_write w,void *ctx,uint32_t event,uint32_t dt,int32_t typed)
{ return frame_menu_update_audio(r,w,ctx,event,dt,typed,0,0); }
int frame_menu_update(frame_data_read read,frame_data_write write,void *ctx,uint32_t event,uint32_t dt)
{ return frame_menu_update_typed(read,write,ctx,event,dt,-1); }
int frame_menu_results_begin(frame_data_read read,frame_data_write write,void *ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t page=pm_load(&m,0x562b9cu,4),s=screen_at(page);
    if (!read || !write || page>=61u) return -1;
    /*50DE50 post-race branch: native race order, points and achievements
     * already come from40E390/frame_results_collect. */
    pm_store(&m,0x53e94cu,0,4);pm_store(&m,0x562b90u,page,4);pm_store(&m,0x562bfcu,1,4);pm_store(&m,0x5376b0u,12,4);pm_store(&m,0x5884c0u,pm_mul(pm_i32(&m,s+16u),640),4);pm_store(&m,0x5884c4u,pm_mul(pm_i32(&m,s+20u),416),4);
    if (pm_load(&m,0x590338u,4)) { pm_store(&m,0x562bdcu,1,4);pm_store(&m,0x562be0u,0,4);pm_store(&m,0x562be4u,0,4);pm_store(&m,0x562be8u,0,4);pm_store(&m,0x562bd4u,pm_load(&m,0x5903f8u,4),4);pm_store(&m,0x562bd8u,pm_load(&m,0x5903f0u,4),4);pm_store(&m,0x588ee8u,character_index(pm_load(&m,0x530458u,1))*65536u+32768u,4);pm_store(&m,0x5376b0u,9,4); }
    return m.error;
}
