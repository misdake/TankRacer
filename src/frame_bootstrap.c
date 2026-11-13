#include "frame_bootstrap.h"
#include "physics_memory.h"
#include "frame_projectile.h"
#include "frame_file.h"
/* A compact declarative class catalogue. These are class/schema operations,
 * never CPU instructions, captured scene values or callbacks to original code. */
struct boot_name { uint32_t address,length;const char *name; };
struct boot_value { uint32_t kind,value,offset; };
struct boot_step { uint32_t kind,result,count;struct boot_value arg[4]; };
#include "frame_class_catalog.h"
#include "frame_parameter_names.h"
struct vehicle_default { uint32_t address,value; };
#include "frame_vehicle_defaults.h"
int frame_bootstrap_level_shapes(frame_data_read read,frame_data_write write,void *ctx,uint32_t entry,const frame_entity_factory_services *e,const frame_shape_resources *r)
{
    physics_memory m={read,write,ctx,0};uint32_t map,shape=0,space,n,destination;const char *name;int status;
    if (!read || !write || !e || !r) return -1;
    map=pm_load(&m,0x52ed90u,4);space=pm_load(&m,0x55ae40u,4);
    if (entry==0x40ab60u) { name=map==4u ? "handsign.sm" : "chevron.sm";destination=map==4u ? 0x53d144u : 0x53d140u; }
    else if (entry==0x413a40u) { if (map!=2u) { pm_store(&m,0x53e8b4u,0,1);pm_store(&m,0x53e8b0u,0,1);return m.error; }name="light.sm";destination=0x53e8acu; }
    else if (entry==0x404c10u) { if (map!=4u) return m.error;name="umbrella.sm";destination=0x53cc28u; }
    else return -5;
    if (m.error) return m.error;
    for (n=0;name[n];n++) { }
    status=frame_shape_load_name(read,write,ctx,(const unsigned char *)name,n+1u,space,e,r,&shape);if (status) return status;
    pm_store(&m,destination,shape,4);if (entry==0x413a40u) { if (!shape) return -4;pm_store(&m,shape+14u,19200,2);pm_store(&m,0x53e8b4u,1,1); }return m.error;
}
static int boot_material(physics_memory *m,frame_object_material_name material,void *ctx,const char *name,uint32_t mode,uint32_t destination)
{
    uint32_t n,value;int status;if (m->error) return m->error;if (!material) return -5;
    for (n=0;name[n];n++) { }
    status=material(ctx,(const unsigned char *)name,n+1u,mode,&value);if (status) return status;pm_store(m,destination,value,4);return m->error;
}
int frame_bootstrap_environment(frame_data_read read,frame_data_write write,void *ctx,uint32_t entry,frame_object_material_name material,void *material_ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t i,j,k,at,value,flags,swamp=1;int status;
    static const char *names[]={"glow","smoke2","filth","crap","splash2","crater","xhair","damage1","damage2"};
    static const uint32_t addresses[]={0x5466a0,0x5466a4,0x5466a8,0x5466ac,0x5466b0,0x5466b4,0x5466c0,0x5466b8,0x5466bc};
    if (!read || !write) return -1;
#define BTRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
#define S(a,v,n) pm_store(&m,(a),(v),(n))
    flags=pm_load(&m,0x5964d4u,4);
    if (entry==0x40d34bu) return boot_material(&m,material,material_ctx,"blob",1,0x54b498u);
    if (entry==0x40d313u) return boot_material(&m,material,material_ctx,"cloudy",1,0x54b490u);
    if (entry==0x42c550u) { BTRY(boot_material(&m,material,material_ctx,"crater",1,0x545ea0u));S(0x58f3c0,0,4);S(0x58f3f8,0,4);S(0x58f3c4,0,4);return m.error; }
    if (entry==0x4427c0u) { BTRY(boot_material(&m,material,material_ctx,"reflex",1,0x5468a8u));return m.error; }
    if (entry!=0x438d60u) return -5;
    for (i=0;i<9u;i++) { const char *name=names[i];if (!(flags&0x8000u)) { if (!i) name="pcglow";if (i==1u) name="pcsmoke";if (i==4u) name="splash3"; }BTRY(boot_material(&m,material,material_ctx,name,1,addresses[i])); }
    value=pm_load(&m,0x5466a8u,4);S(0x5321c3,value,4);S(0x5321f1,value,4);S(0x53221f,value,4);S(0x532236,value,4);S(0x5321da,pm_load(&m,0x5466acu,4),4);S(0x532208,pm_load(&m,0x5466b0u,4),4);
    if (flags&0x8000u) { for (i=0;i<3u;i++) S(0x5321f5u+i,30,1);S(0x5321fa,1,2); }
    else { S(0x5321fa,3,2);for (i=0;i<3u;i++) S(0x5321f5u+i,64,1); }
    for (i=0;i<6u;i++) { value=pm_load(&m,0x590540u+i,1);if (value>=65u && value<=90u) value+=32u;if (value!=(uint32_t)(unsigned char)"swamp"[i]) swamp=0; }
    S(0x5321d0,swamp ? 650u : 0u,2);S(0x5321d6,swamp ? 80u : 0u,4);
    value=pm_load(&m,0x5466a0u,4);S(0x546670,value,4);S(0x546430,value,4);S(0x54668c,2000,4);S(0x546688,2000,4);
    S(0x546690,0,1);S(0x546691,0,1);S(0x546692,63,1);S(0x546693,63,1);S(0x546684,0,4);S(0x546680,1,4);S(0x546698,0,2);for (i=0;i<3u;i++) S(0x546694u+i,30,1);
    S(0x546450,0,1);S(0x546451,0,1);S(0x546452,63,1);S(0x546453,63,1);S(0x546444,0,4);S(0x546440,1,4);S(0x54644c,100,4);S(0x546448,100,4);
    S(0x546348,0,4);S(0x5462c0,0,4);S(0x546238,0,4);S(0x5461b0,0,4);
    BTRY(boot_material(&m,material,material_ctx,"Flare3",1,0x5463d0u));S(0x5463f6,40,1);S(0x5463f5,40,1);S(0x5463f0,0,1);S(0x5463f1,0,1);S(0x5463f2,63,1);S(0x5463f3,63,1);S(0x5463e4,0,4);S(0x5463e0,1,4);
    BTRY(boot_material(&m,material,material_ctx,"Flare3",1,0x546400u));S(0x546410,1,4);S(0x546420,0,1);S(0x546421,0,1);S(0x546422,63,1);S(0x546423,63,1);S(0x546414,0,4);S(0x546424,50,1);S(0x546425,127,1);S(0x546428,0,2);S(0x546418,500,4);S(0x54641c,500,4);S(0x54666c,0,4);S(0x546564,0,4);
    /* Four39-byte particles per160-byte strip. Preserve partial stores and
     * seed progression even with a renderer that never draws these effects. */
    for (i=0;i<9u;i++) {
        at=0x58ee24u+i*160u;S(at+152u,0,4);
        for (j=0;j<4u;j++,at+=39u) {
            for (k=0;k<5u;k++) { value=pm_load(&m,0x55b6acu,4)*1664525u+1013904223u;S(0x55b6ac,value,4);value=(value>>4)&(k<2u ? 16383u : 65535u);if (k>=3u) value-=32768u;S(at-4u+k*4u,value,4); }
            S(at+16u,200u+i*100u,4);S(at+20u,0,1);S(at+21u,0,2);
        }
    }
    return m.error;
#undef S
#undef BTRY
}
int frame_bootstrap_transients(frame_data_read read,frame_data_write write,void *ctx,uint32_t entry,frame_object_material_name material,void *material_ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t i,j,at,value,flags;const char *glow;int status;
    static const struct { uint32_t at,value,size; } atlas[]={
        {0x5450e4,2143,4},{0x545138,2143,4},{0x5450d0,7280,2},{0x5450d2,10920,2},{0x5450d4,0xfffff8b9,4},{0x5450d8,0xffffec59,4},{0x5450dc,10192,2},{0x5450de,11648,2},{0x5450e0,3540,4},
        {0x5450e8,11284,2},{0x5450ea,16380,2},{0x5450ec,7547,4},{0x5450f0,3727,4},{0x5450f4,27300,2},{0x5450f6,30940,2},{0x5450f8,0xfffff088,4},{0x5450fc,0xffffe3cb,4},
        {0x545100,29848,2},{0x545102,32760,2},{0x545104,0xfffff82d,4},{0x545108,0xffffed42,4},{0x54510c,32760,2},{0x54510e,38220,2},{0x545110,7920,4},{0x545114,3261,4},
        {0x545118,44590,2},{0x54511a,47502,2},{0x54511c,0xfffff114,4},{0x545120,0xffffe912,4},{0x545124,47320,2},{0x545126,50232,2},{0x545128,0xfffff4b8,4},{0x54512c,0xffffecb6,4},
        {0x545130,51506,2},{0x545132,54054,2},{0x545134,4938,4},{0x54513c,53326,2},{0x54513e,60606,2},{0x545140,10017,4},{0x545144,4426,4}};
    if (!read || !write) return -1;
#define BTRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
#define S(a,v,n) pm_store(&m,(a),(v),(n))
    flags=pm_load(&m,0x5964d4u,4);glow=(flags&0x8000u) ? "glow" : "pcglow";
    if (entry==0x42ac70u) { for (i=0;i<4u;i++) S(0x58f588u+i*404u,0,4);return m.error; }
    if (entry==0x426e30u) {
        for (i=0;i<2u;i++) { at=0x58faa3u+i*44u;S(at-3u,0,1);S(at-1u,0,1);S(at,0,1); }
        for (i=0;i<2u;i++) { for (j=0;j<10u;j++) { at=0x545190u+(i*10u+j)*84u;S(at-4u,0,4);S(at,1,1);S(at-56u,0xfeb4024du,4); }for (j=0;j<10u;j++) { at=0x545814u+(i*10u+j)*68u;S(at-4u,0,4);S(at,1,1);S(at+12u,0xfeb4024du,4); } }
        for (i=0;i<sizeof(atlas)/sizeof(atlas[0]);i++) S(atlas[i].at,atlas[i].value,atlas[i].size);
        BTRY(boot_material(&m,material,material_ctx,"flare3",1,0x545d30u));BTRY(boot_material(&m,material,material_ctx,glow,1,0x545d34u));return m.error;
    }
    if (entry==0x407830u) { BTRY(boot_material(&m,material,material_ctx,glow,1,0x53cce8u));S(0x53cd00,1024,4);S(0x53cd04,1024,4);S(0x53ccf8,1,4);S(0x53cd09,0,1);S(0x53cd08,0,1);S(0x53cd0b,63,1);S(0x53cd0a,63,1);S(0x53cd10,0,2);S(0x53ccfc,0xffffffe2,4);return m.error; }
    if (entry==0x415f60u) { BTRY(boot_material(&m,material,material_ctx,glow,0,0x53e9c0u));S(0x53e9e1,0,1);S(0x53e9e0,0,1);S(0x53e9e3,63,1);S(0x53e9e2,63,1);S(0x53e9e6,0,1);S(0x53e9e8,0,2);S(0x53e9e4,127,1);S(0x53e9e5,127,1);S(0x53e9dc,500,4);S(0x53e9d8,500,4);S(0x53e9d0,1,4);S(0x53e9d4,0xfffffffb,4);return m.error; }
    if (entry!=0x430720u) return -5;
    BTRY(boot_material(&m,material,material_ctx,glow,1,0x545fb0u));S(0x545fc0,1,4);S(0x545fc4,0xffffffec,4);S(0x545fcc,500,4);S(0x545fc8,500,4);S(0x545fd1,0,1);S(0x545fd0,0,1);S(0x545fd3,63,1);S(0x545fd2,63,1);S(0x545fd8,0,2);
    /* The second material result is published after its style fields. */
    if (!material) return -5;
    for (i=0;glow[i];i++) { }
    BTRY(material(material_ctx,(const unsigned char *)glow,i+1u,1,&value));S(0x545f9c,500,4);S(0x545f98,500,4);S(0x545fa1,0,1);S(0x545fa0,0,1);S(0x545fa8,0,2);S(0x545f80,value,4);S(0x545f90,1,4);S(0x545f94,0xffffffec,4);S(0x545fa3,63,1);S(0x545fa2,63,1);S(0x545fa4,127,1);S(0x545fa5,60,1);S(0x545fa6,35,1);return m.error;
#undef S
#undef BTRY
}
int frame_bootstrap_background_span(frame_data_read read,frame_data_write write,void *ctx,const unsigned char *bytes,uint32_t length,uint32_t limit,const frame_object_parameters_services *s)
{
    physics_memory m={read,write,ctx,0};frame_file file={bytes,length,0,limit};uint32_t i,j,index,more,used=0,value,at,material,uv[8],rgb[3];unsigned char names[2][32];int status;
    if (!read || !write || (!bytes && length) || !limit || !s) return -1;
#define BACK_TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
    for (i=0;i<10u;i++) { pm_store(&m,0x5367b4u+i*32u,0,4);pm_store(&m,0x5367b0u+i*32u,0,4); }
    if (!bytes) return m.error;
    for (;;) {
        if (used++>=limit) return -3;
        BACK_TRY(frame_file_integer(&file,&index,&more));if (!more) break;
        for (i=0;i<2u;i++) { BACK_TRY(frame_file_token(&file,names[i],32,&more));if (!more) return -4; }
        for (i=0;i<8u;i++) { BACK_TRY(frame_file_integer(&file,&uv[i],&more));if (!more) return -4; }
        at=0x5367b0u+index*32u;for (i=0;i<8u;i++) pm_store(&m,at+8u+i,uv[i],1);
        pm_store(&m,at,0,4);pm_store(&m,at+4u,0,4);
        for (i=0;i<2u;i++) {
            value=(names[i][0]=='n' || names[i][0]=='N') && (names[i][1]=='i' || names[i][1]=='I') && (names[i][2]=='l' || names[i][2]=='L') && !names[i][3];if (value) continue;
            if (!s->material) return -5;
            for (j=0;j<32u && names[i][j];j++) { }
            if (j==32u) return -4;
            BACK_TRY(s->material(s->material_ctx,names[i],j+1u,2,&material));pm_store(&m,at+i*4u,material,4);
        }
        for (j=0;j<4u;j++) { for (i=0;i<3u;i++) { BACK_TRY(frame_file_integer(&file,&rgb[i],&more));if (!more) return -4; }for (i=0;i<3u;i++) pm_store(&m,at+16u+j*4u+i,rgb[i],1); }
    }
    return m.error;
#undef BACK_TRY
}
int frame_bootstrap_level_select(frame_data_read read,frame_data_write write,void *ctx,const unsigned char *name,uint32_t bytes)
{
    physics_memory m={read,write,ctx,0};uint32_t i,j,a,b,index=UINT32_MAX,mode,cup;
    static const char *names[]={"england","mexico","swamp","east","theme","bio","monaco","circuit","bm1","bm2","bm3","bm4","vs1","vs2","vs3","vs4","bm5","bm6","bm7","bm8"};
    static const unsigned char cups[3][6]={{0,1,2,4,255,255},{3,2,6,1,5,255},{6,3,4,0,5,7}};
    if (!read || !write || !name || !bytes || name[bytes-1u]) return -1;
    pm_store(&m,0x52ed90u,UINT32_MAX,4);
    for (i=0;i<20u;i++) for (j=0;j<bytes;j++) { a=name[j];b=(unsigned char)names[i][j];if (a>=128u) return -5;if (a>=65u && a<=90u) a+=32u;if (a!=b) break;if (!a) { index=i;pm_store(&m,0x52ed90u,index,4);break; } }
    pm_store(&m,0x52ed94u,UINT32_MAX,4);mode=pm_load(&m,0x52ee9cu,4);
    if (mode<=1u) { cup=pm_load(&m,0x53d18cu,4);if (cup>=3u) return -4;for (i=0;i<6u;i++) if ((uint32_t)cups[cup][i]==index) pm_store(&m,0x52ed94u,i,4); }
    return m.error;
}
int frame_bootstrap_vehicle_constants(frame_data_write write,void *ctx)
{
    physics_memory m={0,write,ctx,0};uint32_t i;if (!write) return -1;
    for (i=0;i<(uint32_t)(sizeof(vehicle_defaults)/sizeof(vehicle_defaults[0]));i++) pm_store(&m,vehicle_defaults[i].address,vehicle_defaults[i].value,4);
    return m.error;
}
static uint32_t resolve(physics_memory *m,const struct boot_value *v,const uint32_t *results)
{
    if (v->kind==0u) return v->value+v->offset;
    if (v->kind==1u && v->value<BOOT_RESULTS) return results[v->value]+v->offset;
    if (v->kind==2u) return pm_load(m,v->value,4)+v->offset;
    m->error=-5;return 0;
}
int frame_bootstrap_static_data(frame_data_write write,void *ctx)
{
    physics_memory m={0,write,ctx,0};uint32_t i;
    if (!write) return -1;
    for (i=0;i<(uint32_t)(sizeof(boot_names)/sizeof(boot_names[0]));i++)
        if (!write(ctx,boot_names[i].address,boot_names[i].name,boot_names[i].length)) return -1;
    for (i=0;i<(uint32_t)(sizeof(parameter_names)/sizeof(parameter_names[0]));i++)
        if (!write(ctx,parameter_names[i].address,parameter_names[i].name,parameter_names[i].length)) return -1;
    pm_store(&m,0x534cfcu,0x5b33a0u,4);pm_store(&m,0x5b3388u,0,4);pm_store(&m,0x5b338cu,0,4);
    pm_store(&m,0x536cb8u,0,4);pm_store(&m,0x536cc4u,1,4);return m.error;
}
int frame_bootstrap_classes(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *e,uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t results[BOOT_RESULTS]={0},i,j,used=0,arg[4],value;int status;
    if (!read || !write || !e || !e->work_limit || !out) return -1;
    status=frame_symbols_initialize(read,write,ctx,e);if (status) return status;
    status=frame_interfaces_initialize(read,write,ctx,e);if (status) return status;
    for (i=0;i<(uint32_t)(sizeof(boot_steps)/sizeof(boot_steps[0]));i++) {
        const struct boot_step *q=&boot_steps[i];if (used++>=e->work_limit) return -3;
        for (j=0;j<4u;j++) arg[j]=resolve(&m,&q->arg[j],results);
        if (m.error) return m.error;
        if (q->kind==1u) {
            status=frame_class_create(read,write,ctx,arg[0],arg[1],arg[2],arg[3],e,&value);if (status) return status;
            if (q->result>=BOOT_RESULTS) return -5;
            results[q->result]=value;
        } else if (q->kind==2u) {
            status=frame_class_bind(read,write,ctx,arg[0],arg[1],arg[2],arg[3],e);if (status) return status;
        } else if (q->kind==3u) {
            status=frame_list_create(read,write,ctx,e->allocate,e->release,e->system_ctx,e->work_limit,&value);if (status) return status;
            if (q->result>=BOOT_RESULTS) return -5;
            results[q->result]=value;
        } else if (q->kind==4u) {
            if (q->count!=1u && q->count!=2u && q->count!=4u) return -5;
            pm_store(&m,arg[0],arg[1],q->count);
        } else if (q->kind==5u) {
            for (j=0;j<q->count;j++) { if (used++>=e->work_limit) return -3;pm_store(&m,arg[0]+j*4u,0,4); }
        } else return -5;
        if (m.error) return m.error;
    }
    *out=BOOT_CLASSES;return 0;
}
int frame_bootstrap_world_constants(frame_data_write write,void *ctx)
{
    static const uint32_t identity[12]={4096,0,0,0,4096,0,0,0,4096,0,0,0};
    static const uint32_t smoke[10]={6,8,3,5,9,1,4,2,0,7};
    physics_memory m={0,write,ctx,0};uint32_t i;
    if (!write) return -1;
    for (i=0;i<12u;i++) pm_store(&m,0x532888u+i*4u,identity[i],4);
    for (i=0;i<10u;i++) pm_store(&m,0x531828u+i*4u,smoke[i],4);
    if (!write(ctx,0x53274cu,":sortpos",9) || !write(ctx,0x52e318u,":solidity",10) || !write(ctx,0x52e30cu,":Solidity",10)) return -1;
    pm_store(&m,0x5366dcu,153600,4);return m.error;
}
int frame_bootstrap_c_locale(frame_data_write write,void *ctx,uint32_t table)
{
    physics_memory m={0,write,ctx,0};uint32_t i,value;
    if (!write) return -1;
    for (i=0;i<256u;i++) { value=i>=48u && i<=57u ? 4u : 0u;if (i==32u || (i>=9u && i<=13u)) value|=8u;pm_store(&m,table+i*2u,value,2); }
    pm_store(&m,0x538838u,table,4);pm_store(&m,0x538a44u,0,4);return m.error;
}
int frame_bootstrap_ingame_shapes(frame_data_read read,frame_data_write write,void *ctx,const frame_entity_factory_services *e,const frame_object_parameters_services *s)
{
    physics_memory m={read,write,ctx,0};uint32_t i,shape,space,p,end,lowest,second,value,handle;int status;
    static const struct { const char *name;uint32_t length,address; } items[]={
        {"Cpara.sm",9,0x5450a8},{"Omine.sm",9,0x53e9b0},{"emine.sm",9,0x53e9b4},{"ufo_beam.sm",12,0x546730},{"shield.sm",10,0x58fa50},{"flame4.sm",10,0x58ee0c}};
#define SHAPE_TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
    if (!read || !write || !e || !e->work_limit || !s || !s->shapes || !s->material) return -1;
    space=pm_load(&m,0x55ae48u,4);
    for (i=0;i<6u;i++) {
        SHAPE_TRY(frame_shape_load_name(read,write,ctx,(const unsigned char *)items[i].name,items[i].length,space,e,s->shapes,&shape));pm_store(&m,items[i].address,shape,4);
        if (!i && shape) pm_store(&m,shape+14u,150,2);
        if (i==4u) pm_store(&m,0x545de0u,0,1);
    }
    if (!shape) return m.error ? m.error : -4;
    p=pm_load(&m,shape+4u,4);end=p+pm_load(&m,shape,2)*8u;lowest=p;second=p;
    pm_store(&m,0x58ee00u,p,4);pm_store(&m,0x58ee10u,pm_load(&m,p+4u,2),2);pm_store(&m,0x58ee04u,p,4);pm_store(&m,0x58ee08u,pm_load(&m,p+4u,2),2);
    for (;p<end;p+=8u) {
        if (m.error) return m.error;
        if (i++>=e->work_limit) return -3;
        if (pm_i16(&m,p+4u)<pm_i16(&m,lowest+4u)) {
            pm_store(&m,0x58ee04u,lowest,4);pm_store(&m,0x58ee08u,pm_load(&m,lowest+4u,2),2);pm_store(&m,0x58ee00u,p,4);pm_store(&m,0x58ee10u,pm_load(&m,p+4u,2),2);second=lowest;lowest=p;
        } else if (pm_i16(&m,p+4u)<pm_i16(&m,second+4u)) { second=p;pm_store(&m,0x58ee04u,p,4);pm_store(&m,0x58ee08u,pm_load(&m,p+4u,2),2); }
    }
    pm_store(&m,shape+12u,256,2);pm_store(&m,0x546700u,1,4);value=pm_load(&m,0x5964d4u,4)&0x8000u;
    SHAPE_TRY(s->material(s->material_ctx,(const unsigned char *)(value ? "glow" : "pcglow"),value ? 5u : 7u,1,&handle));pm_store(&m,0x5466f0u,handle,4);
    pm_store(&m,0x546712u,63,1);pm_store(&m,0x546713u,63,1);pm_store(&m,0x546710u,0,1);pm_store(&m,0x546711u,0,1);pm_store(&m,0x546714u,64,1);pm_store(&m,0x546715u,64,1);pm_store(&m,0x546716u,100,1);pm_store(&m,0x546718u,0,2);
    SHAPE_TRY(frame_projectile_model(read,write,ctx,e,s->shapes));return m.error;
#undef SHAPE_TRY
}
