#include "physics_geometry.h"
#include "reference_io.h"

static unsigned char ram[65536];
static int read_data(void *ctx,uint32_t address,void *out,uint32_t n)
{(void)ctx;if (address>sizeof(ram) || n>sizeof(ram)-address) return 0;memcpy(out,ram+address,n);return 1;}
static int write_data(void *ctx,uint32_t address,const void *bytes,uint32_t n)
{(void)ctx;if (address>sizeof(ram) || n>sizeof(ram)-address) return 0;memcpy(ram+address,bytes,n);return 1;}

int main(void)
{
    uint32_t first,object=0x100,mesh=0x200,vertices=0x300,records=0x2000;
    oracle_binary();fwrite("DF01",1,4,stdout);
    while (oracle_word(&first)) {
        uint32_t i,j,n,count;int32_t world[3];physics_deform_result out={0};int status;
        memset(ram,0x5a,sizeof(ram));
        oracle_set(ram+object,first,4);
        for (i=1;i<3;i++) oracle_set(ram+object+i*4,oracle_require(),4);
        for (i=0;i<9;i++) oracle_set(ram+object+0x18+i*4,oracle_require(),4);
        for (i=0;i<4;i++) oracle_set(ram+object+0xba+i*4,oracle_require(),4);
        for (i=0;i<4;i++) oracle_set(ram+object+(i<2 ? (i ? 0x6c : 0x60) : (i==2 ? 0x68 : 0x74)),oracle_require(),4);
        oracle_set(ram+object+0xb6,oracle_require(),4);
        for (i=0;i<3;i++) world[i]=oracle_signed(oracle_require());
        oracle_set(ram+object+0xcc,oracle_require(),1);
        n=oracle_require();if (n>256) return 2;
        oracle_set(ram+object+0x48,mesh,4);oracle_set(ram+mesh+4,vertices,4);oracle_set(ram+mesh,n,2);
        for (i=0;i<n;i++) for (j=0;j<3;j++) oracle_set(ram+vertices+i*8+j*2,oracle_require(),2);
        count=oracle_require();if (count>64) return 2;
        oracle_set(ram+object+0xcd,count,4);oracle_set(ram+object+0xd5,count ? records : 0,4);
        for (i=0;i<count;i++) {
            for (j=0;j<3;j++) oracle_set(ram+records+i*32+j*4,oracle_require(),4);
            oracle_set(ram+records+i*32+0x18,oracle_require(),4);
        }
        status=physics_deform_query(read_data,write_data,0,object,world,&out);oracle_put((uint32_t)status);
        for (i=0;i<3;i++) oracle_put((uint32_t)out.local_raw[i]);
        for (i=0;i<3;i++) oracle_put((uint32_t)out.local_clamped[i]);
        oracle_put((uint32_t)out.vertex_height);oracle_put((uint32_t)out.edge_weight);
        oracle_put((uint32_t)out.offset);oracle_put(out.activated_records);oracle_put(ram[object+0xcc]);
        for (i=0;i<count;i++) {
            for (j=0;j<3;j++) oracle_put(oracle_get(ram+records+i*32+j*4,4));
            oracle_put(oracle_get(ram+records+i*32+0x18,4));
        }
    }
    return ferror(stdin) ? 5 : 0;
}
