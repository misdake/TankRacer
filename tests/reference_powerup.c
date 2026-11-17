/* Isolated real Powerup dispatch after full resource-based world construction. */
#include "reference_game.h"
#include "frame_native.h"
static frame_native_context *native;
int __real_frame_native_dispatch(void *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t *);
int __wrap_frame_native_dispatch(void *ctx,uint32_t target,uint32_t entity,uint32_t has,uint32_t payload,uint32_t *out)
{native=(frame_native_context *)ctx;return __real_frame_native_dispatch(ctx,target,entity,has,payload,out);}
static uint32_t word(uint32_t a){return reference_game_word(a,4);}
static void set(uint32_t a,uint32_t v){oracle_set((unsigned char *)(uintptr_t)tank_data()+a,v,4);}
static uint32_t object_of(uint32_t id){uint32_t r=word(word(0x55c694)+id*8);return r?word(r+4):0;}
static void words(uint32_t at,unsigned n){unsigned i;for(i=0;i<n;i++)oracle_put(word(at+i*4));}
static void list(uint32_t address){uint32_t h=word(address),p,n=0;for(p=word(h);p!=h;p=word(p)){if(++n>100000)exit(8);}oracle_put(n);for(p=word(h);p!=h;p=word(p))oracle_put(word(p+8));}
static void snapshot(uint32_t o){
    words(o,3);words(o+24,9);words(o+0xaa,1);words(o+0x14f,1);words(o+0x137,3);words(o+0x143,3);
    words(o+0xfb,3);words(o+0x107,9);words(o+0x153,2);words(o+0xb2,1);words(o+0x16f,1);
    oracle_put(reference_game_word(word(o+0x88)+34,2));words(o+0x90,1);words(0x55b6ac,1);list(0x54b488);
}
int main(int argc,char **argv){
    uint32_t map,id,o,p,steps,i,j,result,has_y;int status;
    if(argc!=4)return 2;
    map=(uint32_t)strtoul(argv[2],0,10);id=(uint32_t)strtoul(argv[3],0,10);
    status=reference_game_start(argv[1],map,1,0);if(status)return status;if(!native)return 8;
    o=object_of(id);p=object_of(word(0x53e8b8));if(!o||!p)return 8;
    oracle_binary();steps=oracle_require();snapshot(o);
    for(i=0;i<steps;i++){
        set(0x5904fc,oracle_require());set(0x590564,oracle_require());
        for(j=0;j<3;j++){set(p+j*4,oracle_require());}
        for(j=0;j<3;j++){set(0x74aee0+j*4,oracle_require());}
        has_y=oracle_require();if(has_y)set(o+4,oracle_require());
        status=frame_native_dispatch(native,0x422050,id,0,0,&result);oracle_put((uint32_t)status);snapshot(o);if(status)break;
    }
    tank_destroy();return 0;
}
