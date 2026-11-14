/* Unchanged Spin hit/tick, explicit terrain/device/lifecycle responses.
 * SR04 separately uses the actual authored resources and shared scene owners. */
#define frame_spin_initialize oracle_spin_initialize
#define frame_spin_construct oracle_spin_construct
#define frame_spin_hit oracle_spin_hit
#define frame_spin_tick oracle_spin_tick
#define physics_terrain_height probe_height
#define physics_terrain_normal_height probe_normal
#define physics_fallback_query probe_fallback
#define frame_entity_activate probe_activate
#define frame_entity_dispatch probe_dispatch
#define frame_entity_queue probe_queue
#include "../src/frame_spin.c"
#include "frame_math.h"
#include "reference_io.h"
static unsigned char ram[0x800000];
static uint32_t events[256],event_words,event_count,fail_at;
static int failure;
static int32_t ground,normal_ground,normal_value[3],fallback_value[3];
static uint32_t fallback_hit;
static int event(uint32_t kind){events[event_words++]=kind;return ++event_count==fail_at?failure:0;}
static void args(int32_t x,int32_t z){events[event_words++]=(uint32_t)x;events[event_words++]=(uint32_t)z;}
int probe_height(frame_data_read r,void *c,uint32_t root,int32_t x,int32_t z,int32_t *out)
{int s;(void)r;(void)c;(void)root;s=event(3);args(x,z);if(!s)*out=ground;return s;}
int probe_normal(frame_data_read r,void *c,uint32_t root,int32_t x,int32_t z,int32_t *out,int32_t n[3])
{int s;(void)r;(void)c;(void)root;s=event(4);args(x,z);if(!s){*out=normal_ground;memcpy(n,normal_value,12);}return s;}
int probe_fallback(frame_data_read r,void *c,uint32_t root,const int32_t p[3],int32_t mode,int32_t n[3])
{int s;(void)r;(void)c;(void)root;(void)mode;s=event(5);args(p[0],p[2]);events[event_words++]=(uint32_t)p[1];if(s)return s;if(fallback_hit)memcpy(n,fallback_value,12);return (int)fallback_hit;}
int probe_activate(frame_data_read r,frame_data_write w,void *c,uint32_t id,frame_native_method method,void *mc,uint32_t *out)
{(void)r;(void)w;(void)c;(void)id;(void)method;(void)mc;*out=0;return event(1);}
int probe_dispatch(frame_data_read r,frame_data_write w,void *c,uint32_t id,uint32_t slot,uint32_t has,uint32_t payload,frame_native_method method,void *mc,uint32_t *out)
{(void)r;(void)w;(void)c;(void)id;(void)slot;(void)has;(void)payload;(void)method;(void)mc;*out=0;return event(2);}
int probe_queue(frame_data_read r,frame_data_write w,void *c,uint32_t id,uint32_t destroy,frame_pool_system_alloc a,frame_pool_system_free f,void *sc,uint32_t limit)
{(void)r;(void)w;(void)c;(void)id;(void)destroy;(void)a;(void)f;(void)sc;(void)limit;return event(8);}
static int read_data(void *c,uint32_t a,void *out,uint32_t n){(void)c;if(a>sizeof(ram)||n>sizeof(ram)-a)return 0;memcpy(out,ram+a,n);return 1;}
static int write_data(void *c,uint32_t a,const void *in,uint32_t n){(void)c;if(a>sizeof(ram)||n>sizeof(ram)-a)return 0;memcpy(ram+a,in,n);return 1;}
static void set(uint32_t a,uint32_t v,uint32_t n){oracle_set(ram+a,v,n);}
static void load(uint32_t a,uint32_t n){uint32_t i;for(i=0;i<n;i++)set(a+i*4,oracle_require(),4);}
static void emit(uint32_t a,uint32_t n){uint32_t i;for(i=0;i<n;i++)oracle_put(oracle_get(ram+a+i*4,4));}
static const uint32_t fields[][3]={
 {0x50,4,1},{0x54,4,1},{0x58,4,1},{0xaa,4,0},{0xb2,2,1},{0xb4,1,1},{0xb5,1,0},{0xb6,4,0},
 {0xba,4,1},{0xbe,4,1},{0xc2,4,1},{0xc6,4,1},{0xca,4,1},{0xce,4,1},{0xd2,4,1},{0xd6,4,1},
 {0xda,2,1},{0xdc,2,1},{0xde,2,0},{0xe0,2,1},{0xe2,2,0},{0xe4,2,1},{0xe6,2,0},
 {0xe8,2,1},{0xea,2,0},{0xec,2,1},{0xee,2,0},{0xf0,2,1},{0xae,4,1}};
int main(void)
{
 const uint32_t o=0x10000,t=0x11000,shape=0x13000;uint32_t op,i,external,status;frame_entity_factory_services e={0};
 oracle_binary();fwrite("SP01",1,4,stdout);e.work_limit=10000;
 while(oracle_word(&op)){
  memset(ram,0,sizeof(ram));event_count=event_words=0;
  set(0x55c694,0x7000,4);set(0x7008,0x8000,4);set(0x8004,o,4);set(0x7010,0x8010,4);set(0x8014,t,4);set(o+0x48,shape,4);
  load(0x5904fc,1);load(o+0x80,1);load(o+0x7c,1);load(o+0x5c,1);load(o+16,1);load(o+0x88,1);
  if(oracle_get(ram+o+0x88,4))set(o+0x88,0x14000,4);
  set(0x14022,oracle_require(),2);load(0x530ac8,1);set(shape+12,oracle_require(),2);
  for(i=0;i<6;i++)set(shape+0x1c+i*2,oracle_require(),2);
  fail_at=oracle_require();failure=(int32_t)oracle_require();ground=(int32_t)oracle_require();normal_ground=(int32_t)oracle_require();
  for(i=0;i<3;i++)normal_value[i]=(int32_t)oracle_require();
  fallback_hit=oracle_require();
  for(i=0;i<3;i++)fallback_value[i]=(int32_t)oracle_require();
  load(o,3);load(o+24,9);external=oracle_require();load(0x12000,9);set(o+20,external==1?o+24:external?0x12000:0,4);
  for(i=0;i<sizeof(fields)/sizeof(*fields);i++)set(o+fields[i][0],oracle_require(),fields[i][1]);
  load(t,3);load(t+0x50,3);load(t+0xec,1);external=oracle_require();load(0x12100,9);set(t+20,external?0x12100:0,4);
  status=(uint32_t)(op?oracle_spin_tick(read_data,write_data,0,1,&e,frame_math_trig()):oracle_spin_hit(read_data,write_data,0,1,2,&e,frame_math_trig(),0,0));
  oracle_put(status);emit(o+0x80,1);emit(o,3);emit(o+24,9);external=oracle_get(ram+o+20,4);oracle_put(external!=0);if(external)emit(external,9);else for(i=0;i<9;i++)oracle_put(0);
  for(i=0;i<sizeof(fields)/sizeof(*fields);i++){
   uint32_t v=oracle_get(ram+o+fields[i][0],fields[i][1]);
   if(fields[i][2]&&fields[i][1]==1)v=(uint32_t)(int32_t)(int8_t)v;
   if(fields[i][2]&&fields[i][1]==2)v=(uint32_t)(int32_t)(int16_t)v;
   oracle_put(v);
  }
  oracle_put(event_words);for(i=0;i<event_words;i++)oracle_put(events[i]);
 }
 return ferror(stdin)?3:0;
}
