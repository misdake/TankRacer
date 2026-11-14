//! Unattached scenery explosions. The same entity/scheduler and RNG as gameplay
//! own the animation and ten smoke puffs; collision-attached/blast variants are
//! separate pending bindings, never silently approximated here.
use crate::{fixed, object_query::EntityId, random};

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Sprite {
    pub material: u32,
    pub position: [i32; 3],
    pub flags: u32,
    pub blend: i32,
    pub size: [i32; 2],
    pub uv: [u8; 4],
    pub color: [u8; 3],
    pub rotation: u16,
}
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Smoke {
    pub sprite: Sprite,
    pub velocity: [i32; 3],
    pub lift: i32,
    pub angular_velocity: i32,
    pub active: u16,
}
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct State {
    pub slot: i16,
    pub sprite: Sprite,
    pub velocity: [i32; 3],
    pub timer: i32,
    pub angular_velocity: i32,
    pub visible: u8,
    pub smoke: [Smoke; 10],
    pub smoke_timer: i32,
    pub smoke_count: u8,
    pub band: u32,
    pub ring: u32,
}
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Slot {
    pub entity: Option<EntityId>,
    pub frame: u32,
}
#[derive(Debug, Default)]
pub struct World {
    pub count: i32,
    pub slots: [Slot; 15],
    pub overflow: u8,
    pub fog: bool,
}
#[derive(Debug, Clone, Copy)]
pub struct Resources {
    pub main: u32,
    pub smoke: u32,
    pub small: bool,
    pub damping: u32,
    pub map_fog: bool,
}
impl Default for Resources {
    fn default() -> Self {
        Self {
            main: 4,
            smoke: 4,
            small: false,
            damping: 0x08010001,
            map_fog: false,
        }
    }
}
/// The first random draw precedes activation/list callbacks. The owner supplies
/// it separately so failures preserve the exact constructor prefix.
pub fn initialize(
    state: &mut State,
    position: [i32; 3],
    first_draw: u32,
    seed: &mut u32,
    r: Resources,
) {
    state.sprite.position = position;
    state.sprite.uv = [0, 0, 31, 31];
    let b = (random::next(seed) >> 4) & 65535;
    state.angular_velocity = ((b % 40 + 55) * 0xb600 / 1000) as i32;
    state.sprite.rotation = first_draw.wrapping_mul(182) as u16;
    state.sprite.size = [if r.small { 2000 } else { 3000 }; 2];
    state.sprite.blend = -6;
    state.sprite.color = [128; 3];
    state.sprite.flags = 1;
    state.sprite.material = r.main;
}
pub fn finish_spawn(state: &mut State) {
    state.visible = 1;
    state.band = u32::MAX;
    state.ring = u32::MAX;
}
/// Returns true exactly at the original retirement boundary. The owner clears
/// its slot, removes drawing, queues deletion and only then decrements count.
pub fn tick(
    state: &mut State,
    origin: [i32; 3],
    dt: i32,
    seed: &mut u32,
    r: Resources,
) -> Result<bool, i32> {
    state.timer = state.timer.wrapping_add(dt);
    if state.timer > 30 {
        state.timer = 0;
        let uv = &mut state.sprite.uv;
        if uv[0] < 95 {
            uv[0] = uv[0].wrapping_add(32);
            uv[2] = uv[2].wrapping_add(32);
        } else if uv[1] < 95 {
            uv[0] = 0;
            uv[1] = uv[1].wrapping_add(32);
            uv[2] = 31;
            uv[3] = uv[3].wrapping_add(32);
        } else {
            state.visible = 0;
            if state.smoke.iter().all(|s| s.active == 0) {
                return Ok(true);
            }
        }
    }
    state.angular_velocity = state.angular_velocity.wrapping_sub(dt);
    state.sprite.rotation = state
        .sprite
        .rotation
        .wrapping_add((dt.wrapping_mul(state.angular_velocity) >> 8) as u16);
    for i in 0..3 {
        state.sprite.position[i] =
            state.sprite.position[i].wrapping_add(state.velocity[i].wrapping_mul(dt) >> 8);
    }
    for i in [0, 2] {
        // This packed source damping has a signed low-short force and high-byte shift.
        state.velocity[i] = fixed::decay(
            fixed::rate_step(state.velocity[i], i32::from(r.damping as i16), dt),
            (r.damping >> 24) as u8,
            dt,
        );
    }
    if state.band != u32::MAX || state.ring != u32::MAX {
        return Err(-5);
    }
    if state.smoke_count < 10 {
        state.smoke_timer = state.smoke_timer.wrapping_add(dt);
        if state.smoke_timer >= 97 {
            let puff = &mut state.smoke[usize::from(state.smoke_count)];
            let a = (random::next(seed) >> 4) & 65535;
            let b = (random::next(seed) >> 4) & 65535;
            state.smoke_timer = 0;
            puff.sprite.position = [origin[0], origin[1].wrapping_sub(1024), origin[2]];
            puff.sprite.material = r.smoke;
            puff.sprite.uv = [0, 0, 63, 63];
            puff.sprite.flags = 1;
            puff.sprite.size = [300; 2];
            puff.sprite.blend = -16;
            puff.sprite.color = [128; 3];
            puff.sprite.rotation = 0;
            puff.velocity = [
                (((a & 1) as i32).wrapping_mul(1024).wrapping_sub(1024)) >> 3,
                ((a % 3 + 2) as i32).wrapping_mul(3072) >> 4,
                (((b & 1) as i32).wrapping_mul(1024).wrapping_sub(1024)) >> 3,
            ];
            puff.angular_velocity = ((b % 360) as i32 - 180).wrapping_mul(182) >> 2;
            puff.active = 1;
            puff.lift = 1536;
            state.smoke_count = state.smoke_count.wrapping_add(1);
        }
    }
    for puff in &mut state.smoke {
        if puff.active == 0 {
            continue;
        }
        for i in 0..3 {
            let speed = puff.velocity[i].wrapping_add(if i == 1 { puff.lift } else { 0 });
            puff.sprite.position[i] =
                puff.sprite.position[i].wrapping_add(speed.wrapping_mul(dt) >> 8);
        }
        if puff.lift != 0 {
            puff.lift = puff.lift.wrapping_sub(dt.wrapping_mul(8));
            if puff.lift < 0 {
                puff.lift = 0;
            }
        }
        for size in &mut puff.sprite.size {
            *size = size.wrapping_add(dt >> 1);
        }
        let omega = puff.angular_velocity;
        puff.sprite.rotation = puff
            .sprite
            .rotation
            .wrapping_add((dt.wrapping_mul(omega) >> 8) as u16);
        let step = dt.wrapping_mul(2);
        puff.angular_velocity =
            omega.wrapping_sub(if omega < 1 { step.wrapping_neg() } else { step });
        let fade = dt >> 4;
        if fade < i32::from(puff.sprite.color[0]) {
            puff.sprite.color = puff
                .sprite
                .color
                .map(|v| (i32::from(v).wrapping_sub(fade)) as u8);
        } else {
            puff.active = 0;
        }
    }
    Ok(false)
}
