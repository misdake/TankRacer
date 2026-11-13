//! Debris activation and motion (41CF80/41C980), on the constructed resources.
//! The owning scene performs blast callbacks and list changes at phase boundaries.
use crate::{
    fixed::trunc_shift,
    geometry::approximate_distance,
    math::{self, Axis, Matrix},
    object_query::EntityId,
    obstacle_startup::{Animation, Debris, Piece},
    random,
};

pub struct Activation {
    incoming: [i32; 3],
}
#[derive(Clone, Copy)]
pub struct BlastTarget {
    pub entity: EntityId,
    pub position: [i32; 3],
    pub protection_timer: i32,
}
impl BlastTarget {
    pub fn affected(self, source: [i32; 3]) -> bool {
        self.position[0].wrapping_sub(source[0]).wrapping_abs() < 20480
            && self.position[2].wrapping_sub(source[2]).wrapping_abs() < 20480
            && self.protection_timer <= 0
    }
}
/// Commit the active/retired prefix before callbacks. None is an already-active
/// success. Incoming velocity is captured before blast methods can mutate peers.
pub fn begin(
    state: &mut Debris,
    velocity: [i32; 3],
    limit: u32,
) -> Result<Option<Activation>, i32> {
    if limit == 0 {
        return Err(-1);
    }
    let flags = state.flags;
    if flags & 1 != 0 {
        return Ok(None);
    }
    state.retired = 0;
    state.flags |= 0x401;
    let incoming = if flags & 64 != 0 {
        Matrix::from_angles([0, state.direction as u32, 0]).local_to_world([
            0,
            0,
            trunc_shift(approximate_distance(velocity), 6),
        ])
    } else if flags & 2 == 0 {
        velocity.map(|v| trunc_shift(v, 6))
    } else {
        [0; 3]
    };
    Ok(Some(Activation { incoming }))
}
/// Resume after immediate blast callbacks, re-reading live flags and animation.
/// Work-limit failure preserves all completed pieces and random consumption.
pub fn activate(
    state: &mut Debris,
    position: [i32; 3],
    activation: Activation,
    seed: &mut u32,
    limit: u32,
) -> Result<(), i32> {
    if limit == 0 {
        return Err(-1);
    }
    if let Some(animation) = &mut state.animation {
        animation.position = std::array::from_fn(|i| state.offset[i].wrapping_add(position[i]));
        animation.rotation = (random::next(seed) >> 4) as u16;
        animation.size = [animation.initial_size; 2];
        animation.cycle = 0;
        animation.timer = 0;
        state.flags |= 8;
    }
    for (i, piece) in state.pieces.iter_mut().enumerate() {
        if i >= limit as usize {
            return Err(-3);
        }
        // v intentionally remains unscaled in the slower-explode path: native
        // uses it, rather than stored velocity, to derive angular impulse.
        let mut v = [0; 3];
        if state.flags & 2 != 0 {
            v = std::array::from_fn(|i| {
                ((random::next(seed) >> 4) & 8191) as i32 - if i == 1 { 0 } else { 4096 }
            });
            piece.velocity = v;
            if state.flags & 2048 != 0 {
                piece.velocity = [v[0] >> 1, v[1].wrapping_mul(3) >> 2, v[2] >> 1];
            }
        } else if state.flags & 256 == 0 {
            v = activation
                .incoming
                .map(|v| trunc_shift(v.wrapping_mul(i32::from(piece.velocity_gain)), 7));
            let (pitch, yaw) = if state.flags & 64 != 0 {
                (
                    ((random::next(seed) >> 4) & 16383) + 4096,
                    ((random::next(seed) >> 4) & 32767).wrapping_sub(16384),
                )
            } else {
                (
                    (random::next(seed) >> 4) & 4095,
                    ((random::next(seed) >> 4) & 16383).wrapping_sub(8192),
                )
            };
            v = Matrix::from_angles([pitch, yaw, 0]).local_to_world(v);
            piece.velocity = v;
        }
        if state.flags & 256 == 0 {
            piece.position = std::array::from_fn(|i| {
                piece.position[i]
                    .wrapping_add(piece.velocity[i])
                    .wrapping_add(state.offset[i])
            });
            let amount = (((random::next(seed) >> 4) & 127) as i32)
                .wrapping_add(i32::from(piece.spin_gain))
                .wrapping_mul(approximate_distance(v))
                >> 6;
            if state.flags & 128 != 0 {
                piece.angular[1] = if random::next(seed) & 16 != 0 {
                    piece.angular[1].wrapping_add(amount)
                } else {
                    piece.angular[1].wrapping_sub(amount)
                };
            } else {
                let axis = (((random::next(seed) >> 4) & 65535) % 6) as usize;
                let value = &mut piece.angular[axis % 3];
                *value = if axis < 3 {
                    value.wrapping_add(amount)
                } else {
                    value.wrapping_sub(amount)
                };
            }
        }
        piece.flags |= 1;
    }
    Ok(())
}
fn approach(value: &mut i32, amount: i32) {
    if *value < 0 {
        *value = value.wrapping_add(amount);
        if *value > 0 {
            *value = 0;
        }
    } else if *value > 0 {
        *value = value.wrapping_sub(amount);
        if *value < 0 {
            *value = 0;
        }
    }
}
fn spin_random(piece: &mut Piece, seed: &mut u32) {
    piece.angular = std::array::from_fn(|_| ((random::next(seed) >> 4) & 8191) as i32 - 4096);
}
/// Returns whether the owner must immediately dispatch deactivation. Retired
/// pieces still spend a loop step; the count and retired words retain 16 bits.
pub fn tick(
    state: &mut Debris,
    entity: EntityId,
    dt: i32,
    frame_group: u32,
    seed: &mut u32,
    limit: u32,
) -> Result<bool, i32> {
    if limit == 0 {
        return Err(-1);
    }
    let friction = trunc_shift(state.friction.wrapping_mul(dt), 8);
    for (i, piece) in state.pieces.iter_mut().enumerate() {
        if i >= limit as usize {
            return Err(-3);
        }
        let flags = piece.flags;
        if flags & 2 != 0 {
            continue;
        }
        if flags & 1 != 0 {
            piece.velocity[1] =
                piece.velocity[1].wrapping_sub(trunc_shift(state.gravity.wrapping_mul(dt), 8));
        } else if flags & 4 == 0 {
            approach(&mut piece.velocity[0], friction);
            approach(&mut piece.velocity[2], friction);
            for v in &mut piece.angular {
                approach(v, friction);
            }
        }
        piece.position = std::array::from_fn(|i| {
            piece.position[i].wrapping_add(trunc_shift(piece.velocity[i].wrapping_mul(dt), 8))
        });
        let radius = if state.flags & 128 != 0 {
            piece.sprite_angle = piece
                .sprite_angle
                .wrapping_add(trunc_shift(piece.angular[1].wrapping_mul(dt), 8));
            state.sprite.as_ref().ok_or(-1)?.size[0]
        } else {
            for (i, axis) in [Axis::X, Axis::Y, Axis::Z].into_iter().enumerate() {
                piece.matrix.rotate(
                    axis,
                    trunc_shift(piece.angular[i].wrapping_mul(dt), 8) as u32,
                );
            }
            piece.source.as_ref().map_or(0, |s| i32::from(s.radius) * 8)
        };
        if flags & 1 != 0 {
            let floor = radius.wrapping_add(state.ground_height);
            if piece.position[1] < floor {
                if flags & 8 != 0 {
                    piece.velocity = [0; 3];
                    piece.flags = (flags & !1) | 4;
                } else {
                    piece.position[1] = floor;
                    if piece.velocity[1].wrapping_abs() & !1 <= 2048 {
                        piece.velocity[1] = 0;
                        piece.flags &= !1;
                    } else {
                        piece.velocity[1] = piece.velocity[1].wrapping_abs() >> 1;
                        spin_random(piece, seed);
                        let yaw = ((random::next(seed) >> 4) & 4095).wrapping_sub(2048);
                        piece.velocity =
                            Matrix::from_angles([0, yaw, 0]).local_to_world(piece.velocity);
                    }
                }
            }
        } else if flags & 4 != 0 {
            let m = piece.matrix.0;
            let tilt = math::angle(m[7], math::length2(m[6], m[8])).map_err(|_| -2)?;
            if piece.previous_tilt == -1 || tilt >= piece.previous_tilt {
                piece.previous_tilt = tilt;
            } else {
                piece.flags |= 2;
                state.retired = state.retired.wrapping_add(1);
                let yaw = math::angle(m[6], m[8]).map_err(|_| -2)?;
                piece.matrix = Matrix::from_angles([0xffffc004, yaw.wrapping_neg() as u32, 0]);
            }
        } else if approximate_distance(piece.velocity) <= 1024 {
            state.retired = state.retired.wrapping_add(1);
            piece.flags |= 2;
        } else if entity.0 & 3 == frame_group
            && ((random::next(seed) >> 4) & 65535).is_multiple_of(3)
        {
            spin_random(piece, seed);
            let pitch = 0u32.wrapping_sub((random::next(seed) >> 4) & 8191);
            let yaw = ((random::next(seed) >> 4) & 4095).wrapping_sub(2048);
            piece.velocity = Matrix::from_angles([pitch, yaw, 0]).local_to_world(piece.velocity);
            piece.flags |= 1;
        }
    }
    if state.flags & 8 != 0 {
        let animation = state.animation.as_mut().ok_or(-1)?;
        animate(animation, dt);
        if animation.cycle == animation.cycles {
            state.flags &= !8;
        } else {
            // Both source sign branches are arithmetic shifts, not truncation.
            let amount = i32::from(animation.growth);
            let change = if amount > 0 {
                dt.wrapping_mul(amount) >> 7
            } else {
                (dt.wrapping_mul(amount).wrapping_neg() >> 7).wrapping_neg()
            };
            for size in &mut animation.size {
                *size = size.wrapping_add(change);
            }
        }
    }
    Ok(state.retired as usize == state.pieces.len() && state.flags & 8 == 0)
}
/// 41CEF0 advances at most one cell per update and discards timer excess.
pub fn animate(a: &mut Animation, dt: i32) {
    a.timer = a.timer.wrapping_add(dt as i16);
    if a.timer <= 60 {
        return;
    }
    let end = i32::from(a.sheet_size as i16)
        .wrapping_sub(i32::from(a.cell_size as i16))
        .wrapping_sub(1);
    let cell = a.cell_size as u8;
    a.timer = 0;
    if i32::from(a.uv[0]) < end {
        a.uv[0] = a.uv[0].wrapping_add(cell);
        a.uv[2] = a.uv[2].wrapping_add(cell);
    } else if i32::from(a.uv[1]) < end {
        a.uv[0] = 0;
        a.uv[1] = a.uv[1].wrapping_add(cell);
        a.uv[2] = cell.wrapping_sub(1);
        a.uv[3] = a.uv[3].wrapping_add(cell);
    } else {
        a.cycle = a.cycle.wrapping_add(1);
        a.uv = [0, 0, cell.wrapping_sub(1), cell.wrapping_sub(1)];
    }
}
