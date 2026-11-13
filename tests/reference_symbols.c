/* Production interning; logical entity/class values replace external records. */
#include "frame_classes.h"
#include "reference_io.h"
static unsigned char ram[0x600000];
static uint32_t allocation=0x100000,symbols[4096],count;
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{ (void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1; }
static int write_data(void *ctx,uint32_t at,const void *in,uint32_t n)
{ (void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(ram+at,in,n);return 1; }
static int allocate(void *ctx,uint32_t bytes,uint32_t *out)
{ (void)ctx;if(bytes>0x400000-allocation)return -4;*out=allocation;allocation+=(bytes+3u)&~3u;memset(ram+*out,0,bytes);return 0; }
static int release(void *ctx,uint32_t at) { (void)ctx;(void)at;return 0; }
static void set(uint32_t at,uint32_t v) { oracle_set(ram+at,v,4); }
static uint32_t word(uint32_t at) { return oracle_get(ram+at,4); }
int main(void)
{
    frame_entity_factory_services e={0};uint32_t n,id=0,symbol,entity,klass,i,j,length;int status;
    unsigned char name[1024];
    oracle_binary();fwrite("SY01",1,4,stdout);
    e.allocate=allocate;e.release=release;e.work_limit=10000;
    set(0x54b3d8,1);set(0x534cfc,0x20000);memcpy(ram+0x534d00,"nil",4);
    if(frame_symbols_initialize(read_data,write_data,0,&e))return 2;
    symbols[count++]=word(0x5b35ac);
    while(oracle_word(&n)) {
        if(n>=sizeof(name))return 2;
        for(i=0;i<n;i++)name[i]=(unsigned char)oracle_require();
        name[n]=0;entity=oracle_require();klass=oracle_require();symbol=0;
        status=frame_symbol_intern_name(read_data,write_data,0,name,n+1,&e,&symbol);
        if(!status) {
            for(id=0;id<count&&symbols[id]!=symbol;id++) { }
            if(id==count) { if(count==4096)return 2;symbols[count++]=symbol; }
            if(entity&&symbol!=symbols[0]) { set(symbol,128);set(symbol+4,entity); }
            if(klass) { set(symbol+8,4096);set(symbol+12,klass); }
        }
        oracle_put((uint32_t)status);oracle_put(status?UINT32_MAX:id);oracle_put(count);
        for(i=0;i<count;i++) {
            symbol=symbols[i];oracle_put(word(symbol)==128?word(symbol+4):0);
            oracle_put(word(symbol+8)==4096?word(symbol+12):0);
            length=(uint32_t)strlen((const char *)ram+word(symbol+32));oracle_put(length);
            for(j=0;j<length;j++)oracle_put(ram[word(symbol+32)+j]);
        }
    }
    return ferror(stdin)?3:0;
}
