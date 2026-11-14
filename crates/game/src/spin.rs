//! Sliding and edge-pivoting scenery. Cached terrain, narrow timers and signed
//! stop comparisons follow the original update order rather than a rigid body solver.
use crate::{
    fixed::trunc_shift,
    math::{self, Axis},
    object_query::Collider,
    terrain::MISS_HEIGHT,
    vector,
};
use tankracer_assets::{config::Gain, object_catalog::Catalog, parameters::Parameters};

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct State {
    pub velocity: [i32; 3],
    pub yawing: u32,
    pub timer: u8,
    pub pivot_axis: u32,
    pub ground: i32,
    pub edge_ground: i32,
    pub normal: [i32; 3],
    pub angular_velocity: [i32; 3],
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Config {
    pub elasticity: i16,
    pub friction: i8,
    pub yaw_factor: i16,
    pub hit_x: Gain,
    pub hit_z: Gain,
    pub hit_slow: Gain,
    pub yaw_slow: Gain,
    pub flatten: Gain,
    pub stop_yaw: i16,
    pub deflection: i32,
}
impl Default for Config {
    fn default() -> Self {
        Self {
            elasticity: 70,
            friction: 25,
            yaw_factor: 10,
            hit_x: Gain {
                multiplier: 1,
                shift: 8,
            },
            hit_z: Gain {
                multiplier: 1,
                shift: 7,
            },
            hit_slow: Gain {
                multiplier: 1,
                shift: 2,
            },
            yaw_slow: Gain {
                multiplier: 80,
                shift: 0,
            },
            flatten: Gain {
                multiplier: 10,
                shift: 4,
            },
            stop_yaw: 800,
            deflection: 0,
        }
    }
}
#[derive(Debug, Clone, Copy)]
pub struct Component {
    pub state: State,
    pub config: Config,
    pub reference_alias: bool,
}
impl Default for Component {
    fn default() -> Self {
        Self {
            state: State::default(),
            config: Config::default(),
            reference_alias: true,
        }
    }
}
impl Config {
    pub fn construct(
        p: Parameters<'_>,
        catalog: &Catalog,
        mut c: Self,
        weight: &mut i32,
        threshold: &mut i32,
    ) -> Self {
        if let Some(t) = p
            .find(":type")
            .or_else(|| p.find(":Type"))
            .and_then(|mut v| v.word())
            .and_then(|n| catalog.get(n))
        {
            c.elasticity = t.elasticity as i16;
            c.friction = t.friction as i8;
            *weight = i32::from(t.solidity);
            *threshold = i32::from(t.threshold as i8);
            c.yaw_factor = t.y_spin_factor as i16;
            c.hit_x = t.hit_spin_x;
            c.hit_z = t.hit_spin_z;
            c.hit_slow = t.hit_slow;
            c.yaw_slow = t.spin_slow;
            c.flatten = t.flatten;
            c.stop_yaw = t.stop_spin as i16;
            // The preset is an unsigned short widened to the object's full word.
            c.deflection = i32::from(t.max_deflection);
        }
        macro_rules! number {
            ($field:expr, $key:literal, $upper:literal, $ty:ty) => {
                if let Some(v) = p.integer($key).or_else(|| p.integer($upper)) {
                    $field = v as $ty;
                }
            };
        }
        number!(c.elasticity, ":elasticity", ":Elasticity", i16);
        number!(c.friction, ":friction", ":Friction", i8);
        number!(*threshold, ":threshold", ":Threshold", i32);
        number!(*weight, ":solidity", ":Solidity", i32);
        number!(c.yaw_factor, ":y_spin_factor", ":Y_spin_factor", i16);
        number!(c.hit_x.multiplier, ":hit_rtsp_xmul", ":Hit_rtsp_xmul", u16);
        number!(c.hit_x.shift, ":hit_rtsp_xshf", ":Hit_rtsp_xshf", u16);
        number!(c.hit_z.multiplier, ":hit_rtsp_zmul", ":Hit_rtsp_zmul", u16);
        number!(c.hit_z.shift, ":hit_rtsp_zshf", ":Hit_rtsp_zshf", u16);
        number!(
            c.hit_slow.multiplier,
            ":hit_rtslw_mul",
            ":Hit_rtslw_mul",
            u16
        );
        number!(c.hit_slow.shift, ":hit_rtslw_shf", ":Hit_rtslw_shf", u16);
        number!(
            c.yaw_slow.multiplier,
            ":slow_spin_mul",
            ":Slow_spin_mul",
            u16
        );
        number!(c.yaw_slow.shift, ":slow_spin_shf", ":Slow_spin_shf", u16);
        number!(c.flatten.multiplier, ":rot_flat_mul", ":Rot_flat_mul", u16);
        number!(c.flatten.shift, ":rot_flat_shf", ":Rot_flat_shf", u16);
        number!(c.stop_yaw, ":stop_spin_spd", ":Stop_spin_spd", i16);
        if let Some(v) = p
            .integer(":max_deflect")
            .or_else(|| p.integer(":Max_deflect"))
        {
            c.deflection = v.wrapping_mul(182);
        }
        c
    }
}
pub struct Motion<'a> {
    pub state: &'a mut State,
    pub collider: &'a mut Collider,
    pub activity: &'a mut i32,
    pub reference_alias: bool,
}
impl Motion<'_> {
    fn rotate(&mut self, axis: Axis, angle: i32) {
        if let Some(m) = &mut self.collider.external_basis {
            m.rotate(axis, angle as u32);
            if self.reference_alias {
                self.collider.pose.matrix = *m;
            }
        }
    }
    fn normalize(&mut self) -> Result<(), i32> {
        if let Some(m) = &mut self.collider.external_basis {
            let result = m.normalize().map_err(|_| -2);
            if self.reference_alias {
                self.collider.pose.matrix = *m;
            }
            result?;
        }
        Ok(())
    }
}
pub fn hit(
    motion: Motion<'_>,
    config: Config,
    weight: i32,
    vehicle: crate::bounce::Hit,
    activation_blocked: bool,
    mut activate: impl FnMut() -> Result<(), i32>,
) -> Result<(), i32> {
    let mut relative = [
        vehicle.position[0].wrapping_sub(motion.collider.pose.position[0]),
        0,
        vehicle.position[2].wrapping_sub(motion.collider.pose.position[2]),
    ];
    if let Some(m) = vehicle.external {
        relative = m.world_to_local(relative);
    }
    let angle = config.deflection.wrapping_mul(relative[0]) >> 11;
    motion.state.angular_velocity[1] = i32::from(config.yaw_factor)
        .wrapping_mul(vehicle.forward_velocity >> 6)
        .wrapping_mul(relative[0])
        >> 11;
    motion.state.angular_velocity[2] = 0;
    motion.state.angular_velocity[0] = 0;
    let local = motion
        .collider
        .external_basis
        .ok_or(-4)?
        .world_to_local(vehicle.velocity);
    let a = &mut motion.state.angular_velocity;
    if local[0].wrapping_abs() < local[2].wrapping_abs() {
        motion.state.pivot_axis = 0;
        a[0] = signed_gain(config.hit_x, vehicle.forward_velocity.wrapping_abs() >> 6);
        if local[2] > 0 {
            a[0] = a[0].wrapping_neg();
        }
    } else if local[0].wrapping_abs() > local[2].wrapping_abs() {
        motion.state.pivot_axis = 1;
        a[2] = signed_gain(config.hit_z, vehicle.forward_velocity.wrapping_abs() >> 6);
        if local[0] < 0 {
            a[2] = a[2].wrapping_neg();
        }
    }
    let factor = i32::from(config.elasticity)
        .wrapping_sub(weight)
        .wrapping_add(128);
    motion.state.velocity = vehicle.velocity.map(|v| factor.wrapping_mul(v) >> 13);
    motion.state.velocity = vector::yaw(motion.state.velocity, angle as u32);
    motion.state.yawing = 1;
    if *motion.activity == 0 && !activation_blocked {
        *motion.activity = 1;
        activate()?;
    }
    Ok(())
}
fn signed_gain(g: Gain, v: i32) -> i32 {
    v.wrapping_mul(i32::from(g.multiplier as i16))
        .wrapping_shr(u32::from(g.shift as u8))
}
fn damp(v: &mut i32, amount: i32, include_zero: bool) {
    if *v > 0 {
        *v = v.wrapping_sub(amount);
        if *v < 0 {
            *v = 0;
        }
    } else if *v < 0 || include_zero {
        *v = v.wrapping_add(amount);
        if *v > 0 {
            *v = 0;
        }
    }
}
pub trait Services {
    fn stop(&mut self) -> Result<(), i32>;
    fn normal_height(&mut self, x: i32, z: i32, normal: &mut [i32; 3]) -> Result<i32, i32>;
    fn height(&mut self, x: i32, z: i32) -> Result<i32, i32>;
    fn fallback(&mut self, position: [i32; 3], normal: &mut [i32; 3]) -> Result<bool, i32>;
}
pub fn tick(
    mut motion: Motion<'_>,
    config: Config,
    threshold: i32,
    bounds: [[i32; 3]; 2],
    gravity: i32,
    dt: i32,
    services: &mut impl Services,
) -> Result<(), i32> {
    if *motion.activity == 0 {
        return Ok(());
    }
    if motion.state.yawing != 0 {
        motion.rotate(
            Axis::Y,
            dt.wrapping_mul(motion.state.angular_velocity[1]) >> 12,
        );
        damp(
            &mut motion.state.angular_velocity[1],
            signed_gain(config.yaw_slow, dt),
            true,
        );
        if motion.state.angular_velocity[1].wrapping_abs() <= i32::from(config.stop_yaw) {
            motion.state.yawing = 0;
        }
    }
    let timer = motion.state.timer;
    motion.state.timer = timer.wrapping_add(1);
    if timer.is_multiple_of(3) {
        let mut normal = [0; 3];
        let height = services.normal_height(
            motion.collider.pose.position[0],
            motion.collider.pose.position[2],
            &mut normal,
        )?;
        if height != MISS_HEIGHT {
            motion.state.normal = normal;
        }
        motion.state.ground = height;
        if motion.collider.radius > 6144 {
            for i in 0..4 {
                let corner = [
                    bounds[usize::from(i & 1 != 0)][0].wrapping_mul(8),
                    0,
                    bounds[usize::from(i & 2 == 0)][2].wrapping_mul(8),
                ];
                let corner = motion
                    .collider
                    .external_basis
                    .map_or(corner, |m| m.local_to_world(corner));
                motion.state.edge_ground = services.height(
                    motion.collider.pose.position[0].wrapping_add(corner[0]),
                    motion.collider.pose.position[2].wrapping_add(corner[2]),
                )?;
                if motion.state.edge_ground == MISS_HEIGHT {
                    break;
                }
            }
        }
    }
    if motion.state.timer.is_multiple_of(10) {
        motion.normalize()?;
    }
    let local = motion
        .collider
        .external_basis
        .ok_or(-4)?
        .world_to_local(motion.state.normal);
    let pitch = math::angle(local[1], local[2])
        .map_err(|_| -2)?
        .wrapping_sub(16384);
    let roll = math::angle(local[0].wrapping_neg(), local[1])
        .map_err(|_| -2)?
        .wrapping_neg();
    motion.rotate(Axis::X, signed_gain(config.flatten, pitch));
    motion.rotate(Axis::Z, signed_gain(config.flatten, roll));
    if motion.state.angular_velocity[0] != 0 || motion.state.angular_velocity[2] != 0 {
        let axis = motion.state.pivot_axis;
        if axis <= 1 {
            let i = if axis == 0 { 0 } else { 2 };
            let v = motion.state.angular_velocity[i];
            let mut edge = [0; 3];
            let j = if axis == 0 { 2 } else { 0 };
            edge[j] = bounds[1][j].wrapping_sub(bounds[0][j]) >> 1;
            if v < 0 {
                edge[j] = edge[j].wrapping_neg();
            }
            damp(
                &mut motion.state.angular_velocity[i],
                signed_gain(config.hit_slow, dt),
                false,
            );
            let before = motion
                .collider
                .external_basis
                .ok_or(-4)?
                .local_to_world(edge);
            motion.rotate(
                if axis == 0 { Axis::X } else { Axis::Z },
                v.wrapping_mul(dt),
            );
            let after = motion
                .collider
                .external_basis
                .ok_or(-4)?
                .local_to_world(edge);
            for j in 0..3 {
                motion.collider.pose.position[j] =
                    motion.collider.pose.position[j].wrapping_add(before[j].wrapping_sub(after[j]));
            }
        }
    }
    for i in [0, 2] {
        let v = motion.state.velocity[i];
        motion.state.velocity[i] = v.wrapping_sub(v.wrapping_mul(i32::from(config.friction)) >> 7);
    }
    let ground = motion.state.ground;
    if motion.collider.pose.position[1] < ground {
        motion.state.velocity[1] = 0;
        motion.collider.pose.position[1] = ground;
    } else if ground < motion.collider.pose.position[1] && ground != MISS_HEIGHT {
        motion.state.velocity[1] = motion.state.velocity[1].wrapping_sub(gravity.wrapping_mul(dt));
    }
    if ground == MISS_HEIGHT || motion.state.edge_ground == MISS_HEIGHT {
        let mut normal = [0; 3];
        if services.fallback(motion.collider.pose.position, &mut normal)? {
            let dot =
                trunc_shift(normal[0].wrapping_mul(motion.state.velocity[0]), 11).wrapping_add(
                    trunc_shift(normal[2].wrapping_mul(motion.state.velocity[2]), 11),
                );
            if dot < 0 {
                let x = motion.state.velocity[0]
                    .wrapping_sub(trunc_shift(dot.wrapping_mul(normal[0]), 12));
                let z = motion.state.velocity[2]
                    .wrapping_sub(trunc_shift(dot.wrapping_mul(normal[2]), 12));
                motion.state.velocity[0] = x.wrapping_mul(i32::from(config.elasticity)) >> 7;
                motion.state.velocity[2] = z.wrapping_mul(i32::from(config.elasticity)) >> 7;
            }
        }
    }
    for i in 0..3 {
        motion.collider.pose.position[i] = motion.collider.pose.position[i]
            .wrapping_add(dt.wrapping_mul(motion.state.velocity[i]) >> 8);
    }
    let limit = threshold.wrapping_mul(i32::from(config.friction));
    if motion.state.yawing == 0
        && motion.state.velocity[0] <= limit
        && motion.state.velocity[2] <= limit
        && motion.collider.pose.position[1] == motion.state.ground
        && pitch == 0
        && roll == 0
    {
        *motion.activity = 0;
        services.stop()?;
    }
    Ok(())
}
