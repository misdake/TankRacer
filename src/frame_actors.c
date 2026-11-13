#include "frame_actors.h"
#include "frame_text.h"
#include "frame_lap_events.h"
#include "physics_memory.h"
#include "frame_ufo.h"
#include "frame_camera.h"

static uint32_t object_of(physics_memory *m,uint32_t entity)
{ uint32_t r=pm_load(m,pm_load(m,0x55c694u,4)+entity*8u,4);return r ? pm_load(m,r+4u,4) : 0; }
static int parent(physics_memory *m,uint32_t entity,uint32_t parser,const frame_entity_factory_services *e)
{ uint32_t klass=pm_load(m,0x5466e4u,4),unused;if (m->error) return m->error;return frame_class_dispatch_payload(m->read,m->ctx,klass,entity,parser,e->method,e->method_ctx,&unused); }
static int find(physics_memory *m,uint32_t parser,const char *name,uint32_t bytes,uint32_t limit,uint32_t *out)
{ if (m->error) return m->error;return frame_text_find_name(m->read,m->write,m->ctx,parser,(const unsigned char *)name,bytes,limit,out); }
#define FIND(name) find(&m,parser,name,(uint32_t)sizeof(name),e->work_limit,&found)
#define TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
static int list_remove(physics_memory *m,uint32_t entity,uint32_t global,const frame_entity_factory_services *e)
{ uint32_t h=pm_load(m,global,4);if (m->error) return m->error;return frame_list_remove_value(m->read,m->write,m->ctx,entity,h,e->release,e->system_ctx,e->work_limit); }
int frame_smoker_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,
                               const frame_entity_factory_services *e,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,i,j,found,head,unused;int status;
    if (!read || !write || !e || !e->work_limit || !out) return -1;
    o=object_of(&m,entity);TRY(parent(&m,entity,parser,e));
    for (i=0;i<16u;i++) {
        pm_store(&m,o+0x90u+i*42u,0,2);
        for (j=0;j<3u;j++) pm_store(&m,o+0x92u+i*42u+j*4u,pm_load(&m,o+j*4u,4),4);
    }
    TRY(FIND(":white"));pm_store(&m,o+0x31eu,found ? 1u : 0u,4);
    TRY(FIND(":billow"));if (found) pm_store(&m,o+0x31du,1,1);
    TRY(list_remove(&m,entity,0x54b48cu,e));head=pm_load(&m,0x54b488u,4);
    TRY(frame_list_append(read,write,ctx,entity,head,e->allocate,e->release,e->system_ctx,e->work_limit,&unused));
    *out=0;return m.error;
}
static uint32_t sequence(physics_memory *m)
{ return pm_load(m,0x531828u+pm_load(m,0x545e18u,4)*4u,4); }
static void advance(physics_memory *m)
{ uint32_t x=pm_load(m,0x545e18u,4);pm_store(m,0x545e18u,x==9u ? 0u : x+1u,4); }
int frame_smoker_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity)
{
    physics_memory m={read,write,ctx,0};uint32_t o,p,i,available=0,near,dt,delta,life,value;int status;
    if (!read || !write) return -1;
    o=object_of(&m,entity);pm_store(&m,o+0x31cu,0,1);
    TRY(frame_entity_near_camera(read,ctx,o,0,19200,&near));if (!near) return m.error;
    pm_store(&m,o+0x31cu,1,1);dt=pm_load(&m,0x5904fcu,4);delta=(uint32_t)pm_sar(pm_signed(dt),4);
    for (i=0;i<16u;i++) {
        p=o+0x78u+i*42u;
        if (pm_load(&m,p+38u,1)) {
            life=pm_load(&m,p+40u,1);
            if (pm_signed(life)<pm_signed(delta)) pm_store(&m,p+38u,0,1);
            else {
                pm_store(&m,p+40u,life-delta,1);value=pm_load(&m,p,2)*dt;
                if (!pm_load(&m,p+39u,1)) value=0u-value;
                pm_store(&m,p+24u,pm_load(&m,p+24u,2)+value,2);
                pm_store(&m,p+16u,pm_load(&m,p+16u,4)+sequence(&m)+(uint32_t)pm_sar(pm_signed(dt),2),4);advance(&m);
                pm_store(&m,p+20u,pm_load(&m,p+20u,4)+sequence(&m)+(uint32_t)pm_sar(pm_signed(dt),2),4);advance(&m);
                pm_store(&m,p+26u,pm_load(&m,p+26u,4)+pm_load(&m,p+4u,4)*dt-sequence(&m),4);advance(&m);
                pm_store(&m,p+30u,pm_load(&m,p+30u,4)+pm_load(&m,p+8u,4)*dt+sequence(&m),4);advance(&m);
                pm_store(&m,p+34u,pm_load(&m,p+34u,4)+pm_load(&m,p+12u,4)*dt-sequence(&m),4);advance(&m);
                continue;
            }
        }
        available=p; /* last free/expired particle, as the original */
    }
    if (pm_i32(&m,o+0x318u)<pm_signed(dt)) {
        if (available) {
            p=available;
            for (i=0;i<3u;i++) { pm_store(&m,p+26u+i*4u,sequence(&m)*128u-576u+pm_load(&m,o+i*4u,4),4);advance(&m); }
            pm_store(&m,p+4u,UINT32_MAX,4);
            value=2;
            if (pm_load(&m,o+0x31du,1)) { value=sequence(&m)>>2;pm_store(&m,p+8u,value,4);advance(&m); }
            else pm_store(&m,p+8u,value,4);
            pm_store(&m,p+12u,UINT32_MAX,4);
            pm_store(&m,p+38u,1,1);pm_store(&m,p+39u,sequence(&m)&1u,1);advance(&m);
            pm_store(&m,p,sequence(&m)<<1,4);advance(&m);pm_store(&m,p+24u,sequence(&m)<<11,2);advance(&m);
            pm_store(&m,p+40u,(sequence(&m)+55u)*2u,1);advance(&m);
            pm_store(&m,p+16u,(sequence(&m)+25u)*16u,4);advance(&m);pm_store(&m,p+20u,(sequence(&m)+25u)*16u,4);advance(&m);
            pm_store(&m,o+0x318u,sequence(&m)+150u,4);advance(&m);
        }
    } else pm_store(&m,o+0x318u,pm_load(&m,o+0x318u,4)-dt,4);
    return m.error;
}
int frame_smoker_render(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_actor_services *graphics)
{
    physics_memory m={read,write,ctx,0};uint32_t o,p,i,alpha;int status;
    if (!read || !write) return -1;
    o=object_of(&m,entity);if (!pm_load(&m,o+0x31cu,1)) return m.error;
    pm_store(&m,0x545df8u,pm_load(&m,o+0x31eu,4),4);
    for (i=0;i<16u;i++) {
        p=o+0x78u+i*42u;if (!pm_load(&m,p+38u,1)) continue;
        alpha=pm_load(&m,p+40u,1);pm_store(&m,0x545e0cu,alpha,1);pm_store(&m,0x545e0du,alpha,1);pm_store(&m,0x545e0eu,alpha,1);
        pm_store(&m,0x545e00u,pm_load(&m,p+16u,4),4);pm_store(&m,0x545e04u,pm_load(&m,p+20u,4),4);pm_store(&m,0x545e10u,pm_load(&m,p+24u,2),2);
        pm_store(&m,0x545decu,pm_load(&m,p+26u,4),4);pm_store(&m,0x545df0u,pm_load(&m,p+30u,4),4);pm_store(&m,0x545df4u,pm_load(&m,p+34u,4),4);
        if (m.error) return m.error;
        if (!graphics || !graphics->sprite) return -5;
        TRY(graphics->sprite(graphics->sprite_ctx,0x545de8u));
    }
    return m.error;
}
/* x87 FISTP signed64 and EAX low32 after an exact binary32 power-of-two
 * scale. Handles signed overflow/NaN indefinite without undefined C casts. */
static uint32_t float_integer(uint32_t bits,uint32_t scale)
{
    uint32_t exponent=(bits>>23)&255u,mantissa=(bits&0x7fffffu)|0x800000u,sign=bits>>31;uint64_t magnitude;
    if (!exponent || exponent==255u || exponent+scale<127u || exponent+scale>=191u) return 0;
    if (exponent+scale>=150u) magnitude=(uint64_t)mantissa<<(exponent+scale-150u);
    else magnitude=mantissa>>(150u-exponent-scale);
    if (magnitude>INT64_MAX && !(sign && magnitude==(UINT64_C(1)<<63))) return 0;
    return sign ? 0u-(uint32_t)magnitude : (uint32_t)magnitude;
}
int frame_noisy_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,
                              const frame_entity_factory_services *e,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,found,more,i,j,n,bits=0,defined=0;unsigned char name[1024];int status;
    if (!read || !write || !e || !e->work_limit || !out) return -1;
    o=object_of(&m,entity);TRY(parent(&m,entity,parser,e));TRY(FIND(":sound_list"));
    if (found) {
        TRY(frame_text_read_i32(read,write,ctx,parser,o+0x78u,e->work_limit,&more));n=pm_load(&m,o+0x78u,4);
        for (i=0;pm_signed(i)<pm_signed(n);i++) {
            if (i>=e->work_limit) return -3;
            TRY(frame_text_next(read,write,ctx,parser,name,1024,e->work_limit,&more));pm_store(&m,0x544e28u+i*20u,0,4);frame_entity_sound_name(e,o,i,name);
            for (j=0;j<4u;j++) {
                TRY(frame_text_read_f32_value(read,write,ctx,parser,e->work_limit,&bits,&more));if (more) defined=1;if (!defined) return -4;
                pm_store(&m,0x544e18u+i*20u+j*4u,float_integer(bits,10),4);
            }
        }
    }
    pm_store(&m,o+0x7cu,UINT32_MAX,4);TRY(list_remove(&m,entity,0x54b48cu,e));*out=entity;return m.error;
}
int frame_noisy_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t limit)
{
    physics_memory m={read,write,ctx,0};uint32_t o,index,near,n,i;int status;
    if (!read || !write || !limit) return -1;
    o=object_of(&m,entity);if ((entity^pm_load(&m,0x590564u,4))&4u) return m.error;
    index=pm_load(&m,o+0x7cu,4);
    if (index!=UINT32_MAX) {
        TRY(frame_entity_near_camera(read,ctx,0x544e18u+index*20u,0,pm_i32(&m,0x544e24u+index*20u),&near));
        if (!near) pm_store(&m,o+0x7cu,UINT32_MAX,4);
        return m.error;
    }
    n=pm_load(&m,o+0x78u,4);
    /* Original compares absolute end pointer unsigned; preserve wrap. */
    if (0x544e18u>=0x544e18u+n*20u) return m.error;
    for (i=0;0x544e18u+i*20u<0x544e18u+n*20u;i++) {
        if (i>=limit) return -3;
        TRY(frame_entity_near_camera(read,ctx,0x544e18u+i*20u,0,pm_i32(&m,0x544e24u+i*20u),&near));
        if (near) { pm_store(&m,o+0x7cu,i,4);pm_store(&m,o+0x80u,0,4);break; } /* disabled voice ID */
    }
    return m.error;
}
static int same(const unsigned char *a,const char *b)
{ uint32_t i,x,y;for (i=0;i<64u;i++) { x=a[i];y=(unsigned char)b[i];if (x>=65u && x<=90u) x+=32u;if (y>=65u && y<=90u) y+=32u;if (x!=y) return 0;if (!x) return 1; }return 0; }
static int weather(physics_memory *memory,uint32_t parser,uint32_t object,uint32_t slot,const frame_entity_factory_services *e)
{
    physics_memory m=*memory;unsigned char type[64],strength[64];uint32_t more;int status;
    TRY(frame_text_next(m.read,m.write,m.ctx,parser,type,64,e->work_limit,&more));
    TRY(frame_text_next(m.read,m.write,m.ctx,parser,strength,64,e->work_limit,&more));
    if (same(type,"rain")) pm_store(&m,object+0x94u+slot,2,1);
    else if (same(type,"snow")) pm_store(&m,object+0x94u+slot,3,1);
    else if (same(type,"lightning")) pm_store(&m,object+0x94u+slot,4,1);
    else if (same(type,"rain_and_lightning")) pm_store(&m,object+0x94u+slot,5,1);
    else if (same(type,"off")) pm_store(&m,object+0x94u+slot,1,1);
    if (same(strength,"light")) pm_store(&m,object+0x97u+slot,8,1);
    else if (same(strength,"medium")) pm_store(&m,object+0x97u+slot,16,1);
    else if (same(strength,"heavy")) pm_store(&m,object+0x97u+slot,32,1);
    memory->error=m.error;return m.error;
}
static int waypoint_insert(physics_memory *memory,uint32_t entity,const frame_entity_factory_services *e)
{
    physics_memory m=*memory;uint32_t o,head,unused;int32_t x,z;int status;
    o=object_of(&m,entity);if (!o) return m.error;
    x=pm_sar(pm_sub(pm_i32(&m,o),pm_i32(&m,0x546870u)),pm_load(&m,0x546878u,4));
    z=pm_sar(pm_sub(pm_i32(&m,o+8u),pm_i32(&m,0x546874u)),pm_load(&m,0x54687cu,4));
    if (x>=0 && x<8 && z>=0 && z<8) {
        head=pm_load(&m,0x546770u+((uint32_t)x+(uint32_t)z*8u)*4u,4);
        TRY(frame_list_prepend(m.read,m.write,m.ctx,entity,head,e->allocate,e->release,e->system_ctx,e->work_limit,&unused));
    }
    head=pm_load(&m,0x5468a0u,4);TRY(frame_list_append(m.read,m.write,m.ctx,entity,head,e->allocate,e->release,e->system_ctx,e->work_limit,&unused));
    memory->error=m.error;return m.error;
}
int frame_waypoint_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,
                                 const frame_entity_factory_services *e,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t o,found,more,value,bits=0,defined=0,i,at,symbol;unsigned char name[1024];int status;
    if (!read || !write || !e || !e->work_limit || !out) return -1;
    o=object_of(&m,entity);if (!o) { if (m.error) return m.error;*out=0;return 0; }
    TRY(parent(&m,entity,parser,e));TRY(FIND(":next"));
    if (found) {
        TRY(frame_text_next(read,write,ctx,parser,name,1024,e->work_limit,&more));at=o+0x78u;i=0;
        while (more && name[0]!=41u && name[0]!=58u) {
            if (i++>=e->work_limit) return -3;
            TRY(frame_symbol_intern_name(read,write,ctx,name,1024,e,&symbol));pm_store(&m,at,symbol,4);at+=4u;
            TRY(frame_text_next(read,write,ctx,parser,name,1024,e->work_limit,&more));
        }
    }
    for (i=0;i<6u;i++) pm_store(&m,o+0x94u+i,0,1);
    TRY(FIND(":weather_1"));if (found) TRY(weather(&m,parser,o,0,e));
    TRY(FIND(":weather_2"));if (found) TRY(weather(&m,parser,o,1,e));
    TRY(FIND(":weather_3"));if (found) TRY(weather(&m,parser,o,2,e));
    TRY(FIND(":lap"));if (found) pm_store(&m,o+0x80u,1,1);
    TRY(FIND(":hairpin"));if (found) pm_store(&m,o+0x81u,1,1);
    TRY(FIND(":width"));if (found) { TRY(frame_text_read_i32(read,write,ctx,parser,o+0x88u,e->work_limit,&more));pm_store(&m,o+0x88u,pm_load(&m,o+0x88u,4)<<10,4); }
    else pm_store(&m,o+0x88u,20480,4);
    TRY(FIND(":gate_size"));if (found) {
        TRY(frame_text_read_f32_value(read,write,ctx,parser,e->work_limit,&bits,&more));if (!more) return -4;defined=1;
        pm_store(&m,o+0x84u,float_integer(bits,0)<<10,4);
    }
    TRY(FIND(":max_speed"));if (found) TRY(frame_text_read_i32(read,write,ctx,parser,o+0x8cu,e->work_limit,&more));else pm_store(&m,o+0x8cu,1000000,4);
    TRY(FIND(":braking_range"));if (found) {
        value=parser;TRY(frame_text_read_value(read,write,ctx,parser,e->work_limit,&value,&more));pm_store(&m,o+0x82u,value,2);pm_store(&m,o+0x82u,pm_load(&m,o+0x82u,2)<<10,2);
    }
    TRY(FIND(":lap_to_activate"));if (found) {
        value=parser;TRY(frame_text_read_value(read,write,ctx,parser,e->work_limit,&value,&more));pm_store(&m,o+0x9au,value,1);pm_store(&m,o+0x9au,pm_load(&m,o+0x9au,1)-1u,1);
    } else pm_store(&m,o+0x9au,255,1);
    TRY(FIND(":check"));if (found) {
        value=bits;TRY(frame_text_read_value(read,write,ctx,parser,e->work_limit,&value,&more));if (!more && !defined) return -4;pm_store(&m,o+0x9bu,value,1);
    }
    TRY(FIND(":backwards_check"));if (found) pm_store(&m,0x54689cu,entity,4);
    TRY(waypoint_insert(&m,entity,e));pm_store(&m,o+0x48u,0,4);TRY(list_remove(&m,entity,0x54b48cu,e));
    TRY(frame_entity_queue(read,write,ctx,entity,0,e->allocate,e->release,e->system_ctx,e->work_limit));
    *out=entity;return m.error;
}
#undef TRY
#undef FIND
int frame_waypoint_grid_initialize(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t world,i,n,shift,width,used=0,list;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    world=pm_load(&m,0x55ae60u,4);
    pm_store(&m,0x546870u,pm_load(&m,world+0x108u,4)<<3,4);pm_store(&m,0x546874u,pm_load(&m,world+0x10cu,4)<<3,4);
    pm_store(&m,0x546878u,pm_load(&m,world+0x110u,2)+3u,4);pm_store(&m,0x54687cu,pm_load(&m,world+0x112u,2)+3u,4);
    pm_store(&m,0x546880u,1u<<(pm_load(&m,0x546878u,4)&31u),4);
    pm_store(&m,0x546888u,1,4);pm_store(&m,0x546884u,1u<<(pm_load(&m,0x54687cu,4)&31u),4);
    for (i=0;i<2u;i++) {
        if (i) pm_store(&m,0x54688cu,1,4);
        shift=0x546880u+i*4u;width=pm_load(&m,shift,4);n=1;
        while (pm_signed(width*n)<153600) {
            if (m.error) return m.error;
            if (used++>=e->work_limit) return -3;
            n++;
        }
        if (n>1u) pm_store(&m,0x546888u+i*4u,n,4);
    }
    for (i=0;i<64u;i++) {
        if (m.error) return m.error;
        status=frame_list_create(read,write,ctx,e->allocate,e->release,e->system_ctx,e->work_limit,&list);if (status) return status;
        pm_store(&m,0x546770u+i*4u,list,4);
    }
    return m.error;
}
int frame_waypoint_grid_destroy(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t i,list;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    for (i=0;i<64u;i++) {
        list=pm_load(&m,0x546770u+i*4u,4);if (m.error) return m.error;
        if (list) { status=frame_list_destroy(read,write,ctx,list,e->release,e->system_ctx,e->work_limit);if (status) return status; }
        pm_store(&m,0x546770u+i*4u,0,4);
    }
    return m.error;
}

static int waypoint_weather(physics_memory *m,uint32_t start,uint32_t limit)
{
    uint32_t lap,id,o,kind,last,offset,i,used=0,value=0,word;
    for (lap=0;lap<3u;lap++) {
        id=start;offset=lap*64u;
        do {
            if (used++>=limit) return -3;
            o=object_of(m,id);kind=pm_load(m,o+0x94u+lap,1);
            if (kind==1u) value=0;else if (kind>=2u && kind<=5u) value=pm_load(m,o+0x97u+lap,1)|(kind==2u ? 1u : kind==3u ? 2u : kind==4u ? 4u : 5u);
            last=pm_load(m,o+0x90u,4);pm_store(m,0x58ed40u+offset+last,value,1);id=pm_load(m,o+0x78u,4);if (m->error) return m->error;
        } while (id!=start);
        if (last<64u) {
            word=value*0x01010101u;
            for (i=0;i<(64u-last)/4u;i++) pm_store(m,0x58ed40u+offset+last+i*4u,word,4);
            for (i=((64u-last)&~3u);i<64u-last;i++) pm_store(m,0x58ed40u+offset+last+i,value,1);
        }
    }
    return m->error;
}
int frame_waypoint_link(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t lists[3],head,node,id,o,at,symbol,found,unused,i,start=0,next,index=0,used=0;int status;
#define LINK_TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
#define LINK_STEP() do { if (m.error) return m.error;if (used++>=e->work_limit) return -3; } while (0)
    if (!read || !write || !e || !e->work_limit) return -1;
    for (i=0;i<3u;i++) { LINK_TRY(frame_list_create(read,write,ctx,e->allocate,e->release,e->system_ctx,e->work_limit,&lists[i])); }
    head=pm_load(&m,0x5468a0u,4);node=pm_load(&m,head,4);
    while (node!=head) {
        LINK_STEP();id=pm_load(&m,node+8u,4);o=object_of(&m,id);
        if (o) for (i=0;i<2u;i++) {
            at=o+0x78u+i*4u;symbol=pm_load(&m,at,4);
            if (!symbol || pm_load(&m,symbol,4)!=128u) pm_store(&m,at,0,4);
            else {
                next=pm_load(&m,symbol+4u,4);pm_store(&m,at,next,4);
                LINK_TRY(frame_list_contains(read,ctx,next,lists[0],e->work_limit,&found));
                if (found) { LINK_TRY(frame_list_append(read,write,ctx,next,lists[2],e->allocate,e->release,e->system_ctx,e->work_limit,&unused)); }
                LINK_TRY(frame_list_append(read,write,ctx,next,lists[0],e->allocate,e->release,e->system_ctx,e->work_limit,&unused));
                if (i) { LINK_TRY(frame_list_append(read,write,ctx,id,lists[1],e->allocate,e->release,e->system_ctx,e->work_limit,&unused)); }
            }
        }
        node=pm_load(&m,node,4);
    }
    node=pm_load(&m,head,4);
    while (node!=head) {
        LINK_STEP();id=pm_load(&m,node+8u,4);o=object_of(&m,id);
        if (o) { LINK_TRY(frame_list_contains(read,ctx,id,lists[1],e->work_limit,&found));if (found) {
            next=pm_load(&m,o+0x7cu,4);LINK_TRY(frame_list_contains(read,ctx,next,lists[2],e->work_limit,&found));
            while (!found) { LINK_STEP();o=object_of(&m,next);next=pm_load(&m,o+0x78u,4);pm_store(&m,o+0x90u,UINT32_MAX,4);LINK_TRY(frame_list_contains(read,ctx,next,lists[2],e->work_limit,&found)); }
        } }
        node=pm_load(&m,node,4);
    }
    node=pm_load(&m,head,4);
    while (node!=head) { LINK_STEP();id=pm_load(&m,node+8u,4);o=object_of(&m,id);if (o && pm_load(&m,o+0x80u,1) && pm_load(&m,o+0x90u,4)!=UINT32_MAX) start=id;node=pm_load(&m,node,4); }
    /* The original faults on a missing lap circuit; report undefined input. */
    if (!start) return m.error ? m.error : -4;
    id=start;
    do { LINK_STEP();o=object_of(&m,id);if (pm_load(&m,o+0x90u,4)!=UINT32_MAX) pm_store(&m,o+0x90u,index++,4);id=pm_load(&m,o+0x78u,4); } while (id!=start);
    id=pm_load(&m,0x54689cu,4);
    if (id) { pm_store(&m,0x546898u,id,4);pm_store(&m,0x546894u,pm_load(&m,object_of(&m,id)+0x90u,4),4); }
    else {
        index=(uint32_t)pm_sar((int32_t)index,1);pm_store(&m,0x546894u,index,4);id=start;
        do { LINK_STEP();o=object_of(&m,id);if (pm_load(&m,o+0x90u,4)==index) pm_store(&m,0x546898u,id,4);id=pm_load(&m,o+0x78u,4); } while (id!=start);
    }
    LINK_TRY(waypoint_weather(&m,start,e->work_limit));
    for (i=0;i<3u;i++) { LINK_TRY(frame_list_destroy(read,write,ctx,lists[i],e->release,e->system_ctx,e->work_limit)); }
    return m.error;
#undef LINK_STEP
#undef LINK_TRY
}
int frame_background_construct(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t parser,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o,parent,unused,found,more,value=0,bits=0,defined=0,i,head,symbol;unsigned char token[16];int status;
#define BG_TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
#define BG_FIND(key) frame_text_find_name(read,write,ctx,parser,(const unsigned char *)(key),(uint32_t)sizeof(key),e->work_limit,&found)
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,entity);parent=pm_load(&m,0x53e4b0u,4);BG_TRY(frame_class_dispatch_payload(read,ctx,parent,entity,parser,e->method,e->method_ctx,&unused));
    BG_TRY(BG_FIND(":inrange"));if (found) { BG_TRY(frame_text_read_i32(read,write,ctx,parser,o+0x98u,e->work_limit,&more));pm_write32(&m,o+0x98u,pm_sar(pm_signed(pm_load(&m,o+0x98u,4)<<10),3)); }
    BG_TRY(BG_FIND(":ufo"));if (found) {
        pm_store(&m,o+0x7cu,1,4);head=pm_load(&m,0x54b48cu,4);BG_TRY(frame_list_remove_value(read,write,ctx,entity,head,e->release,e->system_ctx,e->work_limit));
        BG_TRY(BG_FIND(":target"));if (!found) { BG_TRY(BG_FIND(":Target")); }
        if (found) { BG_TRY(frame_text_next(read,write,ctx,parser,token,16,e->work_limit,&more));BG_TRY(frame_symbol_intern_name(read,write,ctx,token,16,e,&symbol));pm_store(&m,o+0x80u,symbol,4); }
        BG_TRY(BG_FIND(":new_pos"));if (found) for (i=0;i<3u;i++) { BG_TRY(frame_text_read_f32_value(read,write,ctx,parser,e->work_limit,&bits,&more));if (more) defined=1;if (!defined) return -4;pm_store(&m,o+0x88u+i*4u,frame_text_fixed1024(bits),4); }
        head=pm_load(&m,0x592e7cu,4);BG_TRY(frame_list_append(read,write,ctx,entity,head,e->allocate,e->release,e->system_ctx,e->work_limit,&unused));
    }
    BG_TRY(BG_FIND(":screen_shake"));if (found) {
        pm_store(&m,o+0x7cu,2,4);BG_TRY(BG_FIND(":time"));if (found) { value=parser;BG_TRY(frame_text_read_value(read,write,ctx,parser,e->work_limit,&value,&more));pm_store(&m,o+0x9cu,value,2);pm_store(&m,o+0x9cu,pm_load(&m,o+0x9cu,2)*1000u,2); }
        BG_TRY(BG_FIND(":max_amount"));if (found) { BG_TRY(frame_text_read_i32(read,write,ctx,parser,o+0x9eu,e->work_limit,&more)); }
    }
    return m.error;
#undef BG_FIND
#undef BG_TRY
}
int frame_background_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t o,near,type,camera,unused,descriptor;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    o=object_of(&m,entity);type=pm_load(&m,o+0x7cu,4);
    if (!pm_load(&m,o+0x94u,4)) {
        status=frame_entity_near_camera(read,ctx,o,0,pm_i32(&m,o+0x98u),&near);if (status) return status;
        if (near) { pm_store(&m,o+0x94u,1,4);if (type==2u) pm_store(&m,o+0xa2u,0,1); }
        return m.error;
    }
    if (type==1u) {
        status=frame_ufo_spawn(read,write,ctx,pm_load(&m,o+0x84u,4),o+0x88u,e,&unused);if (status) return status;
        descriptor=pm_load(&m,0x545ed0u,4);status=frame_entity_dispatch(read,write,ctx,entity,descriptor,0,0,e->method,e->method_ctx,&unused);if (status) return status;
    }
    if (pm_load(&m,o+0x7cu,4)==2u) {
        camera=pm_load(&m,0x53cc38u+pm_load(&m,o+0xa2u,1)*4u,4);
        status=frame_camera_shake(read,write,ctx,camera,pm_load(&m,o+0x9cu,2),pm_load(&m,o+0x9eu,4));if (status) return status;
        descriptor=pm_load(&m,0x545ed0u,4);status=frame_entity_dispatch(read,write,ctx,entity,descriptor,0,0,e->method,e->method_ctx,&unused);if (status) return status;
    }
    return m.error;
}
int frame_background_link(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *e)
{
    physics_memory m={read,write,ctx,0};uint32_t head,node,o,id,target,t,descriptor,unused,used=0,i;int status;
    if (!read || !write || !e || !e->work_limit) return -1;
    head=pm_load(&m,0x592e7cu,4);node=pm_load(&m,head,4);
    while (node!=head) {
        if (m.error) return m.error;
        if (used++>=e->work_limit) return -3;
        id=pm_load(&m,node+8u,4);o=object_of(&m,id);
        if (pm_load(&m,o+0x7cu,4)==1u) {
            target=pm_load(&m,pm_load(&m,o+0x80u,4)+4u,4);pm_store(&m,o+0x84u,target,4);t=object_of(&m,target);if (!t) return m.error ? m.error : -4;
            for (i=0;i<3u;i++) pm_store(&m,o+i*4u,pm_load(&m,t+i*4u,4),4);
            if (pm_i16(&m,t+0x78u)!=0 && pm_i16(&m,o+0x78u)<pm_i16(&m,t+0x78u)) {
                descriptor=pm_load(&m,0x545f44u,4);status=frame_entity_dispatch(read,write,ctx,target,descriptor,0,0,e->method,e->method_ctx,&unused);if (status) return status;
                status=frame_list_remove_value(read,write,ctx,target,pm_load(&m,0x53e4b4u,4),e->release,e->system_ctx,e->work_limit);if (status) return status;
            }
        }
        node=pm_load(&m,node,4);
    }
    return m.error;
}
