/* Actual player blast kernel with semantic field inputs, not a memory dump. */
#include "frame_shatter.h"
#include "reference_io.h"
static unsigned char ram[0x800000];
static int read_data(void *c,uint32_t a,void *out,uint32_t n){(void)c;if(a>sizeof(ram)||n>sizeof(ram)-a)return 0;memcpy(out,ram+a,n);return 1;}
static int write_data(void *c,uint32_t a,const void *in,uint32_t n){(void)c;if(a>sizeof(ram)||n>sizeof(ram)-a)return 0;memcpy(ram+a,in,n);return 1;}
static void set(uint32_t a,uint32_t v,unsigned n){oracle_set(ram+a,v,n);}
static void load(uint32_t a,unsigned n){unsigned i;for(i=0;i<n;i++)set(a+i*4,oracle_require(),4);}
static void emit(uint32_t a,unsigned n){unsigned i;for(i=0;i<n;i++)oracle_put(oracle_get(ram+a+i*4,4));}
int main(void){
 const uint32_t o=0x10000;uint32_t seed,mode,damage,mark,i;int32_t source[3];int status;
 oracle_binary();fwrite("BL01",1,4,stdout);
 while(oracle_word(&seed)){
  memset(ram,0,sizeof(ram));set(0x55c694,0x7000,4);set(0x7008,0x8000,4);set(0x8004,o,4);set(0x55b6ac,seed,4);
  load(o+0x78,3);for(i=0;i<3;i++)source[i]=(int32_t)oracle_require();mode=oracle_require();damage=oracle_require();
  set(0x52ee9c,damage?4:0,4);set(0x53e3fc,damage,4);load(0x531ba4,1);load(0x58e7ac,1);mark=oracle_require();set(0x53e8b8,mark?1:0,4);
  load(o+0x50,3);load(o+0xb4,3);load(o+0x107e,1);load(o+0x10c2,1);set(o+0x1122,oracle_require(),2);set(o+0x1124,oracle_require(),2);
  load(o+0xdaf,1);set(o+0xded,oracle_require(),1);set(o+0x10da,oracle_require(),2);set(o+0x10dc,oracle_require(),2);
  status=frame_shatter_player_impulse_values(read_data,write_data,0,1,source,mode,0,0);
  oracle_put((uint32_t)status);emit(0x55b6ac,1);emit(o+0x50,3);emit(o+0xb4,3);emit(o+0x107e,1);emit(o+0x10c2,1);
  oracle_put(oracle_get(ram+o+0x1122,2));oracle_put(oracle_get(ram+o+0x1124,2));emit(o+0xdaf,1);oracle_put(ram[o+0xded]);oracle_put(oracle_get(ram+o+0x10da,2));oracle_put(oracle_get(ram+o+0x10dc,2));
 }
 return ferror(stdin)?3:0;
}
