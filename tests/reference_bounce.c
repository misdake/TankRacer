/* Unchanged Bounce kernels with explicit terrain/lifecycle callback inputs.
 * Real resource construction, lists and collision dispatch are covered by SR02. */
#define frame_bounce_initialize oracle_bounce_initialize
#define frame_bounce_construct oracle_bounce_construct
#define frame_bounce_hit oracle_bounce_hit
#define frame_bounce_tick oracle_bounce_tick
#define physics_terrain_height probe_height
#define physics_terrain_normal_height probe_normal
#define physics_fallback_query probe_fallback
#define frame_entity_activate probe_activate
#define frame_entity_dispatch probe_dispatch
#define frame_spatial_remove probe_remove
#define frame_spatial_insert probe_insert
#define frame_entity_queue probe_queue
#include "../src/frame_bounce.c"
#include "reference_io.h"
#include "frame_math.h"
static unsigned char ram[0x800000];
static uint32_t events[128],event_count,fail_at;
static int fail_status;
static int32_t ground,normal_ground,normal_value[3],fallback_value[3];
static uint32_t fallback_hit;
static int event(uint32_t kind){events[event_count++]=kind;return event_count==fail_at?fail_status:0;}
int probe_height(frame_data_read r,void *ctx,uint32_t root,int32_t x,int32_t z,int32_t *out)
{int s;(void)r;(void)ctx;(void)root;(void)x;(void)z;s=event(3);if(!s)*out=ground;return s;}
int probe_normal(frame_data_read r,void *ctx,uint32_t root,int32_t x,int32_t z,int32_t *out,int32_t n[3])
{int s;(void)r;(void)ctx;(void)root;(void)x;(void)z;s=event(4);if(!s){*out=normal_ground;memcpy(n,normal_value,12);}return s;}
int probe_fallback(frame_data_read r,void *ctx,uint32_t root,const int32_t p[3],int32_t mode,int32_t n[3])
{int s;(void)r;(void)ctx;(void)root;(void)p;(void)mode;s=event(5);if(s)return s;if(fallback_hit)memcpy(n,fallback_value,12);return (int)fallback_hit;}
int probe_activate(frame_data_read r,frame_data_write w,void *ctx,uint32_t id,frame_native_method method,void *mc,uint32_t *out)
{(void)r;(void)w;(void)ctx;(void)id;(void)method;(void)mc;*out=0;return event(1);}
int probe_dispatch(frame_data_read r,frame_data_write w,void *ctx,uint32_t id,uint32_t d,uint32_t has,uint32_t payload,frame_native_method method,void *mc,uint32_t *out)
{(void)r;(void)w;(void)ctx;(void)id;(void)d;(void)has;(void)payload;(void)method;(void)mc;*out=0;return event(2);}
int probe_remove(frame_data_read r,frame_data_write w,void *ctx,uint32_t id,uint32_t cell,frame_pool_system_free release,void *sc,uint32_t limit)
{(void)r;(void)w;(void)ctx;(void)id;(void)cell;(void)release;(void)sc;(void)limit;return event(6);}
int probe_insert(frame_data_read r,frame_data_write w,void *ctx,uint32_t id,frame_pool_system_alloc allocate,frame_pool_system_free release,void *sc,uint32_t limit,uint32_t *out)
{int s;(void)r;(void)w;(void)ctx;(void)id;(void)allocate;(void)release;(void)sc;(void)limit;s=event(7);if(!s)*out=17;return s;}
int probe_queue(frame_data_read r,frame_data_write w,void *ctx,uint32_t id,uint32_t destroy,frame_pool_system_alloc allocate,frame_pool_system_free release,void *sc,uint32_t limit)
{(void)r;(void)w;(void)ctx;(void)id;(void)destroy;(void)allocate;(void)release;(void)sc;(void)limit;return event(8);}
static int read_data(void *ctx,uint32_t a,void *out,uint32_t n){(void)ctx;if(a>sizeof(ram)||n>sizeof(ram)-a)return 0;memcpy(out,ram+a,n);return 1;}
static int write_data(void *ctx,uint32_t a,const void *in,uint32_t n){(void)ctx;if(a>sizeof(ram)||n>sizeof(ram)-a)return 0;memcpy(ram+a,in,n);return 1;}
static void set(uint32_t a,uint32_t v,uint32_t n){oracle_set(ram+a,v,n);}
static void load(uint32_t a,uint32_t n){uint32_t i;for(i=0;i<n;i++)set(a+i*4,oracle_require(),4);}
static void emit(uint32_t a,uint32_t n){uint32_t i;for(i=0;i<n;i++)oracle_put(oracle_get(ram+a+i*4,4));}
/* Fields are semantic config/state values, not a captured RAM fixture. */
static const uint32_t fields[][3]={
 {0x50,4,1},{0x54,4,1},{0x58,4,1},{0xaa,4,1},{0xba,4,1},{0xc0,2,0},{0xc2,2,1},
 {0xc4,1,0},{0xc5,1,0},{0xc6,1,0},{0xcc,1,0},{0xb8,2,1},{0xbe,1,1},{0xbf,1,1},
 {0xb0,2,1},{0xb2,2,0},{0xb4,2,1},{0xb6,2,0},{0xae,2,1},{0xc7,4,1},{0xcb,1,0}};
int main(void)
{
 uint32_t op,i,external,status;frame_entity_factory_services e={0};const uint32_t o=0x10000,t=0x11000;
 oracle_binary();fwrite("BO01",1,4,stdout);e.work_limit=10000;
 while(oracle_word(&op)){
  memset(ram,0,sizeof(ram));event_count=0;
  set(0x55c694,0x7000,4);set(0x7008,0x8000,4);set(0x8004,o,4);set(0x7010,0x8010,4);set(0x8014,t,4);
  load(0x5904fc,1);load(o+0x80,1);load(o+0x7c,1);load(o+0x90,1);load(0x55b6ac,1);
  fail_at=oracle_require();fail_status=(int32_t)oracle_require();ground=(int32_t)oracle_require();normal_ground=(int32_t)oracle_require();
  for(i=0;i<3;i++)normal_value[i]=(int32_t)oracle_require();
  fallback_hit=oracle_require();for(i=0;i<3;i++)fallback_value[i]=(int32_t)oracle_require();
  load(o,3);load(o+24,9);external=oracle_require();load(0x12000,9);set(o+20,external==1?o+24:external?0x12000:0,4);
  for(i=0;i<sizeof(fields)/sizeof(*fields);i++)set(o+fields[i][0],oracle_require(),fields[i][1]);
  load(t,3);load(t+0x50,3);load(t+0xec,1);load(t+24,9);external=oracle_require();load(0x12100,9);set(t+20,external?0x12100:0,4);
  status=(uint32_t)(op?oracle_bounce_tick(read_data,write_data,0,1,&e,frame_math_trig()):oracle_bounce_hit(read_data,write_data,0,1,2,&e,frame_math_trig(),0,0));
  oracle_put(status);emit(0x55b6ac,1);emit(o+0x80,1);emit(o+0x90,1);emit(o,3);emit(o+24,9);external=oracle_get(ram+o+20,4);oracle_put(external!=0);if(external)emit(external,9);else for(i=0;i<9;i++)oracle_put(0);
  for(i=0;i<sizeof(fields)/sizeof(*fields);i++){
   uint32_t v=oracle_get(ram+o+fields[i][0],fields[i][1]);
   if(fields[i][2]&&fields[i][1]==1)v=(uint32_t)(int32_t)(int8_t)v;
   if(fields[i][2]&&fields[i][1]==2)v=(uint32_t)(int32_t)(int16_t)v;
   oracle_put(v);
  }
  oracle_put(event_count);for(i=0;i<event_count;i++)oracle_put(events[i]);
 }
 return ferror(stdin)?3:0;
}
