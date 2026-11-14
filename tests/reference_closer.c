/* Original Closer tick plus actual integer matrix-angle conversion.
 * Lifecycle/fragment callback responses are declared inputs; SR03 uses owners. */
#define frame_closer_construct oracle_closer_construct
#define frame_closer_hit oracle_closer_hit
#define frame_closer_tick oracle_closer_tick
#define frame_entity_queue probe_queue
#define frame_entity_dispatch probe_dispatch
#include "../src/frame_closer.c"
#include "frame_math.h"
#include "reference_io.h"
static unsigned char ram[0x800000];
static uint32_t events[8],count;
static int failure;
static int event(uint32_t kind){events[count++]=kind;return failure;}
int probe_queue(frame_data_read r,frame_data_write w,void *ctx,uint32_t id,uint32_t destroy,frame_pool_system_alloc a,frame_pool_system_free f,void *sc,uint32_t limit)
{(void)r;(void)w;(void)ctx;(void)id;(void)destroy;(void)a;(void)f;(void)sc;(void)limit;return event(1);}
int probe_dispatch(frame_data_read r,frame_data_write w,void *ctx,uint32_t id,uint32_t d,uint32_t has,uint32_t payload,frame_native_method method,void *mc,uint32_t *out)
{(void)r;(void)w;(void)ctx;(void)id;(void)d;(void)has;(void)payload;(void)method;(void)mc;*out=0;return event(2);}
static int read_data(void *ctx,uint32_t a,void *out,uint32_t n){(void)ctx;if(a>sizeof(ram)||n>sizeof(ram)-a)return 0;memcpy(out,ram+a,n);return 1;}
static int write_data(void *ctx,uint32_t a,const void *in,uint32_t n){(void)ctx;if(a>sizeof(ram)||n>sizeof(ram)-a)return 0;memcpy(ram+a,in,n);return 1;}
static uint32_t word(uint32_t a){return oracle_get(ram+a,4);}
static void set(uint32_t a,uint32_t v){oracle_set(ram+a,v,4);}
static void load(uint32_t a,uint32_t n){uint32_t i;for(i=0;i<n;i++)set(a+i*4,oracle_require());}
static void emit(uint32_t a,uint32_t n){uint32_t i;for(i=0;i<n;i++)oracle_put(word(a+i*4));}
static uint32_t phase(uint32_t p){return p==1?0x406490:p==2?0x4064e0:p==3?0x406480:0;}
int main(void)
{
 uint32_t op,id,frame,dt,limit,player,p,external,i,result,angles[3],which;
 const uint32_t o=0x10000,t=0x11000;frame_entity_factory_services e={0};int status;
 if(frame_math_initialize(write_data,0))return 2;
 oracle_binary();fwrite("CL01",1,4,stdout);
 while(oracle_word(&op)){
  memset(ram+0x7000,0,0x200);memset(ram+o,0,0x3000);count=0;
  id=oracle_require();frame=oracle_require();dt=oracle_require();limit=oracle_require();player=oracle_require();
  set(0x55c694,0x7000);set(0x7000+id*8,0x8000);set(0x8004,o);set(0x7010,player?0x8010:0);set(0x8014,t);set(0x53e8b8,player?2:3);load(t,3);
  load(o,3);load(o+0xaa,2);p=oracle_require();set(o+0xb2,phase(p));which=oracle_require();load(o+24,9);load(0x12000,9);external=which==1?o+24:which?0x12000:0;set(o+20,external);failure=(int32_t)oracle_require();
  set(0x590564,frame);set(0x5904fc,dt);e.work_limit=limit;result=77;angles[0]=11;angles[1]=22;angles[2]=33;
  if(!op)status=frame_camera_matrix_angle_values(read_data,0,external,angles,&result);
  else status=oracle_closer_tick(read_data,write_data,0,id,&e,frame_math_trig());
  oracle_put((uint32_t)status);emit(o+0xaa,2);p=word(o+0xb2);oracle_put(p==0x406490?1:p==0x4064e0?2:p==0x406480?3:0);emit(o+24,9);oracle_put(external!=0);if(external)emit(external,9);else for(i=0;i<9;i++)oracle_put(0);
  for(i=0;i<3;i++)oracle_put(angles[i]);
  oracle_put(result);oracle_put(count);for(i=0;i<count;i++)oracle_put(events[i]);
 }
 return ferror(stdin)?3:0;
}
