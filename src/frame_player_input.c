#include "frame_player_input.h"
#include "frame_controls.h"
#include "physics_memory.h"

enum {
    KEY_BINDINGS=0x592000, BUTTON_BINDINGS=0x592900,
    DEVICE_ENABLED=0x53ccdc, ANALOG_LONGITUDINAL=0x53cce0,
    PLAYER_ANALOG=0x53cca8, TWIN_TRACK_MODE=0x53e404,
    STEER_ANALOG_LATCH=0x544fb8, DT=0x5904fc, START_STAGE=0x5904e0,
    GAME_MODE=0x52ee9c, PUSH_THROTTLE=0x530c54, PUSH_BRAKE=0x530c58,
    PUSH_STEER=0x530c5c, RETURN_STEER=0x530c68
};

static int32_t signed_word(uint32_t v)
{
    v&=65535u;
    return v<32768u ? (int32_t)v : (int32_t)v-65536;
}

static int key_one(physics_memory *m,uint32_t code)
{
    uint32_t value=0;
    int status;
    if (m->error) return 0;
    status=frame_control_key(m->read,m->ctx,code,&value);
    if (status) m->error=status;
    return value==1;
}

/* Keyboard is queried only for player0. Primary keycode0 is a real query;
 * only a zero alternate binding is skipped. The alternate table stride is
 * 100 bytes, and its button DATA code gains player*12 after lookup. */
static int binding_down(physics_memory *m,int32_t player,uint32_t action)
{
    uint32_t code;
    if (player!=0 || m->error) return 0;
    code=pm_load(m,KEY_BINDINGS+action*4u,4);
    if (key_one(m,code)) return 1;
    if (!pm_load(m,DEVICE_ENABLED,4)) return 0;
    code=pm_load(m,BUTTON_BINDINGS+(uint32_t)player*100u+action*4u,4);
    return code && key_one(m,code+(uint32_t)player*12u);
}

static int32_t axis_query(physics_memory *m,uint32_t argument)
{
    int32_t value=0;
    int status;
    if (m->error) return 0;
    status=frame_control_axis(m->read,m->ctx,argument,&value);
    if (status) m->error=status;
    return value;
}

/* Exact 441370: signed16 rate, wrapped product and signed comparisons. A
 * release always writes one value, even if the old value already was zero. */
static void release_control(physics_memory *m,uint32_t address,uint16_t rate)
{
    int32_t amount=pm_mul(signed_word(rate),pm_i32(m,DT));
    int32_t value=pm_i32(m,address);
    value=value>amount ? pm_sub(value,amount) :
          value<pm_neg(amount) ? pm_add(value,amount) : 0;
    pm_write32(m,address,value);
}

static void clamp_control(physics_memory *m,uint32_t address,int32_t low,int32_t high)
{
    if (pm_i32(m,address)<low) pm_write32(m,address,low);
    if (pm_i32(m,address)>high) pm_write32(m,address,high);
}

static void start_press(physics_memory *m,uint32_t object)
{
    int32_t stage=pm_i32(m,START_STAGE);
    if (stage==1) pm_store(m,object+0x1106u,1,1);
    else if (stage==2 && !pm_load(m,object+0x1106u,1)) pm_store(m,object+0x1107u,1,1);
}

static void countdown_input(physics_memory *m,uint32_t object,
                             const frame_player_input_rates *rates,int32_t player)
{
    int used_digital=0;
    int32_t value;
    if (pm_i32(m,START_STAGE)==4) return;
    if (binding_down(m,player,5)) {
        value=pm_mul(pm_i32(m,PUSH_THROTTLE),pm_i32(m,DT));
        pm_write32(m,object+0x90u,pm_add(pm_i32(m,object+0x90u),value));
        used_digital=1;
        start_press(m,object);
    } else {
        release_control(m,object+0x90u,rates->throttle_return);
        value=pm_i32(m,START_STAGE);
        if (value>0 && value<3 && !pm_load(m,PLAYER_ANALOG+(uint32_t)player*4u,4))
            pm_store(m,object+0x1106u,0,1);
    }
    if (binding_down(m,player,6)) {
        value=pm_mul(pm_i32(m,PUSH_BRAKE),pm_i32(m,DT));
        pm_write32(m,object+0x98u,pm_add(pm_i32(m,object+0x98u),value));
        used_digital=1;
    } else release_control(m,object+0x98u,rates->brake_return);
    if (pm_load(m,ANALOG_LONGITUDINAL,4) &&
        pm_load(m,PLAYER_ANALOG+(uint32_t)player*4u,4) && !used_digital) {
        value=axis_query(m,3);
        if (value) {
            pm_write32(m,object+0x90u,pm_mul(value,-2));
            if (value<0) {
                if (value<-16384) start_press(m,object);
                else {
                    value=pm_i32(m,START_STAGE);
                    if (value>0 && value<3) pm_store(m,object+0x1106u,0,1);
                }
            }
        }
    }
    /* Countdown clamps +90 only: stale brake/steer values are not repaired. */
    clamp_control(m,object+0x90u,0,65535);
}

static void twin_track_input(physics_memory *m,uint32_t object)
{
    int32_t left=pm_i32(m,0x53cc84u),right=pm_i32(m,0x53cc8cu);
    int32_t opposite=pm_neg(pm_add(left,right)),speed;
    pm_write32(m,object+0x9cu,pm_sub(right,left));
    pm_write32(m,object+0x94u,0);
    pm_write32(m,object+0x98u,0);
    speed=pm_i32(m,object+0xecu);
    if (opposite>0 && speed<0) pm_write32(m,object+0x98u,opposite);
    else if (opposite<=0 && speed>0) pm_write32(m,object+0x98u,pm_neg(opposite));
    else pm_write32(m,object+0x94u,opposite);
}

static void driving_input(physics_memory *m,uint32_t object,
                           const frame_player_input_rates *rates,int32_t player)
{
    int32_t speed,force,value;
    uint32_t throttle_slot=5,brake_slot=6;
    int reverse_key,throttle_key,brake_key;
    if (pm_load(m,PLAYER_ANALOG+(uint32_t)player*4u,4) && pm_load(m,TWIN_TRACK_MODE,4)) {
        twin_track_input(m,object);
        return;
    }
    reverse_key=binding_down(m,player,6);
    speed=pm_i32(m,object+0xecu);
    force=pm_i32(m,PUSH_THROTTLE);
    if ((reverse_key && speed==0) || speed<0) {
        throttle_slot=6;brake_slot=5;force=pm_neg(force);
    }
    throttle_key=binding_down(m,player,throttle_slot);
    if (throttle_key)
        pm_write32(m,object+0x94u,pm_add(pm_i32(m,object+0x94u),pm_mul(force,pm_i32(m,DT))));
    else release_control(m,object+0x94u,rates->throttle_return);
    brake_key=binding_down(m,player,brake_slot);
    if (brake_key)
        pm_write32(m,object+0x98u,pm_add(pm_i32(m,object+0x98u),pm_mul(pm_i32(m,PUSH_BRAKE),pm_i32(m,DT))));
    else release_control(m,object+0x98u,rates->brake_return);
    if (pm_load(m,ANALOG_LONGITUDINAL,4) &&
        pm_load(m,PLAYER_ANALOG+(uint32_t)player*4u,4) && !throttle_key && !brake_key) {
        value=axis_query(m,3);
        if (value) {
            speed=pm_i32(m,object+0xecu);
            if (speed>0 && value>0) pm_write32(m,object+0x98u,pm_mul(value,2));
            else if (speed<0 && value<0) pm_write32(m,object+0x98u,pm_mul(value,-2));
            else pm_write32(m,object+0x94u,pm_mul(value,-2));
        }
    }
    /* Always query the axis before the per-player analog gate. A disabled
     * global device returns zero, but an enabled malformed axis can still fail. */
    value=axis_query(m,0);
    if (pm_load(m,PLAYER_ANALOG+(uint32_t)player*4u,4)) {
        if (value) pm_store(m,STEER_ANALOG_LATCH,1,4);
        if (pm_load(m,STEER_ANALOG_LATCH,4)) pm_write32(m,object+0x9cu,pm_mul(value,2));
    }
    if (binding_down(m,player,1)) {
        value=pm_mul(pm_i32(m,PUSH_STEER),pm_i32(m,DT));
        pm_write32(m,object+0x9cu,pm_sub(pm_i32(m,object+0x9cu),value));
        pm_store(m,STEER_ANALOG_LATCH,0,4);
    } else if (binding_down(m,player,2)) {
        value=pm_mul(pm_i32(m,PUSH_STEER),pm_i32(m,DT));
        pm_write32(m,object+0x9cu,pm_add(pm_i32(m,object+0x9cu),value));
        pm_store(m,STEER_ANALOG_LATCH,0,4);
    } else if (!pm_load(m,STEER_ANALOG_LATCH,4)) {
        value=pm_i32(m,object+0x9cu);
        if (value>0) {
            value=pm_sub(value,pm_mul(pm_i32(m,RETURN_STEER),pm_i32(m,DT)));
            pm_write32(m,object+0x9cu,value);
            if (value<0) pm_write32(m,object+0x9cu,0);
        } else if (value<0) {
            value=pm_add(value,pm_mul(pm_i32(m,RETURN_STEER),pm_i32(m,DT)));
            pm_write32(m,object+0x9cu,value);
            if (value>0) pm_write32(m,object+0x9cu,0);
        }
    }
}

static void restore_overrides(physics_memory *m,uint32_t object)
{
    uint32_t kind=pm_load(m,object+0x716u,4),surface=0x546020u+kind*40u;
    int32_t current,target,delta;
    target=(int32_t)pm_load(m,surface,2);
    current=pm_i16(m,object+0x10b6u);
    if (current!=target) {
        current=signed_word((uint32_t)pm_add(current,pm_mul(pm_i32(m,DT),2)));
        pm_store(m,object+0x10b6u,(uint32_t)current,2);
        if (current>target) pm_store(m,object+0x10b6u,(uint32_t)target,2);
    }
    current=pm_i16(m,object+0x10b8u);target=pm_i16(m,object+0x6c2u);
    if (current!=target) {
        delta=signed_word((uint32_t)pm_sar(pm_i32(m,DT),2));
        if (current<target) {
            current=signed_word((uint32_t)pm_add(current,delta));
            pm_store(m,object+0x10b8u,(uint32_t)current,2);
            if (current>target) pm_store(m,object+0x10b8u,(uint32_t)target,2);
        } else {
            current=signed_word((uint32_t)pm_sub(current,delta));
            pm_store(m,object+0x10b8u,(uint32_t)current,2);
            if (current<target) pm_store(m,object+0x10b8u,(uint32_t)target,2);
        }
    }
    target=pm_i16(m,object+0x10bau);
    current=pm_i32(m,object+0x66au);
    if (current!=target) {
        current=pm_sub(current,pm_mul(pm_i32(m,DT),4));
        pm_write32(m,object+0x66au,current);
        if (current<target) pm_write32(m,object+0x66au,target);
    }
    if (pm_i16(m,object+0x10b6u)==(int32_t)pm_load(m,surface,2) &&
        pm_i16(m,object+0x10b8u)==pm_i16(m,object+0x6c2u) &&
        pm_i32(m,object+0x66au)==pm_i16(m,object+0x10bau)) {
        pm_store(m,object+0x10bcu,0,1);
        if (pm_i32(m,GAME_MODE)==4) pm_write32(m,object+0x10beu,2000);
    }
}

int frame_player_restore_overrides(frame_data_read read,frame_data_write write,
                                   void *ctx,uint32_t object)
{
    physics_memory m={read,write,ctx,0};
    if (!read || !write) return -1;
    restore_overrides(&m,object);
    return m.error;
}

int frame_player_input_step(frame_data_read read,frame_data_write write,void *ctx,
                            uint32_t object,const frame_player_input_rates *rates,
                            frame_player_input_result *out)
{
    physics_memory m={read,write,ctx,0};
    frame_player_input_result result={0,0};
    int32_t player;
    if (!read || !write || !rates || !out) return -1;
    result.driving=!!pm_load(&m,object+0xdeau,1);
    player=pm_i8(&m,object+0xdf0u);
    if (result.driving) {
        result.auxiliary_down=(uint32_t)binding_down(&m,player,15);
        driving_input(&m,object,rates,player);
        clamp_control(&m,object+0x94u,-65535,65535);
        clamp_control(&m,object+0x98u,0,65535);
        clamp_control(&m,object+0x9cu,-65535,65535);
    } else if (pm_i32(&m,GAME_MODE)==4) {
        pm_write32(&m,object+0x98u,65536);
        pm_write32(&m,object+0x9cu,0);
        pm_write32(&m,object+0x94u,0);
        if (pm_i32(&m,object+0x10c2u)<1) restore_overrides(&m,object);
    } else countdown_input(&m,object,rates,player);
    if (m.error) return m.error;
    *out=result;
    return 0;
}
