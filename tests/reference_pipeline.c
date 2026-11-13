/* Composed C oracle: real initialized world, explicit typed stage inputs.
 * The reserved tail is fixture storage only; no production guest arena is added. */
#include "game_host.h"
#include "physics_sampling.h"
#include "physics_vehicle.h"
#include "reference_io.h"

static unsigned char *ram;
static uint32_t clear_at,fail_clear;
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if (at>TANKRACER_DATA_BYTES || n>TANKRACER_DATA_BYTES-at) return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *data,uint32_t n)
{(void)ctx;if ((fail_clear&&at==clear_at)||at>TANKRACER_DATA_BYTES || n>TANKRACER_DATA_BYTES-at) return 0;memcpy(ram+at,data,n);return 1;}
static void vec(int32_t *out,unsigned n)
{unsigned i;for (i=0;i<n;i++) out[i]=oracle_signed(oracle_require());}
static void emit(const int32_t *values,unsigned n)
{unsigned i;for (i=0;i<n;i++) oracle_put((uint32_t)values[i]);}
static int start(const char *assets,const char *map_name)
{
    static const char *maps[]={"ENGLAND","MEXICO","SWAMP","EAST","THEME","BIO","MONACO","CIRCUIT"};
    const char *packs[]={"DATA","TANKS/BRONZE","TANKS/SILVER","TANKS/GOLD","INGAME",0,"COMMON"};
    char level[64],path[4096];unsigned i,map,cup;int status;
    for (map=0;map<8 && strcmp(map_name,maps[map]);map++) { }
    if (map==8) return 2;
    cup=map==7 ? 2 : map==3 || map==5 || map==6 ? 1 : 0;
    status=tank_create(map,cup,0,0,0);if (status) return 3;
    snprintf(level,sizeof(level),"LEVELS/%s",map_name);packs[5]=level;
    for (i=0;i<7;i++) {
        FILE *file;long n;unsigned char *bytes;
        int length=snprintf(path,sizeof(path),"%s/%s/FASTFILE.DAT",assets,packs[i]);
        if (length<0 || (size_t)length>=sizeof(path)) return 3;
        file=fopen(path,"rb");if (!file) return 3;
        if (fseek(file,0,SEEK_END) || (n=ftell(file))<0 || fseek(file,0,SEEK_SET)) {fclose(file);return 3;}
        bytes=(unsigned char *)malloc((size_t)n);if (!bytes) {fclose(file);return 3;}
        if (fread(bytes,1,(size_t)n,file)!=(size_t)n) {free(bytes);fclose(file);return 3;}
        fclose(file);status=tank_mount(i,bytes,(uint32_t)n);free(bytes);if (status) return 3;
    }
    if (tank_start()) return 4;
    ram=(unsigned char *)malloc(TANKRACER_DATA_BYTES);if (!ram) return 3;
    memcpy(ram,tank_data(),TANKRACER_DATA_BYTES);return 0;
}
static void load_sampling(physics_sampling_state *s)
{
    unsigned i;
    for (i=0;i<4;i++) {
        physics_primary_point *p=&s->primary.point[i];vec(p->local,3);vec(p->world,3);
        p->kind=oracle_signed(oracle_require());p->gap=oracle_signed(oracle_require());p->previous_gap=oracle_signed(oracle_require());
        p->raw_height=oracle_signed(oracle_require());p->travel=oracle_signed(oracle_require());p->flags=oracle_require();vec(p->normal,3);
    }
    s->primary.load=oracle_signed(oracle_require());s->primary.airborne_count=oracle_signed(oracle_require());
    s->primary.special_count=oracle_signed(oracle_require());s->primary.flagged_count=oracle_signed(oracle_require());s->primary.has_extra=oracle_signed(oracle_require());
    vec(s->primary.extra,4);vec(s->primary.surface,4);s->primary.flagged_ground_count=oracle_signed(oracle_require());
    for (i=0;i<4;i++) s->primary.node[i]=oracle_require();
    s->effect.timer=oracle_signed(oracle_require());s->effect.restore=oracle_signed(oracle_require());s->effect.active=oracle_signed(oracle_require());
    s->effect.phase=oracle_signed(oracle_require());s->effect.impulse0=oracle_signed(oracle_require());s->effect.impulse2=oracle_signed(oracle_require());
    s->effect.pose=oracle_signed(oracle_require());s->effect.seed=oracle_require();s->effect.clear_player_slot=oracle_signed(oracle_require());
    for (i=0;i<2;i++) {
        vec(s->auxiliary[i].world,3);s->auxiliary[i].kind=oracle_signed(oracle_require());
        s->auxiliary[i].cached=oracle_signed(oracle_require());s->auxiliary[i].surface=oracle_signed(oracle_require());
    }
    vec(s->materials,2);s->fallback_mode=oracle_signed(oracle_require());s->feedback_required=oracle_signed(oracle_require());
}
static void emit_sampling(const physics_sampling_state *s)
{
    unsigned i;
    for (i=0;i<4;i++) {
        const physics_primary_point *p=&s->primary.point[i];emit(p->local,3);emit(p->world,3);
        oracle_put((uint32_t)p->kind);oracle_put((uint32_t)p->gap);oracle_put((uint32_t)p->previous_gap);
        oracle_put((uint32_t)p->raw_height);oracle_put((uint32_t)p->travel);oracle_put(p->flags);emit(p->normal,3);
    }
    oracle_put((uint32_t)s->primary.load);oracle_put((uint32_t)s->primary.airborne_count);
    oracle_put((uint32_t)s->primary.special_count);oracle_put((uint32_t)s->primary.flagged_count);oracle_put((uint32_t)s->primary.has_extra);
    emit(s->primary.extra,4);emit(s->primary.surface,4);oracle_put((uint32_t)s->primary.flagged_ground_count);
    for (i=0;i<4;i++) oracle_put(s->primary.node[i]);
    oracle_put((uint32_t)s->effect.timer);oracle_put((uint32_t)s->effect.restore);oracle_put((uint32_t)s->effect.active);
    oracle_put((uint32_t)s->effect.phase);oracle_put((uint32_t)s->effect.impulse0);oracle_put((uint32_t)s->effect.impulse2);
    oracle_put((uint32_t)s->effect.pose);oracle_put(s->effect.seed);oracle_put((uint32_t)s->effect.clear_player_slot);
    for (i=0;i<2;i++) {
        emit(s->auxiliary[i].world,3);oracle_put((uint32_t)s->auxiliary[i].kind);
        oracle_put((uint32_t)s->auxiliary[i].cached);oracle_put((uint32_t)s->auxiliary[i].surface);
    }
    emit(s->materials,2);oracle_put((uint32_t)s->fallback_mode);oracle_put((uint32_t)s->feedback_required);
}
static void load_response(physics_suspension_state *s)
{
    unsigned i;
    for (i=0;i<4;i++) {
        physics_suspension_point *p=&s->point[i];p->kind=oracle_signed(oracle_require());p->gap=oracle_signed(oracle_require());
        p->corner=oracle_signed(oracle_require());p->travel=oracle_signed(oracle_require());p->rate=oracle_signed(oracle_require());p->output=oracle_signed(oracle_require());
    }
    s->angular0=oracle_signed(oracle_require());s->angular2=oracle_signed(oracle_require());s->tilt0=oracle_signed(oracle_require());s->tilt1=oracle_signed(oracle_require());
    s->penetrating_input_sum=oracle_signed(oracle_require());s->travel_sum=oracle_signed(oracle_require());s->vertical_delta=oracle_signed(oracle_require());
    s->world_vy=oracle_signed(oracle_require());s->position_y=oracle_signed(oracle_require());s->timer=oracle_signed(oracle_require());
    vec(s->auxiliary_type,2);vec(s->auxiliary_cached,2);s->max_penetration=oracle_signed(oracle_require());s->last_penetration=oracle_signed(oracle_require());
}
static void emit_response(const physics_suspension_state *s)
{
    unsigned i;for (i=0;i<4;i++) {
        const physics_suspension_point *p=&s->point[i];oracle_put((uint32_t)p->kind);oracle_put((uint32_t)p->gap);oracle_put((uint32_t)p->corner);
        oracle_put((uint32_t)p->travel);oracle_put((uint32_t)p->rate);oracle_put((uint32_t)p->output);
    }
    oracle_put((uint32_t)s->angular0);oracle_put((uint32_t)s->angular2);oracle_put((uint32_t)s->tilt0);oracle_put((uint32_t)s->tilt1);
    oracle_put((uint32_t)s->penetrating_input_sum);oracle_put((uint32_t)s->travel_sum);oracle_put((uint32_t)s->vertical_delta);
    oracle_put((uint32_t)s->world_vy);oracle_put((uint32_t)s->position_y);oracle_put((uint32_t)s->timer);
    emit(s->auxiliary_type,2);emit(s->auxiliary_cached,2);oracle_put((uint32_t)s->max_penetration);oracle_put((uint32_t)s->last_penetration);
}
static void load_environment(physics_sampling_environment *e,physics_suspension_environment *se)
{
    unsigned i;
    for (i=0;i<2;i++) {vec(e->primary.track[i].matrix,9);vec(e->primary.track[i].origin,3);}
    e->primary.root=oracle_get(ram+0x55ae60,4);e->primary.surface_table=0x702000;
    for (i=0;i<16;i++) e->primary.noise[i]=(uint16_t)oracle_require();
    e->primary.force_surface=oracle_signed(oracle_require());e->primary.effect_multiplier=oracle_require();e->primary.effect_shift=oracle_require();
    e->primary.load_base=oracle_signed(oracle_require());e->primary.load_floor=oracle_signed(oracle_require());e->primary.load_multiplier=oracle_require();e->primary.load_shift=oracle_require();
    for (i=0;i<2;i++) vec(e->auxiliary_local[i],3);
    e->dt=oracle_signed(oracle_require());e->frame=oracle_require();e->player=oracle_require();
    se->dt=oracle_signed(oracle_require());se->environment_flag=oracle_signed(oracle_require());se->session_active=oracle_signed(oracle_require());
    se->mode=oracle_signed(oracle_require());se->is_player=oracle_signed(oracle_require());
    se->base_x=oracle_signed(oracle_require());se->base_z=oracle_signed(oracle_require());
    se->region_x0=oracle_signed(oracle_require());se->region_z0=oracle_signed(oracle_require());se->region_x1=oracle_signed(oracle_require());se->region_z1=oracle_signed(oracle_require());
    se->player_multiplier=oracle_require();se->player_shift=oracle_require();se->other_multiplier=oracle_require();se->other_shift=oracle_require();se->penetration_shift=oracle_require();
    vec(se->auxiliary_query_type,2);
}
static uint32_t load_deform(void)
{
    uint32_t object=0x700000,mesh=0x700200,vertices=0x700300,records=0x701000,i,j,n,count;
    memset(ram+object,0,0x2000);
    for (i=0;i<3;i++) oracle_set(ram+object+i*4,oracle_require(),4);
    for (i=0;i<9;i++) oracle_set(ram+object+0x18+i*4,oracle_require(),4);
    for (i=0;i<4;i++) oracle_set(ram+object+0xba+i*4,oracle_require(),4);
    for (i=0;i<4;i++) oracle_set(ram+object+(i<2 ? (i ? 0x6c : 0x60) : (i==2 ? 0x68 : 0x74)),oracle_require(),4);
    oracle_set(ram+object+0xb6,oracle_require(),4);oracle_set(ram+object+0xcc,oracle_require(),1);
    n=oracle_require();if (n>256) exit(2);
    oracle_set(ram+object+0x48,mesh,4);oracle_set(ram+mesh+4,vertices,4);oracle_set(ram+mesh,n,2);
    for (i=0;i<n;i++) for (j=0;j<3;j++) oracle_set(ram+vertices+i*8+j*2,oracle_require(),2);
    count=oracle_require();if (count>64) exit(2);
    oracle_set(ram+object+0xcd,count,4);oracle_set(ram+object+0xd5,count ? records : 0,4);
    for (i=0;i<count;i++) {
        for (j=0;j<3;j++) oracle_set(ram+records+i*32+j*4,oracle_require(),4);
        oracle_set(ram+records+i*32+0x18,oracle_require(),4);
    }
    return object;
}
static void put_vec(uint32_t at,const int32_t *v,unsigned n)
{unsigned i;for(i=0;i<n;i++)oracle_set(ram+at+i*4u,(uint32_t)v[i],4);}
static void value(uint32_t at,unsigned width)
{uint32_t v=oracle_get(ram+at,width);oracle_put(width==2&&v>32767?v|0xffff0000u:v);}
static int vehicle_contact(physics_sampling_state *s,physics_suspension_state *r,int32_t pivot[3],
    physics_sampling_environment *e,const physics_contact_config *c,physics_suspension_environment *se,const int32_t layout[5])
{
    uint32_t o=0x704000u,feedback=0x7182u;unsigned i;int status;
    static const uint32_t response_at[]={0xb4,0xbc,0xd0,0xd4,0xf0,0xf8,0xc8,0x54,0x7c,0xd14};
    int32_t response[]={r->angular0,r->angular2,r->tilt0,r->tilt1,r->penetrating_input_sum,r->travel_sum,r->vertical_delta,r->world_vy,r->position_y,r->timer};
    uint32_t config[]={ (uint32_t)c->lower,(uint32_t)c->upper,(uint32_t)c->force_positive,(uint32_t)c->force_negative,(uint32_t)c->output_gain,(uint32_t)c->tilt_gain,(uint32_t)c->shape_shift,(uint32_t)c->restore_positive,(uint32_t)c->restore_negative,(uint32_t)c->rate_damping,(uint32_t)c->travel_dead_step };
    static const uint32_t config_at[]={0x64a,0x64e,0x6d2,0x6d6,0x6da,0x6de,0x6e2,0x6e6,0x6ea,0x6ee,0x6f2},layout_at[]={0x632,0x636,0x63a,0x63e,0x646};
    memset(ram+o,0,0x1200);
    for(i=0;i<4;i++){
        uint32_t a=o+0x11c+i*70u;physics_primary_point *p=&s->primary.point[i];physics_suspension_point *q=&r->point[i];
        put_vec(a,p->local,3);put_vec(a+12,p->world,3);oracle_set(ram+a+24,(uint32_t)p->kind,2);
        oracle_set(ram+a+28,(uint32_t)p->gap,4);oracle_set(ram+a+32,(uint32_t)p->previous_gap,4);oracle_set(ram+a+36,(uint32_t)p->raw_height,4);oracle_set(ram+a+40,(uint32_t)q->corner,4);
        oracle_set(ram+a+44,(uint32_t)p->travel,4);oracle_set(ram+a+48,(uint32_t)q->rate,4);oracle_set(ram+a+52,(uint32_t)q->output,4);put_vec(a+56,p->normal,3);oracle_set(ram+a+68,p->flags,2);
    }
    for(i=0;i<10;i++)oracle_set(ram+o+response_at[i],(uint32_t)response[i],4);
    for(i=0;i<11;i++)oracle_set(ram+o+config_at[i],config[i],4);
    for(i=0;i<5;i++)oracle_set(ram+o+layout_at[i],(uint32_t)layout[i],4);
    oracle_set(ram+o+0xf4,(uint32_t)s->primary.load,4);oracle_set(ram+o+0xde1,(uint32_t)s->primary.airborne_count,1);oracle_set(ram+o+0xde2,(uint32_t)s->primary.special_count,1);oracle_set(ram+o+0xde3,(uint32_t)s->primary.flagged_count,1);oracle_set(ram+o+0xdee,(uint32_t)s->primary.has_extra,1);put_vec(o+0x26e,s->primary.extra,4);
    oracle_set(ram+o+0xe0b,(uint32_t)s->effect.timer,2);oracle_set(ram+o+0xe0d,(uint32_t)s->effect.restore,2);oracle_set(ram+o+0xe08,(uint32_t)s->effect.active,1);oracle_set(ram+o+0xe0f,(uint32_t)s->effect.phase,4);oracle_set(ram+o+0x768,(uint32_t)s->effect.impulse0,4);oracle_set(ram+o+0x770,(uint32_t)s->effect.impulse2,4);oracle_set(ram+o+0x30a,(uint32_t)s->effect.pose,4);oracle_set(ram+0x55b6ac,s->effect.seed,4);
    for(i=0;i<2;i++){
        put_vec(o+0x3eau+i*104u,e->primary.track[i].matrix,9);put_vec(o+0x3deu+i*104u,e->primary.track[i].origin,3);
        put_vec(o+0x234+i*29u,e->auxiliary_local[i],3);put_vec(o+0x240+i*29u,s->auxiliary[i].world,3);oracle_set(ram+o+0x24c+i*29u,(uint32_t)s->auxiliary[i].kind,4);oracle_set(ram+o+0x250+i*29u,(uint32_t)s->auxiliary[i].cached,1);
    }
    put_vec(o+0x716,s->materials,2);put_vec(o+0xd8,pivot,3);
    for(i=0;i<16;i++)oracle_set(ram+0x546748+i*2u,e->primary.noise[i],2);
    for(i=0;i<10;i++)oracle_set(ram+0x54603c+i*40u,oracle_get(ram+e->primary.surface_table+i*40u,4),4);
    oracle_set(ram+o+0xde5,e->player,1);oracle_set(ram+0x5904fc,(uint32_t)e->dt,4);oracle_set(ram+0x590564,e->frame,4);oracle_set(ram+0x53e414,(uint32_t)e->primary.force_surface,4);
    oracle_set(ram+0x531b9c,e->primary.effect_multiplier,2);oracle_set(ram+0x531b9e,e->primary.effect_shift,1);
    oracle_set(ram+o+0x69a,(uint32_t)e->primary.load_base,4);oracle_set(ram+o+0x69e,(uint32_t)e->primary.load_floor,4);oracle_set(ram+o+0x6a2,e->primary.load_multiplier,2);oracle_set(ram+o+0x6a4,e->primary.load_shift,1);
    if(e->primary.deformable){oracle_set(ram+o+0xd8c,1,4);oracle_set(ram+0x55c694,0x706000,4);oracle_set(ram+0x706008,0x706800,4);oracle_set(ram+0x706804,e->primary.deformable,4);}
    oracle_set(ram+0x546000,(uint32_t)se->environment_flag,4);oracle_set(ram+0x58e7ac,(uint32_t)se->session_active,4);oracle_set(ram+0x52ed90,(uint32_t)se->mode,4);
    oracle_set(ram+o,(uint32_t)se->base_x,4);oracle_set(ram+o+8,(uint32_t)se->base_z,4);oracle_set(ram+0x531be8,(uint32_t)se->region_x0,4);oracle_set(ram+0x531bf0,(uint32_t)se->region_z0,4);oracle_set(ram+0x531bf8,(uint32_t)se->region_x1,4);oracle_set(ram+0x531c00,(uint32_t)se->region_z1,4);
    oracle_set(ram+0x531948,se->player_multiplier,4);oracle_set(ram+0x53194c,se->player_shift,1);oracle_set(ram+0x531950,se->other_multiplier,4);oracle_set(ram+0x531954,se->other_shift,1);oracle_set(ram+o+0x700,se->penetration_shift,1);
    fail_clear=oracle_require();status=physics_vehicle_contact(read_data,write_data,0,o,&feedback);oracle_put((uint32_t)status);
    for(i=0;i<4;i++){
        uint32_t a=o+0x11c+i*70u;unsigned j;for(j=0;j<6;j++)value(a+j*4,4);value(a+24,2);
        for(j=0;j<7;j++)value(a+28+j*4,4);
        for(j=0;j<3;j++)value(a+56+j*4,4);
        oracle_put(oracle_get(ram+a+68,2));
    }
    value(o+0xf4,4);value(o+0xde1,1);value(o+0xde2,1);value(o+0xde3,1);value(o+0xdee,1);
    for(i=0;i<4;i++)value(o+0x26eu+i*4,4);
    for(i=0;i<10;i++)value(o+response_at[i],4);
    value(o+0xe0b,2);value(o+0xe0d,2);value(o+0xe08,1);value(o+0xe0f,4);value(o+0x768,4);value(o+0x770,4);value(o+0x30a,4);value(0x55b6ac,4);
    for(i=0;i<2;i++){unsigned j;for(j=0;j<4;j++)value(o+0x240+i*29u+j*4,4);value(o+0x250+i*29u,1);}
    value(o+0x716,4);value(o+0x71a,4);for(i=0;i<3;i++)value(o+0xd8+i*4,4);oracle_put(feedback);
    return status;
}
int main(int argc,char **argv)
{
    uint32_t op,i;int init;if (argc!=3) return 2;oracle_binary();init=start(argv[1],argv[2]);if (init) return init;
    fwrite("CP01",1,4,stdout);oracle_put(0x5dab54);
    while (oracle_word(&op)) {
        physics_sampling_state s={0};physics_suspension_state response={0};physics_sampling_environment e={0};
        physics_suspension_environment se={0};physics_contact_config c;int32_t pivot[3],layout[5];int status;uint32_t deform,patch,old=0;
        if (op!=1 && op!=2 && op!=3) return 2;
        patch=oracle_require();if (patch) {
            if (patch<0x5dab54 || patch>=0x66c354) return 2;
            old=oracle_get(ram+patch,2);oracle_set(ram+patch,0,2);
        }
        load_sampling(&s);load_response(&response);vec(pivot,3);load_environment(&e,&se);
        for (i=0;i<10;i++) oracle_set(ram+e.primary.surface_table+i*40,oracle_require(),4);
        c.lower=oracle_signed(oracle_require());c.upper=oracle_signed(oracle_require());
        c.force_positive=oracle_require();c.force_negative=oracle_require();c.output_gain=oracle_require();c.tilt_gain=oracle_require();
        c.shape_shift=oracle_require();c.restore_positive=oracle_require();c.restore_negative=oracle_require();c.rate_damping=oracle_require();c.travel_dead_step=oracle_require();
        vec(layout,5);deform=oracle_require();if (deform) e.primary.deformable=load_deform();
        clear_at=0x58eebc+(e.player&255u)*160u;fail_clear=0;oracle_set(ram+clear_at,0x12345678,4);
        if(op==3)status=vehicle_contact(&s,&response,pivot,&e,&c,&se,layout);
        else{
        status=op==1 ? physics_contact_pipeline(read_data,write_data,0,&s,&response,pivot,&e,&c,&se,layout)
                     : physics_contact_sample(read_data,write_data,0,&s,&e);
        oracle_put((uint32_t)status);emit_sampling(&s);emit_response(&response);emit(pivot,3);
        }
        oracle_put(oracle_get(ram+0x58eebc+(e.player&255u)*160u,4));
        if (deform) {
            uint32_t count=oracle_get(ram+0x7000cd,4);oracle_put(ram[0x7000cc]);
            for (i=0;i<count;i++) oracle_put(oracle_get(ram+0x701018+i*32,4));
        }
        if (patch) oracle_set(ram+patch,old,2);
    }
    free(ram);tank_destroy();return ferror(stdin) ? 5 : 0;
}
