#include "frame_file.h"
#include "physics_memory.h"
#include <stdlib.h>
#include <string.h>
typedef struct file_work { frame_file *file;uint32_t used;int error; } file_work;
static uint32_t get(file_work *w)
{
    frame_file *f=w->file;if (w->error) return UINT32_MAX;
    if (w->used++>=f->work_limit) { w->error=-3;return UINT32_MAX; }
    return f->cursor<f->length ? f->bytes[f->cursor++] : UINT32_MAX;
}
static void unget(file_work *w,uint32_t c)
{ if (c!=UINT32_MAX && w->file->cursor) w->file->cursor--; }
static int whitespace(uint32_t c)
{ return c==32u || (c>=9u && c<=13u) || c==0u; }
static void skip(file_work *w)
{
    uint32_t c,comments,spaces;
    do {
        do {
            comments=0;c=get(w);
            if (c==35u) {
                comments=1;do { c=get(w); } while (c!=10u && c!=UINT32_MAX && !w->error);
                if (c==UINT32_MAX) return;
                c=get(w);if (c!=13u) unget(w,c);
            } else unget(w,c);
        } while (comments && !w->error);
        spaces=0;c=get(w);while (whitespace(c) && !w->error) { spaces=1;c=get(w); }unget(w,c);
    } while (spaces && !w->error);
}
static int put(unsigned char *token,uint32_t at,uint32_t c,frame_data_write write,void *ctx,uint32_t destination)
{ unsigned char value=(unsigned char)c;if (token) token[at]=value;return write && !write(ctx,destination+at,&value,1) ? -1 : 0; }
static int token_to(frame_file *file,unsigned char *token,uint32_t capacity,uint32_t *present,frame_data_write write,void *ctx,uint32_t destination)
{
    file_work w={file,0,0};uint32_t mode=0,n=0,c=0,previous=0;int status;
    if (!file || (!token && !write) || capacity<2u || !present || !file->work_limit || (!file->bytes && file->length)) return -1;
    skip(&w);if (w.error) return w.error;status=put(token,0,0,write,ctx,destination);if (status) return status;
    if (file->cursor>=file->length) { *present=0;return 0; }
    while (n<capacity-1u) {
        c=get(&w);if (w.error) return w.error;
        if (c==UINT32_MAX) { c=0;if (!mode) { *present=0;return 0; }break; }
        if (c<32u && c!=9u && c!=10u && c!=13u) break;
        if (!mode) {
            if (c>32u) { if (c!=34u) { mode=1;status=put(token,n++,c,write,ctx,destination);if (status) return status; }else mode=2; }
        } else {
            if (mode==1u) { if (c<33u) break; }
            else {
                if (c<32u) break;
                if (c==92u && previous==92u) { if (!n) return -4;n--; }
                if (c==34u) { if (previous!=92u) break;if (!n) return -4;n--; }
            }
            status=put(token,n++,c,write,ctx,destination);if (status) return status;
        }
        previous=c;
    }
    if (c && (mode<2u || c!=34u)) unget(&w,c);
    status=put(token,n,0,write,ctx,destination);if (status) return status;*present=n!=0u;return 0;
}
int frame_file_token(frame_file *file,unsigned char *token,uint32_t capacity,uint32_t *present)
{ return token_to(file,token,capacity,present,0,0,0); }
int frame_file_token_data(frame_file *file,frame_data_write write,void *ctx,uint32_t dest,uint32_t capacity,uint32_t *present)
{ if (!write) return -1;return token_to(file,0,capacity,present,write,ctx,dest); }
int frame_file_integer(frame_file *file,uint32_t *value,uint32_t *present)
{
    file_work w={file,0,0};uint32_t c,sign=0,n=0,found=0;
    if (!file || !value || !present || !file->work_limit || (!file->bytes && file->length)) return -1;
    skip(&w);if (w.error) return w.error;
    for (;;) {
        c=get(&w);if (w.error) return w.error;if (c==UINT32_MAX) break;
        if (c<48u || c>57u) {
            if (!sign && (c==45u || c==43u)) { sign=c==45u ? 2u : 1u;continue; }
            unget(&w,c);break;
        }
        if (!sign) sign=1;
        found=1;n=n*10u+c-48u;
    }
    *value=sign==2u ? 0u-n : n;*present=found;return 0;
}
int frame_file_name(frame_file *file,unsigned char *token,uint32_t capacity,uint32_t word,uint32_t *present)
{
    file_work w={file,0,0};uint32_t c,n=0;
    if (!file || !token || capacity<2u || !present || !file->work_limit || (!file->bytes && file->length)) return -1;
    skip(&w);if (w.error) return w.error;*present=0;
    if (file->cursor>=file->length) return 0;
    c=file->bytes[file->cursor];if (c>=128u) return -5;
    if (word && !(c>=48u && c<=57u) && !(c>=65u && c<=90u) && !(c>=97u && c<=122u)) return 0;
    for (;;) {
        c=get(&w);if (w.error) return w.error;if (c==UINT32_MAX) return 0;
        if (whitespace(c)) { token[n]=0;*present=n!=0u;return 0; }
        if (n>=capacity-1u) return -3;
        token[n++]=(unsigned char)c;
    }
}
int frame_file_real32(frame_file *file,uint32_t *bits,uint32_t *present)
{
    file_work w={file,0,0};char text[256],*end;uint32_t n=0;double value;float rounded;
    if (!file || !bits || !present || !file->work_limit || (!file->bytes && file->length)) return -1;
    skip(&w);if (w.error) return w.error;
    while (file->cursor+n<file->length && n+1u<sizeof(text)) { text[n]=(char)file->bytes[file->cursor+n];n++; }
    text[n]=0;value=strtod(text,&end);*present=end!=text;if (!*present) return 0;
    if (end==text+n && file->cursor+n<file->length) return -3;
    file->cursor+=(uint32_t)(end-text);rounded=(float)value;memcpy(bits,&rounded,4);return 0;
}
int frame_file_vector_data(frame_file *file,frame_data_write write,void *ctx,uint32_t dest,uint32_t *assigned)
{
    physics_memory m={0,write,ctx,0};file_work w={file,0,0};uint32_t i,c,negative,value,found;
    if (!file || !write || !assigned || !file->work_limit || (!file->bytes && file->length)) return -1;
    skip(&w);if (w.error) return w.error;*assigned=0;
    for (i=0;i<3u;i++) {
        c=get(&w);while ((c==32u || (c>=9u && c<=13u)) && !w.error) c=get(&w);
        negative=c==45u;if (negative || c==43u) c=get(&w);value=0;found=0;
        while (c>=48u && c<=57u && !w.error) { found=1;value=value*10u+c-48u;c=get(&w); }
        unget(&w,c);if (w.error) return w.error;if (!found) break;
        pm_store(&m,dest+i*4u,negative ? 0u-value : value,4);if (m.error) return m.error;(*assigned)++;
    }
    return 0;
}
