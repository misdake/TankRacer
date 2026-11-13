#include "frame_surface_config.h"
#include "frame_file.h"
#include "physics_memory.h"
static int equal(const unsigned char *a,const char *b)
{
    unsigned i;uint32_t x,y;
    for (i=0;i<32u;i++) { x=a[i];y=(unsigned char)b[i];if (x>=128u || y>=128u) return -5;if (x>=65u && x<=90u) x+=32u;if (y>=65u && y<=90u) y+=32u;if (x!=y) return 0;if (!x) return 1; }
    return 0;
}
int frame_surface_configure_span(frame_data_read read,frame_data_write write,void *ctx,const unsigned char *bytes,uint32_t length,uint32_t limit,uint32_t *consumed)
{
    static const char *const surfaces[]={"none","track","grass","water","mud","gravel","sand","ice","cattle_grid","snow"};
    static const char *const factors[]={"drag_factor:","grip_factor:","slide_factor:","slide_factor_rain_light:","slide_factor_rain_medium:","slide_factor_rain_heavy:","slide_factor_snow_light:","slide_factor_snow_medium:","slide_factor_snow_heavy:","bump_factor:"};
    physics_memory m={read,write,ctx,0};frame_file f={bytes,length,0,limit};unsigned char token[32];uint32_t present,surface,factor,i,j,n,value,used=0;int status,match;
    if (!read || !write || (!bytes && length) || !limit || !consumed) return -1;
#define TRY(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
    for (;;) {
        if (used++>=limit) return -3;
        TRY(frame_file_token(&f,token,32,&present));if (!present) break; /* header token itself is deliberately ignored */
        TRY(frame_file_token(&f,token,32,&present));surface=10;
        for (i=0;i<10u;i++) { match=equal(token,surfaces[i]);if (match<0) return match;if (match) surface=i; }
        if (surface==10u) return -4;
        for (n=0;n<10u;n++) {
            TRY(frame_file_token(&f,token,32,&present));factor=10;
            for (i=0;i<10u;i++) { match=equal(token,factors[i]);if (match<0) return match;if (match) factor=i; }
            if (factor==10u) return -4;
            for (j=0;j<2u;j++) { TRY(frame_file_integer(&f,&value,&present));pm_store(&m,0x546018u+surface*40u+factor*4u+j*2u,value,2); }
        }
    }
    for (i=0;i<10u;i++) pm_store(&m,0x546018u+i*4u,pm_load(&m,0x546040u+i*4u,4),4);
    if (m.error) return m.error;
    *consumed=f.cursor;return 0;
#undef TRY
}
