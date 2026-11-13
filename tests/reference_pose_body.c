#include "physics_pose.h"
#include "physics_body.h"
#include "frame_math.h"
#include "reference_io.h"
static unsigned char ram[0x600000];
static const uint32_t object=0x10000;
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if (at>sizeof(ram)||n>sizeof(ram)-at) return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *data,uint32_t n)
{(void)ctx;if (at>sizeof(ram)||n>sizeof(ram)-at) return 0;memcpy(ram+at,data,n);return 1;}
static void load(uint32_t at,unsigned words) {unsigned i;for (i=0;i<words;i++) oracle_set(ram+at+i*4,oracle_require(),4);}
static void store_width(uint32_t at,unsigned width) {oracle_set(ram+at,oracle_require(),width);}
static void emit(uint32_t at,unsigned words) {unsigned i;for (i=0;i<words;i++) oracle_put(oracle_get(ram+at+i*4,4));}
static void pose_state(int output)
{
    static const uint32_t poses[]={0x2a6,0x3de,0x446,0x30e,0x376,0x4ae,0x516};unsigned i;
    if (output) {emit(object+0x78,3);emit(object+0x18,9);emit(object,3);} else {load(object+0x78,3);load(object+0x18,9);load(object,3);}
    for (i=0;i<7;i++) {if (output) emit(object+poses[i],12);else load(object+poses[i],12);}
    if (output) {emit(object+0x5e6,3);oracle_put(ram[object+0xdf5]);oracle_put(ram[object+0xdf6]);}
    else {load(object+0x5e6,3);store_width(object+0xdf5,1);store_width(object+0xdf6,1);}
}
int main(void)
{
    uint32_t op;oracle_binary();fwrite("PB01",1,4,stdout);
    while (oracle_word(&op)) {
        unsigned i,j;int status;
        memset(ram+object,0,0x2000);
        if (op==1) {
            uint32_t external;
            pose_state(0);load(object+0x3d2,3);load(object+0x43a,3);load(object+0x302,3);load(object+0x36a,3);
            load(object+0x4a2,3);load(object+0x50a,3);load(object+0x5da,3);load(object+0x62e,1);load(object+0x6fe,1);load(0x531bd8,3);
            for (i=0;i<4;i++) load(object+0x148+i*70,1);
            store_width(object+0xdd0,2);load(0x545ffc,1);load(0x58e7ac,1);load(0x59054c,1);
            load(object+0xe13,1);store_width(object+0x762,2);load(object+0x764,1);load(object+0x76c,1);
            external=oracle_require();load(0x2000,9);oracle_set(ram+object+0x14,external==1 ? object+0x18 : external==2 ? 0x2000 : 0,4);
            status=physics_pose_update(read_data,write_data,0,object,frame_math_trig());oracle_put((uint32_t)status);pose_state(1);
        } else if (op==2) {
            uint32_t mesh,vertices=0x4000,count;int32_t recoil_decay,recoil_step;
            load(object+0x764,4);recoil_decay=oracle_signed(oracle_require());recoil_step=oracle_signed(oracle_require());
            oracle_set(ram+0x5319c8,(uint32_t)recoil_decay<<24,4);oracle_set(ram+0x5319cc,(uint32_t)recoil_step,2);load(0x5319d0,1);load(0x5904fc,1);
            mesh=oracle_require() ? 0x3000 : 0;oracle_set(ram+object+0x366,mesh,4);oracle_set(ram+0x3004,vertices,4);
            for (i=0;i<4;i++) store_width(object+0x784+i,1);
            for (i=0;i<4;i++) store_width(object+0x788+i*2,2);
            count=oracle_require();if (count>256) return 2;
            for (i=0;i<count;i++) for (j=0;j<3;j++) store_width(vertices+i*8+j*2,2);
            status=physics_body_articulation(read_data,write_data,0,object);oracle_put((uint32_t)status);emit(object+0x764,4);
            for (i=0;i<count;i++) for (j=0;j<3;j++) {uint32_t v=oracle_get(ram+vertices+i*8+j*2,2);oracle_put(v>32767 ? v|0xffff0000u : v);}
        } else if (op==3) {
            load(object+0x77c,2);load(object+0x774,2);load(object+0xc4,1);load(object+0xcc,1);load(0x5904fc,1);
            oracle_set(ram+0x531ca4,oracle_require()<<24,4);status=physics_player_lean(read_data,write_data,0,object);oracle_put((uint32_t)status);emit(object+0x77c,2);emit(object+0x774,2);
        } else if (op==4) {
            load(object+0x78,3);load(object+0x84,3);
            for (i=0;i<4;i++) {load(object+0x128+i*70,3);load(object+0x138+i*70,1);}
            for (i=0;i<12;i++) oracle_set(ram+0x532888+i*4,i<9 && i%4==0 ? 4096 : 0,4);
            for (i=0;i<4;i++) oracle_set(ram+0x531c88+i*4,(uint32_t[]){1,3,2,0}[i],4);
            oracle_set(ram+object+0x71e,0x3000,4);oracle_set(ram+0x3004,0x4000,4);
            status=physics_contact_hull_refresh(read_data,write_data,0,object);oracle_put((uint32_t)status);emit(object+0x722,3);emit(object+0x72e,9);
            for (i=0;i<4;i++) for (j=0;j<3;j++) {uint32_t v=oracle_get(ram+0x4000+i*8+j*2,2);oracle_put(v>32767 ? v|0xffff0000u : v);}
            oracle_put(oracle_get(ram+0x300c,2));
        } else return 2;
    }
    return ferror(stdin) ? 3 : 0;
}
