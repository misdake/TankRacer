/* Real entity/list routines, scripted callback side effects, semantic output. */
#include "frame_entities.h"
#include "reference_io.h"

#define ENTITIES 6u
#define ACTIONS 32u
#define TRACE 1024u
static unsigned char ram[0x600000];
static uint32_t allocation,action_count,trace_count,failure_at,failure_code;
static uint32_t actions[ACTIONS][4],trace[TRACE][3];
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{ (void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1; }
static int write_data(void *ctx,uint32_t at,const void *in,uint32_t n)
{ (void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(ram+at,in,n);return 1; }
static int allocate(void *ctx,uint32_t bytes,uint32_t *out)
{ (void)ctx;if(bytes>0x400000-allocation)return -4;*out=allocation;allocation+=(bytes+3u)&~3u;memset(ram+*out,0,bytes);return 0; }
static int release(void *ctx,uint32_t at) { (void)ctx;(void)at;return 0; }
static void set(uint32_t at,uint32_t v) { oracle_set(ram+at,v,4); }
static uint32_t word(uint32_t at) { return oracle_get(ram+at,4); }
static int method(void *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t *);
static int operation(uint32_t op,uint32_t id,uint32_t limit)
{
    uint32_t result=0;
    switch(op) {
    case 1:return frame_entity_enable_tick(read_data,write_data,0,id,allocate,release,0,limit,&result);
    case 2:return frame_entity_disable_tick(read_data,write_data,0,id,release,0,limit,&result);
    case 3:case 4:return frame_entity_queue(read_data,write_data,0,id,op==4,allocate,release,0,limit);
    case 5:return frame_entity_cancel_notification(read_data,write_data,0,id,release,0,limit);
    case 6:return frame_entities_tick(read_data,write_data,0,method,0,limit);
    case 7:return frame_entities_drain_queues(read_data,write_data,0,method,0,release,0,limit);
    default:return -1;
    }
}
static int method(void *ctx,uint32_t target,uint32_t id,uint32_t has,uint32_t payload,uint32_t *out)
{
    uint32_t i,ordinal;int status;(void)ctx;(void)has;(void)payload;
    if(target<0x6000||target>0x6003||trace_count==TRACE)return -5;
    trace[trace_count][0]=target-0x6000;trace[trace_count][1]=id;trace[trace_count][2]=word(0x55c698);
    ordinal=++trace_count;
    for(i=0;i<action_count;i++)if(actions[i][0]==ordinal) {
        status=operation(actions[i][1],actions[i][2],actions[i][3]);if(status)return status;
    }
    if(ordinal==failure_at)return oracle_signed(failure_code);
    if(target==0x6003) { status=frame_entity_release_id(read_data,write_data,0,id);if(status)return status; }
    *out=id;return 0;
}
static void load_list(uint32_t head)
{
    uint32_t count=oracle_require(),i,result;
    set(head,head);set(head+4,head);set(head+8,head);
    for(i=0;i<count;i++) {
        uint32_t id=oracle_require();if(!id||id>ENTITIES)exit(2);
        if(frame_list_append(read_data,write_data,0,id,head,allocate,release,0,1000,&result))exit(2);
    }
}
static void emit_list(uint32_t head)
{
    uint32_t node=word(head),n=0;
    while(node!=head) { if(n++==TRACE)exit(2);node=word(node); }
    oracle_put(n);node=word(head);
    while(node!=head) { oracle_put(word(node+8));node=word(node); }
}
static void emit(int status)
{
    uint32_t i,cursor=word(0x55b6c0);
    oracle_put((uint32_t)status);oracle_put(word(0x55c698));oracle_put(word(0x55c68c));
    for(i=1;i<=ENTITIES;i++) { oracle_put(word(0x10010+i*0x1000));oracle_put(word(0x7000+i*8)!=0); }
    oracle_put(!cursor||cursor==0x50000?0:word(cursor+8));
    emit_list(0x50000);emit_list(0x50010);emit_list(0x50020);
    oracle_put(trace_count);
    for(i=0;i<trace_count;i++) { oracle_put(trace[i][0]);oracle_put(trace[i][1]);oracle_put(trace[i][2]); }
}
int main(void)
{
    uint32_t steps,i,j;oracle_binary();fwrite("LC01",1,4,stdout);
    while(oracle_word(&steps)) {
        memset(ram,0,sizeof(ram));allocation=0x100000;trace_count=0;
        set(0x54b3d8,1);set(0x55c694,0x7000);set(0x55c690,16);set(0x55c68c,ENTITIES);set(0x55c698,9);
        set(0x9000+28,3);set(0x9000+36,0x9100);
        for(i=0;i<4;i++) { set(0x9100+i*4,0x6000+i);set(0x9200+i*64+28,i); }
        set(0x545eb8,0x9200);set(0x545f34,0x9240);set(0x545ef4,0x9280);set(0x545ed4,0x92c0);
        for(i=1;i<=ENTITIES;i++) {
            set(0x7000+i*8,0x8000+i*8);set(0x8000+i*8,0x9000);set(0x8004+i*8,0x10000+i*0x1000);
            set(0x10010+i*0x1000,oracle_require());
        }
        set(0x55b6a8,0x50000);set(0x55b6bc,0x50010);set(0x55b6b8,0x50020);
        load_list(0x50000);load_list(0x50010);load_list(0x50020);
        action_count=oracle_require();if(action_count>ACTIONS)return 2;
        for(i=0;i<action_count;i++)for(j=0;j<4;j++)actions[i][j]=oracle_require();
        failure_at=oracle_require();failure_code=oracle_require();
        for(i=0;i<steps;i++) {
            uint32_t op=oracle_require(),id=oracle_require(),limit=oracle_require();
            emit(operation(op,id,limit));
        }
    }
    return ferror(stdin)?3:0;
}
