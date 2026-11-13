#include "reference_game.h"
#include "physics_objects.h"
#include "physics_memory.h"
static uint32_t objects[4096],entities[4096],count;
static uint32_t word(uint32_t at){return reference_game_word(at,4);}
static int32_t signed_word(uint32_t at){return oracle_signed(word(at));}
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if(at>TANKRACER_DATA_BYTES||n>TANKRACER_DATA_BYTES-at)return 0;memcpy(out,tank_data()+at,n);return 1;}
static void emit(uint32_t at,unsigned n){unsigned i;for(i=0;i<n;i++)oracle_put(word(at+i*4));}
static uint32_t object_of(uint32_t id){uint32_t link=word(word(0x55c694)+id*8);return link?word(link+4):0;}
static uint32_t object_index(uint32_t o){unsigned i;if(!o)return UINT32_MAX;for(i=0;i<count;i++)if(objects[i]==o)return i;exit(5);}
static void collect(uint32_t id)
{
    uint32_t o=object_of(id);unsigned i;if(!o)exit(5);
    for(i=0;i<count;i++)if(objects[i]==o)return;
    if(count==4096)exit(5);
    objects[count]=o;entities[count++]=id;
}
static void list(uint32_t sentinel,int output)
{
    uint32_t node=word(sentinel),n=0;
    while(node!=sentinel){if(n++==65536)exit(5);if(!output)collect(word(node+8));node=word(node);}
    if(!output)return;
    oracle_put(n);node=word(sentinel);while(node!=sentinel){oracle_put(object_index(object_of(word(node+8))));node=word(node);}
}
static int is_tank(uint32_t id){unsigned i;for(i=0;i<9;i++)if(word(0x53e8e0u+i*4u)==id)return 1;return 0;}
static void collider(uint32_t o,uint32_t id)
{
    uint32_t external=word(o+0x14);unsigned i;oracle_put(id);emit(o,3);emit(o+0x18,9);oracle_put(external!=0);
    if(external)emit(external,9);else for(i=0;i<9;i++)oracle_put(i%4==0?4096:0);
    oracle_put(reference_game_word(word(o+0x48)+12,2));emit(o+0x60,6);
    if(is_tank(id)){
        oracle_put(reference_game_word(o+0xe07,1));oracle_put(reference_game_word(o+0xded,1)!=0);oracle_put((reference_game_word(o+0xd90,1)&1u)!=0);oracle_put(reference_game_word(o+0xde9,1)!=0);
        emit(o+0x632,6);for(i=0;i<4;i++)emit(o+0x128+i*70u,3);
    }else for(i=0;i<22;i++)oracle_put(0);
}
static void result(uint32_t op,uint32_t self_id,const int32_t a[3],const int32_t b[3])
{
    physics_object_hit h={63,objects[0],93,{17,19,23},{31,37,41}};int status;unsigned i;
    if(op==2)status=physics_obstacle_query(read_data,0,self_id,a,b,&h);
    else if(op==3)status=physics_deformable_query(read_data,0,a,b,&h);
    else status=physics_tank_query(read_data,0,object_of(self_id),a,b,&h);
    oracle_put((uint32_t)status);oracle_put(h.entity);oracle_put(object_index(h.object));oracle_put((uint32_t)h.code);
    for(i=0;i<3;i++)oracle_put((uint32_t)h.point[i]);
    for(i=0;i<3;i++)oracle_put((uint32_t)h.normal[i]);
}
static void query(uint32_t self_id,const int32_t a[3],const int32_t b[3])
{unsigned i;for(i=0;i<3;i++)oracle_put((uint32_t)a[i]);for(i=0;i<3;i++)oracle_put((uint32_t)b[i]);for(i=2;i<=4;i++)result(i,self_id,a,b);}
int main(int argc,char **argv)
{
    uint32_t map,self_id,i,j;int status;
    if(argc!=3)return 2;
    map=(uint32_t)strtoul(argv[2],0,10);
    status=reference_game_start_with_opponents(argv[1],map,1,0,1);if(status)return status;
    /* Physics marks peers query-ready after their first actual update. */
    status=tank_step(1,0,1);if(status)return 6;
    for(i=0;i<64;i++)list(word(0x544e70u+i*4u),0);
    list(word(0x53cd18),0);
    for(i=0;i<9;i++){uint32_t id=word(0x53e8e0u+i*4u);if(id)collect(id);}
    self_id=word(0x53e8b8);collect(self_id);oracle_binary();fwrite("OW01",1,4,stdout);
    oracle_put(count);for(i=0;i<count;i++)collider(objects[i],entities[i]);emit(0x544f70,4);
    for(i=0;i<64;i++)list(word(0x544e70u+i*4u),1);
    list(word(0x53cd18),1);
    for(i=0;i<9;i++){uint32_t id=word(0x53e8e0u+i*4u);oracle_put(id?object_index(object_of(id)):UINT32_MAX);}
    oracle_put(object_index(object_of(self_id)));oracle_put(self_id);oracle_put(count*7u+64u);
    for(i=0;i<count;i++) {
        int32_t a[3],b[3];for(j=0;j<3;j++)a[j]=b[j]=signed_word(objects[i]+j*4);query(self_id,a,b);
        for(j=0;j<3;j++) {
            int32_t amount=(int32_t)(reference_game_word(word(objects[i]+0x48)+12,2)+96u)*16;unsigned k;
            for(k=0;k<3;k++)a[k]=b[k]=signed_word(objects[i]+k*4);
            a[j]=pm_sub(a[j],amount);b[j]=pm_add(b[j],amount);query(self_id,a,b);
            {int32_t t=a[j];a[j]=b[j];b[j]=t;}query(self_id,a,b);
        }
    }
    for(i=0;i<64;i++) {
        int32_t a[3],b[3];a[0]=pm_add(signed_word(0x544f70),pm_signed((i%8u)<<(word(0x544f78)&31u)));
        a[2]=pm_add(signed_word(0x544f74),pm_signed((i/8u)<<(word(0x544f7c)&31u)));a[1]=0;
        for(j=0;j<3;j++)b[j]=a[j];
        b[0]=pm_add(b[0],20000);b[2]=pm_add(b[2],20000);query(self_id,a,b);
    }
    tank_destroy();return 0;
}
