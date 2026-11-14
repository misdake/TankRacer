//! Proximity-triggered closing barrier. Phase identities are semantic Rust
//! states; no instruction/data pointers or floating-point physics are needed.
use crate::{
    math::{Axis, Matrix},
    object_query::EntityId,
};
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub enum Phase {
    #[default]
    Uninitialized,
    Waiting,
    Closing,
    Fragments,
}
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct State {
    pub speed: i32,
    pub flags: u32,
    pub phase: Phase,
}
#[derive(Debug, Clone, Copy)]
pub struct Component {
    pub state: State,
    pub reference_alias: bool,
}
impl Default for Component {
    fn default() -> Self {
        Self {
            state: State::default(),
            reference_alias: true,
        }
    }
}

pub fn poll(
    state: &mut State,
    entity: EntityId,
    frame: u32,
    position: [i32; 3],
    player: Option<[i32; 3]>,
) {
    if (entity.0 ^ frame) & 7 != 0 {
        return;
    }
    let near = player.map_or(entity.0 != 0, |p| {
        p[0].wrapping_sub(position[0]).wrapping_abs() < 40960
            && p[2].wrapping_sub(position[2]).wrapping_abs() < 40960
    });
    if near {
        state.phase = Phase::Closing;
    }
}
pub fn close(
    state: &mut State,
    matrix: &mut Matrix,
    dt: i32,
    work_limit: u32,
    mut stop: impl FnMut() -> Result<(), i32>,
) -> Result<(), i32> {
    if work_limit == 0 {
        return Err(-1);
    }
    let mut roll = matrix.angles().map_err(|_| -2)?.0[2] as i32;
    let mut work = 0;
    while roll > 32760 {
        if work >= work_limit {
            return Err(-3);
        }
        work += 1;
        roll = roll.wrapping_sub(65520);
    }
    while roll <= -32760 {
        if work >= work_limit {
            return Err(-3);
        }
        work += 1;
        roll = roll.wrapping_add(65520);
    }
    let target = roll.wrapping_neg();
    let acceleration = dt.wrapping_mul(1820) >> 8;
    if state.flags & 4 != 0 {
        state.speed = state.speed.wrapping_add(acceleration);
    } else if state.speed < acceleration {
        state.flags |= 4;
        state.speed = acceleration.wrapping_sub(state.speed);
    } else {
        state.speed = state.speed.wrapping_sub(acceleration);
    }
    let speed = state.speed;
    let mut delta = dt.wrapping_mul(speed) >> 8;
    if state.flags & 4 != 0 {
        if target < 0 && target.wrapping_abs() < delta {
            delta = target;
            if speed & !3 > 7280 {
                state.flags &= !6;
                state.speed = speed >> 2;
            } else {
                state.flags |= 1;
                stop()?;
            }
        } else if target >= 0 && target < delta {
            delta = target;
            if speed & !3 != 0 {
                state.flags = (state.flags & !4) | 2;
                state.speed = speed >> 2;
            } else {
                state.flags |= 1;
                stop()?;
            }
        } else if target < 0 {
            delta = delta.wrapping_neg();
        }
    } else if state.flags & 2 != 0 {
        delta = delta.wrapping_neg();
    }
    matrix.rotate(Axis::Z, delta as u32);
    // Native normalization works in a temporary array. On error, the preceding
    // rotation remains committed but partial normalized rows never escape.
    let mut normalized = *matrix;
    normalized.normalize().map_err(|_| -2)?;
    *matrix = normalized;
    Ok(())
}
