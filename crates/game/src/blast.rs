//! Immediate shatter/player blast response, including discarded random draws.
use crate::{fixed::trunc_shift, geometry::approximate_distance, random, vector};
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct State {
    pub health: i32,
    pub health_timer: u16,
    pub health_color: u16,
    pub protection_timer: i32,
    pub angular_enabled: bool,
    pub hit_axes: [u16; 2],
}
pub struct Target<'a> {
    pub state: &'a mut State,
    pub velocity: &'a mut [i32; 3],
    pub angular: &'a mut [i32; 3],
    pub marker: &'a mut u32,
}
#[derive(Clone, Copy)]
pub struct Input {
    pub position: [i32; 3],
    pub source: [i32; 3],
    pub mode: u32,
    pub damage: bool,
    pub max_health: i32,
    pub record_axes: bool,
    pub mark: bool,
}
fn signed_random(seed: &mut u32) -> i32 {
    let sign = random::next(seed);
    let value = ((random::next(seed) >> 5) & 32767) as i32;
    if sign & 32 != 0 {
        value + 20000
    } else {
        -20000 - value
    }
}
/// True selects the subsequent device feedback call; false is the original
/// out-of-range early return. Errors retain health writes before division.
pub fn apply(target: Target<'_>, input: Input, seed: &mut u32) -> Result<bool, i32> {
    let mut delta = std::array::from_fn(|i| input.position[i].wrapping_sub(input.source[i]));
    let distance = approximate_distance(delta);
    let force = if input.mode == 1 {
        (if distance < 5120 {
            80_i32
        } else if distance < 20480 {
            60
        } else {
            40
        })
        .wrapping_sub(trunc_shift(distance, 9))
    } else {
        if distance >= 7168 {
            return Ok(false);
        }
        25_i32.wrapping_sub(trunc_shift(distance, 9))
    };
    if force < 0 {
        return Ok(false);
    }
    let state = target.state;
    if input.damage {
        state.health = state.health.wrapping_sub(force.wrapping_mul(128));
        state.health_timer = 220;
        let half = input.max_health >> 1;
        if state.health <= half {
            state.health_color = state.health.wrapping_mul(175).checked_div(half).ok_or(-2)? as u16;
        }
    }
    if state.health < 0 {
        state.health = 0;
    }
    vector::normalize(&mut delta);
    for (velocity, delta) in target.velocity.iter_mut().zip(delta) {
        *velocity = velocity.wrapping_add(delta.wrapping_mul(force));
    }
    let _discarded = signed_random(seed);
    target.angular[1] = target.angular[1].wrapping_add(force.wrapping_mul(signed_random(seed)));
    let mut rates = [0; 2];
    for rate in &mut rates {
        let negative = force.wrapping_mul(signed_random(seed)) < 0;
        let product = force.wrapping_mul(signed_random(seed));
        *rate = if negative {
            (product.wrapping_neg() >> 1).wrapping_neg()
        } else {
            product >> 1
        };
    }
    let [pitch, roll] = rates;
    if state.angular_enabled {
        target.angular[2] = target.angular[2].wrapping_add(pitch);
        target.angular[0] = target.angular[0].wrapping_add(roll);
    }
    if input.record_axes {
        state.hit_axes = [if roll > 0 { 1 } else { 2 }, if pitch > 0 { 1 } else { 2 }];
    }
    if input.mark {
        *target.marker = 300;
    }
    Ok(true)
}
