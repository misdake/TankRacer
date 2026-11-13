#include "frame_results.h"
#include "physics_memory.h"
#define TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
static uint32_t object_of(physics_memory *m,uint32_t id)
{ uint32_t r=pm_load(m,pm_load(m,0x55c694u,4)+id*8u,4);return r ? pm_load(m,r+4u,4) : 0; }
int frame_results_constants(frame_data_write write,void *ctx)
{
    physics_memory m={0,write,ctx,0};uint32_t i;
    static const unsigned char cups[18]={0,1,2,4,255,255,3,2,6,1,5,255,6,3,4,0,5,7};
    static const uint32_t costs[5]={2,6,12,22,30},points[9]={UINT32_MAX,10,6,4,2,0,0,0,0};
    if (!write) return -1;
    for (i=0;i<18u;i++) pm_store(&m,0x52ee88u+i,cups[i],1);
    for (i=0;i<5u;i++) pm_store(&m,0x52eea0u+i*4u,costs[i],4);
    for (i=0;i<9u;i++) pm_store(&m,0x52eeb4u+i*4u,points[i],4);
    for (i=0;i<3u;i++) pm_store(&m,0x52f2d8u+i*4u,i,4);
    return m.error;
}
int frame_results_defaults(frame_data_write write,void *ctx)
{
    physics_memory m={0,write,ctx,0};uint32_t cup,map,place,kind,i,at;static const char names[]="AMHMATTIM381MS-SMC";
    if (!write) return -1;
    for (cup=0;cup<3u;cup++) for (map=0;map<16u;map++) for (kind=0;kind<2u;kind++) for (place=0;place<6u;place++) {
        at=(kind ? 0x53daa8u : 0x53d1a8u)+((cup*16u+map)*6u+place)*8u;
        for (i=0;i<3u;i++) pm_store(&m,at+4u+i,(unsigned char)names[place*3u+i],1);
        pm_store(&m,at,kind ? 120000u : 300000u,4);pm_store(&m,at+7u,((place+map)&3u)+cup*5u+1u,1);
    }
    return m.error;
}
int frame_results_route_random(frame_data_read read,frame_data_write write,void *ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t i,v;if (!read || !write) return -1;
    for (i=0;i<16u;i++) { v=pm_load(&m,0x55b6acu,4)*1664525u+1013904223u;pm_store(&m,0x55b6acu,v,4);pm_store(&m,0x546748u+i*2u,v>>4,2); }return m.error;
}
int frame_results_insert(frame_data_read read,frame_data_write write,void *ctx,uint32_t mode,uint32_t time,uint32_t vehicle,uint32_t place,uint32_t cup,uint32_t map,uint32_t player)
{
    physics_memory m={read,write,ctx,0};uint32_t at,flags,i,index;if (!read || !write || mode>1u) return -1;
    at=(mode ? 0x53daa8u : 0x53d1a8u)+(cup*16u+map)*48u+40u;flags=mode ? 0x590534u : 0x590514u;
    if (place<5u) for (i=5u-place;i;i--) { pm_store(&m,at,pm_load(&m,at-8u,4),4);pm_store(&m,at+4u,pm_load(&m,at-4u,4),4);pm_store(&m,flags,pm_load(&m,flags-4u,4),4);at-=8u;flags-=4u; }
    index=(cup*16u+map)*6u+place;at=(mode ? 0x53daa8u : 0x53d1a8u)+index*8u;
    pm_store(&m,at,time,4);pm_store(&m,at+7u,vehicle,1);for (i=0;i<3u;i++) pm_store(&m,at+4u+i,45,1);pm_store(&m,(mode ? 0x590520u : 0x590500u)+place*4u,player,4);return m.error;
}
static void achievement(physics_memory *m,uint32_t bit,uint32_t award)
{
    uint32_t flags=pm_load(m,0x53d19au,4);if (flags&bit) return;
    pm_store(m,0x53d19au,flags|bit,4);pm_store(m,0x59040cu,award,4);pm_store(m,0x53e400u,1,4);
}
/*40E390 opponent estimates and stable tie-break: completed lap times are
 * exact; unfinished rivals use the original CRT random/last-lap estimate. */
static uint32_t results_random(physics_memory *m)
{ uint32_t v=pm_load(m,0x538820u,4)*214013u+2531011u;pm_store(m,0x538820u,v,4);return (v>>16)&32767u; }
static int opponents(physics_memory *m,uint32_t mode)
{
    uint32_t times[7],objects[7],i,j,k,lap,rank,points,at,player_time=pm_load(m,0x59032cu,4);int32_t category;
    for (i=0;i<7u;i++) objects[i]=object_of(m,pm_load(m,0x53e8c0u+i*4u,4));
    for (i=0;i<7u && !objects[i];i++) { }
    if (i==7u) return m->error;
    for (i=0;i<7u;i++) if (!objects[i]) return m->error ? m->error : -4;
    for (i=0;i<7u;i++) {
        lap=pm_load(m,objects[i]+0xcb6u,4);times[i]=0;
        if (pm_signed(lap)<3) {
            if (pm_signed(lap)<2) times[i]=(results_random(m)&0x8fffu)+300000u;
            else { for (k=0;k<lap;k++) times[i]+=pm_load(m,objects[i]+0xcbeu+k*7u,4);for (;k<3u;k++) times[i]+=pm_load(m,objects[i]+0xcc5u,4); }
            if (times[i]<=player_time) times[i]=results_random(m)%5000u+10000u+player_time;
        } else for (k=0;k<3u;k++) times[i]+=pm_load(m,objects[i]+0xcbeu+k*7u,4);
    }
    for (i=0;i<7u;i++) {
        rank=1;for (j=0;j<7u;j++) if (i!=j && times[j]<=times[i] && (times[j]!=times[i] || i<j)) rank++;
        if (player_time<times[i]) rank++;
        if (rank==pm_load(m,0x590330u,4)) rank++;
        points=pm_load(m,0x52eeb4u+rank*4u,4);
        if (!mode) { category=pm_add(pm_i8(m,objects[i]+0xde0u),pm_mul(pm_sub(pm_i8(m,objects[i]+0xddfu),1)%5,2));at=0x53e3b8u+(uint32_t)category*4u;pm_store(m,at,pm_load(m,at,4)+points,4); }
        at=0x590360u+i*20u;pm_store(m,at,times[i],4);pm_store(m,at+4u,rank,4);pm_write32(m,at+8u,pm_i8(m,objects[i]+0xddfu));pm_write32(m,at+12u,pm_i8(m,objects[i]+0xde0u));pm_store(m,at+16u,points,4);
    }return m->error;
}
int frame_results_collect(frame_data_read read,frame_data_write write,void *ctx,uint32_t *action)
{
    physics_memory m={read,write,ctx,0};uint32_t o,mode,cup,map,rank,vehicle,points,i,j,k,l,at,value,current,category,selected,next,wins;int status;
    if (!read || !write || !action) return -1;
    if (pm_load(&m,0x53e908u,4) || pm_load(&m,0x53e928u,4)) { if (m.error) return m.error;*action=2;return 0; }
    mode=pm_load(&m,0x52ee9cu,4);if (mode>1u) return -5;
    o=object_of(&m,pm_load(&m,0x53e8b8u,4));cup=pm_load(&m,0x53d18cu,4);map=pm_load(&m,0x52ed90u,4);
    if (!o || cup>=3u || map>=16u) return m.error ? m.error : -4;
    pm_store(&m,0x590480u,1,4);pm_store(&m,0x59040cu,0,4);
    for (i=0;i<3u;i++) pm_store(&m,0x590320u+i*4u,pm_load(&m,o+0xcbeu+i*7u,4),4);
    current=pm_load(&m,o+0xcb6u,4);
    if (pm_signed(current)<3) { pm_store(&m,0x590328u,600000,4);if (pm_signed(current)<2) pm_store(&m,0x590324u,600000,4);if (pm_signed(current)<1) pm_store(&m,0x590320u,600000,4);pm_store(&m,0x59032cu,600000,4); }
    else pm_store(&m,0x59032cu,pm_load(&m,o+0xcccu,4)+pm_load(&m,o+0xcbeu,4)+pm_load(&m,o+0xcc5u,4),4);
    rank=pm_load(&m,o+0xdddu,1);vehicle=(uint32_t)pm_i8(&m,o+0xddfu);if (rank>8u) return -4;
    pm_store(&m,0x590330u,rank,4);pm_store(&m,0x590334u,vehicle,4);pm_store(&m,0x5903ecu,mode,4);points=pm_load(&m,0x52eeb4u+rank*4u,4);pm_store(&m,0x59033cu,points,4);
    pm_store(&m,0x5903f0u,cup,4);pm_store(&m,0x5903f4u,map,4);pm_store(&m,0x5903f8u,UINT32_MAX,4);pm_store(&m,0x53e3a8u,pm_load(&m,0x53e3a8u,4)+points,4);
    for (i=0;i<6u;i++) if (pm_load(&m,0x52ee88u+cup*6u+i,1)==map) pm_store(&m,0x5903f8u,i,4);
    TRY(opponents(&m,mode));
    for (i=0;i<6u;i++) pm_store(&m,0x590500u+i*4u,0,4);
    for (i=0;i<6u;i++) pm_store(&m,0x590520u+i*4u,0,4);
    pm_store(&m,0x590338u,0,4);pm_store(&m,0x590358u,0,4);
    if (!pm_load(&m,0x53e414u,4)) {
        for (j=0;j<4u;j++) {
            value=pm_load(&m,j ? 0x59031cu+j*4u : 0x59032cu,4);
            for (i=0;i<6u;i++) { at=(j ? 0x53daa8u : 0x53d1a8u)+((cup*16u+map)*6u+i)*8u;if (value<pm_load(&m,at,4)) { TRY(frame_results_insert(read,write,ctx,j!=0u,value,vehicle,i,cup,map,1));break; } }
        }
    }
    for (j=0;j<2u;j++) for (i=0;i<6u;i++) { value=pm_load(&m,(j ? 0x590520u : 0x590500u)+i*4u,4);if (value==1u) pm_store(&m,0x590338u,1,4);if (value==2u) pm_store(&m,0x590358u,1,4); }
    pm_store(&m,0x5903fcu,0,4);pm_store(&m,0x590404u,0,4);pm_store(&m,0x590400u,0,4);pm_store(&m,0x590408u,0,4);
    if (!mode) {
        wins=pm_load(&m,0x53e3b0u,4)+1u;
        if (rank!=1u) { pm_store(&m,0x53e3b0u,0,4);wins=0; }
        else switch (pm_load(&m,0x53e3b0u,4)) {
        case 2:achievement(&m,cup==0u ? 0x2000u : cup==1u ? 0x4000u : 16u,cup==0u ? 13u : cup==1u ? 14u : 5u);break;
        case 3:if (!cup) { if (!pm_load(&m,0x53d194u,2)) { pm_store(&m,0x59040cu,10,4);pm_store(&m,0x53d194u,1,2);pm_store(&m,0x53e400u,1,4); } }else achievement(&m,cup==1u ? 64u : 32u,cup==1u ? 7u : 6u);break;
        case 4:if (cup && !pm_load(&m,0x53d194u+cup*2u,2)) { pm_store(&m,0x59040cu,cup+10u,4);pm_store(&m,0x53e3b0u,wins,4);pm_store(&m,0x53d194u+cup*2u,1,2);pm_store(&m,0x53e400u,1,4);wins=pm_load(&m,0x53e3b0u,4); }break;
        case 5:if (cup==2u) achievement(&m,128,8);break;
        default:break;
        }
        pm_store(&m,0x53e3b0u,wins,4);category=(uint32_t)(pm_sub(pm_signed(vehicle),1)%5);if (category==4u) category=pm_load(&m,0x52f2d8u+cup*4u,4);selected=category;
        for (i=0;i<4u;i++) for (j=0;j<2u;j++) {
            at=0x53e3d8u+(j+i*2u)*4u;
            if (selected==i && !j) pm_store(&m,0x53e3acu,1,4);else pm_store(&m,at,1,4);
            for (k=0;k<4u;k++) for (l=0;l<2u;l++) if (k!=i || l!=j) {
                current=pm_load(&m,0x53e3b8u+(j+i*2u)*4u,4);value=pm_load(&m,0x53e3b8u+(l+k*2u)*4u,4);
                if (selected==k && !l) { if (pm_signed(current)<pm_i32(&m,0x53e3a8u)) pm_store(&m,at,pm_load(&m,at,4)+1u,4); }
                else if (selected==i && !j) { if (pm_i32(&m,0x53e3a8u)<pm_signed(value)) pm_store(&m,0x53e3acu,pm_load(&m,0x53e3acu,4)+1u,4); }
                else if (pm_signed(current)<pm_signed(value)) pm_store(&m,at,pm_load(&m,at,4)+1u,4);
            }
        }
        current=pm_load(&m,0x53e3f8u,4);next=current+1u;
        if (next!=6u && pm_i8(&m,0x52ee89u+cup*6u+current)!=-1) {
            pm_store(&m,0x53e3f8u,next,4);
            if (pm_i32(&m,0x52ee9cu+next*4u)<=pm_i32(&m,0x53e3a8u)) { at=0x53d19cu+cup*4u;if (pm_load(&m,at,4)<next) { pm_store(&m,at,next,4);pm_store(&m,0x53e400u,1,4); }pm_store(&m,0x5903fcu,1,4); }
        } else {
            pm_store(&m,0x590408u,1,4);pm_store(&m,0x53e3f8u,next,4);
            if (pm_load(&m,0x53e3acu,4)==1u) { pm_store(&m,0x590400u,1,4);value=pm_load(&m,0x53d19au,4);if (!(value&(512u<<cup))) { pm_store(&m,0x53d19au,(value|(512u<<cup))&65535u,2);pm_store(&m,0x53e400u,1,4); }if (cup==2u) { pm_store(&m,0x590404u,1,4);pm_store(&m,0x53e400u,1,4); }else if (cup==pm_load(&m,0x53d190u,4)) { pm_store(&m,0x53d190u,cup+1u,4);pm_store(&m,0x53e400u,1,4); } }
        }
    }
    if (m.error) return m.error;
    *action=1;return 0;
}
int frame_results_initials(frame_data_read read,frame_data_write write,void *ctx,uint32_t player)
{
    physics_memory m={read,write,ctx,0};uint32_t i,j,kind,at,cup,map,match=1,flags;if (!read || !write) return -1;
    for (i=0;i<3u;i++) if (pm_load(&m,0x530458u+i,1)!=(unsigned char)"T5C"[i]) match=0;
    if (match) { flags=pm_load(&m,0x53d19au,4);if (!(flags&4096u)) { pm_store(&m,0x53d19au,flags|4096u,2);pm_store(&m,0x54671cu,1,4);for (i=0;i<3u;i++) pm_store(&m,0x530458u+i,(unsigned char)"MPH"[i],1); }else { pm_store(&m,0x53d19au,flags&~4096u,2);pm_store(&m,0x54671cu,0,4); } }
    cup=pm_load(&m,0x5903f0u,4);map=pm_load(&m,0x5903f4u,4);
    for (kind=0;kind<2u;kind++) for (i=0;i<6u;i++) if (pm_load(&m,(kind ? 0x590520u : 0x590500u)+i*4u,4)==player+1u) { at=(kind ? 0x53daa8u : 0x53d1a8u)+((cup*16u+map)*6u+i)*8u+4u;for (j=0;j<3u;j++) pm_store(&m,at+j,pm_load(&m,0x530458u+player*4u+j,1),1); }
    return m.error;
}
int frame_results_lap_display(frame_data_read read,frame_data_write write,void *ctx,uint32_t object)
{
    physics_memory m={read,write,ctx,0};uint32_t i=0,at=object+0xcbeu,v;int32_t elapsed;if (!read || !write) return -1;
    while (pm_signed(i)<pm_i32(&m,object+0xcb6u)) { if (i>=3u) return -4;if (pm_load(&m,at+4u,1)>99u) pm_store(&m,at+4u,99,1);i++;at+=7u; }
    if (pm_i32(&m,object+0xcb6u)<3) { elapsed=pm_i32(&m,object+0xcbau);pm_store(&m,at,(uint32_t)elapsed,4);v=(uint32_t)(elapsed/60000)&255u;pm_store(&m,at+4u,v,1);if (v>99u) pm_store(&m,at+4u,99,1);pm_store(&m,at+5u,(uint32_t)((elapsed/1000)%60),1);pm_store(&m,at+6u,(uint32_t)((elapsed/10)%100),1); }return m.error;
}
int frame_results_rank_display(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t i,o,rank=1;if (!read || !write || !out) return -1;
    for (i=0;i<9u;i++) { uint32_t id=pm_load(&m,0x53e8e0u+i*4u,4);if (id) { o=object_of(&m,id);if (o!=object && pm_i32(&m,object+0xd18u)<pm_i32(&m,o+0xd18u)) rank=(rank+1u)&255u; } }
    pm_store(&m,object+0xddcu,rank,1);if (m.error) return m.error;*out=rank;return 0;
}
int frame_results_hud(frame_data_read read,frame_data_write write,void *ctx,uint32_t slot)
{
    physics_memory m={read,write,ctx,0};uint32_t o,rank,mode,v;int status;if (!read || !write || slot>1u) return -1;
    if (pm_load(&m,0x53e928u,4)) return -5;
    o=object_of(&m,pm_load(&m,0x53e8b8u+slot*4u,4));if (!o) return m.error ? m.error : -4;
    pm_store(&m,0x52fbb6u,pm_load(&m,0x53e510u+pm_load(&m,o+0xd0fu,4)*4u,4),4);pm_store(&m,0x52fd14u,pm_load(&m,o+0xdecu,1) ? pm_load(&m,0x53e548u,4) : 0u,4);
    v=pm_load(&m,0x53e4f0u,4);if (v) { uint32_t stage=pm_signed(v)>2000 ? 3u : pm_signed(v)>1000 ? 2u : 1u;if (pm_load(&m,0x52fa28u,4)!=stage) pm_store(&m,0x52fa28u,stage,4); }
    if (pm_load(&m,0x53e4f4u,4) && pm_load(&m,0x52fa28u,4)) pm_store(&m,0x52fa28u,0,4);
    mode=pm_load(&m,0x52ee9cu,4);if (mode==2u || mode==4u) return -5;
    TRY(frame_results_lap_display(read,write,ctx,o));rank=pm_load(&m,o+0xdddu,1);
    if (!pm_load(&m,o+0xde6u,1) && (pm_load(&m,0x53e3fcu,4) || (mode==5u && pm_load(&m,0x543d34u,4)))) { TRY(frame_results_rank_display(read,write,ctx,o,&rank)); }
    if (pm_load(&m,0x53e3fcu,4)) pm_store(&m,o+0xdddu,rank,1);
    return m.error;
}
#undef TRY
