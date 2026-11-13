#include "frame_io.h"
#include "frame_host.h"
#include <string.h>

static frame_io_file *lookup(frame_io_files *s,uint32_t handle)
{
    unsigned i;
    if (!s) return 0;
    for (i=0;i<FRAME_IO_MAX_FILES;i++)
        if (s->files[i].live && s->files[i].handle==handle) return &s->files[i];
    s->last_error=6;
    return 0;
}
void frame_io_files_init(frame_io_files *s)
{
    if (!s) return;
    memset(s,0,sizeof(*s));s->next_handle=0x7a000000u;
}
int frame_io_open(frame_io_files *s,uint32_t resource,uint32_t length,uint32_t *handle)
{
    unsigned i;
    if (!s) return -1;
    if (!handle) { s->last_error=87;return -1; }
    if (s->next_handle<0x7a000000u || s->next_handle==UINT32_MAX) { s->last_error=4;return -3; }
    for (i=0;i<FRAME_IO_MAX_FILES;i++) if (!s->files[i].live) {
        frame_io_file *f=&s->files[i];
        f->handle=s->next_handle++;f->resource=resource;f->length=length;f->cursor=0;f->live=1;
        *handle=f->handle;s->last_error=0;return 0;
    }
    s->last_error=4;return -3;
}
int frame_io_close(frame_io_files *s,uint32_t handle)
{
    frame_io_file *f=lookup(s,handle);
    if (!f) return -1;
    f->live=0;return 0;
}
int frame_io_size(frame_io_files *s,uint32_t handle,uint32_t *length)
{
    frame_io_file *f=lookup(s,handle);
    if (!f) return -1;
    if (!length) { s->last_error=87;return -1; }
    *length=f->length;s->last_error=0;return 0;
}
int frame_io_read(frame_io_files *s,uint32_t handle,uint32_t requested,frame_io_span *out)
{
    frame_io_file *f=lookup(s,handle);
    if (!f) return -1;
    if (!out) { s->last_error=87;return -1; }
    out->resource=f->resource;
    return frame_host_read_span(f->length,&f->cursor,requested,&out->offset,&out->length);
}
int frame_io_seek(frame_io_files *s,uint32_t handle,int32_t distance,uint32_t origin,uint32_t *position)
{
    frame_io_file *f=lookup(s,handle);
    int status;
    if (!f) return -1;
    if (!position) { s->last_error=87;return -1; }
    status=frame_host_seek(f->length,&f->cursor,distance,origin);
    if (status) { s->last_error=status==-2 ? 131u : 87u;return -2; }
    *position=f->cursor;s->last_error=0;return 0;
}
void frame_io_clock_init(frame_io_clock *s,uint32_t tick)
{
    if (s) { s->tick=tick;s->samples=0; }
}
void frame_io_clock_advance(frame_io_clock *s,uint32_t elapsed)
{
    if (s) s->tick+=elapsed;
}
int frame_io_clock_poll(void *ctx,uint32_t *tick)
{
    frame_io_clock *s=(frame_io_clock *)ctx;
    if (!s || !tick) return 0;
    *tick=s->tick;s->samples++;return 1;
}
