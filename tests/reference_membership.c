#include "frame_spatial.h"
#include "frame_entities.h"
#include "reference_io.h"
static unsigned char ram[0x600000];
static uint32_t next_allocation;
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *data,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(ram+at,data,n);return 1;}
static int allocate(void *ctx,uint32_t n,uint32_t *out)
{(void)ctx;if(next_allocation+n>0x400000)return -1;*out=next_allocation;next_allocation+=(n+3u)&~3u;memset(ram+*out,0,n);return 0;}
static int release(void *ctx,uint32_t at){(void)ctx;(void)at;return 0;}
static void set(uint32_t at,uint32_t v){oracle_set(ram+at,v,4);}
static uint32_t word(uint32_t at){return oracle_get(ram+at,4);}
static void load(uint32_t at,unsigned n){unsigned i;for(i=0;i<n;i++)set(at+i*4u,oracle_require());}
static void list(uint32_t head)
{
    uint32_t n=oracle_require(),last=head,i;set(head+8,head);
    for(i=0;i<n;i++){uint32_t value=oracle_require(),node=next_allocation;next_allocation+=12;if(value>2)exit(2);set(last,node);set(node+4,last);set(node+8,value+1);last=node;}
    set(last,head);set(head+4,last);
}
static void emit_list(uint32_t head)
{
    uint32_t n=0,node=word(head);while(node!=head){if(n++==65536)exit(2);node=word(node);}oracle_put(n);node=word(head);while(node!=head){oracle_put(word(node+8)-1);node=word(node);}
}
int main(void)
{
    uint32_t op;oracle_binary();fwrite("MB01",1,4,stdout);
    while(oracle_word(&op)){
        uint32_t i,id,limit,packed,result=0x7182,cursor;int status;next_allocation=0x100000;set(0x54b3d8,1);set(0x55c694,0x7000);
        for(i=0;i<3;i++){uint32_t o=0x10000+i*0x1000,shape=o+0x800;set(0x7008+i*8,0x8000+i*8);set(0x8004+i*8,o);load(o,3);load(o+0x10,1);set(o+0x48,oracle_require()?shape:0);oracle_set(ram+shape+12,oracle_require(),2);}
        load(0x544f70,4);for(i=0;i<64;i++){uint32_t head=0x50000+i*16;set(0x544e70+i*4,head);list(head);}
        set(0x55b6a8,0x51000);list(0x51000);cursor=oracle_require();set(0x55b6c0,0);
        if(cursor!=UINT32_MAX){uint32_t node=word(0x51000);while(node!=0x51000&&word(node+8)!=cursor+1)node=word(node);if(node==0x51000)exit(2);set(0x55b6c0,node);}
        id=oracle_require();limit=oracle_require();packed=oracle_require();if(id>2)return 2;
        if(op==1)status=frame_spatial_insert(read_data,write_data,0,id+1,allocate,release,0,limit,&packed);
        else if(op==2)status=frame_spatial_remove(read_data,write_data,0,id+1,packed,release,0,limit);
        else if(op==3)status=frame_entity_enable_tick(read_data,write_data,0,id+1,allocate,release,0,limit,&result);
        else if(op==4)status=frame_entity_disable_tick(read_data,write_data,0,id+1,release,0,limit,&result);
        else return 2;
        oracle_put((uint32_t)status);oracle_put(packed);oracle_put(result);for(i=0;i<3;i++)oracle_put(word(0x10010+i*0x1000));
        cursor=word(0x55b6c0);oracle_put(!cursor||cursor==0x51000?UINT32_MAX:word(cursor+8)-1);
        for(i=0;i<64;i++)emit_list(0x50000+i*16);
        emit_list(0x51000);
    }
    return ferror(stdin)?3:0;
}
