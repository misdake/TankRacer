/* Fixed-word test protocol for the immutable C contact/suspension reference.
 * Input: op (1 contact / 2 resolved suspension / 3 helpers), then its words.
 * Output: SC01, followed by output words in the same record order. */
#include "physics_suspension.h"
#include <stdio.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

static int word(uint32_t *out)
{
    unsigned char b[4];
    if (fread(b,1,4,stdin)!=4) return 0;
    *out=(uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);
    return 1;
}
static void put(uint32_t value)
{
    unsigned char b[4];unsigned i;
    for (i=0;i<4;i++) b[i]=(unsigned char)(value>>(i*8));
    fwrite(b,1,4,stdout);
}
static int32_t signed_word(uint32_t value)
{return value<=INT32_MAX ? (int32_t)value : (int32_t)((int64_t)value-INT64_C(4294967296));}
static void config(const uint32_t *v,physics_contact_config *p)
{
    p->lower=signed_word(v[0]);p->upper=signed_word(v[1]);
    p->force_positive=v[2];p->force_negative=v[3];p->output_gain=v[4];p->tilt_gain=v[5];
    p->shape_shift=v[6];p->restore_positive=v[7];p->restore_negative=v[8];
    p->rate_damping=v[9];p->travel_dead_step=v[10];
}
static void state(const uint32_t *v,physics_suspension_state *s)
{
    unsigned i,k=0;
    for (i=0;i<4;i++) {
        physics_suspension_point *p=&s->point[i];
        p->kind=signed_word(v[k++]);p->gap=signed_word(v[k++]);p->corner=signed_word(v[k++]);
        p->travel=signed_word(v[k++]);p->rate=signed_word(v[k++]);p->output=signed_word(v[k++]);
    }
    s->angular0=signed_word(v[k++]);s->angular2=signed_word(v[k++]);
    s->tilt0=signed_word(v[k++]);s->tilt1=signed_word(v[k++]);
    s->penetrating_input_sum=signed_word(v[k++]);s->travel_sum=signed_word(v[k++]);
    s->vertical_delta=signed_word(v[k++]);s->world_vy=signed_word(v[k++]);
    s->position_y=signed_word(v[k++]);s->timer=signed_word(v[k++]);
    for (i=0;i<2;i++) s->auxiliary_type[i]=signed_word(v[k++]);
    for (i=0;i<2;i++) s->auxiliary_cached[i]=signed_word(v[k++]);
    s->max_penetration=signed_word(v[k++]);s->last_penetration=signed_word(v[k++]);
}
static void emit_state(const physics_suspension_state *s)
{
    unsigned i;
    for (i=0;i<4;i++) {
        const physics_suspension_point *p=&s->point[i];
        put((uint32_t)p->kind);put((uint32_t)p->gap);put((uint32_t)p->corner);
        put((uint32_t)p->travel);put((uint32_t)p->rate);put((uint32_t)p->output);
    }
    put((uint32_t)s->angular0);put((uint32_t)s->angular2);put((uint32_t)s->tilt0);put((uint32_t)s->tilt1);
    put((uint32_t)s->penetrating_input_sum);put((uint32_t)s->travel_sum);put((uint32_t)s->vertical_delta);
    put((uint32_t)s->world_vy);put((uint32_t)s->position_y);put((uint32_t)s->timer);
    for (i=0;i<2;i++) put((uint32_t)s->auxiliary_type[i]);
    for (i=0;i<2;i++) put((uint32_t)s->auxiliary_cached[i]);
    put((uint32_t)s->max_penetration);put((uint32_t)s->last_penetration);
}
static void environment(const uint32_t *v,physics_suspension_environment *e)
{
    unsigned k=0;
    e->dt=signed_word(v[k++]);e->environment_flag=signed_word(v[k++]);e->session_active=signed_word(v[k++]);
    e->mode=signed_word(v[k++]);e->is_player=signed_word(v[k++]);
    e->base_x=signed_word(v[k++]);e->base_z=signed_word(v[k++]);
    e->region_x0=signed_word(v[k++]);e->region_z0=signed_word(v[k++]);
    e->region_x1=signed_word(v[k++]);e->region_z1=signed_word(v[k++]);
    e->player_multiplier=v[k++];e->player_shift=v[k++];
    e->other_multiplier=v[k++];e->other_shift=v[k++];e->penetration_shift=v[k++];
    e->auxiliary_query_type[0]=signed_word(v[k++]);e->auxiliary_query_type[1]=signed_word(v[k++]);
}
typedef struct resolver {
    const physics_suspension_environment *env;
    int32_t fail_side;uint32_t write_on_failure,calls,trace;
} resolver;
static int resolve(void *ctx,unsigned side,int32_t *kind)
{
    resolver *r=(resolver *)ctx;r->calls++;r->trace=(r->trace<<4)|(side+1);
    if ((int32_t)side==r->fail_side) {
        if (r->write_on_failure) *kind=r->env->auxiliary_query_type[side];
        return -9;
    }
    *kind=r->env->auxiliary_query_type[side];return 0;
}
int main(void)
{
    uint32_t op,v[71],count,i;
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
    fwrite("SC01",1,4,stdout);
    while (word(&op)) {
        count=op==1 ? 20u : op==2 ? 71u : op==3 ? 19u : 0u;
        if (!count) return 2;
        for (i=0;i<count;i++) if (!word(&v[i])) return 3;
        if (op==1) {
            physics_contact_config p;physics_contact_state s;
            config(v,&p);s.travel=signed_word(v[11]);s.rate=signed_word(v[12]);
            s.output=signed_word(v[13]);s.tilt0=signed_word(v[14]);s.tilt1=signed_word(v[15]);
            physics_contact_step(&s,&p,signed_word(v[16]),signed_word(v[17]),v[18],signed_word(v[19]));
            put((uint32_t)s.travel);put((uint32_t)s.rate);put((uint32_t)s.output);
            put((uint32_t)s.tilt0);put((uint32_t)s.tilt1);
        } else if (op==2) {
            physics_contact_config p;physics_suspension_state s;physics_suspension_environment e;resolver r;int status;
            config(v,&p);state(v+11,&s);environment(v+51,&e);
            r.env=&e;r.fail_side=signed_word(v[69]);r.write_on_failure=v[70];r.calls=r.trace=0;
            status=physics_suspension_step_resolved(&s,&p,&e,resolve,&r);
            put((uint32_t)status);emit_state(&s);put(r.calls);put(r.trace);
        } else {
            int32_t kinds[4],travel[4],p[5],out[3];
            for (i=0;i<4;i++) {kinds[i]=signed_word(v[i]);travel[i]=signed_word(v[4+i]);}
            for (i=0;i<5;i++) p[i]=signed_word(v[8+i]);
            physics_contact_layout(kinds,p,out);for (i=0;i<3;i++) put((uint32_t)out[i]);
            put((uint32_t)physics_contact_load(kinds,travel,signed_word(v[13]),signed_word(v[14]),v[15],v[16]));
            put((uint32_t)physics_contact_agree(signed_word(v[17]),signed_word(v[18])));
        }
    }
    return ferror(stdin) || ferror(stdout) ? 4 : 0;
}
