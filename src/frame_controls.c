#include "frame_controls.h"
#include "physics_memory.h"

int frame_control_key(frame_data_read read,void *ctx,uint32_t code,uint32_t *out)
{
    physics_memory m={read,0,ctx,0};
    uint32_t value;
    if (!read || !out) return -1;
    value=pm_load(&m,0x74b600u+code,1);
    if (m.error) return m.error;
    *out=value;
    return 0;
}

int frame_control_axis(frame_data_read read,void *ctx,uint32_t argument,int32_t *out)
{
    physics_memory m={read,0,ctx,0};
    uint32_t index;
    int32_t limit,value,numerator,divisor,result;
    if (!read || !out) return -1;
    if (!pm_load(&m,0x53ccdcu,4)) {
        if (m.error) return m.error;
        *out=0;
        return 0;
    }
    index=argument==0 ? pm_load(&m,0x53cce4u,4) :
          argument==3 ? pm_load(&m,0x52e51cu,4) : argument;
    limit=pm_i32(&m,0x52e4c8u+index*4u);
    value=pm_i32(&m,0x53cc80u+index*4u);
    if (m.error) return m.error;
    if ((value>=pm_neg(limit) && value<=limit) || value==0) {
        *out=0;
        return 0;
    }
    if (value>0) {
        uint32_t delta=(uint32_t)pm_sub(value,limit);
        numerator=pm_signed((delta<<15)-delta);
        divisor=pm_sub(pm_i32(&m,0x52e4f8u+index*4u),limit);
    } else {
        numerator=pm_signed((uint32_t)pm_neg(pm_add(value,limit))<<15);
        divisor=pm_add(pm_i32(&m,0x52e4e0u+index*4u),limit);
    }
    if (m.error) return m.error;
    if (!divisor || (numerator==INT32_MIN && divisor==-1)) return -2;
    result=(int32_t)((int64_t)numerator/(int64_t)divisor);
    if (value>0 && result>32767) result=32767;
    if (value<0 && result<-32768) result=-32768;
    *out=result;
    return 0;
}
