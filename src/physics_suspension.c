#include "physics_suspension.h"

static int32_t s32(uint32_t value)
{
    return value <= INT32_MAX ? (int32_t)value : (int32_t)((int64_t)value - INT64_C(4294967296));
}
static int32_t add32(int32_t a, int32_t b) { return s32((uint32_t)a + (uint32_t)b); }
static int32_t sub32(int32_t a, int32_t b) { return s32((uint32_t)a - (uint32_t)b); }
static int32_t neg32(int32_t a) { return s32(0u - (uint32_t)a); }
static int32_t mul32(int32_t a, int32_t b) { return s32((uint32_t)a * (uint32_t)b); }
static int32_t sar32(int32_t a, unsigned n)
{
    uint32_t bits = (uint32_t)a;
    n &= 31u;
    return s32(a < 0 ? ~(~bits >> n) : bits >> n);
}
static int32_t shl32(int32_t a, unsigned n) { return s32((uint32_t)a << (n & 31u)); }
static int32_t trunc32(int32_t a, unsigned n)
{
    return a < 0 ? neg32(sar32(neg32(a), n)) : sar32(a, n);
}

int32_t physics_contact_agree(int32_t a, int32_t b)
{
    if (!a || !b || (a < 0 && b > 0) || (a > 0 && b < 0)) return 0;
    if (a > 0) return a < b ? a : b;
    return a > b ? a : b;
}

void physics_suspension_combine(physics_suspension_state *s)
{
    int32_t a = s->point[0].output, b = s->point[1].output;
    int32_t c = s->point[2].output, d = s->point[3].output;
    s->angular0 = sub32(s->angular0, s->tilt0);
    s->angular2 = sub32(s->angular2, s->tilt1);
    s->vertical_delta = add32(physics_contact_agree(add32(a,c), add32(b,d)),
                              physics_contact_agree(add32(a,b), add32(c,d)));
}

int physics_suspension_step_resolved(physics_suspension_state *s,const physics_contact_config *p,
                                     const physics_suspension_environment *e,
                                     physics_auxiliary_resolver resolve,void *ctx)
{
    int half = e->environment_flag != 0 && e->session_active == 0;
    int region = e->base_x >= e->region_x0 && e->base_x <= e->region_x1
              && e->base_z >= e->region_z0 && e->base_z <= e->region_z1;
    int modified = (e->mode == 5 && region) || half;
    unsigned i;
    s->tilt0 = s->tilt1 = s->penetrating_input_sum = s->travel_sum = 0;
    s->max_penetration = s->last_penetration = 0;
    for (i = 0; i < 4; ++i) {
        physics_suspension_point *q = &s->point[i];
        physics_contact_state point;
        int32_t force = 0;
        if (q->kind == 2) {
            if (modified) {
                uint32_t multiplier = e->is_player ? e->player_multiplier : e->other_multiplier;
                uint32_t shift = e->is_player ? e->player_shift : e->other_shift;
                force = neg32(mul32(sar32(mul32((int32_t)(multiplier & 65535u),4915200),shift),e->dt));
                if (half) force = sar32(force,1);
            } else force = mul32(-4915200,e->dt);
        }
        if (q->kind == 0) {
            int32_t penetration = sub32(-75,q->gap);
            int32_t travel = add32(q->travel,shl32(penetration,e->penetration_shift));
            q->travel = travel;
            if (travel > p->upper) {
                int32_t excess = sub32(travel,p->upper);
                s->last_penetration = trunc32(excess,e->penetration_shift);
                if (s->last_penetration > s->max_penetration) s->max_penetration = s->last_penetration;
                q->travel = p->upper;
            }
            if (penetration > 400) penetration = 400;
            force = add32(force,mul32(penetration,10240));
            s->penetrating_input_sum = add32(s->penetrating_input_sum,force);
        }
        point.travel=q->travel; point.rate=q->rate; point.output=q->output;
        point.tilt0=s->tilt0; point.tilt1=s->tilt1;
        if (force<=0 && !s->auxiliary_cached[i/2]) {
            if (resolve) {
                int status=resolve(ctx,i/2,&s->auxiliary_type[i/2]);
                if (status<0) return status;
            } else s->auxiliary_type[i/2]=e->auxiliary_query_type[i/2];
            s->auxiliary_cached[i/2]=1;
        }
        physics_contact_step(&point,p,force,e->dt,(uint32_t)q->corner,s->auxiliary_type[i/2]);
        q->travel=point.travel; q->rate=point.rate; q->output=point.output;
        s->tilt0=point.tilt0; s->tilt1=point.tilt1;
        s->travel_sum=add32(s->travel_sum,q->travel);
    }
    physics_suspension_combine(s);
    if (s->max_penetration > 0) {
        if (s->vertical_delta < 0) s->vertical_delta = 0;
        if (s->world_vy < 0) s->world_vy = trunc32(s->world_vy,1);
        /* The original uses LAST, not max, in this addition. */
        s->position_y=add32(s->position_y,add32(s->last_penetration,1));
    }
    if (s->timer > 0) s->timer=sub32(s->timer,e->dt);
    return 0;
}
void physics_suspension_step(physics_suspension_state *s,const physics_contact_config *p,
                            const physics_suspension_environment *e)
{
    (void)physics_suspension_step_resolved(s,p,e,0,0);
}

void physics_contact_layout(const int32_t kind[4], const int32_t p[5], int32_t out[3])
{
    int g0=kind[0]==0 || kind[0]==1, g1=kind[1]==0 || kind[1]==1;
    int g2=kind[2]==0 || kind[2]==1, g3=kind[3]==0 || kind[3]==1;
    int left=g0||g1, right=g2||g3, front=g0||g2, rear=g1||g3;
    out[0]=left&&!right ? p[0] : right&&!left ? p[3] : 0;
    out[1]=left||right ? p[1] : 0;
    out[2]=rear&&!front ? p[2] : front&&!rear ? p[4] : 0;
}

int32_t physics_contact_load(const int32_t kind[4],const int32_t travel[4],
                             int32_t base,int32_t floor,uint32_t multiplier,uint32_t shift)
{
    unsigned i;int32_t total=0;
    for (i=0;i<4;++i) if (kind[i]==0 || kind[i]==1) {
        int32_t value=add32(base,travel[i]);
        if (value>0) total=add32(total,trunc32(mul32(value,(int32_t)(multiplier&65535u)),shift));
        total=add32(total,floor);
    }
    return total;
}
