#include "frame_overlay.h"
#include "physics_memory.h"
#include <string.h>

int frame_overlay_set(frame_data_read read, frame_data_write write, void *ctx,
                      uint32_t enable, uint32_t slot, const frame_overlay_params *p)
{
    physics_memory m={read,write,ctx,0};
    uint32_t index=slot*4u;
    if (!read || !write || (enable && !p)) return -1;
    if (!enable) {
        pm_store(&m,0x54b4a0u+index,0,4);
        return m.error;
    }
    /* Store order matches 4F739C..4F73CF, including the active flag preceding
     * blue/blend. Do not replace these overlapping two-slot tables by an
     * assumed packed host struct. */
    pm_store(&m,0x54b460u+index,p->alpha,4);
    pm_store(&m,0x54b478u+index,p->red,4);
    pm_store(&m,0x54b470u+index,p->green,4);
    pm_store(&m,0x54b4a0u+index,1,4);
    pm_store(&m,0x54b468u+index,p->blue,4);
    pm_store(&m,0x54b458u+index,p->blend_mode,4);
    return m.error;
}
static void word(unsigned char *p,uint32_t v)
{ uint32_t i;for (i=0;i<4u;i++) p[i]=(unsigned char)(v>>(i*8u)); }
static void real(unsigned char *p,float f)
{ uint32_t v;memcpy(&v,&f,4);word(p,v); }
int frame_overlay_draw(frame_data_read read,void *ctx,frame_commands *c,uint32_t slot)
{
    physics_memory m={read,0,ctx,0};unsigned char vertices[128];uint32_t i,index=slot*4u,colour,mode;
    if (!read || !c || slot>1u) return -1;
    if (!pm_load(&m,0x54b4a0u+index,4)) return m.error;
    colour=((pm_load(&m,0x54b460u+index,4)&255u)<<24)|((pm_load(&m,0x54b478u+index,4)&255u)<<16)|((pm_load(&m,0x54b470u+index,4)&255u)<<8)|(pm_load(&m,0x54b468u+index,4)&255u);mode=pm_load(&m,0x54b458u+index,4);
    if (m.error) return m.error;
    memset(vertices,0,sizeof(vertices));
    for (i=0;i<4u;i++) { unsigned char *v=vertices+i*32u;real(v,i==0u || i==3u ? 0.0f : (float)c->width);real(v+4u,i<2u ? 0.0f : (float)c->height);real(v+12u,1);word(v+16u,colour);word(v+20u,0xff000000u); }
    frame_commands_state(c,4,0);frame_commands_state(c,7,0);frame_commands_state(c,14,1);frame_commands_state(c,15,0);frame_commands_state(c,19,5);frame_commands_state(c,20,mode==1u ? 2u : 6u);frame_commands_state(c,21,2);frame_commands_state(c,22,1);frame_commands_state(c,23,4);frame_commands_state(c,27,1);frame_commands_state(c,28,0);frame_commands_state(c,29,0);frame_commands_state(c,41,0);
    return frame_commands_submit(c,6,0,0,vertices,4,0);
}
