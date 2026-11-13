#include "frame_world.h"
#include "frame_world_text.h"
#include "frame_classes.h"
#include "frame_file.h"
#include "physics_memory.h"
#include <string.h>

static uint32_t integer_position(double value)
{
    uint64_t bits,fraction,whole;uint32_t exponent,shift;int sign;
    /* binary64 *1024 is exact; reconstruct x87 signed64 truncation and low32,
     * including its indefinite result on overflow, without host cast UB. */
    memcpy(&bits,&value,8);sign=(int)(bits>>63);exponent=(uint32_t)((bits>>52)&2047u);fraction=(bits&UINT64_C(0xfffffffffffff))|UINT64_C(0x10000000000000);
    if (!exponent || exponent<1013u) return 0;
    if (exponent>=1076u) return 0;
    if (exponent>=1065u) { shift=exponent-1065u;whole=fraction<<shift; }
    else whole=fraction>>(1065u-exponent);
    return sign ? 0u-(uint32_t)whole : (uint32_t)whole;
}
int frame_world_object_read(frame_data_read read,frame_data_write write,void *ctx,uint32_t entity,uint32_t legacy_record,
                                const frame_entity_factory_services *e,const frame_object_parameters_services *resources,
                                uint32_t *out)
{
    physics_memory m={read,write,ctx,0};uint32_t descriptor,unused,r,o,i,position[3],angles[3],shape,group,at,equal=1,c;double value;int status;
    unsigned char name[16];
    if (!read || !write || !out || !e || !e->work_limit) return -1;
#define TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
    descriptor=pm_load(&m,0x545f0cu,4);TRY(frame_entity_dispatch(read,write,ctx,entity,descriptor,0,0,e->method,e->method_ctx,&unused));
    r=pm_load(&m,pm_load(&m,0x55c694u,4)+entity*8u,4);o=r ? pm_load(&m,r+4u,4) : 0;
    (void)legacy_record;
    if (pm_i32(&m,0x536d64u)<5) return m.error ? m.error : -5;
    for (i=0;i<3u;i++) { TRY(frame_world_decimal(read,write,ctx,e->work_limit,&value));position[i]=integer_position(value); }
    for (i=0;i<3u;i++) { TRY(frame_world_integer(read,write,ctx,e->work_limit,&angles[i]));angles[i]*=182u; }
    TRY(frame_world_token(read,write,ctx,name,16,e->work_limit));
    for (i=0;i<16u;i++) {
        c=pm_load(&m,0x534d00u+i,1);if (c>=65u && c<=90u) c+=32u;
        at=name[i];if (at>=65u && at<=90u) at+=32u;
        if (c>=128u || at>=128u) return -5;
        if (c!=at) { equal=0;break; }if (!c) break;
    }
    if (!equal) {
        at=pm_load(&m,0x55b680u,4);
        TRY(frame_shape_load_name(read,write,ctx,name,16,at,e,resources ? resources->shapes : 0,&shape));pm_store(&m,o+72u,shape,4);
    }
    pm_store(&m,0x55b680u,0,4);for (i=0;i<3u;i++) pm_store(&m,o+i*4u,position[i],4);
    pm_store(&m,o+20u,o+24u,4);TRY(frame_matrix_initialize(read,write,ctx,o+24u,angles,resources ? resources->trig : 0));
    pm_store(&m,0x55b280u,0,1);pm_store(&m,0x55b27cu,0,4);pm_store(&m,0x55b278u,0,4);
    TRY(frame_world_group(read,write,ctx,0x55b280u,1024,e->work_limit,&group));pm_store(&m,0x55b278u,group,4);
    if (m.error) return m.error;
    if (group==UINT32_MAX) return -4;
    if (pm_load(&m,0x55b684u,4)) for (i=0;i<3u;i++) {
        if (pm_signed(position[i])<pm_i32(&m,0x55b688u+i*4u) || pm_signed(position[i])>pm_i32(&m,0x55b698u+i*4u)) { if (m.error) return m.error;*out=0;return 0; }
    }
    descriptor=pm_load(&m,0x545eccu,4);TRY(frame_entity_dispatch(read,write,ctx,entity,descriptor,1,0x55b278u,e->method,e->method_ctx,&unused));
    descriptor=pm_load(&m,0x545f10u,4);TRY(frame_entity_dispatch(read,write,ctx,entity,descriptor,0,0,e->method,e->method_ctx,&unused));
    if (m.error) return m.error;
    *out=entity;return 0;
#undef TRY
}
typedef struct world_span { frame_file file;frame_data_write write;void *data_ctx; } world_span;
static int read_span(void *ctx,uint32_t file,uint32_t dest,uint32_t requested,uint32_t *received)
{
    world_span *s=(world_span *)ctx;uint32_t n;(void)file;
    n=s->file.cursor<s->file.length ? s->file.length-s->file.cursor : 0;if (n>requested) n=requested;
    if (n && !s->write(s->data_ctx,dest,s->file.bytes+s->file.cursor,n)) return -1;
    s->file.cursor+=n;*received=n;return 0;
}
int frame_world_load_span(frame_data_read read,frame_data_write write,void *ctx,uint32_t file,uint32_t list,
                              const unsigned char *bytes,uint32_t length,const frame_entity_factory_services *e,
                              uint32_t *out,uint32_t *consumed)
{
    world_span s;physics_memory m={read,write,ctx,0};unsigned char magic[80];uint32_t present,version;int status;
    frame_world_loader_services io={read_span,&s,0,0};
    if (!read || !write || (!bytes && length) || !e || !e->work_limit || !out || !consumed) return -1;
    s.file.bytes=bytes;s.file.length=length;s.file.cursor=0;s.file.work_limit=e->work_limit;s.write=write;s.data_ctx=ctx;
    status=frame_file_token(&s.file,magic,80,&present);if (status) return status;
    if (!present) { *out=0;*consumed=s.file.cursor;return 0; }
    status=frame_file_integer(&s.file,&version,&present);if (status) return status;pm_store(&m,0x536d64u,version,4);if (m.error) return m.error;
    if (!present || magic[0]!='F' || magic[1]!='S' || magic[2]!='T' || magic[3]!='P' || magic[4]!='C' || magic[5] || pm_signed(version)<1 || pm_signed(version)>5) { *out=0;*consumed=s.file.cursor;return 0; }
    status=frame_world_load_objects(read,write,ctx,file,list,e,&io,out);if (status) return status;
    *consumed=s.file.cursor;return 0;
}
int frame_world_load_objects(frame_data_read read,frame_data_write write,void *ctx,uint32_t file,uint32_t list,
                                 const frame_entity_factory_services *e,const frame_world_loader_services *files,uint32_t *out)
{
    static const unsigned char paths[]="Paths",unknown[]="Unknown";
    physics_memory m={read,write,ctx,0};unsigned char klass_name[32],object_name[64];
    uint32_t buffer,received,klass,symbol,entity,record,object,descriptor,result,unused,i,used=0,fallback;int status;
    if (!read || !write || !e || !e->work_limit || !files || !out) return -1;
#define TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
    if (pm_i32(&m,0x536d64u)<5) return m.error ? m.error : -5;
    buffer=pm_load(&m,0x5367a8u,4);pm_store(&m,0x55c6a0u,buffer,4);if (m.error) return m.error;
    if (!files->read) return -5;
    TRY(files->read(files->read_ctx,file,buffer,40000,&received));
    pm_store(&m,0x55c6a4u,0,4);
    for (;;) {
        if (used++>=e->work_limit) return -3;
        TRY(frame_world_token(read,write,ctx,klass_name,32,e->work_limit));
        for (i=0;i<sizeof(paths) && klass_name[i]==paths[i];i++) { }
        if (i==sizeof(paths)) { *out=1;return 0; }
        fallback=0;
        TRY(frame_class_lookup_name(read,write,ctx,klass_name,32,e,&klass));
        if (!klass) {
            TRY(frame_class_lookup_name(read,write,ctx,unknown,sizeof(unknown),e,&klass));
            if (!klass) return -4; /* original diagnostic followed by invalid class use */
            fallback=1;
        }
        TRY(frame_world_token(read,write,ctx,object_name,64,e->work_limit));
        TRY(frame_symbol_intern_name(read,write,ctx,object_name,64,e,&symbol));
        if (symbol==pm_load(&m,0x5b35acu,4) || pm_load(&m,symbol,4)!=128u) {
            TRY(frame_entity_create(read,write,ctx,klass,e,&entity));
            if (!entity) return -4;
        } else {
            entity=pm_load(&m,symbol+4u,4);TRY(frame_entity_fill_reserved(read,write,ctx,entity,klass,e));
        }
        if (symbol!=pm_load(&m,0x5b35acu,4)) { pm_store(&m,symbol,128,4);pm_store(&m,symbol+4u,entity,4); }
        record=pm_load(&m,pm_load(&m,0x55c694u,4)+entity*8u,4);object=record ? pm_load(&m,record+4u,4) : 0;
        if (m.error) return m.error;
        if (!object) return -4;
        pm_store(&m,object+12u,symbol,4);descriptor=pm_load(&m,0x545f28u,4);
        TRY(frame_entity_dispatch(read,write,ctx,entity,descriptor,1,file,e->method,e->method_ctx,&result));
        if (result) {
            if (fallback) {
                descriptor=pm_load(&m,0x545f40u,4);if (m.error) return m.error;
                if (!files->name) return -5;
                TRY(files->name(files->name_ctx,result,descriptor,klass_name,32));
            }
            if (list) TRY(frame_list_append(read,write,ctx,result,list,e->allocate,e->release,e->system_ctx,e->work_limit,&unused));
        }
    }
#undef TRY
}
