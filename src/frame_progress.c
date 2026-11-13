#include "frame_progress.h"
#include "physics_memory.h"

static uint32_t entity_object(physics_memory *m,uint32_t id)
{
    uint32_t table=pm_load(m,0x55c694u,4);
    uint32_t slot=pm_load(m,table+id*8u,4);
    return slot ? pm_load(m,slot+4u,4) : 0;
}

int frame_checkpoint_at(frame_data_read read,void *ctx,uint32_t position,
                            uint32_t checkpoint_entity,uint32_t side,
                            frame_checkpoint_result *out)
{
    physics_memory mem={read,0,ctx,0},*m=&mem;
    frame_checkpoint_result result={0,0};
    uint32_t racer,checkpoint,next,target,matrix;
    int32_t delta[3],local[3];
    unsigned i,j;
    if (!read || !out) return -1;
    racer=position;
    checkpoint=entity_object(m,checkpoint_entity);
    next=pm_load(m,checkpoint+0x78u+side*4u,4);
    if (next) {
        target=entity_object(m,next);
        if (target) {
            for (i=0;i<3;i++) delta[i]=pm_sub(pm_i32(m,racer+i*4u),pm_i32(m,target+i*4u));
            if (pm_abs(delta[0])<51200 && pm_abs(delta[1])<51200 && pm_abs(delta[2])<51200) {
                matrix=pm_load(m,target+0x14u,4);
                for (i=0;i<3;i++) local[i]=delta[i];
                if (matrix) {
                    for (i=0;i<3;i++) {
                        int32_t sum=0;
                        for (j=0;j<3;j++) sum=pm_add(sum,pm_mul(delta[j],pm_i32(m,matrix+(i*3u+j)*4u)));
                        local[i]=pm_sar(sum,12);
                    }
                }
                if (local[2]>0 && pm_abs(local[0])<pm_sar(pm_i32(m,target+0x88u),1)) {
                    result.next_entity=next;
                    result.finishes_lap=!!pm_load(m,target+0x80u,1);
                }
            }
        }
    }
    if (m->error) return m->error;
    *out=result;
    return 0;
}

int frame_checkpoint_query(frame_data_read read,void *ctx,uint32_t racer_entity,
                            uint32_t checkpoint_entity,uint32_t side,frame_checkpoint_result *out)
{
    physics_memory m={read,0,ctx,0};uint32_t object;
    if (!read || !out) return -1;
    object=entity_object(&m,racer_entity);if (m.error) return m.error;
    return frame_checkpoint_at(read,ctx,object,checkpoint_entity,side,out);
}

int frame_lap_record(frame_data_read read,frame_data_write write,void *ctx,uint32_t id)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    uint32_t object,record;
    int32_t count;
    if (!read || !write) return -1;
    object=entity_object(m,id);
    if (m->error) return m->error;
    if (!object) return -3;
    count=pm_i32(m,object+0xcb6u);
    if (count<3) {
        record=object+0xcbeu+(uint32_t)count*7u;
        pm_store(m,record+4u,(uint32_t)(pm_i32(m,object+0xcbau)/60000),1);
        pm_store(m,record+5u,(uint32_t)((pm_i32(m,object+0xcbau)/1000)%60),1);
        pm_store(m,record+6u,(uint32_t)((pm_i32(m,object+0xcbau)/10)%100),1);
        pm_write32(m,record,pm_i32(m,object+0xcbau));
        count=pm_add(pm_i32(m,object+0xcb6u),1);
        pm_write32(m,object+0xcbau,0);
        pm_write32(m,object+0xcb6u,count);
    }
    return m->error;
}

int frame_route_progress(frame_data_read read,void *ctx,int32_t x,int32_t z,
                           uint32_t checkpoint_entity,int32_t lap,uint32_t work_limit,int32_t *out)
{
    physics_memory mem={read,0,ctx,0},*m=&mem;
    uint32_t current,best_object=0,best_id=0,work=0;
    int32_t gx,gz,rz,row,row_end,best_distance=1024000,score=-1;
    if (!read || !out || !work_limit) return -1;
    current=entity_object(m,checkpoint_entity);
    gx=pm_sar(pm_sub(x,pm_i32(m,0x546870u)),pm_load(m,0x546878u,4));
    gz=pm_sar(pm_sub(z,pm_i32(m,0x546874u)),pm_load(m,0x54687cu,4));
    rz=pm_i32(m,0x54688cu);row=pm_sub(gz,rz);row_end=pm_add(gz,rz);
    while (row<=row_end) {
        int32_t rx=pm_i32(m,0x546888u);
        if (m->error) return m->error;
        if (work++>=work_limit) return -3;
        if (row>=0 && row<8) {
            int32_t col=pm_sub(gx,rx),col_end=pm_add(gx,rx);
            while (col<=col_end) {
                if (work++>=work_limit) return -3;
                if (col>=0 && col<8) {
                    uint32_t head=pm_load(m,0x546770u+((uint32_t)col+(uint32_t)row*8u)*4u,4);
                    uint32_t node=pm_load(m,head,4);
                    while (node!=head) {
                        uint32_t id,object;
                        if (m->error) return m->error;
                        if (work++>=work_limit) return -3;
                        id=pm_load(m,node+8u,4);object=entity_object(m,id);
                        if (pm_i32(m,object+0x90u)!=-1) {
                            int32_t dx=pm_sub(x,pm_i32(m,object)),dz=pm_sub(z,pm_i32(m,object+8u));
                            if (pm_abs(dx)<153600 && pm_abs(dz)<153600) {
                                uint32_t matrix=pm_load(m,object+0x14u,4);
                                int32_t facing=pm_add(pm_mul(pm_i32(m,matrix+32u),dz),pm_mul(pm_i32(m,matrix+24u),dx));
                                if (facing<0) {
                                    int32_t width_test=0;
                                    if (pm_load(m,object+0x81u,1) && pm_sub(pm_i32(m,object+0x90u),pm_i32(m,current+0x90u))!=1)
                                        width_test=pm_add(pm_mul(dx,pm_i32(m,matrix)),pm_mul(pm_i32(m,matrix+8u),dz));
                                    if (!width_test || pm_abs(width_test)<pm_sar(pm_i32(m,object+0x88u),1)) {
                                        int32_t ax=pm_abs(dx),az=pm_abs(dz);
                                        int32_t distance=pm_add(pm_sub(az,pm_sar(az<=ax ? az : ax,1)),ax);
                                        if (distance<best_distance) { best_id=id;best_object=object;best_distance=distance; }
                                    }
                                }
                            }
                        }
                        node=pm_load(m,node,4);
                    }
                }
                if (m->error) return m->error;
                col=pm_add(col,1);
            }
        }
        row=pm_add(row,1);
    }
    if (best_object) {
        if (pm_load(m,best_object+0x80u,1)) {
            score=pm_sub(pm_mul(pm_add(lap,1),10240000),best_distance);
            if (pm_load(m,current+0x78u,4)!=best_id) score=pm_sub(score,10240000);
        } else {
            score=pm_sub(pm_mul(pm_add(pm_i32(m,best_object+0x90u),pm_mul(lap,50)),204800),best_distance);
            if (pm_i32(m,current+0x90u)<pm_sub(pm_i32(m,best_object+0x90u),2)) score=pm_sub(score,10240000);
        }
    }
    if (m->error) return m->error;
    *out=score;
    return 0;
}

int frame_route_nearest(frame_data_read read,void *ctx,uint32_t position,uint32_t limit,uint32_t *out)
{
    physics_memory m={read,0,ctx,0};int32_t x,z,xmin,xmax,zmin,zmax,cx,cz,dx,dy,dz;uint32_t head,node,o,id,best=0xfa000u,selected=0,used=0,a,b,c,distance;
    if (!read || !limit || !out) return -1;
    cx=pm_sar(pm_sub(pm_i32(&m,position),pm_i32(&m,0x546870u)),pm_load(&m,0x546878u,4));cz=pm_sar(pm_sub(pm_i32(&m,position+8u),pm_i32(&m,0x546874u)),pm_load(&m,0x54687cu,4));
    xmin=pm_sub(cx,pm_i32(&m,0x546888u));xmax=pm_add(cx,pm_i32(&m,0x546888u));zmin=pm_sub(cz,pm_i32(&m,0x54688cu));zmax=pm_add(cz,pm_i32(&m,0x54688cu));
    for (z=zmin;z<=zmax;z=pm_add(z,1)) {
        if (used++>=limit) return -3;
        if (z<0 || z>=8) continue;
        for (x=xmin;x<=xmax;x=pm_add(x,1)) {
            if (used++>=limit) return -3;
        if (x<0 || x>=8) continue;
            head=pm_load(&m,0x546770u+((uint32_t)x+(uint32_t)z*8u)*4u,4);node=pm_load(&m,head,4);
            while (node!=head) {
                if (m.error) return m.error;
                if (used++>=limit) return -3;
                id=pm_load(&m,node+8u,4);o=entity_object(&m,id);
                if (o && pm_load(&m,o+0x90u,4)!=UINT32_MAX) {
                    dx=pm_abs(pm_sub(pm_i32(&m,position),pm_i32(&m,o)));dy=pm_abs(pm_sub(pm_i32(&m,position+4u),pm_i32(&m,o+4u)));dz=pm_abs(pm_sub(pm_i32(&m,position+8u),pm_i32(&m,o+8u)));
                    if (dx<153600 && dy<153600 && dz<153600) {
                        a=(uint32_t)dx;b=(uint32_t)dy;c=(uint32_t)dz;
                        distance=b<a && c<a ? (c>>2)+a+(b>>2) : b>=a && c<b ? (c>>2)+b+(a>>2) : (b>>2)+c+(a>>2);
                        if (distance<best) { best=distance;selected=id; }
                    }
                }
                node=pm_load(&m,node,4);
            }
        }
    }
    if (m.error) return m.error;
    *out=selected;return 0;
}
