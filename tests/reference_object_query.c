#include "physics_objects.h"
#include "reference_io.h"
static unsigned char ram[0x600000];
static uint32_t objects[64],entities[64],count,next_node;
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if(at>sizeof(ram)||n>sizeof(ram)-at)return 0;memcpy(out,ram+at,n);return 1;}
static void load(uint32_t at,unsigned n,unsigned width)
{unsigned i;for(i=0;i<n;i++)oracle_set(ram+at+i*width,oracle_require(),width);}
static void vector(int32_t v[3]) {unsigned i;for(i=0;i<3;i++)v[i]=oracle_signed(oracle_require());}
static void object(unsigned index)
{
    uint32_t o=0x10000u+index*0x2000u,entity=oracle_require(),external;unsigned i;
    if(!entity||entity>=64)exit(2);
    objects[index]=o;entities[index]=entity;memset(ram+o,0,0x2000);
    oracle_set(ram+0x7000u+entity*8u,0x8000u+entity*8u,4);
    oracle_set(ram+0x8004u+entity*8u,o,4);
    load(o,3,4);load(o+0x18,9,4);external=oracle_require();load(o+0x1900,9,4);
    oracle_set(ram+o+0x14,external?o+0x1900:0,4);oracle_set(ram+o+0x48,o+0x1800,4);load(o+0x180c,1,2);
    load(o+0x60,6,4);load(o+0xe07,1,1);load(o+0xded,1,1);load(o+0xd90,1,1);load(o+0xde9,1,1);load(o+0x632,6,4);
    for(i=0;i<4;i++)load(o+0x128+i*70u,3,4);
}
static void list(uint32_t sentinel)
{
    uint32_t n=oracle_require(),last=sentinel,i;
    if(n>65537)exit(2);
    for(i=0;i<n;i++) {
        uint32_t id=oracle_require(),node=next_node;next_node+=16;
        if(id>=count||next_node>=sizeof(ram))exit(2);
        oracle_set(ram+last,node,4);oracle_set(ram+node+8,entities[id],4);last=node;
    }
    oracle_set(ram+last,sentinel,4);
}
static uint32_t index_of(uint32_t address)
{unsigned i;if(!address)return UINT32_MAX;for(i=0;i<count;i++)if(objects[i]==address)return i;exit(2);}
int main(void)
{
    uint32_t op;oracle_binary();fwrite("OQ01",1,4,stdout);
    while(oracle_word(&op)) {
        unsigned i;int status;int32_t a[3],b[3];
        if(op==1) {
            int32_t low[3],high[3],point[3],normal[3];vector(a);vector(b);vector(low);vector(high);vector(point);vector(normal);
            status=physics_segment_box(a,b,low,high,point,normal);oracle_put((uint32_t)status);
            for(i=0;i<3;i++)oracle_put((uint32_t)point[i]);
            for(i=0;i<3;i++)oracle_put((uint32_t)normal[i]);
        } else if(op>=2&&op<=5) {
            uint32_t self,other,self_entity,initial;physics_object_hit hit;
            count=oracle_require();if(!count||count>64)exit(2);
            oracle_set(ram+0x55c694,0x7000,4);for(i=0;i<count;i++)object(i);
            load(0x544f70,4,4);next_node=0x100000;
            for(i=0;i<64;i++){uint32_t sentinel=0x90000u+i*16u;oracle_set(ram+0x544e70u+i*4u,sentinel,4);list(sentinel);}
            oracle_set(ram+0x53cd18,0x90400,4);list(0x90400);
            for(i=0;i<9;i++){uint32_t id=oracle_require();if(id!=UINT32_MAX&&id>=count)exit(2);oracle_set(ram+0x53e8e0u+i*4u,id==UINT32_MAX?0:entities[id],4);}
            self=oracle_require();self_entity=oracle_require();other=oracle_require();if(self>=count||other>=count)exit(2);
            vector(a);vector(b);hit.entity=oracle_require();initial=oracle_require();if(initial!=UINT32_MAX&&initial>=count)exit(2);
            hit.object=initial==UINT32_MAX?0:objects[initial];hit.code=oracle_signed(oracle_require());vector(hit.point);vector(hit.normal);
            if(op==2)status=physics_obstacle_query(read_data,0,self_entity,a,b,&hit);
            else if(op==3)status=physics_deformable_query(read_data,0,a,b,&hit);
            else if(op==4)status=physics_tank_query(read_data,0,objects[self],a,b,&hit);
            else status=physics_tank_overlap(read_data,0,objects[self],objects[other]);
            oracle_put((uint32_t)status);
            if(op!=5){oracle_put(hit.entity);oracle_put(index_of(hit.object));oracle_put((uint32_t)hit.code);for(i=0;i<3;i++)oracle_put((uint32_t)hit.point[i]);for(i=0;i<3;i++)oracle_put((uint32_t)hit.normal[i]);}
        } else return 2;
    }
    return ferror(stdin)?3:0;
}
