#include "physics_drive.h"
#include "physics_velocity.h"
#include "physics_vector.h"
#include "frame_tank_config.h"
#include "frame_surface_config.h"
#include "reference_io.h"
struct vehicle_default {uint32_t address,value;};
#include "frame_vehicle_defaults.h"
static uint32_t default_value(uint32_t address)
{
    uint32_t value=0;unsigned i,found=0;
    for (i=0;i<sizeof(vehicle_defaults)/sizeof(vehicle_defaults[0]);i++) {
        if (vehicle_defaults[i].address==address) {value=vehicle_defaults[i].value;found=1;}
    }
    if (!found) exit(6);
    return value;
}
static unsigned char ram[0x600000];
static const uint32_t object=0x10000;
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if (at>sizeof(ram)||n>sizeof(ram)-at) return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *data,uint32_t n)
{(void)ctx;if (at>sizeof(ram)||n>sizeof(ram)-at) return 0;memcpy(ram+at,data,n);return 1;}
static unsigned char *file_bytes(const char *name,uint32_t *size)
{
    FILE *file=fopen(name,"rb");long n;unsigned char *bytes;if (!file) return 0;
    if (fseek(file,0,SEEK_END)||(n=ftell(file))<0||fseek(file,0,SEEK_SET)) {fclose(file);return 0;}
    bytes=(unsigned char *)malloc((size_t)n);if (!bytes) {fclose(file);return 0;}
    if (fread(bytes,1,(size_t)n,file)!=(size_t)n) {free(bytes);fclose(file);return 0;}
    fclose(file);*size=(uint32_t)n;return bytes;
}
static void store(uint32_t at,unsigned n) {oracle_set(ram+at,oracle_require(),n);}
static int32_t sign_width(uint32_t value,unsigned n)
{
    if (n==1) return value>127 ? (int32_t)value-256 : (int32_t)value;
    return oracle_signed(value);
}
int main(int argc,char **argv)
{
    unsigned char *tank,*surface;uint32_t tank_size,surface_size,map,consumed=0,i;int status,powertrain=argc==4&&!strcmp(argv[3],"--powertrain");
    static const uint32_t state_offsets[]={0xe4,0xe8,0xec,0xb4,0xb8,0xbc,0xa4,0xac,0xb0,0xa0,0xdeb,0xdde,0xc0,0xc4,0xcc,0xf4};
    if (argc!=3&&!powertrain) return 2;
    tank=file_bytes(argv[1],&tank_size);surface=file_bytes(argv[2],&surface_size);
    if (!tank||!surface) return 3;
    status=frame_surface_configure_span(read_data,write_data,0,surface,surface_size,1000000,&consumed);free(surface);if (status) return 4;
    oracle_set(ram+0x55c694,0x2000,4);oracle_set(ram+0x2008,0x2100,4);oracle_set(ram+0x2100,0x1234,4);oracle_set(ram+0x544f98,0x1234,4);
    oracle_binary();fwrite(powertrain?"PW01":"DR01",1,4,stdout);
    for (i=0;i<5;i++) oracle_put(default_value(0x531b80+i*4));
    for (i=0;i<5;i++) oracle_put(default_value(0x531b68+i*4));
    for (i=0;i<5;i++) oracle_put(default_value(0x5319e0+i*4));
    oracle_put(default_value(0x5319d4));oracle_put(default_value(0x5319d8));oracle_put(default_value(0x5319dc));
    while (oracle_word(&map)) {
        uint32_t special=oracle_require(),circuit=oracle_require();
        memset(ram+object,0,0x1200);oracle_set(ram+0x52ed90,map,4);oracle_set(ram+0x53e414,special,4);oracle_set(ram+0x5319c4,circuit,4);
        store(0x5904fc,4);store(0x52ee9c,4);store(object+0xde5,1);store(object+0xdea,1);store(object+0x1107,1);
        store(object+0x716,4);store(object+0x71a,4);
        for (i=0;i<4;i++) store(object+0x134+i*70,2);
        for (i=0;i<4;i++) store(object+0x12c+i*70,4);
        store(object+0xde1,1);store(object+0xdd6,2);store(object+0xc9e,4);store(object+0xcaa,4);
        store(object+0x94,4);store(object+0x98,4);store(object+0x9c,4);store(object+0x107a,4);
        store(object+0x10bc,1);store(object+0x10b6,2);store(object+0x10b8,2);
        for (i=0;i<16;i++) store(object+state_offsets[i],i==10||i==11 ? 1 : 4);
        for (i=0;i<5;i++) store(0x531b80+i*4,4);
        for (i=0;i<5;i++) store(0x531b68+i*4,4);
        for (i=0;i<5;i++) store(0x5319e0+i*4,4);
        store(0x5319d4,4);store(0x5319d8,4);store(0x5319dc,4);
        status=frame_tank_configure_span(read_data,write_data,0,object,tank,tank_size,1000000,0,&consumed);if (status) return 4;
        if(powertrain){
            int32_t world[3],local[3],matrix[9],out[3];int large;
            for(i=0;i<3;i++)store(object+0x50+i*4,4);
            for(i=0;i<9;i++)store(object+0x18+i*4,4);
            store(object+0xc8,4);store(object+0x10be,4);
            for(i=0;i<3;i++)store(0x531c98+i*4,4);
            if(oracle_signed(oracle_get(ram+0x5904fc,4))<=0)status=-2;
            else{
                for(i=0;i<3;i++)world[i]=oracle_signed(oracle_get(ram+object+0x50+i*4,4));
                for(i=0;i<9;i++)matrix[i]=oracle_signed(oracle_get(ram+object+0x18+i*4,4));
                large=physics_velocity_large(world);physics_velocity_transform(world,matrix,local,0,large);
                for(i=0;i<3;i++)oracle_set(ram+object+0xe4+i*4,(uint32_t)local[i],4);
                status=physics_vehicle_drive(read_data,write_data,0,object,1);
                if(!status&&!ram[object+0x10bc]&&oracle_signed(oracle_get(ram+object+0x10be,4))<1)status=physics_yaw_correction(read_data,write_data,0,object);
                if(!status){
                    for(i=0;i<3;i++)local[i]=oracle_signed(oracle_get(ram+object+0xe4+i*4,4));
                    physics_velocity_transform(local,matrix,out,1,large);
                    for(i=0;i<3;i++)oracle_set(ram+object+0x50+i*4,(uint32_t)out[i]+(i==1?oracle_get(ram+object+0xc8,4):0u),4);
                }
            }
        }else status=physics_vehicle_drive(read_data,write_data,0,object,1);
        oracle_put((uint32_t)status);
        for (i=0;i<16;i++) {
            unsigned n=i==10||i==11 ? 1 : 4;uint32_t value=oracle_get(ram+object+state_offsets[i],n);
            oracle_put(i==10 ? value : (uint32_t)sign_width(value,n));
        }
        if(powertrain)for(i=0;i<3;i++)oracle_put(oracle_get(ram+object+0x50+i*4,4));
    }
    free(tank);return ferror(stdin) ? 5 : 0;
}
