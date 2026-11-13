//! Original velocity transform precision branches and rearweight yaw correction.
use crate::{contact::Coefficient, fixed::trunc_shift, math::Matrix, vector};

pub fn large(world: [i32; 3]) -> bool {
    world[0].wrapping_abs() > 500000 || world[2].wrapping_abs() > 500000
}
#[derive(Debug, Clone, Copy)]
pub enum Direction {
    WorldToBody,
    BodyToWorld,
}

/// Compute `large` ONCE from the incoming world velocity and reuse on the return
/// transform. The large path compares signed X/Z, not their absolute magnitudes.
pub fn transform(src: [i32; 3], matrix: Matrix, direction: Direction, large: bool) -> [i32; 3] {
    let x_is_large = src[0] > src[2];
    let x = src[0] >> if large && x_is_large { 8 } else { 4 };
    let y = src[1] >> 4;
    let z = src[2] >> if large && !x_is_large { 8 } else { 4 };
    let inverse = matches!(direction, Direction::BodyToWorld);
    let m = matrix.0;
    std::array::from_fn(|i| {
        let mut tx = m[if inverse { i } else { 3 * i }].wrapping_mul(x);
        let mut ty = m[if inverse { 3 + i } else { 3 * i + 1 }].wrapping_mul(y);
        let mut tz = m[if inverse { 6 + i } else { 3 * i + 2 }].wrapping_mul(z);
        if large {
            if !x_is_large {
                tx >>= 4;
            }
            ty >>= 4;
            if x_is_large {
                tz >>= 4;
            }
        }
        tx.wrapping_add(tz).wrapping_add(ty) >> if large { 4 } else { 8 }
    })
}

#[derive(Debug, Clone, Copy)]
pub struct YawConfig {
    pub axis: [i32; 3],
    pub decelerating: Coefficient,
    pub accelerating: Coefficient,
}
impl From<&tankracer_assets::config::Drive> for YawConfig {
    fn from(config: &tankracer_assets::config::Drive) -> Self {
        Self {
            axis: [0, 0, -4096],
            decelerating: config.rearweight_fraction.into(),
            accelerating: config.rearweight_accel_fraction.into(),
        }
    }
}
pub fn correct_yaw(
    velocity: [i32; 3],
    longitudinal_delta: i32,
    yaw: i32,
    config: &YawConfig,
) -> i32 {
    let mut direction = [velocity[0], 0, velocity[2]];
    vector::normalize(&mut direction);
    let cross = vector::cross(config.axis, direction);
    let change = trunc_shift(longitudinal_delta.wrapping_abs().wrapping_mul(cross[1]), 12);
    let gain = if longitudinal_delta < 0 {
        config.decelerating
    } else {
        config.accelerating
    };
    let change = gain.apply_truncated(change);
    if longitudinal_delta < 0 {
        yaw.wrapping_sub(change)
    } else {
        yaw.wrapping_add(change)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn large_velocity_uses_signed_comparison_and_retains_int_min_abs_behavior() {
        assert!(!large([i32::MIN, 0, 0]));
        assert!(large([-500001, 0, 0]));
        assert_eq!(
            transform(
                [-600001, 0, -100001],
                Matrix::IDENTITY,
                Direction::WorldToBody,
                true
            ),
            [-600016, 0, -100096]
        );
    }
}
