#include "frame_vehicle_scene.h"
#include "physics_memory.h"
#include "physics_vector.h"
#include "physics_effects.h"
#include "frame_player_collision.h"

static uint32_t random_next(physics_memory *m)
{
    uint32_t seed=pm_load(m,0x55b6acu,4)*1664525u+1013904223u;pm_store(m,0x55b6acu,seed,4);return seed;
}
/* Camera matrix and points are local values; no emulated stack is required. */
int frame_pickup_effect_initialize(frame_data_read read,frame_data_write write,void *ctx,frame_object_material_name material,void *material_ctx)
{
    physics_memory m={read,write,ctx,0};uint32_t i,j,at,value;int status;
    static const struct { uint32_t address,value,size; } sectors[]={
        {0x5450e4,0x85f,4},{0x545138,0x85f,4},
        {0x5450d0,0x1c70,2},{0x5450d2,0x2aa8,2},{0x5450d4,0xfffff8b9,4},{0x5450d8,0xffffec59,4},
        {0x5450dc,0x27d0,2},{0x5450de,0x2d80,2},{0x5450e0,0xdd4,4},
        {0x5450e8,0x2c14,2},{0x5450ea,0x3ffc,2},{0x5450ec,0x1d7b,4},{0x5450f0,0xe8f,4},
        {0x5450f4,0x6aa4,2},{0x5450f6,0x78dc,2},{0x5450f8,0xfffff088,4},{0x5450fc,0xffffe3cb,4},
        {0x545100,0x7498,2},{0x545102,0x7ff8,2},{0x545104,0xfffff82d,4},{0x545108,0xffffed42,4},
        {0x54510c,0x7ff8,2},{0x54510e,0x954c,2},{0x545110,0x1ef0,4},{0x545114,0xcbd,4},
        {0x545118,0xae2e,2},{0x54511a,0xb98e,2},{0x54511c,0xfffff114,4},{0x545120,0xffffe912,4},
        {0x545124,0xb8d8,2},{0x545126,0xc438,2},{0x545128,0xfffff4b8,4},{0x54512c,0xffffecb6,4},
        {0x545130,0xc932,2},{0x545132,0xd326,2},{0x545134,0x134a,4},
        {0x54513c,0xd04e,2},{0x54513e,0xecbe,2},{0x545140,0x2721,4},{0x545144,0x114a,4}};
    if (!read || !write) return -1;
    for (i=0;i<2u;i++) { at=0x58faa0u+i*44u;pm_store(&m,at,0,1);pm_store(&m,at+2u,0,1);pm_store(&m,at+3u,0,1); }
    for (i=0;i<2u;i++) {
        for (j=0;j<10u;j++) { at=0x545190u+i*840u+j*84u;pm_store(&m,at-4u,0,4);pm_store(&m,at,1,1);pm_store(&m,at-56u,0xfeb4024du,4); }
        for (j=0;j<10u;j++) { at=0x545814u+i*680u+j*68u;pm_store(&m,at-4u,0,4);pm_store(&m,at,1,1);pm_store(&m,at+12u,0xfeb4024du,4); }
    }
    for (i=0;i<sizeof(sectors)/sizeof(sectors[0]);i++) pm_store(&m,sectors[i].address,sectors[i].value,sectors[i].size);
    if (m.error) return m.error;
    if (!material) return -5;
    status=material(material_ctx,(const unsigned char *)"flare3",7,1,&value);if (status) return status;pm_store(&m,0x545d30u,value,4);
    if (m.error) return m.error;
    at=pm_load(&m,0x5964d4u,4)&32768u;
    status=material(material_ctx,(const unsigned char *)(at ? "glow" : "pcglow"),at ? 5u : 7u,1,&value);if (status) return status;pm_store(&m,0x545d34u,value,4);return m.error;
}
static void pickup_transform(const int32_t matrix[9],const int32_t v[3],int32_t out[3],unsigned transpose)
{
    unsigned i,j;for (i=0;i<3u;i++) { out[i]=0;for (j=0;j<3u;j++) out[i]=pm_add(out[i],pm_mul(v[j],matrix[transpose ? j*3u+i : i*3u+j]));out[i]=pm_sar(out[i],12); }
}
static int32_t pickup_point(physics_memory *m,uint32_t at,const int32_t matrix[9],const int32_t origin[3],int32_t radius,uint32_t angle)
{
    int32_t v[3],out[3];unsigned i;
    v[0]=pm_trunc(pm_mul(radius,pm_i32(m,0x59b380u+(angle&65532u))),12);
    v[1]=pm_trunc(pm_mul(radius,pm_i32(m,0x597380u+(angle&65532u))),12);v[2]=0;
    pickup_transform(matrix,v,out,1);for (i=0;i<3u;i++) pm_write32(m,at+i*4u,pm_add(origin[i],out[i]));return pm_add(origin[0],out[0]);
}
static int32_t pickup_far_radius(int32_t angle,const int32_t view[3],int32_t previous_x,unsigned second)
{
    int32_t r;
    if (pm_abs(view[0])<2048) r=view[2];
    /* The second endpoint's original OR comparison covers every signed angle.
     * Keep this asymmetry; replacing it with the first endpoint's sectors would
     * change scene geometry. Four first-endpoint boundaries retain prior EAX. */
    else if (second || (angle>8190 && angle<24570) || (angle<-8190 && angle>-24570)) r=pm_add(view[2],pm_abs(view[0]));
    else if (angle>-8190 && angle<8190) r=pm_sub(view[2],pm_abs(view[1]));
    else if (angle<-24570 || angle>24570) r=pm_add(view[2],pm_abs(view[1]));
    else r=previous_x;
    return r<12288 ? 12288 : r;
}
int frame_pickup_effect_project(frame_data_read read,frame_data_write write,void *ctx,uint32_t target)
{
    physics_memory m={read,write,ctx,0};uint32_t bank,b,camera,i,at,p,id,point,angle_a,angle_b;int32_t matrix[9],origin[3],relative[3],view[3],last_x,radius;int status;
    if (!read || !write) return -1;
    for (bank=0;bank<2u;bank++) {
        b=0x58faa0u+bank*44u;if (pm_load(&m,b+4u,4)!=target || !pm_load(&m,b,1)) continue;
        if (pm_load(&m,b+12u,4)==pm_load(&m,0x590564u,4)) { if (pm_load(&m,b+1u,1)==1u) return m.error;pm_store(&m,b+1u,1,1); }
        camera=pm_load(&m,0x54b4a8u,4);for (i=0;i<9u;i++) matrix[i]=pm_i32(&m,0x74af00u+camera*48u+i*4u);
        point=b+(pm_load(&m,b+2u+camera,1) ? 28u : 16u);
        for (i=0;i<3u;i++) { origin[i]=pm_i32(&m,point+i*4u);relative[i]=pm_sub(origin[i],pm_mul(pm_i32(&m,0x74aee0u+camera*12u+i*4u),8)); }
        pickup_transform(matrix,relative,view,0);
        for (i=0;i<10u;i++) {
            at=0x545148u+bank*840u+i*84u;angle_a=(uint32_t)pm_i16(&m,at);angle_b=(uint32_t)pm_i16(&m,at+2u);
            (void)pickup_point(&m,at+20u,matrix,origin,pm_i32(&m,0x5457d8u+bank*4u),angle_a);
            last_x=pickup_point(&m,at+56u,matrix,origin,pm_i32(&m,0x5457d8u+bank*4u),angle_b);
            radius=pickup_far_radius(pm_signed(angle_a),view,last_x,0);last_x=pickup_point(&m,at+32u,matrix,origin,radius,angle_a);
            radius=pickup_far_radius(pm_signed(angle_b),view,last_x,1);(void)pickup_point(&m,at+44u,matrix,origin,radius,angle_b);
            id=pm_load(&m,at+16u,4);if (m.error) return m.error;
            status=frame_light_replace(read,write,ctx,id,at+20u);if (status) return status;
        }
        for (i=0;i<10u;i++) {
            p=0x5457e0u+bank*680u+i*68u;id=pm_load(&m,p+64u,4);if (id==0xfeb4024du) continue;
            angle_a=pm_load(&m,0x531698u+i*4u,4);angle_b=pm_load(&m,0x531698u+((i+1u)%10u)*4u,4);
            (void)pickup_point(&m,p,matrix,origin,pm_i32(&m,0x5450c8u+bank*4u),angle_b);
            (void)pickup_point(&m,p+36u,matrix,origin,pm_i32(&m,0x5450c8u+bank*4u),angle_a);
            (void)pickup_point(&m,p+12u,matrix,origin,pm_i32(&m,0x5450c0u+bank*4u),angle_b);
            (void)pickup_point(&m,p+24u,matrix,origin,pm_i32(&m,0x5450c0u+bank*4u),angle_a);
            if (m.error) return m.error;
            status=frame_light_replace(read,write,ctx,id,p);if (status) return status;
        }
    }
    return m.error;
}
int frame_pickup_effect_start(frame_data_read read,frame_data_write write,void *ctx,uint32_t source,uint32_t target)
{
    physics_memory m={read,write,ctx,0};uint32_t bank=0,b,i,j,at,id,oldest=UINT32_MAX,time;int32_t velocity[3];int status;
    if (!read || !write) return -1;
    for (i=0;i<2u;i++) { time=pm_load(&m,0x58faacu+i*44u,4);if (time<oldest || oldest==UINT32_MAX) { oldest=time;bank=i; } }
    b=0x58faa0u+bank*44u;
    if (pm_load(&m,b,1)) {
        for (i=0;i<10u;i++) {
            at=0x545158u+bank*840u+i*84u;id=pm_load(&m,at,4);if (m.error) return m.error;
            status=frame_light_release(read,write,ctx,id);if (status) return status;pm_store(&m,at,0xfeb4024du,4);
        }
        for (i=0;i<10u;i++) {
            at=0x545820u+bank*680u+i*68u;id=pm_load(&m,at,4);if (id==0xfeb4024du) continue;if (m.error) return m.error;
            status=frame_light_release(read,write,ctx,id);if (status) return status;pm_store(&m,at,0xfeb4024du,4);
        }
    }
    pm_store(&m,b,1,1);pm_store(&m,b+4u,target,4);pm_store(&m,b+12u,pm_load(&m,0x590564u,4),4);pm_store(&m,b+1u,0,1);
    pm_store(&m,b+16u,pm_load(&m,source,4),4);pm_store(&m,b+20u,pm_load(&m,source+4u,4),4);time=pm_load(&m,source+8u,4);
    pm_store(&m,b+3u,0,1);pm_store(&m,b+2u,0,1);pm_store(&m,b+24u,time,4);pm_store(&m,b+8u,65536,4);
    if (pm_load(&m,target+0xde5u,1) && !pm_load(&m,target+0xde6u,1)) {
        pm_store(&m,b+2u+(uint32_t)pm_i8(&m,target+0xdf0u),1,1);
        for (i=0;i<3u;i++) pm_store(&m,b+28u+i*4u,pm_load(&m,b+16u+i*4u,4),4);
        for (i=0;i<3u;i++) velocity[i]=pm_sar(pm_i32(&m,target+0x50u+i*4u),6);
        pm_write32(&m,b+40u,frame_collision_change_magnitude(velocity));
    }
    pm_store(&m,0x5450c0u+bank*4u,1500,4);pm_store(&m,0x5450c8u+bank*4u,100,4);pm_store(&m,0x5457d8u+bank*4u,100,4);
    for (i=0;i<10u;i++) {
        at=0x545148u+bank*840u+i*84u;pm_store(&m,at,pm_load(&m,0x5450d0u+i*12u,2),2);pm_store(&m,at+2u,pm_load(&m,0x5450d2u+i*12u,2),2);
        pm_store(&m,at+4u,pm_load(&m,0x5450d4u+i*12u,4),4);time=pm_load(&m,0x5450d8u+i*12u,4);pm_store(&m,at+12u,i,4);pm_store(&m,at+8u,time,4);
        for (j=0;j<3u;j++) pm_store(&m,at+73u+j,pm_load(&m,0x5316c0u+i*3u+j,1),1);
        pm_store(&m,at+68u,pm_load(&m,0x545d30u,4),4);for (j=0;j<8u;j++) pm_store(&m,at+76u+j,(j<3u || j==7u) ? 53u : 10u,1);
        if (m.error) return m.error;
        status=frame_light_allocate(read,write,ctx,at+20u,&id);if (status) return status;pm_store(&m,at+16u,id,4);
    }
    for (i=0;i<10u;i++) {
        at=0x5457e0u+bank*680u+i*68u;
        pm_store(&m,at+53u,pm_load(&m,0x5316e0u,1),1);pm_store(&m,at+54u,pm_load(&m,0x545d40u,1),1);pm_store(&m,at+55u,pm_load(&m,0x545d44u,1),1);
        pm_store(&m,at+48u,pm_load(&m,0x545d34u,4),4);for (j=0;j<8u;j++) pm_store(&m,at+56u+j,(j<3u || j==7u) ? 53u : 10u,1);
        if (m.error) return m.error;
        status=frame_light_allocate(read,write,ctx,at,&id);if (status) return status;pm_store(&m,at+64u,id,4);
    }
    if (m.error) return m.error;
    return frame_pickup_effect_project(read,write,ctx,target);
}
int frame_vehicle_debris_clear(frame_data_read read,frame_data_write write,void *ctx,uint32_t object)
{
    physics_memory m={read,write,ctx,0};uint32_t bank,i,at;
    if (!read || !write) return -1;
    if (!pm_load(&m,object+0xc92u,4)) return m.error;
    bank=pm_load(&m,object+0xde5u,1)*264u;
    for (i=0;i<8u;i++) { at=0x54647bu+bank+i*32u;if (pm_load(&m,at,1)) pm_store(&m,at,0,1); }
    pm_store(&m,0x546564u+bank,0,4);pm_store(&m,object+0xc92u,0,4);pm_store(&m,object+0xc96u,0,4);return m.error;
}
static void seek_random(physics_memory *m,uint32_t at,int32_t step,uint32_t mask,int32_t offset)
{
    int32_t value=pm_i32(m,at),target=pm_i32(m,at+4u);int change=0;
    if (value<target) { value=pm_add(value,step);pm_write32(m,at,value);if (target<=value) { pm_write32(m,at,target);change=1; } }
    else if (value>target) { value=pm_sub(value,step);pm_write32(m,at,value);if (value<=target) { pm_write32(m,at,target);change=1; } }
    else change=1;
    if (change) pm_write32(m,at+4u,pm_add((int32_t)((random_next(m)>>4)&mask),offset));
}
int frame_shield_orbit_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t object)
{
    physics_memory m={read,write,ctx,0};uint32_t bank,at,i,j,k,matrix,pitch,yaw;int32_t dt,value,v[3],rotated[3],sum,cp,sp,cy,sy;
    if (!read || !write) return -1;
    if (!pm_load(&m,object+0xe08u,1)) return m.error;
    bank=pm_load(&m,object+0xde5u,1);
    if (pm_load(&m,0x58e7acu,4) && !bank) bank=pm_load(&m,object+0x1135u,4);
    dt=pm_i32(&m,0x5904fcu);
    for (i=0;i<4u;i++) {
        at=0x58ee2cu+bank*160u+i*39u;
        pm_write32(&m,at-4u,pm_add(pm_i32(&m,at-4u),pm_trunc(pm_mul(dt,pm_i32(&m,at)),8)));
        seek_random(&m,at,pm_mul(dt,4),65535u,-32768);seek_random(&m,at-12u,pm_mul(dt,16),16383u,0);
        pitch=pm_load(&m,at-12u,2)&65532u;yaw=pm_load(&m,at-4u,2)&65532u;
        cp=pm_i32(&m,0x59b380u+pitch);sp=pm_i32(&m,0x597380u+pitch);cy=pm_i32(&m,0x59b380u+yaw);sy=pm_i32(&m,0x597380u+yaw);
        v[0]=pm_sar(pm_mul(pm_sar(pm_mul(cp,1500),12),cy),12);v[1]=pm_sar(pm_mul(sp,1500),12);v[2]=pm_sar(pm_mul(pm_sar(pm_mul(cp,1500),12),sy),12);
        matrix=pm_load(&m,object+0x14u,4);
        if (matrix) {
            for (j=0;j<3u;j++) { sum=0;for (k=0;k<3u;k++) sum=pm_add(sum,pm_mul(v[k],pm_i32(&m,matrix+(k*3u+j)*4u)));rotated[j]=pm_sar(sum,12); }
            for (j=0;j<3u;j++) v[j]=rotated[j];
        }
        for (j=0;j<3u;j++) pm_write32(&m,at+15u+j*4u,pm_add(v[j],pm_i32(&m,object+j*4u)));
        value=pm_i32(&m,at+8u);
        if (!pm_load(&m,at+12u,1)) {
            if (value<201) { pm_store(&m,at+8u,200,4);pm_store(&m,at+12u,1,1); }
            else pm_write32(&m,at+8u,pm_sub(value,pm_mul(dt,2)));
        } else if (value<500) pm_write32(&m,at+8u,pm_add(value,pm_mul(dt,2)));
        else { pm_store(&m,at+8u,500,4);pm_store(&m,at+12u,0,1); }
        pm_store(&m,at+13u,pm_load(&m,at+13u,2)+(uint32_t)pm_mul(dt,8),2);
    }
    return m.error;
}
static void retire_lights(physics_memory *m,uint32_t first,uint32_t stride,int gate)
{
    uint32_t i,at,id;
    if (!gate) return;
    for (i=0;i<10u;i++) {
        at=first+i*stride;id=pm_load(m,at,4);
        if (id!=0xfeb4024du) { if (m->error) return;m->error=frame_light_release(m->read,m->write,m->ctx,id);pm_store(m,at,0xfeb4024du,4); }
    }
}
int frame_vehicle_shared_fade(frame_data_read read,frame_data_write write,void *ctx,uint32_t object)
{
    physics_memory m={read,write,ctx,0};uint32_t bank,p,first,second,i,j,at,frame,index;int32_t dt,life,speed,v[3],value;
    if (!read || !write) return -1;
    for (bank=0;bank<2u;bank++) {
        p=0x58faa0u+bank*44u;first=0x545158u+bank*840u;second=0x545820u+bank*680u;
        if (pm_load(&m,p+4u,4)!=object || !pm_load(&m,p,1)) continue;
        frame=pm_load(&m,0x590564u,4);if (pm_load(&m,p+12u,4)==frame) return m.error;
        dt=pm_i32(&m,0x5904fcu);
        if (pm_load(&m,p+2u,1) || pm_load(&m,p+3u,1)) {
            for (i=0;i<3u;i++) v[i]=pm_sub(pm_i32(&m,object+i*4u),pm_i32(&m,p+28u+i*4u));
            if (m.error) return m.error;
            physics_vector_normalize(v);speed=pm_i32(&m,p+40u);
            for (i=0;i<3u;i++) v[i]=pm_add(pm_i32(&m,p+28u+i*4u),pm_sar(pm_mul(pm_sar(pm_mul(v[i],speed),12),dt),8));
            pm_write32(&m,p+32u,v[1]);pm_write32(&m,p+28u,v[0]);pm_write32(&m,p+36u,v[2]);
            value=pm_sub(speed,pm_mul(dt,128));pm_write32(&m,p+40u,value);if (value<0) pm_store(&m,p+40u,0,4);
        }
        pm_write32(&m,0x5450c8u+bank*4u,pm_add(pm_i32(&m,0x5450c8u+bank*4u),pm_sar(pm_mul(pm_i32(&m,0x531690u),dt),8)));
        pm_write32(&m,0x5450c0u+bank*4u,pm_add(pm_i32(&m,0x5450c0u+bank*4u),pm_sar(pm_mul(pm_i32(&m,0x531694u),dt),8)));
        pm_write32(&m,0x5457d8u+bank*4u,pm_add(pm_i32(&m,0x5457d8u+bank*4u),pm_sar(pm_mul(pm_i32(&m,0x545d38u),dt),8)));
        life=pm_i32(&m,p+8u);
        for (i=0;i<10u;i++) {
            at=first+i*84u;
            pm_store(&m,at-16u,pm_load(&m,at-16u,2)+(uint32_t)pm_sar(pm_mul(pm_i32(&m,at-12u),dt),8),2);
            pm_store(&m,at-14u,pm_load(&m,at-14u,2)+(uint32_t)pm_sar(pm_mul(pm_i32(&m,at-8u),dt),8),2);
            index=pm_load(&m,at-4u,4)*3u;
            for (j=0;j<3u;j++) pm_store(&m,at+57u+j,(uint32_t)pm_sar(pm_mul((int32_t)pm_load(&m,0x5316c0u+index+j,1),life),17),1);
            value=pm_add(pm_i32(&m,at-4u),1);pm_write32(&m,at-4u,value);if (value==10) pm_store(&m,at-4u,0,4);
        }
        retire_lights(&m,second,68u,pm_i32(&m,0x5450c8u+bank*4u)>25600 && pm_i32(&m,0x5450c0u+bank*4u)>25600);
        value=pm_sub(pm_i32(&m,p+8u),pm_mul(pm_i32(&m,0x5904fcu),192));pm_write32(&m,p+8u,value);
        if (value<1) { retire_lights(&m,first,84u,1);retire_lights(&m,second,68u,1);pm_store(&m,p,0,1); }
    }
    return m.error;
}
static void release_vehicle_light(physics_memory *m,uint32_t object,uint32_t side)
{
    uint32_t id;if (!pm_load(m,object+0xe18u+side,1)) return;
    id=pm_load(m,object+0xe1au+side*4u,4);if (m->error) return;
    m->error=frame_light_release(m->read,m->write,m->ctx,id);pm_store(m,object+0xe18u+side,0,1);
}
int frame_vehicle_lights_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t o)
{
    physics_memory m={read,write,ctx,0};uint32_t side,i,j,k,matrix,id,gray,slot;int32_t x1,x2,z,points[12],v[3],rotated[3],height,brake,throttle;unsigned char style[12]={1,128,0,0,1,1,31,1,31,31,1,31};int status,stop;
    if (!read || !write) return -1;
    if (!pm_load(&m,o+0xde5u,1)) stop=!pm_load(&m,o+0xdedu,1) || pm_load(&m,0x54b4b0u,4)!=1u || (pm_load(&m,o+0xd90u,1)&1u);
    else { slot=(uint32_t)pm_i8(&m,o+0xdf0u);stop=pm_load(&m,0x54b4b0u+slot*4u,4)!=1u; }
    if (stop) { release_vehicle_light(&m,o,0);release_vehicle_light(&m,o,1);return m.error; }
    brake=pm_i32(&m,o+0x98u);throttle=pm_i32(&m,o+0x94u);
    if ((!brake && pm_load(&m,o+0xe17u,1)) || (brake && !pm_load(&m,o+0xe17u,1)) || throttle>=0) { release_vehicle_light(&m,o,0);release_vehicle_light(&m,o,1); }
    if (pm_load(&m,o+0x10u,1)&32u) return m.error;
    if (!brake) { if (throttle>=0) return m.error;pm_store(&m,o+0xe17u,0,1);gray=128; }
    else if ((pm_i32(&m,o+0xecu)>1000 && !throttle) || (pm_load(&m,o+0x10bdu,1) && pm_i32(&m,o+0xecu)<-1000)) { pm_store(&m,o+0xe17u,1,1);gray=0; }
    else { release_vehicle_light(&m,o,0);release_vehicle_light(&m,o,1);return m.error; }
    for (side=0;side<2u;side++) {
        height=pm_add(pm_add(pm_i32(&m,o+0x212u),pm_i32(&m,o+0x140u)),pm_add(pm_i32(&m,o+0x186u),pm_i32(&m,o+0x1ccu)));
        if (pm_load(&m,o+0x716u+side*4u,4)!=1u || pm_sub(pm_i32(&m,o+0x7cu),pm_sar(height,2))>2999) { release_vehicle_light(&m,o,side);continue; }
        x1=pm_i32(&m,o+0x656u);x2=side ? pm_add(x1,325) : pm_add(pm_neg(x1),125);x1=side ? pm_sub(x1,125) : pm_sub(pm_neg(x1),325);z=pm_i32(&m,o+0x65eu);matrix=pm_load(&m,o+0x14u,4);
        for (i=0;i<4u;i++) {
            v[0]=(i==0u || i==3u) ? x2 : x1;v[1]=-1500;v[2]=i<2u ? z : pm_add(z,1200);
            if (matrix) { for (j=0;j<3u;j++) { rotated[j]=0;for (k=0;k<3u;k++) rotated[j]=pm_add(rotated[j],pm_mul(v[k],pm_i32(&m,matrix+(k*3u+j)*4u)));rotated[j]=pm_sar(rotated[j],12); }for (j=0;j<3u;j++) v[j]=rotated[j]; }
            for (j=0;j<3u;j++) points[(3u-i)*3u+j]=pm_add(v[j],pm_i32(&m,o+0x78u+j*4u));
        }
        if (m.error) return m.error;
        if (!pm_load(&m,o+0xe18u+side,1)) {
            style[2]=style[3]=(unsigned char)gray;
            id=pm_load(&m,0x5468a8u,4);if (m.error) return m.error;
            status=frame_light_allocate_values(read,write,ctx,points,id,style,&id);if (status) return status;
            pm_store(&m,o+0xe1au+side*4u,id,4);pm_store(&m,o+0xe18u+side,1,1);
        } else {
            id=pm_load(&m,o+0xe1au+side*4u,4);if (m.error) return m.error;
            status=frame_light_update_values(read,write,ctx,id,points);if (status) return status;
        }
    }
    return m.error;
}
int frame_vehicle_scene_inactive(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t i;int status;
    if (!read || !write || !trig) return -1;
    if (pm_load(&m,object+0xaa8u,4)) { for (i=0;i<8u;i++) pm_store(&m,object+0x9a8u+i*34u,0,2);pm_store(&m,object+0xaa8u,0,4); }
#define SCENE(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
    SCENE(frame_vehicle_shared_fade(read,write,ctx,object));SCENE(frame_vehicle_lights_tick(read,write,ctx,object));SCENE(frame_temp_effects_tick(read,write,ctx,object));
    if (!pm_load(&m,0x54b480u,4)) {
        SCENE(frame_vehicle_debris_clear(read,write,ctx,object));SCENE(physics_impact_particles_tick(read,write,ctx,object,trig));
        if (pm_load(&m,object+0x984u,4)) { for (i=0;i<15u;i++) pm_store(&m,object+0x7b0u+i*32u,0,2);pm_store(&m,object+0x984u,0,4); }
    }
    SCENE(frame_shield_orbit_tick(read,write,ctx,object));return m.error;
#undef SCENE
}
static uint32_t debris_color(physics_memory *m,uint32_t surface,uint32_t component)
{
    uint32_t value,mask=surface ? 16383u : 4095u,shift=surface ? 6u : 8u,factor=surface ? (component==0u ? 33u : component==1u ? 18u : 10u) : 30u;
    /* Original evaluates the always-positive range product using r2, then
     * obtains an independent r4 for its result. Preserve all four advances. */
    (void)random_next(m);(void)random_next(m);(void)random_next(m);value=random_next(m);
    return ((65535u-((value>>shift)&mask))*factor)>>16;
}
int frame_vehicle_debris_tick(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,uint32_t entity)
{
    physics_memory m={read,write,ctx,0};uint32_t bank,base,i,j,surface[2],other,record,seed,speed;int32_t dt,gravity,life,velocity[3],dot=0,value,floor;int active=0;
    if (!read || !write) return -1;
    bank=pm_load(&m,object+0xde5u,1)*264u;base=0x546460u+bank;dt=pm_i32(&m,0x5904fcu);
    if (pm_load(&m,object+0xc92u,4)) {
        gravity=pm_trunc(pm_mul(dt,5000),8);
        for (i=0;i<8u;i++) {
            record=base+i*32u;if (!pm_load(&m,record+27u,1)) continue;
            pm_write32(&m,record+4u,pm_sub(pm_i32(&m,record+4u),gravity));
            for (j=0;j<3u;j++) pm_write32(&m,record+12u+j*4u,pm_add(pm_i32(&m,record+12u+j*4u),pm_trunc(pm_mul(dt,pm_i32(&m,record+j*4u)),8)));
            if (pm_i32(&m,record+16u)<pm_i32(&m,base+256u)) pm_store(&m,record+27u,0,1);
            else active=1;
        }
        if (!active) { pm_store(&m,base+260u,0,4);pm_store(&m,object+0xc92u,0,4); }
    }
    for (i=0;i<3u;i++) velocity[i]=pm_i32(&m,object+0x50u+i*4u);
    if (m.error) return m.error;
    speed=(uint32_t)frame_collision_change_magnitude(velocity);life=pm_sub(pm_i32(&m,object+0xc96u),dt);pm_write32(&m,object+0xc96u,life);
    if (life<0) { pm_store(&m,object+0xc96u,0,4);life=0; }
    if (!pm_load(&m,object+0xdeeu,1) || life || (pm_load(&m,object+0xdefu,1) && (pm_load(&m,object+0xc92u,4) || pm_abs(pm_i32(&m,object+0x54u))<102401)) || speed<=102400u) goto finish;
    /*40F7B0 is an audio command builder with exactly one LCG step. */
    (void)random_next(&m);
    for (i=0;i<2u;i++) surface[i]=pm_load(&m,0x5321a0u+pm_load(&m,object+0x716u+i*4u,4),1)==1u;
    pm_store(&m,object+0xc92u,1,4);pm_store(&m,object+0xc96u,500,4);pm_write32(&m,object+0x118u,pm_add(pm_i32(&m,object+0x118u),100));
    /* Preserve camera-space half-plane arithmetic even with wrapped products. */
    for (i=0;i<3u;i++) dot=pm_add(dot,pm_mul(pm_sub(pm_i32(&m,object+i*4u),pm_mul(pm_i32(&m,0x74aee0u+i*4u),8)),pm_i32(&m,0x74af18u+i*4u)));
    if (pm_load(&m,object+0xde5u,1) || dot>=0) {
        other=pm_load(&m,base+260u,4);
        if (other) { other=pm_load(&m,pm_load(&m,0x55c694u,4)+other*8u,4);other=other ? pm_load(&m,other+4u,4) : 0;pm_store(&m,other+0xc92u,0,4); }
        floor=pm_mul(pm_i32(&m,object+0x27au),-8);pm_store(&m,object+0xc92u,1,4);pm_store(&m,base+260u,entity,4);pm_write32(&m,base+256u,floor);
        for (i=0;i<8u;i++) {
            record=base+i*32u;
            for (j=0;j<3u;j++) { seed=debris_color(&m,surface[i&1u],j);pm_store(&m,record+24u+j,seed,1); }
            seed=(random_next(&m)>>4)&2047u;pm_write32(&m,record+12u,pm_add((int32_t)seed-1024,pm_i32(&m,object+0x128u+(i&1u)*140u)));
            seed=(random_next(&m)>>4)&2047u;pm_write32(&m,record+20u,pm_add((int32_t)seed-1024,pm_i32(&m,object+0x130u+(i&1u)*140u)));
            pm_write32(&m,record+16u,pm_i32(&m,base+256u));
            value=pm_trunc(pm_i32(&m,object+0x50u),7);seed=(random_next(&m)>>4)&2047u;
            pm_write32(&m,record,pm_add(pm_add(value,(int32_t)seed-1024),pm_mul(pm_sub(pm_i32(&m,record+12u),pm_i32(&m,object)),2)));
            value=pm_trunc(pm_i32(&m,object+0x58u),7);seed=(random_next(&m)>>4)&2047u;
            pm_write32(&m,record+8u,pm_add(pm_add(value,(int32_t)seed-1024),pm_mul(pm_sub(pm_i32(&m,record+20u),pm_i32(&m,object+8u)),2)));
            value=pm_neg(pm_i32(&m,object+0x54u));value=value<0 ? pm_neg(pm_sar(pm_i32(&m,object+0x54u),7)) : pm_sar(value,7);seed=(random_next(&m)>>4)&1023u;
            pm_write32(&m,record+4u,pm_add(pm_add((int32_t)seed,pm_signed(speed>>8)),value));
            seed=random_next(&m);pm_store(&m,record+27u,1,1);pm_store(&m,record+28u,((seed>>4)&511u)+500u,4);
        }
    }
finish:
    pm_store(&m,object+0xdefu,pm_load(&m,object+0xdeeu,1),1);return m.error;
}
int frame_vehicle_track_spray(frame_data_read read,frame_data_write write,void *ctx,uint32_t o)
{
    physics_memory m={read,write,ctx,0};uint32_t p,i,j,k,side,cfg,spin,weather,valid;int32_t dt,free_slot=-1,type,value,magnitude,size,gain,v[3],rotated[3];
    if (!read || !write) return -1;
    dt=pm_i32(&m,0x5904fcu);pm_store(&m,o+0xaa8u,0,4);
    for (i=0;i<8u;i++) {
        p=o+0x990u+i*34u;
        if (pm_i16(&m,p+24u)<1) { free_slot=(int32_t)i;continue; }
        pm_store(&m,o+0xaa8u,pm_load(&m,o+0xaa8u,4)+1u,4);cfg=0x5321bfu+(uint32_t)pm_mul(pm_i8(&m,p+32u),23);spin=pm_load(&m,cfg,4);
        pm_write32(&m,p+12u,pm_trunc(pm_mul(pm_i32(&m,p+12u),940),10));pm_write32(&m,p+16u,pm_sub(pm_i32(&m,p+16u),100));pm_write32(&m,p+20u,pm_trunc(pm_mul(pm_i32(&m,p+20u),940),10));
        for (j=0;j<3u;j++) pm_write32(&m,p+j*4u,pm_add(pm_i32(&m,p+j*4u),pm_trunc(pm_mul(dt,pm_i32(&m,p+12u+j*4u)),8)));
        pm_store(&m,p+24u,pm_load(&m,p+24u,2)-(uint32_t)pm_mul(dt,6),2);pm_store(&m,p+26u,pm_load(&m,p+26u,2)+(uint32_t)pm_mul(dt,2),2);
        if (pm_i32(&m,p+4u)<pm_i16(&m,p+28u)) pm_write32(&m,p+4u,pm_i16(&m,p+28u));
        if (spin) { value=pm_mul(dt,pm_i16(&m,cfg));pm_store(&m,p+30u,pm_load(&m,p+30u,2)+(uint32_t)(pm_load(&m,p+33u,1) ? pm_neg(value) : value),2); }
    }
    value=pm_add(pm_i32(&m,o+0xaa0u),dt);pm_write32(&m,o+0xaa0u,value);
    if (free_slot<0 || value<40) return m.error;
    p=o+0x990u+(uint32_t)free_slot*34u;side=1u-pm_load(&m,o+0xaa4u,4);pm_store(&m,o+0xaa4u,side,4);
    type=pm_i8(&m,0x5321a0u+pm_load(&m,o+0x716u+side*4u,4));pm_store(&m,p+32u,(uint32_t)type,1);if (type==-1) return m.error;
    if (type==5) { weather=pm_load(&m,0x54b480u,4) ? (uint32_t)pm_i8(&m,o+0xdf0u) : 0u;if (pm_load(&m,0x54b4b0u+weather*4u,4)!=1u) return m.error; }
    magnitude=pm_sub(pm_abs(pm_i32(&m,o+0xa4u)),50000);if (magnitude<1) return m.error;
    valid=0;for (i=0;i<2u;i++) { value=pm_i16(&m,o+0x134u+side*140u+i*70u);if (value==0 || value==1) valid=1; }if (!valid) return m.error;
    cfg=(uint32_t)pm_mul(type,23);size=pm_i16(&m,0x5321b7u+cfg);
    for (i=0;i<3u;i++) pm_write32(&m,p+i*4u,side ? pm_sub(pm_i32(&m,p+i*4u),pm_trunc(pm_mul(size,pm_i32(&m,o+0x18u+i*4u)),12)) : pm_add(pm_i32(&m,p+i*4u),pm_trunc(pm_mul(size,pm_i32(&m,o+0x18u+i*4u)),12)));
    pm_store(&m,p,pm_load(&m,o+0x128u+side*140u,4),4);pm_store(&m,p+4u,pm_load(&m,o+0x12cu+side*140u,4),4);pm_store(&m,p+33u,side,1);pm_store(&m,p+8u,pm_load(&m,o+0x130u+side*140u,4),4);
    pm_store(&m,p+12u,0,4);pm_store(&m,p+16u,0,4);pm_write32(&m,p+20u,pm_neg(pm_trunc(pm_i32(&m,o+0xa4u),6)));
    for (i=0;i<3u;i++) v[i]=pm_i32(&m,p+12u+i*4u);
    for (i=0;i<3u;i++) { rotated[i]=0;for (k=0;k<3u;k++) rotated[i]=pm_add(rotated[i],pm_mul(v[k],pm_i32(&m,o+0x18u+(k*3u+i)*4u)));rotated[i]=pm_sar(rotated[i],12); }
    for (i=0;i<3u;i++) pm_write32(&m,p+12u+i*4u,rotated[i]);
    pm_write32(&m,p+12u,pm_add(pm_i32(&m,p+12u),pm_trunc(pm_i32(&m,o+0x50u),6)));
    gain=pm_trunc(pm_mul(magnitude,(int32_t)pm_load(&m,0x5321bbu+cfg,2)),pm_load(&m,0x5321bdu+cfg,1));pm_write32(&m,p+16u,pm_add(pm_i32(&m,p+16u),gain));
    pm_store(&m,p+24u,2048,2);pm_write32(&m,p+20u,pm_add(pm_i32(&m,p+20u),pm_trunc(pm_i32(&m,o+0x58u),6)));
    pm_store(&m,p+28u,pm_load(&m,p+4u,2),2);pm_store(&m,p+26u,pm_load(&m,0x5321b9u+cfg,2)+(uint32_t)gain,2);pm_store(&m,o+0xaa0u,0,4);return m.error;
}
static void smoke_velocity(physics_memory *m,uint32_t o,uint32_t p,int rising)
{
    uint32_t i;for (i=0;i<3u;i++) pm_write32(m,p+12u+i*4u,pm_add(pm_trunc(pm_i32(m,o+0x50u+i*4u),6),rising && i==1u ? 800 : 0));
}
static void smoke_position(physics_memory *m,uint32_t o,uint32_t p,uint32_t side)
{
    pm_store(m,p,pm_load(m,o+0x128u+side*140u,4),4);pm_store(m,p+4u,pm_load(m,o+0x12cu+side*140u,4),4);
    pm_store(m,p+30u,side ? 0u : 1u,1);pm_store(m,p+8u,pm_load(m,o+0x130u+side*140u,4),4);pm_store(m,p+31u,1,1);
}
static void smoke_damage_emit(physics_memory *m,uint32_t o,uint32_t p,uint32_t body)
{
    uint32_t source,index,i,j;int32_t v[3],result[3];
    smoke_velocity(m,o,p,1);pm_store(m,p+28u,random_next(m)>>4,2);pm_store(m,p+24u,2048,2);pm_store(m,p+26u,5600,2);
    index=pm_load(m,o+(body ? 0xdf4u : 0xdf3u),1);source=(body ? 0x532288u : 0x532240u)+index*12u;
    for (i=0;i<3u;i++) v[i]=pm_i32(m,source+i*4u);
    for (i=0;i<3u;i++) { result[i]=0;for (j=0;j<3u;j++) result[i]=pm_add(result[i],pm_mul(v[j],pm_i32(m,o+(body ? 0x2b2u : 0x31au)+(j*3u+i)*4u)));result[i]=pm_sar(result[i],12); }
    for (i=0;i<3u;i++) pm_write32(m,p+i*4u,result[i]);
    pm_write32(m,p,pm_add(pm_i32(m,p),pm_i32(m,o+(body ? 0x2a6u : 0x30eu))));pm_write32(m,p+4u,pm_add(pm_i32(m,p+4u),pm_i32(m,o+(body ? 0x2aau : 0x312u))));
    pm_store(m,p+31u,0,1);pm_write32(m,p+8u,pm_add(pm_i32(m,p+8u),pm_i32(m,o+(body ? 0x2aeu : 0x316u))));
}
int frame_vehicle_damage_smoke(frame_data_read read,frame_data_write write,void *ctx,uint32_t o,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};uint32_t i,p,side,body,damage[2],timer,contact;int32_t dt,ordinary=-1,slots[2]={-1,-1},vx,vz,value,life,magnitude,velocity[3];
    if (!read || !write) return -1;
    dt=pm_i32(&m,0x5904fcu);damage[0]=pm_load(&m,o+0xdf1u,1);damage[1]=pm_load(&m,o+0xdf2u,1);
    if (pm_i32(&m,o+0x980u)<pm_i32(&m,o+0xf0u)) pm_store(&m,o+0x980u,pm_load(&m,o+0xf0u,4),4);
    if (pm_load(&m,o+0x980u,4)) pm_write32(&m,o+0x980u,pm_damp(pm_i32(&m,o+0x980u),0x0a010320u,dt));
    pm_store(&m,o+0x984u,0,4);
    for (i=0;i<15u;i++) {
        p=o+0x798u+i*32u;
        if (pm_i16(&m,p+24u)<1) {
            if (damage[0] && slots[0]<0) slots[0]=(int32_t)i;
            else if (damage[1] && slots[1]<0) slots[1]=(int32_t)i;
            else if (ordinary<0) ordinary=(int32_t)i;
            continue;
        }
        pm_store(&m,o+0x984u,pm_load(&m,o+0x984u,4)+1u,4);vx=pm_trunc(pm_mul(pm_i32(&m,p+12u),940),10);vz=pm_trunc(pm_mul(pm_i32(&m,p+20u),940),10);
        pm_write32(&m,p,pm_add(pm_i32(&m,p),pm_trunc(pm_mul(vx,dt),8)));pm_write32(&m,p+4u,pm_add(pm_i32(&m,p+4u),pm_trunc(pm_mul(pm_add(pm_i32(&m,p+16u),20),dt),8)));pm_write32(&m,p+8u,pm_add(pm_i32(&m,p+8u),pm_trunc(pm_mul(vz,dt),8)));
        pm_write32(&m,p+16u,pm_add(pm_i32(&m,p+16u),20));pm_store(&m,p+24u,pm_load(&m,p+24u,2)-(uint32_t)pm_mul(dt,2),2);pm_write32(&m,p+12u,vx);pm_write32(&m,p+20u,vz);
        if (pm_load(&m,p+31u,1)) pm_store(&m,p+26u,pm_load(&m,p+26u,2)+(uint32_t)pm_mul(dt,15),2);
        value=pm_mul(dt,40);pm_store(&m,p+28u,pm_load(&m,p+28u,2)+(uint32_t)(pm_load(&m,p+30u,1) ? pm_neg(value) : value),2);
    }
    pm_write32(&m,o+0x978u,pm_add(pm_i32(&m,o+0x978u),dt));pm_write32(&m,o+0x98cu,pm_add(pm_i32(&m,o+0x98cu),dt));pm_write32(&m,o+0x988u,pm_add(pm_i32(&m,o+0x988u),dt));
    for (body=0;body<2u;body++) if (slots[body]>=0) {
        timer=o+(body ? 0x98cu : 0x988u);
        if (pm_i32(&m,timer)>=(damage[body]>8u ? 150 : 300)) { pm_store(&m,timer,0,4);smoke_damage_emit(&m,o,o+0x798u+(uint32_t)slots[body]*32u,body); }
        else if (ordinary<0) ordinary=slots[body];
    }
    if (ordinary<0 || pm_i32(&m,o+0x978u)<70 || (pm_load(&m,0x53e414u,4) && pm_load(&m,o+0xde5u,1))) return m.error;
    p=o+0x798u+(uint32_t)ordinary*32u;value=pm_i32(&m,o+0xc0u);
    if ((value<-35000 && pm_load(&m,o+0x716u,4)==1u) || (value>35000 && pm_load(&m,o+0x71au,4)==1u)) {
        side=value>35000;smoke_velocity(&m,o,p,0);life=pm_trunc(pm_sub(pm_abs(value),35000),5);pm_store(&m,p+24u,(uint32_t)life,2);pm_store(&m,p+28u,random_next(&m)>>4,2);
        if (pm_i16(&m,p+24u)>2048) pm_store(&m,p+24u,2048,2);
        pm_store(&m,p+26u,8000,2);pm_store(&m,o+0x978u,0,4);smoke_position(&m,o,p,side);
        if (!pm_load(&m,o+0xde5u,1) || (side ? value<130001 : value>-130001)) return m.error;
        for (i=0;i<3u;i++) velocity[i]=pm_i32(&m,o+0x50u+i*4u);
        if (m.error) return m.error;
        if (frame_collision_change_magnitude(velocity)<250001) return 0;
        if (!trig) return -5;
        contact=o+(side ? (pm_i32(&m,o+0xecu)<0 ? 0x1a8u : 0x1eeu) : (pm_i32(&m,o+0xecu)<0 ? 0x11cu : 0x162u));
        if (m.error) return m.error;
        return physics_impact_particles(read,write,ctx,o,contact,side ? 2 : 1,trig);
    }
    magnitude=pm_abs(pm_i32(&m,o+0xa0u));if (pm_i32(&m,o+0x980u)<4000001 && magnitude<1) return m.error;
    side=1u-pm_load(&m,o+0x97cu,4);pm_store(&m,o+0x97cu,side,4);if (pm_load(&m,o+0x716u+side*4u,4)!=1u) return m.error;
    smoke_velocity(&m,o,p,0);life=magnitude<1 ? pm_trunc(pm_sub(pm_i32(&m,o+0x980u),4000000),11) : pm_sar(magnitude,4);
    pm_store(&m,p+24u,(uint32_t)life,2);pm_store(&m,p+28u,random_next(&m)>>4,2);if (pm_i16(&m,p+24u)>2048) pm_store(&m,p+24u,2048,2);
    pm_store(&m,p+26u,8000,2);pm_store(&m,o+0x978u,0,4);smoke_position(&m,o,p,side);return m.error;
}
int frame_vehicle_track_marks(frame_data_read read,frame_data_write write,void *ctx,uint32_t o)
{
    physics_memory m={read,write,ctx,0};uint32_t side,i,id,p,old,eligible,valid,head,count,tail;int32_t dt,front,rear,offset[3],axis[3],points[12],previous[6],value;int status,emit;
    static const unsigned char style[12]={0,6,6,6,1,1,63,1,63,63,1,63};
    if (!read || !write) return -1;
    if (!pm_load(&m,o+0xde5u,1) || pm_load(&m,o+0xd8cu,4) || pm_load(&m,0x52ee9cu,4)==2u || pm_load(&m,0x52ee9cu,4)==3u) return m.error;
    if (pm_abs(pm_i32(&m,o+0xc0u))<80000 && pm_abs(pm_i32(&m,o+0xa0u))<25000) { pm_store(&m,o+0xc54u,0,1);pm_store(&m,o+0xc55u,0,1);return m.error; }
    dt=pm_i32(&m,0x5904fcu);
    for (i=0;i<3u;i++) pm_write32(&m,o+0xc86u+i*4u,pm_trunc(pm_mul(pm_i32(&m,o+0x50u+i*4u),dt),14));
    for (side=0;side<2u;side++) {
        old=pm_load(&m,o+0xc54u+side,1);if (pm_load(&m,o+0x716u+side*4u,4)!=1u) continue;
        front=pm_i32(&m,o+0x148u+side*140u);rear=pm_i32(&m,o+0x18eu+side*140u);eligible=front>rear ? front>=40001 : rear>=40001;
        valid=0;for (i=0;i<2u;i++) { value=pm_i16(&m,o+0x134u+side*140u+i*70u);if (value==0 || value==1) valid=1; }eligible=eligible && valid;
        emit=0;p=o+0xc56u+side*24u;
        if (eligible) {
            if (old) for (i=0;i<6u;i++) previous[i]=pm_i32(&m,p+i*4u);
            pm_store(&m,o+0xc54u+side,1,1);
            for (i=0;i<3u;i++) { axis[i]=pm_trunc(pm_mul(pm_i32(&m,o+0x3eau+side*104u+i*4u),200),10);offset[i]=pm_trunc(pm_i32(&m,o+0x402u+side*104u+i*4u),3); }
            for (i=0;i<3u;i++) pm_store(&m,p+12u+i*4u,pm_load(&m,o+0x16eu+side*140u+i*4u,4),4);
            if (pm_i32(&m,o+0x12cu+side*140u)<pm_i32(&m,o+0x172u+side*140u)) pm_store(&m,p+16u,pm_load(&m,o+0x12cu+side*140u,4),4);
            for (i=0;i<3u;i++) pm_write32(&m,p+12u+i*4u,pm_add(pm_add(pm_i32(&m,p+12u+i*4u),offset[i]),pm_i32(&m,o+0xc86u+i*4u)));
            for (i=0;i<3u;i++) pm_write32(&m,p+i*4u,side ? pm_sub(pm_i32(&m,p+12u+i*4u),axis[i]) : pm_add(pm_i32(&m,p+12u+i*4u),axis[i]));
            pm_store(&m,p+4u,pm_load(&m,p+16u,4),4);
            if (old) { for (i=0;i<6u;i++) points[i]=previous[i];for (i=0;i<3u;i++) { points[6u+i]=pm_i32(&m,p+12u+i*4u);points[9u+i]=pm_i32(&m,p+i*4u); }emit=1; }
        } else if (old) {
            pm_store(&m,o+0xc54u+side,0,1);
            for (i=0;i<6u;i++) points[i]=pm_i32(&m,p+i*4u);
            for (i=0;i<3u;i++) { points[6u+i]=pm_add(points[3u+i],pm_i32(&m,o+0xc86u+i*4u));points[9u+i]=pm_add(points[i],pm_i32(&m,o+0xc86u+i*4u)); }emit=1;
        }
        if (!emit) continue;
        if (pm_load(&m,o+0xc44u,4)==68u) {
            head=pm_load(&m,o+0xc4cu,4);id=pm_load(&m,o+0xaacu+head*6u,4);if (m.error) return m.error;
            status=frame_light_release(read,write,ctx,id);if (status) return status;
            head=pm_load(&m,o+0xc4cu,4)+1u;pm_store(&m,o+0xc4cu,head,4);if (head==68u) pm_store(&m,o+0xc4cu,0,4);
            pm_store(&m,o+0xc44u,pm_load(&m,o+0xc44u,4)-1u,4);
        }
        if (m.error) return m.error;
        status=frame_light_allocate_values(read,write,ctx,points,0,style,&id);if (status) return status;
        if (id!=UINT32_MAX) {
            tail=pm_load(&m,o+0xc50u,4);pm_store(&m,o+0xaacu+tail*6u,id,4);tail=pm_load(&m,o+0xc50u,4)+1u;pm_store(&m,o+0xc50u,tail,4);if (tail==68u) pm_store(&m,o+0xc50u,0,4);
            count=pm_load(&m,o+0xc44u,4);pm_store(&m,o+0xc44u,count+1u,4);
        }
    }
    return m.error;
}
int frame_vehicle_scene_active(frame_data_read read,frame_data_write write,void *ctx,uint32_t object,uint32_t entity,const physics_trig *trig)
{
    physics_memory m={read,write,ctx,0};int status;
    if (!read || !write || !trig) return -1;
#define SCENE(x) do { if (m.error) return m.error;status=(x);if (status) return status; } while (0)
    if (!pm_load(&m,0x53e414u,4) || !pm_load(&m,object+0xde5u,1)) { SCENE(frame_vehicle_track_spray(read,write,ctx,object));SCENE(frame_vehicle_track_marks(read,write,ctx,object)); }
    SCENE(frame_vehicle_shared_fade(read,write,ctx,object));
    if (!pm_load(&m,0x54b480u,4)) { SCENE(physics_impact_particles_tick(read,write,ctx,object,trig));SCENE(frame_vehicle_damage_smoke(read,write,ctx,object,trig));SCENE(frame_vehicle_debris_tick(read,write,ctx,object,entity)); }
    SCENE(frame_vehicle_lights_tick(read,write,ctx,object));SCENE(frame_temp_effects_tick(read,write,ctx,object));SCENE(frame_shield_orbit_tick(read,write,ctx,object));return m.error;
#undef SCENE
}
