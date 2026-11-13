/* Production mesh-break body; injectable height isolates only terrain service.
 * Resource clone, pooling, grid and lifecycle calls remain production C. */
#define physics_terrain_height test_height
#include "../src/frame_mesh_break.c"
#undef physics_terrain_height
#include "frame_math.h"
#include "physics_entities.h"
#include "frame_fall.h"
#include "reference_io.h"
static unsigned char ram[0x800000];
static uint32_t cursor,nvertices,nfaces,queries;static int height_status;static int32_t height_value;
static const uint32_t obj=0x10000,peer=0x20000,aux=0x28000,source=0x30000;
static frame_entity_factory_services entities;
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n){(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *in,uint32_t n){(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(ram+at,in,n);return 1;}
static uint32_t word(uint32_t at){return oracle_get(ram+at,4);}static void set(uint32_t at,uint32_t v){oracle_set(ram+at,v,4);}
static int allocate(void *ctx,uint32_t n,uint32_t *out){(void)ctx;if(n>0x600000-cursor)return -4;*out=cursor;cursor+=(n+3u)&~3u;memset(ram+*out,0,n);return 0;}
static int release(void *ctx,uint32_t at){(void)ctx;(void)at;return 0;}
static void check(int status){if(status){fprintf(stderr,"setup %d\n",status);exit(6);}}
int test_height(physics_world_read read,void *ctx,uint32_t root,int32_t x,int32_t z,int32_t *out){(void)read;(void)ctx;(void)root;(void)x;(void)z;queries++;if(height_status>=0)*out=height_value;return height_status;}
static void load(uint32_t at,uint32_t n,uint32_t width){uint32_t i;for(i=0;i<n;i++)oracle_set(ram+at+i*width,oracle_require(),width);}
static void emit(uint32_t at,uint32_t n){uint32_t i;for(i=0;i<n;i++)oracle_put(word(at+i*4));}
static void short_value(uint32_t at){oracle_put((uint32_t)(int32_t)(int16_t)oracle_get(ram+at,2));}
static void head(uint32_t at){set(at,at);set(at+4,at);set(at+8,at);}
static void load_list(uint32_t at){uint32_t n=oracle_require(),i,out;head(at);for(i=0;i<n;i++)check(frame_list_append(read_data,write_data,0,oracle_require(),at,allocate,release,0,100000,&out));}
static void emit_list(uint32_t at){uint32_t node=word(at),n=0;while(node!=at){if(n++==65536)exit(6);node=word(node);}oracle_put(n);node=word(at);while(node!=at){oracle_put(word(node+8));node=word(node);}}
static int method(void *ctx,uint32_t fn,uint32_t id,uint32_t has,uint32_t payload,uint32_t *out){int status;(void)ctx;(void)has;(void)payload;
    if(fn==0xf000){status=frame_object_deactivate(read_data,write_data,0,id,&entities);if(!status)*out=0;return status;}
    if(fn==0xf001)return frame_entity_dispatch(read_data,write_data,0,id,0x9500,0,0,method,0,out);
    if(fn==0xf004)return frame_entity_enable_tick(read_data,write_data,0,id,allocate,release,0,entities.work_limit,out);
    if(fn==0xf005)return frame_mesh_break_tick(read_data,write_data,0,id,&entities,frame_math_trig(),out);
    if(fn==0xf002){status=frame_entity_queue(read_data,write_data,0,id,1,allocate,release,0,entities.work_limit);if(!status)*out=id;return status;}
    return -5;
}
static int physical_method(void *ctx,uint32_t fn,uint32_t id,uint32_t argument,uint32_t *out){(void)ctx;if(fn==0xf003)return frame_mesh_break_hit(read_data,write_data,0,id,argument,&entities,0,0,out);return -5;}
static int physical_scene(void *ctx,int phase,uint32_t o,uint32_t id,int32_t argument){uint32_t out;(void)ctx;(void)o;
    if(phase==PHYSICS_SCENE_ENTITY_ACTIVATE)return frame_entity_activate(read_data,write_data,0,id,method,0,&out);
    if(phase==PHYSICS_SCENE_REMOVE_GRID)return frame_spatial_remove(read_data,write_data,0,id,(uint32_t)argument,release,0,entities.work_limit);
    return -8;
}
static void snapshot(int status,uint32_t result){uint32_t i,j,shape=word(aux),states=word(aux+20),slot=word(0x55c698),display=word(obj+72);
    oracle_put((uint32_t)status);oracle_put(result);emit(0x55b6ac,1);oracle_put(slot==0?8:slot==1?27:slot==2?4:slot==3?25:slot==4?22:slot);oracle_put(queries);
    oracle_put(oracle_get(ram+aux+34,2));emit(aux+36,2);emit(obj+16,1);oracle_put(display==0?0:display==source?1:2);oracle_put(shape!=0);emit(obj+0x80,1);emit(obj+0xea,1);emit(obj+0xee,2);emit(obj+0xe2,1);emit(obj+0xda,1);emit(obj+24,9);oracle_put(display?oracle_get(ram+display+12,2):0);
    if(shape){oracle_put(oracle_get(ram+shape+12,2));for(i=0;i<nfaces;i++){emit(states+i*32,7);short_value(states+i*32+28);oracle_put(oracle_get(ram+states+i*32+30,2));}
        for(i=0;i<nvertices;i++)for(j=0;j<3;j++)short_value(word(shape+4)+i*8+j*2);
        for(i=0;i<nfaces;i++)oracle_put(oracle_get(ram+word(shape+8)+i*24+18,2));
        for(j=0;j<3;j++)for(i=0;i<nfaces;i++)short_value(word(shape+16+j*4)+i*2);
    }
    emit_list(0x55000);emit_list(0x55100);emit_list(0x55200);emit_list(0x55300);emit_list(0x55400);for(i=0;i<64;i++)emit_list(0x50000+i*16);
}
int main(void){uint32_t steps;oracle_binary();fwrite("MB01",1,4,stdout);
    while(oracle_word(&steps)){
        uint32_t prepared,basis,i,j,faces_at,centers_at,size,out,tail,has_peer,special,failed;
        memset(ram,0,sizeof(ram));cursor=0x100000;queries=0;entities=(frame_entity_factory_services){0};entities.allocate=allocate;entities.release=release;entities.method=method;entities.work_limit=100000;
        set(0x54b3d8,1);set(0x55c694,0x7000);set(0x7008,0x8000);set(0x7010,0x8008);set(0x8000,0x9000);set(0x8004,obj);set(0x8008,0x9000);set(0x800c,peer);
        set(0x901c,4);set(0x9024,0x9100);set(0x9100,0xf000);set(0x9104,0xf001);set(0x9108,0xf004);set(0x910c,0xf003);set(0x9110,0xf005);set(0x545f3c,0x9700);set(0x971c,2);set(0x545ee4,0x9800);set(0x981c,3);set(0x545ee8,0x9900);set(0x991c,4);set(0x545ed0,0x9500);set(0x545f64,0x9600);set(0x951c,0);set(0x961c,1);
        set(0x53e4b0,0xa000);set(0xa01c,1);set(0xa024,0xa100);set(0xa100,0xf002);
        set(obj+0x88,aux);memcpy(ram+aux+4,"frag.sm",8);set(obj+72,source);
        prepared=oracle_require();load(aux+34,1,2);load(obj,3,4);basis=oracle_require();load(obj+24,9,4);set(obj+20,basis?obj+24:0);memcpy(ram+peer+24,ram+obj+24,36);
        has_peer=oracle_require();load(peer+80,3,4);load(0x55b6ac,1,4);load(obj+16,1,4);load(obj+0x90,1,4);load(0x55c698,1,4);special=oracle_require();set(0x545094,special?0x9000:0);
        load(aux+44,1,4);for(i=0;i<5;i++)load(aux+48+i*4,2,2);failed=oracle_require();if(failed)set(0x9100,0xf099);
        load_list(0x55000);load_list(0x55100);load_list(0x55200);load_list(0x55300);load_list(0x55400);
        set(0x54b48c,0x55000);set(0x54b488,0x55100);set(0x55b6a8,0x55200);set(0x55b6bc,0x55300);set(0x55b6b8,0x55400);
        for(i=0;i<64;i++){load_list(0x50000+i*16);set(0x544e70+i*4,0x50000+i*16);}
        nvertices=oracle_require();nfaces=oracle_require();if(nvertices>256||nfaces>64)return 2;
        oracle_set(ram+source,nvertices,2);oracle_set(ram+source+2,nfaces,2);oracle_set(ram+source+12,77,2);set(source+4,source+84);faces_at=source+84+nvertices*8;set(source+8,faces_at);centers_at=faces_at+nfaces*24;
        for(j=0;j<3;j++)set(source+16+j*4,centers_at+j*nfaces*2);
        for(i=0;i<nvertices;i++)for(j=0;j<3;j++)load(source+84+i*8+j*2,1,2);
        for(i=0;i<nfaces;i++){for(j=0;j<3;j++)load(faces_at+i*24+8+j,1,1);load(faces_at+i*24+18,1,2);}
        for(j=0;j<3;j++)load(centers_at+j*nfaces*2,nfaces,2);
        size=(centers_at+nfaces*6-source+3u)&~3u;set(0x41000,source);memcpy(ram+0x41004,"frag.sm",8);set(0x41014,size);head(0x42000);check(frame_list_append(read_data,write_data,0,0x41000,0x42000,allocate,release,0,100000,&out));set(0x55ae54,0x42000);
        if(prepared){check(frame_mesh_break_prepare(read_data,write_data,0,1,aux+4,&entities,0));ram[aux+33]=1;for(i=0;i<nfaces;i++){load(word(aux+20)+i*32,7,4);load(word(aux+20)+i*32+28,2,2);}load(aux+36,2,4);}
        tail=oracle_require();
        for(i=0;i<steps;i++){uint32_t op=oracle_require(),limit;int status;load(0x5904fc,1,4);load(0x52ee9c,1,4);height_status=(int32_t)oracle_require();height_value=(int32_t)oracle_require();limit=oracle_require();entities.work_limit=limit;out=0x7182;
            if(op==1){uint16_t pair=(uint16_t)tail;status=frame_mesh_break_hit(read_data,write_data,0,1,has_peer?2:0,&entities,0,tail==UINT32_MAX?0:&pair,&out);}
            else if(op==3){physics_obstacle_services p={0};physics_entity_environment env={0};p.method=physical_method;env.read=read_data;env.write=write_data;env.trig=frame_math_trig();env.services=&p;env.scene=physical_scene;status=physics_known_entity_method(&env,0x40a3e0,1,has_peer?2:0,&out);}
            else if(op==4){status=frame_fall_tick(read_data,write_data,0,1,&entities,frame_math_trig());if(!status)out=0;}
            else status=frame_mesh_break_tick(read_data,write_data,0,1,&entities,frame_math_trig(),&out);
            snapshot(status,out);
        }
    }
    return ferror(stdin)?3:0;
}
