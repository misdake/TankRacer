#ifndef FRAME_SCENERY_RENDER_H
#define FRAME_SCENERY_RENDER_H
#include "frame_data.h"
/* Testable original special draw rules; writes require a presentation overlay.
 * style0 normal,1=4F7550(0),2=4F7550(1). Position stays in world1/8 units. */
typedef struct frame_scenery_render_services {
    int (*mesh)(void *,uint32_t,const int32_t[3],const int32_t[12],int32_t,uint32_t);
    int (*sprite)(void *,uint32_t);
    void *ctx;
} frame_scenery_render_services;
int frame_scenery_render(frame_data_read,frame_data_write,void *,uint32_t entry,uint32_t object,const frame_scenery_render_services *);
#endif
