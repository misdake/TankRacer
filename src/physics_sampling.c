#include "physics_sampling.h"
#include "physics_suspension.h"

static int32_t s32(uint32_t x) { return x<=INT32_MAX ? (int32_t)x : (int32_t)((int64_t)x-INT64_C(4294967296)); }
static int32_t add(int32_t a,int32_t b) { return s32((uint32_t)a+(uint32_t)b); }
static int32_t sub(int32_t a,int32_t b) { return s32((uint32_t)a-(uint32_t)b); }
static int32_t mul(int32_t a,int32_t b) { return s32((uint32_t)a*(uint32_t)b); }
static int32_t sar(int32_t a,unsigned n) { uint32_t x=(uint32_t)a;n&=31u;return s32(a<0 ? ~(~x>>n) : x>>n); }
static int32_t trunc_shift(int32_t a,unsigned n) { return a<0 ? sub(0,sar(sub(0,a),n)) : sar(a,n); }
static int32_t short_signed(uint32_t a) { a&=65535u;return a>32767u ? (int32_t)a-65536 : (int32_t)a; }

static uint16_t spatial_noise(const uint16_t table[16],const int32_t world[3])
{
    int32_t a=world[0]<0 ? sub(0,world[0]) : world[0];
    int32_t b=world[2]<0 ? sub(0,world[2]) : world[2];
    return table[((uint32_t)sar(a,9)+(uint32_t)sar(b,9))&15u];
}
static int32_t disturbance(uint16_t noise,uint32_t multiplier,uint32_t shift)
{
    return trunc_shift(mul((int32_t)noise-127,s32(multiplier&65535u)),shift);
}
int physics_primary_sample_mutable(physics_world_read read,physics_world_write write,void *ctx,
                                   physics_primary_state *s,const physics_primary_environment *e)
{
    unsigned i,j;int32_t kinds[4],travel[4];
    s->load=0;s->airborne_count=0;s->special_count=0;s->flagged_count=0;
    s->has_extra=0;s->flagged_ground_count=0;
    for (i=0;i<4;++i) {
        physics_primary_point *p=&s->point[i];const physics_track_pose *t=&e->track[i>>1];
        physics_terrain_hit h={0};int status;int32_t height;uint16_t noise;
        for (j=0;j<3;++j)
            p->world[j]=add(sar(add(add(mul(t->matrix[j],p->local[0]),
                mul(t->matrix[3+j],p->local[1])),mul(t->matrix[6+j],p->local[2])),12),t->origin[j]);
        status=physics_terrain_query(read,ctx,e->root,p->world[0],p->world[2],&h);
        if (status<0) return status;
        if (!status) {
            p->gap=p->previous_gap;p->kind=3;s->surface[i]=0;s->node[i]=0;
            /* raw_height, flags and previous_gap deliberately retain old values. */
        } else {
            unsigned char coeff[3];uint32_t multiplier;
            s->surface[i]=e->force_surface ? 1 : (int32_t)h.surface;s->node[i]=h.node;
            p->raw_height=h.height;p->flags=h.flags_word&65535u;
            if (!read(ctx,e->surface_table+(uint32_t)s->surface[i]*40u,coeff,3)) return PHYSICS_TERRAIN_READ_ERROR;
            multiplier=(uint32_t)coeff[0]|((uint32_t)coeff[1]<<8);
            noise=spatial_noise(e->noise,p->world);
            height=add(h.height,disturbance(noise,multiplier,coeff[2]));
            if (e->effect_active) height=add(height,disturbance(noise,e->effect_multiplier,e->effect_shift));
            if (e->deformable) {
                physics_deform_result deformation;
                status=physics_deform_query(read,write,ctx,e->deformable,p->world,&deformation);
                if (status<0) return status;
                height=add(height,deformation.offset);
            }
            if (p->flags&4u) ++s->flagged_ground_count;
            if (h.special==1) ++s->special_count;
            if (p->flags&0x2000u) ++s->flagged_count;
            if (p->flags&0x10u) { s->has_extra=1;for (j=0;j<4;++j) s->extra[j]=h.extra[j]; }
            p->gap=sub(p->world[1],height);p->previous_gap=p->gap;
            p->kind=p->gap < -75 ? 0 : p->gap < 75 ? 1 : 2;
            if (p->kind==2) ++s->airborne_count;
        }
        kinds[i]=p->kind;travel[i]=p->travel;
    }
    s->load=physics_contact_load(kinds,travel,e->load_base,e->load_floor,e->load_multiplier,e->load_shift);
    return 0;
}
int physics_primary_sample(physics_world_read read,void *ctx,
                           physics_primary_state *s,const physics_primary_environment *e)
{
    return physics_primary_sample_mutable(read,0,ctx,s,e);
}

void physics_surface_effect_step(physics_surface_effect *s,int32_t dt,uint32_t frame)
{
    s->clear_player_slot=0;
    if (s->timer<=0) return;
    s->timer=short_signed((uint32_t)s->timer-(uint32_t)dt);
    s->seed=s->seed*UINT32_C(0x19660d)+UINT32_C(0x3c6ef35f);
    s->phase=add(s->phase,(int32_t)(((s->seed>>4)&65535u)%4000u)-2000);
    if (!(frame&3u)) { s->impulse0=sub(s->impulse0,100);s->impulse2=sub(s->impulse2,900); }
    s->pose=add(s->pose,(frame&1u) ? 200 : -200);
    if (s->timer<=0) { s->pose=s->restore;s->clear_player_slot=s->active!=0;s->active=0; }
}

int physics_contact_sample(physics_world_read read,physics_world_write write,void *ctx,
                           physics_sampling_state *s,const physics_sampling_environment *e)
{
    unsigned i;int status;physics_primary_environment primary=e->primary;
    primary.effect_active=s->effect.timer>0;
    status=physics_primary_sample_mutable(read,write,ctx,&s->primary,&primary);
    if (status<0) return status;
    physics_surface_effect_step(&s->effect,e->dt,e->frame);
    if (s->effect.clear_player_slot) {
        const unsigned char zero[4]={0,0,0,0};
        if (!write || !write(ctx,UINT32_C(0x58eebc)+(e->player&255u)*160u,zero,4)) return PHYSICS_TERRAIN_READ_ERROR;
    }
    s->fallback_mode=s->primary.flagged_ground_count==4 ? 2 : s->primary.flagged_ground_count==0 ? 1 : 0;
    for (i=0;i<2;++i) {
        if (s->primary.surface[0]==s->primary.surface[2]) s->materials[i]=s->primary.surface[0];
        else {
            status=physics_auxiliary_query(read,ctx,primary.root,e->auxiliary_local[i],primary.track[i].matrix,
                                            primary.track[i].origin,primary.force_surface,&s->auxiliary[i]);
            if (status<0) return status;
            s->materials[i]=s->auxiliary[i].surface;
        }
    }
    for (i=0;i<4;++i) if (s->primary.point[i].kind==3) {
        status=physics_fallback_query(read,ctx,primary.root,s->primary.point[i].world,
                                      s->fallback_mode,s->primary.point[i].normal);
        if (status<0) return status;
        if (status) s->primary.point[i].kind=4;
    }
    s->feedback_required=e->player!=0;
    return 0;
}

typedef struct auxiliary_context {
    physics_world_read read;void *ctx;physics_sampling_state *state;
    const physics_sampling_environment *env;
} auxiliary_context;
static int resolve_auxiliary(void *raw,unsigned side,int32_t *kind)
{
    auxiliary_context *c=(auxiliary_context *)raw;const physics_primary_environment *e=&c->env->primary;
    int status=physics_auxiliary_query(c->read,c->ctx,e->root,c->env->auxiliary_local[side],
        e->track[side].matrix,e->track[side].origin,e->force_surface,&c->state->auxiliary[side]);
    if (status<0) return status;
    *kind=c->state->auxiliary[side].kind;return 0;
}
int physics_contact_pipeline(physics_world_read read,physics_world_write write,void *ctx,
                             physics_sampling_state *sampling,physics_suspension_state *suspension,
                             int32_t pivot[3],const physics_sampling_environment *env,
                             const physics_contact_config *config,
                             const physics_suspension_environment *suspension_env,
                             const int32_t layout_config[5])
{
    unsigned i;int32_t kinds[4];int status=physics_contact_sample(read,write,ctx,sampling,env);
    auxiliary_context auxiliary={read,ctx,sampling,env};
    if (status<0) return status;
    for (i=0;i<4;++i) {
        kinds[i]=sampling->primary.point[i].kind;
        suspension->point[i].kind=kinds[i];suspension->point[i].gap=sampling->primary.point[i].gap;
        suspension->point[i].travel=sampling->primary.point[i].travel;
    }
    for (i=0;i<2;++i) {
        suspension->auxiliary_type[i]=sampling->auxiliary[i].kind;
        suspension->auxiliary_cached[i]=sampling->auxiliary[i].cached;
    }
    physics_contact_layout(kinds,layout_config,pivot);
    status=physics_suspension_step_resolved(suspension,config,suspension_env,resolve_auxiliary,&auxiliary);
    if (status<0) return status;
    for (i=0;i<4;++i) sampling->primary.point[i].travel=suspension->point[i].travel;
    for (i=0;i<2;++i) {
        sampling->auxiliary[i].kind=suspension->auxiliary_type[i];
        sampling->auxiliary[i].cached=suspension->auxiliary_cached[i];
    }
    return 0;
}
