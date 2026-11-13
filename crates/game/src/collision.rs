//! Exact wall reflection and two-tank impulse exchange, independent of queries.
pub use crate::fixed::Coefficient as Scale;
use crate::{
    fixed::{rate_step, trunc_shift},
    geometry::approximate_distance,
    math::{self, Matrix},
    vector,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Response {
    pub normal: [i32; 3],
    pub velocity: [i32; 3],
    pub approach: i32,
    pub speed_units: i32,
}

pub fn respond(
    input_normal: [i32; 3],
    velocity: [i32; 3],
    local_z: Option<Scale>,
    final_scale: Option<Scale>,
    mode: i32,
    player: bool,
) -> Result<Response, math::Error> {
    let mut normal = [input_normal[0], 0, input_normal[2]];
    let mut toward = [velocity[0].wrapping_neg(), 0, velocity[2].wrapping_neg()];
    let speed_units = trunc_shift(approximate_distance(velocity), 12);
    let mut out = velocity;
    vector::normalize(&mut normal);
    vector::normalize(&mut toward);
    let mut d = trunc_shift(toward[0].wrapping_mul(normal[0]), 12)
        .wrapping_add(trunc_shift(toward[2].wrapping_mul(normal[2]), 12));
    if d > 0 {
        d = d.min(4096);
        let mut reflected = [
            trunc_shift(d.wrapping_mul(normal[0].wrapping_mul(2)), 12).wrapping_sub(toward[0]),
            0,
            trunc_shift(d.wrapping_mul(normal[2].wrapping_mul(2)), 12).wrapping_sub(toward[2]),
        ];
        let yaw = math::angle(normal[0].wrapping_neg(), normal[2])?;
        reflected = vector::yaw(reflected, yaw.wrapping_neg() as u32);
        reflected[2] = local_z.map_or_else(
            || trunc_shift(reflected[2], 3),
            |s| s.apply_truncated(reflected[2]),
        );
        reflected = vector::yaw(reflected, yaw as u32);
        let damping = if mode == 4 || mode == 5 {
            870
        } else if player {
            930
        } else {
            900
        };
        for i in [0, 2] {
            let value = speed_units.wrapping_mul(reflected[i]);
            out[i] = final_scale.map_or_else(
                || trunc_shift(value.wrapping_mul(damping), 10),
                |s| s.apply_truncated(value),
            );
        }
    }
    for i in [0, 2] {
        out[i] = out[i].wrapping_add(normal[i].wrapping_mul(4));
    }
    Ok(Response {
        normal,
        velocity: out,
        approach: d,
        speed_units,
    })
}

/// Original binary64 1.7 multiplied in x87 extended precision, then truncated.
/// Integer multiples of ten fall just below the rational 17/10 result.
pub fn binary_one_point_seven(value: i32) -> i32 {
    let magnitude = i64::from(value).abs();
    let mut scaled = magnitude * 17 / 10;
    if magnitude != 0 && magnitude % 10 == 0 {
        scaled -= 1;
    }
    if value < 0 {
        (-scaled) as i32
    } else {
        scaled as i32
    }
}

/// Exchange an impulse using the caller's saved other velocity and the current
/// mutable pair. The original strong branch is deliberately asymmetric.
pub fn exchange(
    basis: Matrix,
    local_velocity: [i32; 3],
    mine: &mut [i32; 3],
    other: &mut [i32; 3],
    input_normal: [i32; 3],
    incoming_other: [i32; 3],
) -> i32 {
    let mut normal = basis.world_to_local(input_normal);
    normal[1] = 0;
    vector::normalize(&mut normal);
    let local = [
        trunc_shift(local_velocity[0], 4),
        0,
        trunc_shift(local_velocity[2], 4),
    ];
    let mut relative = basis.world_to_local(incoming_other.map(|v| trunc_shift(v, 4)));
    relative[0] = relative[0].wrapping_sub(local[0]);
    relative[1] = 0;
    relative[2] = relative[2].wrapping_sub(local[2]);
    let mut speed = trunc_shift(approximate_distance(relative), 12);
    vector::normalize(&mut relative);
    let mut d = trunc_shift(normal[0].wrapping_mul(relative[0]), 12)
        .wrapping_add(trunc_shift(normal[2].wrapping_mul(relative[2]), 12));
    if d <= 2048 {
        let impulse = [trunc_shift(normal[0], 2), 0, trunc_shift(normal[2], 2)];
        let world = basis.local_to_world(impulse);
        // A tilted basis creates a world-Y impulse despite local Y being zero.
        for i in 0..3 {
            let impulse = world[i].wrapping_mul(4);
            mine[i] = mine[i].wrapping_add(impulse);
            other[i] = other[i].wrapping_sub(impulse);
        }
        return d;
    }
    d = d.min(4096);
    let mut impulse = std::array::from_fn(|i| {
        if i == 1 {
            0
        } else {
            trunc_shift(d.wrapping_mul(normal[i]).wrapping_mul(2), 12).wrapping_sub(relative[i])
        }
    });
    let mut a = [mine[0], 0, mine[2]];
    let mut b = [other[0], 0, other[2]];
    vector::normalize(&mut a);
    vector::normalize(&mut b);
    let dot = a[0]
        .wrapping_mul(b[0])
        .wrapping_add(a[2].wrapping_mul(b[2]));
    if dot >= -12146421 && speed > 3 {
        speed = 3;
    }
    for v in &mut impulse {
        *v = trunc_shift(speed.wrapping_mul(*v).wrapping_mul(600), 10);
    }
    let old = impulse;
    let capped = dot >= -12146421 && approximate_distance(impulse) > 7000;
    if capped {
        vector::normalize(&mut impulse);
        impulse = impulse.map(binary_one_point_seven);
    }
    let world = basis.local_to_world(impulse);
    for i in 0..3 {
        mine[i] = mine[i].wrapping_add(world[i].wrapping_mul(16));
    }
    other[0] = other[0].wrapping_sub(old[0]);
    other[2] = other[2].wrapping_sub(old[2]);
    if capped {
        mine[0] = rate_step(mine[0], 1000, 1);
        mine[2] = rate_step(mine[2], 1000, 1);
        other[0] = rate_step(other[0], 1000, 1);
        other[0] = rate_step(other[0], 1000, 1); // Original repeats X; it does not damp the other Z.
    }
    d
}

pub fn direction(x: i32, z: i32) -> u32 {
    if z > 0 {
        if x > 0 {
            if x <= z { 2 } else { 1 }
        } else if x.wrapping_neg() <= z {
            2
        } else {
            0
        }
    } else if x > 0 {
        if x <= z.wrapping_neg() { 3 } else { 1 }
    } else if x.wrapping_neg() <= z.wrapping_neg() {
        3
    } else {
        0
    }
}

#[derive(Debug, Clone, Copy)]
pub struct Trajectory {
    pub position: [i32; 3],
    pub velocity: [i32; 3],
}
pub fn approaching(a: Trajectory, b: Trajectory, dt: i32) -> bool {
    let mut now = 0_i32;
    let mut next = 0_i32;
    for i in 0..3 {
        let delta = a.position[i].wrapping_sub(b.position[i]);
        let ap = trunc_shift(a.velocity[i].wrapping_mul(dt), 14);
        let bp = trunc_shift(b.velocity[i].wrapping_mul(dt), 14);
        let predicted = a.position[i]
            .wrapping_add(ap)
            .wrapping_sub(b.position[i].wrapping_add(bp));
        now = now.wrapping_add(delta.wrapping_mul(delta));
        next = next.wrapping_add(predicted.wrapping_mul(predicted));
    }
    next < now
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn wall_push_applies_when_separating_and_preserves_vertical_velocity() {
        let r = respond([4096, 3000, 0], [20000, 123, 0], None, None, 0, true).unwrap();
        assert!(r.approach < 0);
        assert_eq!(r.normal, [4096, 0, 0]);
        assert_eq!(r.velocity, [36384, 123, 0]);
    }
    #[test]
    fn gentle_exchange_transforms_vertical_impulse_and_conserves_the_pair_sum() {
        let basis = Matrix::from_angles([4000, 2000, 8000]);
        let (mut a, mut b) = ([0; 3], [0; 3]);
        assert!(exchange(basis, [0; 3], &mut a, &mut b, [4096, 0, 0], [0; 3]) <= 2048);
        assert_ne!(a[1], 0);
        assert_eq!(
            std::array::from_fn::<_, 3, _>(|i| a[i].wrapping_add(b[i])),
            [0; 3]
        );
    }
    #[test]
    fn binary_constant_and_direction_keep_original_boundary_behavior() {
        assert_eq!(binary_one_point_seven(10), 16);
        assert_eq!(binary_one_point_seven(-10), -16);
        assert_eq!(binary_one_point_seven(11), 18);
        assert_eq!(direction(1, 1), 2);
        assert_eq!(direction(-1, -1), 3);
        assert_eq!(direction(0, 0), 3);
    }
}
