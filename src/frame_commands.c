#include "frame_commands.h"
#include <string.h>
#include <limits.h>

_Static_assert(CHAR_BIT == 8 && sizeof(float) == 4, "TCB1 requires 8-bit bytes and binary32 TL input");

static void put32(uint8_t *p, uint32_t v)
{
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}

size_t frame_commands_context_size(void) { return sizeof(frame_commands); }

int frame_commands_begin(frame_commands *c, void *commands, uint32_t cb,
                         void *vertices, uint32_t vb,
                         uint32_t width, uint32_t height, uint32_t sequence)
{
    if (!c || !commands || !vertices || !width || !height) return -1;
    /* Render states persist between frames, as in D3D2. The caller initializes
     * the context and uploads captured states before the first frame. */
    c->commands=(uint8_t *)commands; c->vertices=(uint8_t *)vertices;
    c->command_capacity=cb; c->vertex_capacity=vb;
    c->command_count=0; c->vertex_count=0;
    c->width=width; c->height=height; c->sequence=sequence; c->error=0;
    return 0;
}

int frame_commands_state(frame_commands *c, uint32_t state, uint32_t value)
{
    if (!c || state<1 || state>FRAME_COMMAND_STATES) return -1;
    c->states[state-1]=value;
    return 0;
}

int frame_commands_submit(frame_commands *c, uint32_t primitive, uint32_t texture,
                          uint32_t flags, const void *tl, uint32_t count,
                          uint32_t presentation)
{
    uint32_t i;
    uint8_t *record;
    if (!c || c->error) return -1;
    if (!tl || primitive<1 || primitive>6 || !count || count>4096 || presentation>1) {
        c->error=-2; return c->error;
    }
    if (c->command_count>=c->command_capacity/FRAME_COMMAND_DRAW ||
        count>c->vertex_capacity/FRAME_COMMAND_VERTEX-c->vertex_count) {
        c->error=-3; return c->error;
    }
    record=c->commands+c->command_count*FRAME_COMMAND_DRAW;
    put32(record,primitive); put32(record+4,texture);
    put32(record+8,c->vertex_count); put32(record+12,count);
    put32(record+16,flags); put32(record+20,presentation);
    for (i=0;i<FRAME_COMMAND_STATES;i++) put32(record+24+i*4,c->states[i]);
    put32(record+188,0); /* reserved; must stay zero */
    /* The input is already an explicitly little-endian TL stream, including
     * IEEE754 bit patterns. No C structure padding or float conversion. */
    memcpy(c->vertices+c->vertex_count*FRAME_COMMAND_VERTEX,tl,count*FRAME_COMMAND_VERTEX);
    c->vertex_count+=count; c->command_count++;
    return 0;
}

uint32_t frame_commands_size(const frame_commands *c)
{
    uint64_t size;
    if (!c || c->error) return 0;
    size=(uint64_t)FRAME_COMMAND_HEADER+(uint64_t)c->command_count*FRAME_COMMAND_DRAW+
         (uint64_t)c->vertex_count*FRAME_COMMAND_VERTEX;
    return size>UINT32_MAX ? 0 : (uint32_t)size;
}

uint32_t frame_commands_finish(const frame_commands *c, void *out, uint32_t capacity)
{
    uint32_t size=frame_commands_size(c), vertex_offset;
    uint8_t *p=(uint8_t *)out;
    if (!size || !p || capacity<size) return 0;
    vertex_offset=FRAME_COMMAND_HEADER+c->command_count*FRAME_COMMAND_DRAW;
    memcpy(p,"TCB1",4); put32(p+4,1); put32(p+8,size);
    put32(p+12,c->width); put32(p+16,c->height); put32(p+20,c->sequence);
    put32(p+24,c->command_count); put32(p+28,c->vertex_count);
    put32(p+32,FRAME_COMMAND_HEADER); put32(p+36,vertex_offset);
    put32(p+40,0xff0a0d0au); put32(p+44,0); /* ARGB clear, reserved */
    memcpy(p+FRAME_COMMAND_HEADER,c->commands,c->command_count*FRAME_COMMAND_DRAW);
    memcpy(p+vertex_offset,c->vertices,c->vertex_count*FRAME_COMMAND_VERTEX);
    return size;
}
