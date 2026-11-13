#include "frame_surface.h"
#include <string.h>
static uint32_t word(const unsigned char *b) { return (uint32_t)b[0]|((uint32_t)b[1]<<8); }
static uint32_t dword(const unsigned char *b) { return word(b)|(word(b+2)<<16); }
uint32_t frame_surface_pixel16(const unsigned char p[4],uint32_t flags,uint32_t alpha)
{
    uint32_t v;
    if (((!alpha && (flags&3u)) || (flags&3u)==1u) && !p[3]) return 0;
    if (alpha) return (((flags&3u)==2u ? p[3]>>4 : 15u)<<12)|((uint32_t)(p[2]>>4)<<8)|((uint32_t)(p[1]>>4)<<4)|(p[0]>>4);
    v=((uint32_t)(p[2]>>3)<<11)|((uint32_t)(p[1]>>2)<<5)|(p[0]>>3);
    return (flags&3u) && !v ? 1u : v; /* Keyed opaque black must not become zero. */
}
int frame_surface_upload_rgba(const frame_surface_info *info,uint32_t flags,uint32_t count,const unsigned char *pixels,uint32_t bytes,const unsigned char *palette,uint32_t palette_bytes,unsigned char *rgba,uint32_t capacity,uint32_t *has_alpha)
{
    uint32_t i,v,alpha=(flags&3u)==2u;
    if (!info || !pixels || !rgba || !has_alpha) return -1;
    if (info->kind!=0u && info->kind!=2u) return -5;
    if ((flags&3u)==3u) return -5;
    if ((uint64_t)count*4u>capacity || (uint64_t)count*(info->kind ? 4u : 1u)>bytes || (!info->kind && (!palette || palette_bytes<1024u))) return -4;
    for (i=0;i<count;i++) {
        const unsigned char *p=info->kind ? pixels+i*4u : palette+pixels[i]*4u;
        v=frame_surface_pixel16(p,flags,alpha);
        if (alpha) { rgba[i*4u]=(unsigned char)(((v>>8)&15u)*17u);rgba[i*4u+1u]=(unsigned char)(((v>>4)&15u)*17u);rgba[i*4u+2u]=(unsigned char)((v&15u)*17u);rgba[i*4u+3u]=(unsigned char)((v>>12)*17u); }
        else { rgba[i*4u]=(unsigned char)((((v>>11)&31u)*255u+15u)/31u);rgba[i*4u+1u]=(unsigned char)((((v>>5)&63u)*255u+31u)/63u);rgba[i*4u+2u]=(unsigned char)(((v&31u)*255u+15u)/31u);rgba[i*4u+3u]=(flags&1u) && !v ? 0u : 255u; }
    }
    *has_alpha=alpha;return 0;
}
int frame_surface_open(const unsigned char *b,uint32_t length,uint32_t flags,int32_t override,int32_t preference,frame_surface_info *out)
{
    frame_surface_info info;uint32_t count,i,j,offset=0,found=0;int32_t wanted;unsigned order[4];
    if (!b || !out) return -1;
    if (length<256u) return -4;
    info.version=word(b+8);if (info.version==1u) return -5;
    if (memcmp(b,"SSURFACE",8) || info.version>3u) return 1;
    count=word(b+34);if (count>4u) return 1;
    if (length-256u<count*6u) return -4;
    wanted=override==-1 ? preference : override;
    if (!(flags&4u)) {
        if (wanted!=0 && wanted!=1) return 1;
        if (wanted==1) { order[0]=1;order[1]=0;order[2]=2;order[3]=3; }
        else { order[0]=3;order[1]=2;order[2]=1;order[3]=0; }
    } else {
        if (wanted<0 || wanted>3) return 1;
        if (wanted==0) { order[0]=3;order[1]=2;order[2]=1;order[3]=0; }
        else if (wanted==1) { order[0]=2;order[1]=3;order[2]=1;order[3]=0; }
        else if (wanted==2) { order[0]=1;order[1]=2;order[2]=3;order[3]=0; }
        else { order[0]=0;order[1]=1;order[2]=2;order[3]=3; }
    }
    for (j=0;j<4u && !found;j++) for (i=0;i<count;i++) if (word(b+256u+i*6u)==order[j]) {
        info.format=order[j];offset=dword(b+258u+i*6u);found=1;break;
    }
    if (!found) return 1;
    if (offset>length || length-offset<22u) return -4;
    info.width=word(b+26);info.height=word(b+28);info.mip_levels=word(b+30);info.scale=word(b+32);
    info.flags=dword(b+36);
    info.kind=word(b+offset);info.bits=word(b+offset+2u);info.compression=word(b+offset+20u);info.pixel_offset=offset+22u;
    *out=info;return 0;
}
static uint32_t logarithm(uint32_t x) { uint32_t n=0;while (x>1u) { x>>=1;n++; }return n; }
int frame_surface_prepare(const frame_surface_info *info,uint32_t flags,uint32_t min_width,uint32_t trim,uint32_t requested_skip,uint32_t limit,frame_surface_plan *out)
{
    frame_surface_plan p;uint32_t maximum=0,value,used=0,i;
    if (!info || !out || !limit) return -1;
    if (info->width<min_width) return 1;
    p.flags=(flags&8192u) ? ((info->flags&~3u)|flags) : ((flags&~3u)|info->flags);
    p.width=info->width;p.height=info->height;p.mip_levels=info->mip_levels;p.scale=info->scale;
    if (trim==1u && info->version>=3u && p.mip_levels)
        p.mip_levels=(logarithm(p.width)-logarithm(min_width>4u ? min_width : 4u))&65535u;
    p.skip=p.mip_levels ? requested_skip : 0;
    if (requested_skip) {
        if (info->version<=2u) {
            if (p.mip_levels<p.skip) { p.skip=p.mip_levels;p.mip_levels=0; }
            else p.mip_levels=(p.mip_levels-p.skip)&65535u;
        } else {
            value=p.width>>1;
            while (value>=min_width) { if (used++>=limit) return -3;maximum++;value>>=1; }
            if (maximum<p.skip+p.mip_levels) p.skip=maximum<p.mip_levels ? 0 : maximum-p.mip_levels;
        }
        p.width>>=p.skip&31u;p.height>>=p.skip&31u;p.scale=(p.scale<<(p.skip&31u))&65535u;
    }
    while ((p.width>>(p.mip_levels&31u))<min_width) { if (used++>=limit) return -3;p.mip_levels=(p.mip_levels-1u)&65535u; }
    p.alias_offset=0;value=p.width;
    for (i=0;i<p.skip;i++) { if (used++>=limit) return -3;p.alias_offset+=value;value>>=1; }
    p.flags&=65535u;p.scale<<=10;*out=p;return 0;
}
int frame_surface_rle(const unsigned char *b,uint32_t length,unsigned char *out,uint32_t capacity,uint32_t count,uint32_t stride,uint32_t *consumed)
{
    uint32_t cursor=0,n=0,run,value,i;
    if (!b || !out || !stride || !consumed) return -1;
    do {
        if (cursor>=length) return -4;
        value=b[cursor++];run=1;
        if (value>192u) { run=value&63u;if (cursor>=length) return -4;value=b[cursor++]; }
        if (run>UINT32_MAX-n || (uint64_t)(n+run-1u)*stride>=capacity) return -4;
        for (i=0;i<run;i++) out[(n+i)*stride]=(unsigned char)value;
        n+=run;
    } while (n<count);
    *consumed=cursor;return 0;
}
static int channel(const unsigned char *b,uint32_t length,uint32_t *cursor,unsigned char *out,uint32_t capacity,uint32_t count,uint32_t stride,uint32_t compression)
{
    uint32_t consumed=0,i;int status;
    if (*cursor>length) return -4;
    if (compression) { status=frame_surface_rle(b+*cursor,length-*cursor,out,capacity,count,stride,&consumed);if (status) return status; }
    else {
        if (length-*cursor<count || (count && (uint64_t)(count-1u)*stride>=capacity)) return -4;
        for (i=0;i<count;i++) out[i*stride]=b[*cursor+i];
        consumed=count;
    }
    *cursor+=consumed;return 0;
}
int frame_surface_decode(const unsigned char *b,uint32_t length,const frame_surface_info *info,uint32_t mip,
                             unsigned char *pixels,uint32_t capacity,unsigned char *palette,uint32_t palette_capacity,uint32_t *consumed)
{
    uint32_t cursor,w,h,level,row,c,channels,i,n,got,needed;int status;
    if (!b || !info || !pixels || !consumed) return -1;
    if (!((info->kind==0u && info->bits==8u) || (info->kind==2u && info->bits==32u)) || info->compression>2u) return -5;
    if (info->format==2u) return -5; /*509840 packed-mip rows not yet supported*/
    if (mip>info->mip_levels || mip>31u) return -4;
    cursor=info->pixel_offset;if (cursor>length) return -4;
    channels=info->kind==0u ? 1u : 4u;
    if (channels==1u) {
        if (!palette || palette_capacity<1024u || length-cursor<768u) return -4;
        for (i=0;i<256u;i++) { palette[i*4u]=b[cursor+512u+i];palette[i*4u+1u]=b[cursor+256u+i];palette[i*4u+2u]=b[cursor+i];palette[i*4u+3u]=i ? 255u : 0; }
        cursor+=768u;
    }
    w=info->width;h=info->height;
    for (level=0;level<=mip;level++,w>>=1,h>>=1) {
        if (!w || !h || (uint64_t)w*h*channels>UINT32_MAX) return -4;
        n=w*h;needed=n*channels;
        if (needed>capacity) return -4; /* scratch contains skipped levels too */
        if (info->compression==2u) {
            for (row=0;row<h;row++) for (c=0;c<channels;c++) {
                got=row*w*channels+c;status=channel(b,length,&cursor,pixels+got,capacity-got,w,channels,1);if (status) return status;
            }
        } else for (c=0;c<channels;c++) {
            status=channel(b,length,&cursor,pixels+c,capacity-c,n,channels,info->compression);if (status) return status;
        }
    }
    *consumed=cursor;return 0;
}
