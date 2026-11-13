#include "frame_host.h"
#include <string.h>

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}

int frame_host_sample_input(frame_host_input *s, const uint8_t *keys,
                            uint32_t count, uint32_t raw_dt, int focused)
{
    uint8_t next[256]={0};
    uint32_t i;
    if (!s || (count && !keys) || count>256 || raw_dt<10 || raw_dt>1000) return -1;
    if (focused) for (i=0;i<count;i++) next[keys[i]]=1;
    for (i=0;i<256;i++) {
        s->pressed[i]=(uint8_t)(next[i] && !s->keys[i]);
        s->released[i]=(uint8_t)(s->keys[i] && !next[i]);
    }
    memcpy(s->keys,next,256); s->raw_dt=raw_dt; s->sequence++;
    return 0;
}

int frame_host_archive_find(const void *file, uint32_t bytes, const char *name,
                            uint32_t name_bytes, uint32_t *offset, uint32_t *length)
{
    const uint8_t *p=(const uint8_t *)file;
    uint32_t count,i,found=0,found_offset=0,found_length=0;
    if (!p || !name || !offset || !length || bytes<28 || name_bytes<1 || name_bytes>12 ||
        memcmp(p,"FASTF101",8)) return -1;
    count=get32(p+24);
    if (count>(bytes-28)/24) return -2;
    for (i=0;i<count;i++) {
        const uint8_t *entry=p+28+i*24;
        uint32_t off=get32(entry+12),size=get32(entry+16),n=0;
        if (off>bytes || size>bytes-off || off<28+count*24) return -3;
        while (n<12 && entry[n]) n++;
        if (n==name_bytes && !memcmp(entry,name,n)) {
            if (found) return -4; /* ambiguous exact duplicate */
            found=1; found_offset=off; found_length=size;
        }
    }
    if (!found) return 1;
    *offset=found_offset; *length=found_length;
    return 0;
}

int frame_host_read_span(uint32_t bytes, uint32_t *cursor, uint32_t requested,
                         uint32_t *offset, uint32_t *length)
{
    uint32_t available;
    if (!cursor || !offset || !length) return -1;
    available=*cursor<bytes ? bytes-*cursor : 0;
    *offset=*cursor; *length=requested<available ? requested : available;
    *cursor+=*length;
    return 0;
}
static unsigned char ascii_lower(unsigned char c) { return c>=65u && c<=90u ? (unsigned char)(c+32u) : c; }
int frame_host_shape_find(const void *file,uint32_t bytes,const char *name,uint32_t name_bytes,uint32_t *offset,uint32_t *length)
{
    const unsigned char *p=(const unsigned char *)file;uint32_t count,i,j,n,at,size,found=0,found_at=0,found_size=0;int equal;
    if (!p || !name || !offset || !length || bytes<8u || !name_bytes || name_bytes>11u) return -1;
    if (name_bytes>3u && name[name_bytes-3u]=='.' && ascii_lower((unsigned char)name[name_bytes-2u])=='s' && ascii_lower((unsigned char)name[name_bytes-1u])=='m') name_bytes-=3u;
    if (name_bytes>8u || get32(p)!=bytes) return -1;
    count=get32(p+4u);if (count>(bytes-8u)/12u) return -2;
    for (i=0;i<count;i++) {
        const unsigned char *entry=p+8u+i*12u;at=get32(entry+8u);
        if (at<8u+count*12u || at>bytes || bytes-at<4u) return -3;
        size=get32(p+at);if (size>bytes-at-4u) return -3;
        for (n=0;n<8u && entry[n];n++) { }
        equal=n==name_bytes;
        if (equal) for (j=0;j<n;j++) if (ascii_lower(entry[j])!=ascii_lower((unsigned char)name[j])) { equal=0;break; }
        if (equal) { if (found) return -4;found=1;found_at=at+4u;found_size=size; }
    }
    if (!found) return 1;
    *offset=found_at;*length=found_size;return 0;
}

int frame_host_seek(uint32_t bytes, uint32_t *cursor, int32_t distance, uint32_t origin)
{
    int64_t base,next;
    if (!cursor || origin>2) return -1;
    base=origin==0 ? 0 : origin==1 ? *cursor : bytes;
    next=base+(int64_t)distance;
    if (next<0 || next>UINT32_MAX) return -2;
    *cursor=(uint32_t)next;
    return 0;
}
