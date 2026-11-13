/* Portable draw-command producer. No graphics API, OS, guest pointer or allocator.
 * TCB1 is a little-endian wire format, not a dump of these host structs.
 * The same caller-owned buffers can live in a native process or Wasm memory. */
#ifndef FRAME_COMMANDS_H
#define FRAME_COMMANDS_H
#include <stdint.h>
#include <stddef.h>

#define FRAME_COMMAND_HEADER 48u
#define FRAME_COMMAND_DRAW 192u
#define FRAME_COMMAND_VERTEX 32u
#define FRAME_COMMAND_STATES 41u
#define FRAME_PRESENT_GROUND_SHADOW 1u

typedef struct frame_commands {
    uint8_t *commands, *vertices;
    uint32_t command_capacity, vertex_capacity, command_count, vertex_count;
    uint32_t width, height, sequence, states[FRAME_COMMAND_STATES];
    int error;
} frame_commands;

size_t frame_commands_context_size(void);
int frame_commands_begin(frame_commands *ctx, void *commands, uint32_t command_bytes,
                         void *vertices, uint32_t vertex_bytes,
                         uint32_t width, uint32_t height, uint32_t sequence);
int frame_commands_state(frame_commands *ctx, uint32_t state, uint32_t value);
/* TL bytes: float x,y,z,rhw; uint32 ARGB diffuse,specular; float u,v. */
int frame_commands_submit(frame_commands *ctx, uint32_t primitive, uint32_t texture,
                          uint32_t flags, const void *tl, uint32_t count,
                          uint32_t presentation);
uint32_t frame_commands_size(const frame_commands *ctx);
/* Writes header + draw records + shared vertex arena. Returns byte count or 0. */
uint32_t frame_commands_finish(const frame_commands *ctx, void *out, uint32_t capacity);
#endif
