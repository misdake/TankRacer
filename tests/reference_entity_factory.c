/* Production table/factory/dispatch; explicit scripted class boundaries. */
#include "frame_entity_factory.h"
#include "reference_io.h"
#define TRACE 1024u
static unsigned char ram[0x600000];
static uint32_t allocation,objects,trace_count,trace[TRACE][4],action_count,actions[32][4],failure_at,failure_code;
static frame_entity_factory_services services;
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{ (void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1; }
static int write_data(void *ctx,uint32_t at,const void *in,uint32_t n)
{ (void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(ram+at,in,n);return 1; }
static int allocate(void *ctx,uint32_t bytes,uint32_t *out)
{ (void)ctx;if(bytes>0x200000-allocation)return -4;*out=allocation;allocation+=(bytes+3u)&~3u;memset(ram+*out,0,bytes);return 0; }
static int zero_allocate(void *ctx,uint32_t count,uint32_t bytes,uint32_t *out)
{ return allocate(ctx,count*bytes,out); }
static int release(void *ctx,uint32_t at) { (void)ctx;(void)at;return 0; }
static void set(uint32_t at,uint32_t value) { oracle_set(ram+at,value,4); }
static uint32_t word(uint32_t at) { return oracle_get(ram+at,4); }
static uint32_t record(uint32_t id) { return word(word(0x55c694)+id*8); }
static uint32_t object_value(uint32_t id)
{ uint32_t r=record(id),o=r?word(r+4):0;return o?(o-0x300000)/32:0; }
static int operation(uint32_t op,uint32_t a,uint32_t b,uint32_t *out)
{
    switch(op) {
    case 1:return frame_entity_table_initialize(read_data,write_data,0,a,&services);
    case 2:return frame_entity_table_grow(read_data,write_data,0,&services);
    case 3:return frame_entity_new_record(read_data,write_data,0,a,&services,out);
    case 4:return frame_entity_unlink_free_id(read_data,write_data,0,a,b,out);
    case 5:return frame_entity_release_id(read_data,write_data,0,a);
    case 6: { frame_entity_native_context c={read_data,write_data,0,{0}};return frame_entity_native_base(&c,0x504c50,a,0,0,out); }
    case 7:if(a>4)return -1;return frame_entity_create(read_data,write_data,0,0x9000+a*0x100,&services,out);
    case 8:if(b>4)return -1;return frame_entity_fill_reserved(read_data,write_data,0,a,0x9000+b*0x100,&services);
    case 9:return frame_entity_destroy(read_data,write_data,0,a,services.method,0,release,0);
    case 10:set(0x536cb0,a);set(0x55c680,b);return 0;
    case 11:return frame_entity_enable_tick(read_data,write_data,0,a,allocate,release,0,b,out);
    case 12:return frame_entity_disable_tick(read_data,write_data,0,a,release,0,b,out);
    case 13:case 14:return frame_entity_queue(read_data,write_data,0,a,op==14,allocate,release,0,b);
    case 15:return frame_entity_cancel_notification(read_data,write_data,0,a,release,0,b);
    case 16:case 17: {
        uint32_t previous=services.work_limit;int status;services.work_limit=b;
        status=op==16?frame_entities_tick(read_data,write_data,0,services.method,0,b):frame_entities_drain_queues(read_data,write_data,0,services.method,0,release,0,b);
        services.work_limit=previous;return status;
    }
    case 18: {
        static const uint32_t targets[]={0x504010,0x407000,0x437be0,0x503cf0,0x503d30,0x504000,0x504c40,0x504c50};
        frame_entity_native_context c={read_data,write_data,0,{0}};c.services=services;
        if(b>=8)return -1;
        return frame_entity_native_base(&c,targets[b],a,0,0,out);
    }
    default:return -1;
    }
}
static int method(void *ctx,uint32_t target,uint32_t id,uint32_t has,uint32_t payload,uint32_t *out)
{
    uint32_t method_id=target-0x6000,kind=method_id%4,ordinal,i,unused=0;int status;
    (void)ctx;(void)has;(void)payload;if(method_id>=20||trace_count==TRACE)return -5;
    trace[trace_count][0]=method_id;trace[trace_count][1]=id;trace[trace_count][2]=word(0x55c698);trace[trace_count][3]=object_value(id);
    ordinal=++trace_count;
    if(kind==0) { objects++;set(record(id)+4,0x300000+objects*32); }
    for(i=0;i<action_count;i++)if(actions[i][0]==ordinal) {
        status=operation(actions[i][1],actions[i][2],actions[i][3],&unused);if(status)return status;
    }
    set(0x55c698,word(0x55c698)+5);
    if(ordinal==failure_at)return oracle_signed(failure_code);
    if(kind==3) { status=frame_entity_release_id(read_data,write_data,0,id);if(status)return status; }
    *out=id;return 0;
}
static void emit_list(uint32_t head)
{
    uint32_t node=word(head),count=0;
    while(node!=head) { if(count++==TRACE)exit(2);node=word(node); }
    oracle_put(count);node=word(head);
    while(node!=head) { oracle_put(word(node+8));node=word(node); }
}
static void emit(int status,uint32_t out)
{
    uint32_t capacity=word(0x55c690),table=word(0x55c694),i,j,r,c;
    oracle_put((uint32_t)status);oracle_put(out);oracle_put(capacity);oracle_put(word(0x55c68c));oracle_put(word(0x536cbc));oracle_put(word(0x536cc0));oracle_put(word(0x55c680));oracle_put(word(0x536cb0));oracle_put(word(0x55c698));oracle_put(objects);
    for(i=0;i<capacity;i++) {
        r=word(table+i*8);c=r?word(r):0;
        oracle_put(r!=0);oracle_put(c?(c-0x9000)/0x100+1:0);oracle_put(object_value(i));oracle_put(word(table+i*8+4));oracle_put(r&&word(r+4)?word(word(r+4)+16):UINT32_MAX);
    }
    i=word(0x55b6c0);oracle_put(!i||i==0x50000?0:word(i+8));
    emit_list(0x50000);emit_list(0x50010);emit_list(0x50020);
    oracle_put(trace_count);for(i=0;i<trace_count;i++)for(j=0;j<4;j++)oracle_put(trace[i][j]);
}
int main(void)
{
    uint32_t steps,capacity,i,j;oracle_binary();fwrite("EF01",1,4,stdout);
    services.method=method;services.allocate=allocate;services.zero_allocate=zero_allocate;services.release=release;services.work_limit=100000;
    while(oracle_word(&steps)) {
        memset(ram,0,sizeof(ram));allocation=0x100000;objects=trace_count=0;set(0x54b3d8,1);set(0x55c698,9);
        capacity=oracle_require();if(frame_entity_table_initialize(read_data,write_data,0,capacity,&services))return 2;
        for(i=0;i<3;i++) { uint32_t head=0x50000+i*16;set(head,head);set(head+4,head);set(head+8,head); }
        set(0x55b6a8,0x50000);set(0x55b6bc,0x50010);set(0x55b6b8,0x50020);
        for(i=0;i<5;i++) {
            uint32_t c=0x9000+i*0x100;set(c+28,i==1?0:3);set(c+36,c+64);
            for(j=0;j<4;j++)set(c+64+j*4,0x6000+i*4+j);
        }
        set(0x9240,0);set(0x9344,0);set(0x934c,0);set(0x9448,0);
        for(i=0;i<4;i++)set(0xa000+i*64+28,i);
        set(0x545ef8,0xa000);set(0x545ebc,0xa040);set(0x545ef4,0xa080);set(0x545ed4,0xa0c0);
        set(0x545eb8,0xa040);set(0x545f34,0xa080);
        action_count=oracle_require();if(action_count>32)return 2;
        for(i=0;i<action_count;i++)for(j=0;j<4;j++)actions[i][j]=oracle_require();
        failure_at=oracle_require();failure_code=oracle_require();
        for(i=0;i<steps;i++) {
            uint32_t op=oracle_require(),a=oracle_require(),b=oracle_require(),out=0x7182;
            int status=operation(op,a,b,&out);emit(status,out);
        }
    }
    return ferror(stdin)?3:0;
}
