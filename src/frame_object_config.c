#include "frame_object_config.h"
#include "frame_file.h"
#include "physics_memory.h"
#include <string.h>
struct object_config_field { const char *group,*key;uint32_t count,address[36],kind[36]; };
#include "frame_object_config_schema.h"
static int equal(const unsigned char *a,const char *b)
{
    uint32_t i,x,y;
    for (i=0;i<128u;i++) { x=a[i];y=(unsigned char)b[i];if (x>=128u || y>=128u) return 0;if (x>=65u && x<=90u) x+=32u;if (y>=65u && y<=90u) y+=32u;if (x!=y) return 0;if (!x) return 1; }
    return 0;
}
static int space(uint32_t c) { return c==0u || c==32u || (c>=9u && c<=13u); }
static int skip_space(frame_file *f)
{ uint32_t n=0;while (f->cursor<f->length && space(f->bytes[f->cursor])) { if (n++>=f->work_limit) return -3;f->cursor++; }return 0; }
static uint32_t float_scaled(uint32_t bits,int32_t scale)
{
    float value;double product;memcpy(&value,&bits,4);product=(double)value*(double)scale;
    if (product!=product || product>=9223372036854775808.0 || product< -9223372036854775808.0) return 0;
    return (uint32_t)(int64_t)product;
}
static int conversion(const struct object_config_field *q,int32_t value,int32_t *out)
{
    const unsigned char *key=(const unsigned char *)q->key;
    if (equal((const unsigned char *)q->group,"regen_delays")) *out=pm_mul(value,1000);
    else if (equal(key,":squash_angle") || equal(key,":std_view_ang") || equal(key,":std_view_ang_far") || equal(key,":split_screen_view_ang") || equal(key,":split_scrn_view_ang_far") || equal(key,":overhead_view_ang") || equal(key,":scale_yrotspd") || equal(key,":sh_elevation") || equal(key,":conc_elevation") || equal(key,":elev_snap") || equal(key,":shell_miss_error")) *out=pm_mul(value,182);
    else if (equal(key,":x_rotspeed") || equal(key,":y_rotspeed")) *out=pm_mul(value,182)/1000;
    else if (equal(key,":shell_speed") || equal(key,":conc_speed") || (equal((const unsigned char *)q->group,"homer") && equal(key,":speed"))) *out=pm_signed((uint32_t)value<<18)/1000;
    else if (equal(key,":debunch_dist")) *out=pm_signed((uint32_t)value<<10);
    else return 0;
    return 1;
}
int frame_object_configure_span(frame_data_read read,frame_data_write write,void *ctx,const unsigned char *bytes,uint32_t length,const frame_entity_factory_services *e,uint32_t *consumed)
{
    physics_memory m={read,write,ctx,0};frame_file f={bytes,length,0,e ? e->work_limit : 0};unsigned char token[25],group[25],key[25],text[128];
    uint32_t count,base,object,present,root_object,i,j,n,at,value,more,used=0,mask,temp;int32_t converted;int status;const struct object_config_field *q;
    if (!read || !write || (!bytes && length) || !e || !e->work_limit || !consumed) return -1;
#define TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
    /* Original counts only the first word on each physical line, then seeks
     * back. Existing count is deliberately not reset by this loader. */
    while (f.cursor<f.length) {
        if (used++>=e->work_limit) return -3;
        TRY(frame_file_name(&f,token,sizeof(token),1,&present));
        if (present && equal(token,"object")) pm_store(&m,0x544f88u,pm_load(&m,0x544f88u,4)+1u,4);
        while (f.cursor<f.length && f.bytes[f.cursor]!=10u && f.bytes[f.cursor]!=13u) { if (used++>=e->work_limit) return -3;f.cursor++; }
        TRY(skip_space(&f));
    }
    f.cursor=0;count=pm_load(&m,0x544f88u,4);TRY(frame_pool_allocate_zero(read,write,ctx,count*76u,e->allocate,e->release,e->system_ctx,e->work_limit,&base));pm_store(&m,0x544f84u,base,4);object=base;
    while (f.cursor<f.length) {
        if (used++>=e->work_limit) return -3;
        TRY(frame_file_name(&f,token,sizeof(token),1,&present));if (!present) break;
        root_object=equal(token,"object");if (!root_object && !equal(token,"class")) continue;
        TRY(frame_file_name(&f,group,sizeof(group),1,&present));if (!present) return -4;
        if (root_object) {
            if (!base || object<base || (object-base)/76u>=count) return -4;
            n=0;while (n<sizeof(group) && group[n]) n++;n++;
            for (i=0;i+4u<=n;i+=4u) { value=(uint32_t)group[i]|((uint32_t)group[i+1u]<<8)|((uint32_t)group[i+2u]<<16)|((uint32_t)group[i+3u]<<24);pm_store(&m,object+i,value,4); }
            for (;i<n;i++) pm_store(&m,object+i,group[i],1);
        }
        TRY(skip_space(&f));
        if (f.cursor<f.length && f.bytes[f.cursor]=='(') {
            f.cursor++;
            for (;;) {
                if (used++>=e->work_limit) return -3;
                TRY(frame_file_name(&f,key,sizeof(key),0,&present));if (!present) return -4;q=0;
                for (i=0;i<(uint32_t)(sizeof(object_config_fields)/sizeof(object_config_fields[0]));i++) {
                    const struct object_config_field *candidate=&object_config_fields[i];
                    if (equal(root_object ? (const unsigned char *)"object" : group,candidate->group) && equal(key,candidate->key)) q=candidate;
                }
                if (q) {
                    if (root_object && !q->count) {
                        if (equal(key,":bash_sound")) {
                            TRY(frame_file_token(&f,text,sizeof(text),&more));n=0;while (n<9u && text[n]) n++;
                            for (j=0;j<9u;j++) pm_store(&m,object+64u+j,j<n ? text[j] : 0,1);
                        } else {
                            TRY(frame_file_name(&f,text,25,1,&more));if (!more) return -4;
                            mask=equal(key,":snap_to_track") ? 8u : equal(key,":collidable") ? 4u : equal(key,":oscillating") ? 16u : 32u;
                            if (equal(text,"true") || equal(text,"false")) { value=pm_load(&m,object+58u,4);pm_store(&m,object+58u,equal(text,"true") ? value|mask : value&~mask,4); }
                        }
                    } else for (j=0;j<q->count;j++) {
                        if (q->kind[j]==5u) {
                            temp=0;TRY(frame_file_real32(&f,&temp,&more));if (!more) return -4;
                            at=equal(key,":shell_grav") ? 0x5316e8u : equal(key,":conc_grav") ? 0x5316ecu : 0x592e84u;
                            pm_store(&m,at,float_scaled(temp,at==0x592e84u ? 1000 : 256),4);
                        } else {
                            TRY(frame_file_integer(&f,&value,&more));
                            if (root_object && equal(key,":max_deflect")) pm_store(&m,object+56u,value*182u,2);
                            else { at=q->address[j]+(root_object ? object : 0u);pm_store(&m,at,value,q->kind[j]);if (conversion(q,pm_signed(value),&converted)) pm_store(&m,at,(uint32_t)converted,q->kind[j]); }
                        }
                    }
                }
                TRY(skip_space(&f));if (f.cursor>=f.length) return -4;if (f.bytes[f.cursor]==')') { f.cursor++;break; }
            }
        }
        if (root_object) object+=76u;
    }
    if (m.error) return m.error;
    *consumed=f.cursor;return 0;
#undef TRY
}
