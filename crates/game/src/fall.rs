//! Falling-obstacle motion (40A5F0). Exact fixed-point turn, fall and rebound;
//! terrain queries and immediate stop side effects belong to the scene owner.
use crate::{
    fixed::trunc_shift,
    math::{self, Axis, Matrix},
    object_query::EntityId,
    symbols::Binding,
};
use std::sync::Arc;

#[derive(Debug, Clone, Default)]
pub struct State {
    pub angular_velocity: i32,
    pub rotation: i32,
    pub turn_rate: i32,
    pub last_vehicle: Option<EntityId>,
    pub slope_limit: i32,
    pub basis_backup: Option<Matrix>,
    pub attached: Option<Arc<[Binding]>>,
}
pub use tankracer_assets::object_catalog::Fall as Tuning;
pub struct Motion<'a> {
    pub state: &'a mut State,
    pub matrix: &'a mut Matrix,
    pub flags: &'a mut u32,
    pub activity: &'a mut i32,
    pub position: [i32; 3],
    pub width: i32,
    pub vehicle_basis: Matrix,
    pub dt: i32,
    pub effect_active: bool,
}
pub trait Services {
    fn height(&mut self, x: i32, z: i32) -> Result<i32, i32>;
    /// Called before the final matrix rotation, in original remove/queue order.
    fn stop(&mut self) -> Result<(), i32>;
}
pub fn tick(motion: Motion<'_>, tuning: Tuning, services: &mut impl Services) -> Result<(), i32> {
    let Motion {
        state,
        matrix,
        flags,
        activity,
        position,
        width,
        vehicle_basis,
        dt,
        effect_active,
    } = motion;
    if *activity == 0 {
        return Ok(());
    }
    if *flags & 2 != 0 {
        let vector =
            matrix.world_to_local([vehicle_basis.0[0], vehicle_basis.0[1], vehicle_basis.0[2]]);
        let angle = math::angle(vector[2], vector[0]).map_err(|_| -2)?;
        let (turn, flip) = if (0..16384).contains(&angle) {
            if angle < tuning.squash_angle {
                (0, 1)
            } else {
                (1, 0)
            }
        } else if (-32768..=-16384).contains(&angle) && angle != -32768 {
            if angle.wrapping_add(32768).wrapping_abs() < tuning.squash_angle {
                (0, 2)
            } else {
                (1, 0)
            }
        } else if (16384..=32768).contains(&angle) {
            if angle.wrapping_sub(32768).wrapping_abs() < tuning.squash_angle {
                (0, 2)
            } else {
                (-1, 0)
            }
        } else if (-16384..0).contains(&angle) {
            if angle.wrapping_neg() < tuning.squash_angle {
                (0, 1)
            } else {
                (-1, 0)
            }
        } else {
            (0, 0)
        };
        if flip != 0 {
            *flags = (*flags & !2) | if flip == 2 { 4 } else { 0 };
        }
        if turn != 0 {
            let mut delta = trunc_shift(state.turn_rate.wrapping_mul(dt), 8);
            if turn < 0 {
                delta = delta.wrapping_neg();
            }
            matrix.rotate(Axis::Y, delta as u32);
        }
        if *flags & 2 == 0 {
            let mut x = trunc_shift(matrix.0[6].wrapping_mul(width), 12);
            let mut z = trunc_shift(matrix.0[8].wrapping_mul(width), 12);
            if (*flags & 8 != 0) != (*flags & 4 != 0) {
                x = x.wrapping_neg();
                z = z.wrapping_neg();
            }
            let height =
                services.height(position[0].wrapping_add(x), position[2].wrapping_add(z))?;
            state.slope_limit = if height == i32::MIN + 1 {
                0
            } else {
                math::angle(position[1].wrapping_sub(height), width).map_err(|_| -2)?
            };
        }
    } else {
        let speed = state.angular_velocity;
        let mut delta = trunc_shift(dt.wrapping_mul(speed), 8);
        let old = state.rotation;
        let current = if *flags & 8 != 0 {
            state.angular_velocity = speed.wrapping_add(tuning.gravity.wrapping_mul(dt));
            old.wrapping_sub(delta)
        } else {
            state.angular_velocity = speed.wrapping_sub(tuning.gravity.wrapping_mul(dt));
            old.wrapping_add(delta)
        };
        let limit = state.slope_limit;
        if current.wrapping_abs() > limit.wrapping_add(16380) {
            delta = if delta > 0 {
                limit.wrapping_sub(old.wrapping_abs()).wrapping_add(16384)
            } else {
                old.wrapping_abs().wrapping_sub(limit).wrapping_sub(16384)
            };
            state.rotation = (-16380_i32).wrapping_sub(limit);
            let bounce = tuning
                .elasticity_multiplier
                .wrapping_mul(state.angular_velocity);
            state.angular_velocity = if bounce.wrapping_neg() >= 0 {
                bounce.wrapping_neg().wrapping_shr(tuning.elasticity_shift)
            } else {
                bounce.wrapping_shr(tuning.elasticity_shift).wrapping_neg()
            };
            if state.angular_velocity.wrapping_abs() < 200 {
                *activity = 0;
                if !effect_active {
                    services.stop()?;
                }
            }
        } else {
            state.rotation = current;
        }
        if *flags & 4 != 0 {
            delta = delta.wrapping_neg();
        }
        matrix.rotate(Axis::X, delta as u32);
    }
    Ok(())
}
