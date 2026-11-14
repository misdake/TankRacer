/* Real World/factory construction, explicit route linkage and original queries.
 * The supplied script is authored resource input, never a saved RAM snapshot. */
#define frame_world_load_span original_world_load_span
#include "../src/frame_world.c"
#undef frame_world_load_span
#include "reference_game.h"
#include "frame_actors.h"
#include "frame_progress.h"
static unsigned char script[40000],query_position[12];
static uint32_t script_size,loaded,waypoint_class;
static const frame_entity_factory_services *services;
static frame_data_read read_data;
static frame_data_write write_data;
static void *data_ctx;
static uint32_t word(uint32_t a){return reference_game_word(a,4);}
static uint32_t object_of(uint32_t id){uint32_t r=word(word(0x55c694)+id*8u);return r?word(r+4u):0;}
static void check(int status,const char *phase){if(status){fprintf(stderr,"waypoint oracle %s status %d\n",phase,status);exit(7);}}
static int query_read(void *ctx,uint32_t a,void *out,uint32_t n)
{
    if(a>=0xffff0000u&&a-0xffff0000u<=sizeof(query_position)&&n<=sizeof(query_position)-(a-0xffff0000u)){
        memcpy(out,query_position+(a-0xffff0000u),n);return 1;
    }
    return read_data(ctx,a,out,n);
}
int frame_world_load_span(frame_data_read r,frame_data_write w,void *c,uint32_t f,uint32_t l,const unsigned char *b,uint32_t n,
                         const frame_entity_factory_services *e,uint32_t *out,uint32_t *used)
{
    int status;(void)b;(void)n;services=e;read_data=r;write_data=w;data_ctx=c;
    status=original_world_load_span(r,w,c,f,l,script,script_size,e,out,used);if(status)return status;
    loaded=1;return -98;
}
#include "reference_waypoint_io.h"
int main(int argc,char **argv)
{
    uint32_t i,map,op,current,position[3],side,limit,out;int32_t lap,score;int status,link_status,empty;
    frame_checkpoint_result crossing;
    if(argc!=3)return 2;
    map=(uint32_t)strtoul(argv[2],0,10);
    oracle_binary();script_size=oracle_require();if(script_size>sizeof(script))return 2;
    if(fread(script,1,script_size,stdin)!=script_size)return 3;
    status=reference_game_start(argv[1],map,1,0);
    if(status!=4||!loaded){fprintf(stderr,"waypoint bootstrap %d\n",status);return 7;}
    check(frame_class_lookup_name(read_data,write_data,data_ctx,(const unsigned char *)"WayPoint",9,services,&waypoint_class),"class");
    empty=word(word(0x5468a0))==word(0x5468a0);
    link_status=frame_waypoint_link(read_data,write_data,data_ctx,services);
    if(!empty||link_status!=-4)check(link_status,"link");
    check(frame_entities_drain_queues(read_data,write_data,data_ctx,services->method,services->method_ctx,services->release,services->system_ctx,100000),"queues");
    fwrite("WP02",1,4,stdout);oracle_put((uint32_t)link_status);waypoint_state();
    while(oracle_word(&op)){
        current=oracle_require();for(i=0;i<3;i++)position[i]=oracle_require();lap=(int32_t)oracle_require();side=oracle_require();limit=oracle_require();
        if(op==0){score=(int32_t)0xdeadbeef;status=frame_route_progress(read_data,data_ctx,(int32_t)position[0],(int32_t)position[2],current,lap,limit,&score);oracle_put((uint32_t)status);oracle_put((uint32_t)score);}
        else if(op==1){
            for(i=0;i<3;i++)oracle_set(query_position+i*4,position[i],4);
            out=0xdeadbeef;status=frame_route_nearest(query_read,data_ctx,0xffff0000u,limit,&out);oracle_put((uint32_t)status);oracle_put(out);
        }else if(op==2){
            for(i=0;i<3;i++)oracle_set(query_position+i*4,position[i],4);
            crossing=(frame_checkpoint_result){0xdeadbeef,0xdeadbeef};
            status=frame_checkpoint_at(query_read,data_ctx,0xffff0000u,current,side,&crossing);
            oracle_put((uint32_t)status);oracle_put(crossing.next_entity);oracle_put(crossing.finishes_lap);
        }else return 2;
    }
    tank_destroy();return ferror(stdin)?3:0;
}
