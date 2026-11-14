/* Original Mover constructor/tick; declared parent, terrain and queue responses.
 * SR05 separately exercises actual resources and scene/factory owners. */
#define frame_mover_initialize oracle_mover_initialize
#define frame_mover_construct oracle_mover_construct
#define frame_mover_tick oracle_mover_tick
#define frame_mover_hit oracle_mover_hit
#define frame_class_dispatch probe_parent
#define frame_class_dispatch_payload probe_parent_payload
#define frame_entity_dispatch probe_dispatch
#define frame_entity_queue probe_queue
#define frame_entity_cancel_notification probe_cancel
#define frame_list_remove_value probe_remove
#define frame_list_contains probe_contains
#define frame_list_append probe_append
#define physics_terrain_normal_height probe_normal
#include "../src/frame_mover.c"
#include "frame_math.h"
#include "reference_io.h"
static unsigned char ram[0x800000];
static uint32_t events[64],event_words,event_count,fail_at;
static int failure;
static int32_t height,normal[3];
static int event(uint32_t kind){events[event_words++]=kind;return ++event_count==fail_at?failure:0;}
int probe_parent(frame_data_read r,void *c,uint32_t k,uint32_t id,frame_native_method method,void *mc,uint32_t *out)
{(void)r;(void)c;(void)k;(void)id;(void)method;(void)mc;*out=0;return event(1);}
int probe_parent_payload(frame_data_read r,void *c,uint32_t k,uint32_t id,uint32_t p,frame_native_method method,void *mc,uint32_t *out)
{(void)r;(void)c;(void)k;(void)id;(void)p;(void)method;(void)mc;*out=0;return event(9);}
int probe_dispatch(frame_data_read r,frame_data_write w,void *c,uint32_t id,uint32_t slot,uint32_t has,uint32_t payload,frame_native_method method,void *mc,uint32_t *out)
{(void)r;(void)w;(void)c;(void)id;(void)slot;(void)has;(void)payload;(void)method;(void)mc;*out=0;return event(2);}
int probe_normal(frame_data_read r,void *c,uint32_t root,int32_t x,int32_t z,int32_t *out,int32_t n[3])
{int s;(void)r;(void)c;(void)root;s=event(3);events[event_words++]=(uint32_t)x;events[event_words++]=(uint32_t)z;if(!s){*out=height;memcpy(n,normal,12);}return s;}
int probe_queue(frame_data_read r,frame_data_write w,void *c,uint32_t id,uint32_t destroy,frame_pool_system_alloc a,frame_pool_system_free f,void *sc,uint32_t limit)
{(void)r;(void)w;(void)c;(void)id;(void)destroy;(void)a;(void)f;(void)sc;(void)limit;return event(4);}
int probe_cancel(frame_data_read r,frame_data_write w,void *c,uint32_t id,frame_pool_system_free f,void *sc,uint32_t limit)
{(void)r;(void)w;(void)c;(void)id;(void)f;(void)sc;(void)limit;return event(5);}
int probe_remove(frame_data_read r,frame_data_write w,void *c,uint32_t id,uint32_t head,frame_pool_system_free f,void *sc,uint32_t limit)
{(void)r;(void)w;(void)c;(void)id;(void)head;(void)f;(void)sc;(void)limit;return event(6);}
int probe_contains(frame_data_read r,void *c,uint32_t id,uint32_t head,uint32_t limit,uint32_t *out)
{(void)r;(void)c;(void)id;(void)head;(void)limit;*out=0;return event(7);}
int probe_append(frame_data_read r,frame_data_write w,void *c,uint32_t id,uint32_t head,frame_pool_system_alloc a,frame_pool_system_free f,void *sc,uint32_t limit,uint32_t *out)
{(void)r;(void)w;(void)c;(void)id;(void)head;(void)a;(void)f;(void)sc;(void)limit;*out=0;return event(8);}
static int read_data(void *c,uint32_t a,void *out,uint32_t n){(void)c;if(a>sizeof(ram)||n>sizeof(ram)-a)return 0;memcpy(out,ram+a,n);return 1;}
static int write_data(void *c,uint32_t a,const void *in,uint32_t n){(void)c;if(a>sizeof(ram)||n>sizeof(ram)-a)return 0;memcpy(ram+a,in,n);return 1;}
static void set(uint32_t a,uint32_t v,uint32_t n){oracle_set(ram+a,v,n);}
static void load(uint32_t a,uint32_t n){uint32_t i;for(i=0;i<n;i++)set(a+i*4,oracle_require(),4);}
static void emit(uint32_t a,uint32_t n){uint32_t i;for(i=0;i<n;i++)oracle_put(oracle_get(ram+a+i*4,4));}
int main(void)
{
 const uint32_t o=0x10000,parser=0x16000;frame_entity_factory_services e={0};uint32_t op,i,external,p,exists,live,n,status;
 oracle_binary();fwrite("MV01",1,4,stdout);
 while(oracle_word(&op)){
  memset(ram,0,sizeof(ram));event_words=event_count=0;
  set(0x55c694,0x7000,4);set(0x7008,0x8000,4);set(0x8004,o,4);
  load(0x5904fc,1);e.work_limit=oracle_require();load(o+0x80,1);load(o+0xf2,5);set(o+0xb5,oracle_require(),1);
  load(o+0x50,3);load(o+0xc2,3);load(o,3);load(o+24,9);external=oracle_require();load(0x12000,9);set(o+20,external==1?o+24:external?0x12000:0,4);
  set(o+0xec,oracle_require(),2);set(o+0xee,oracle_require(),2);load(0x74aee0,3);
  for(i=0;i<2;i++){
   p=0x11000+i*0x100;exists=oracle_require();set(0x53e8b8+i*4,exists?i+2:0,4);
   set(0x7010+i*8,0x8010+i*8,4);set(0x8014+i*8,p,4);load(p,3);
  }
  exists=oracle_require();live=oracle_require();set(o+0x8c,exists?0x13000:0,4);set(0x13018,live,1);
  height=(int32_t)oracle_require();for(i=0;i<3;i++)normal[i]=(int32_t)oracle_require();fail_at=oracle_require();failure=(int32_t)oracle_require();
  n=oracle_require();if(n>3000)return 2;set(parser,n,4);for(i=0;i<n;i++)ram[parser+8+i]=(unsigned char)oracle_require();
  /* Declared single-byte ASCII numeric classification, no locale service. */
  set(0x538838,0x1500,4);for(i=48;i<=57;i++)ram[0x1500+i*2]=4;ram[0x1540]=8;
  status=(uint32_t)(op?oracle_mover_tick(read_data,write_data,0,1,&e,frame_math_trig()):oracle_mover_construct(read_data,write_data,0,1,parser,&e));
  oracle_put(status);emit(o+0x80,1);emit(o+0xf2,5);oracle_put(ram[o+0xb5]);emit(o+0x50,3);emit(o+0xc2,3);emit(o,3);emit(o+24,9);
  external=oracle_get(ram+o+20,4);oracle_put(external!=0);if(external)emit(external,9);else for(i=0;i<9;i++)oracle_put(0);
  oracle_put(event_words);for(i=0;i<event_words;i++)oracle_put(events[i]);
 }
 return ferror(stdin)?3:0;
}
