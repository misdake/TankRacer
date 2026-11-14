//! Bounce: collision transfers velocity into airborne/rolling motion. The
//! original signed comparisons, narrow counters and event order are deliberate.
use crate::{
    fixed::trunc_shift,
    geometry::approximate_distance,
    math::{self, Axis, Matrix},
    object_query::Collider,
    random,
    terrain::MISS_HEIGHT,
    vector,
};
use tankracer_assets::{config::Gain, object_catalog::Catalog, parameters::Parameters};

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct State {
    pub velocity: [i32; 3],
    pub spin: i32,
    pub ground: i32,
    pub timer: u16,
    pub age: i16,
    pub airborne: u8,
    pub reverse_spin: u8,
    pub special: u8,
    pub raised: u8,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Config {
    pub elasticity: i16,
    pub friction: i8,
    pub height_factor: i8,
    pub hit_spin: Gain,
    pub slow_spin: Gain,
    pub deflection: i16,
    pub gravity: i32,
    pub height_period: u8,
}
#[derive(Debug, Clone, Copy)]
pub struct Component {
    pub config: Config,
    pub state: State,
    /// Authored bases reference the object's matrix; a custom basis can have
    /// identical values without sharing that storage.
    pub reference_alias: bool,
}
impl Default for Component {
    fn default() -> Self {
        Self {
            config: Config::default(),
            state: State::default(),
            reference_alias: true,
        }
    }
}
impl Default for Config {
    fn default() -> Self {
        Self {
            elasticity: 35,
            friction: 10,
            height_factor: 20,
            hit_spin: Gain::default(),
            slow_spin: Gain::default(),
            deflection: 0,
            gravity: 5,
            height_period: 4,
        }
    }
}
#[derive(Debug, Clone, Copy)]
pub struct Environment {
    pub map: u32,
    pub low_gravity: bool,
    pub gravity: i32,
    pub umbrella: bool,
}
impl Config {
    pub fn construct(
        parameters: Parameters<'_>,
        catalog: &Catalog,
        initial: Self,
        environment: Environment,
        position: &mut [i32; 3],
        state: &mut State,
    ) -> Self {
        let p = parameters;
        let preset = p
            .find(":type")
            .or_else(|| p.find(":Type"))
            .and_then(|mut v| v.word())
            .and_then(|n| catalog.get(n));
        let mut c = initial;
        if let Some(t) = preset {
            c.elasticity = t.elasticity as i16;
            c.friction = t.friction as i8;
            c.height_factor = t.height_factor as i8;
            c.hit_spin = t.hit_spin_x;
            c.slow_spin = t.spin_slow;
            c.deflection = t.max_deflection as i16;
        }
        macro_rules! number {
            ($field:expr,$name:literal,$upper:literal,$ty:ty) => {
                if let Some(n) = p.integer($name).or_else(|| p.integer($upper)) {
                    $field = n as $ty;
                }
            };
        }
        number!(c.elasticity, ":elasticity", ":Elasticity", i16);
        number!(c.friction, ":friction", ":Friction", i8);
        number!(c.height_factor, ":height_factor", ":Height_factor", i8);
        number!(
            c.hit_spin.multiplier,
            ":hit_rtsp_xmul",
            ":Hit_rtsp_xmul",
            u16
        );
        number!(c.hit_spin.shift, ":hit_rtsp_xshf", ":Hit_rtsp_xshf", u16);
        number!(
            c.slow_spin.multiplier,
            ":slow_spin_mul",
            ":Slow_spin_mul",
            u16
        );
        number!(c.slow_spin.shift, ":slow_spin_shf", ":Slow_spin_shf", u16);
        if let Some(n) = p
            .integer(":max_deflect")
            .or_else(|| p.integer(":Max_deflect"))
        {
            c.deflection = n.wrapping_mul(182) as i16;
        }
        // BIO's physical low-gravity rectangle, inclusive source boundaries.
        // Constants are from frame_vehicle_defaults.h; no captured scene state.
        let regional = environment.map == 5
            && (0x4a77f..=0x813d2).contains(&position[0])
            && (-309162..=-32532).contains(&position[2]);
        if regional {
            state.special = 1;
            c.gravity = 1;
            c.height_period = 20;
        } else {
            c.gravity = environment.gravity;
            c.height_period = 4;
        }
        if environment.umbrella {
            state.raised = 1;
            position[1] = position[1].wrapping_add(750);
        }
        if environment.low_gravity && state.special == 0 {
            state.special = 1;
            c.gravity = 2;
            c.height_period = 20;
        }
        c
    }
}
pub struct Motion<'a> {
    pub state: &'a mut State,
    pub collider: &'a mut Collider,
    pub activity: &'a mut i32,
    pub threshold: i32,
    pub reference_alias: bool,
}
#[derive(Clone, Copy)]
pub struct Hit {
    pub position: [i32; 3],
    pub velocity: [i32; 3],
    pub forward_velocity: i32,
    pub reference: Matrix,
    pub external: Option<Matrix>,
}
pub trait Services {
    fn activate(&mut self) -> Result<(), i32>;
    fn deactivate(&mut self) -> Result<(), i32>;
    /// Reinsert the current pose before queueing the stop notification.
    fn settle(&mut self, collider: &Collider) -> Result<(), i32>;
    fn height(&mut self, x: i32, z: i32) -> Result<i32, i32>;
    fn normal_height(&mut self, x: i32, z: i32, normal: &mut [i32; 3]) -> Result<i32, i32>;
    fn fallback(&mut self, position: [i32; 3], normal: &mut [i32; 3]) -> Result<bool, i32>;
}
fn rotate(collider: &mut Collider, reference_alias: bool, axis: Axis, angle: u32) {
    // Authored external bases alias the reference matrix. Preserve the same
    // update when rotating; custom bases remain separate, null stays null.
    if let Some(matrix) = &mut collider.external_basis {
        matrix.rotate(axis, angle);
        if reference_alias {
            collider.pose.matrix = *matrix;
        }
    }
}
pub fn hit(
    motion: Motion<'_>,
    config: Config,
    vehicle: Hit,
    seed: &mut u32,
    services: &mut impl Services,
) -> Result<(), i32> {
    let Motion {
        state,
        collider,
        activity,
        reference_alias,
        ..
    } = motion;
    let relative =
        std::array::from_fn(|i| collider.pose.position[i].wrapping_sub(vehicle.position[i]));
    let relative = vehicle
        .external
        .map_or(relative, |m| m.world_to_local(relative));
    let angle = i32::from(config.deflection)
        .wrapping_mul(relative[0])
        .wrapping_neg()
        >> 11;
    let bounce = i32::from(config.elasticity);
    let split = i32::from(config.height_factor);
    state.velocity = [
        vehicle.velocity[0].wrapping_mul(bounce >> 4) >> 6,
        (vehicle.forward_velocity.wrapping_abs() >> 6)
            .wrapping_mul(split)
            .wrapping_mul(bounce)
            >> 11,
        (vehicle.velocity[2] >> 6)
            .wrapping_mul(128_i32.wrapping_sub(split))
            .wrapping_mul(bounce)
            >> 11,
    ];
    state.velocity = vector::yaw(state.velocity, angle as u32);
    let alias = reference_alias && collider.external_basis.is_some();
    collider.pose.matrix = vehicle.reference;
    if alias {
        collider.external_basis = Some(vehicle.reference);
    }
    rotate(collider, reference_alias, Axis::Y, angle as u32);
    state.airborne = 1;
    if *activity == 0 {
        *activity = 1;
        services.activate()?;
    }
    state.age = 0;
    state.spin = approximate_distance(state.velocity)
        .wrapping_mul(i32::from(config.hit_spin.multiplier as i16))
        .wrapping_neg()
        .wrapping_shr(u32::from(config.hit_spin.shift));
    state.reverse_spin = if vehicle.forward_velocity.wrapping_abs() > 250000 {
        ((random::next(seed) >> 4) & 1) as u8
    } else {
        0
    };
    Ok(())
}
pub fn tick(
    motion: Motion<'_>,
    config: Config,
    dt: i32,
    services: &mut impl Services,
) -> Result<(), i32> {
    let Motion {
        state,
        collider,
        activity,
        threshold,
        reference_alias,
    } = motion;
    if *activity == 0 {
        return Ok(());
    }
    if state.age > 20000 {
        return services.deactivate();
    }
    state.age = state.age.wrapping_add(dt as i16);
    for i in 0..3 {
        collider.pose.position[i] =
            collider.pose.position[i].wrapping_add(dt.wrapping_mul(state.velocity[i]) >> 8);
    }
    state.spin = state.spin.wrapping_add(
        i32::from(config.slow_spin.multiplier as i16)
            .wrapping_mul(dt)
            .wrapping_shr(u32::from(config.slow_spin.shift)),
    );
    if state.spin > 0 {
        state.spin = 0;
    }
    let spin = if state.reverse_spin != 0 {
        state.spin.wrapping_neg()
    } else {
        state.spin
    };
    rotate(collider, reference_alias, Axis::X, spin as u32);
    state.timer = state.timer.wrapping_add(1);
    if config.height_period == 0 {
        return Err(-2);
    }
    let mut ground = state.ground;
    if state.timer.is_multiple_of(u16::from(config.height_period)) {
        ground = services.height(collider.pose.position[0], collider.pose.position[2])?;
        if ground != MISS_HEIGHT {
            state.ground = ground.wrapping_add(if state.raised != 0 { 750 } else { 0 });
        }
    }
    if state.airborne == 0 {
        let limit = i32::from(config.friction).wrapping_mul(threshold);
        collider.pose.position[1] = state.ground;
        // Source compares signed components, not absolute speeds.
        if state.velocity[0] <= limit && state.velocity[2] <= limit {
            let mut normal = [0; 3];
            ground = services.normal_height(
                collider.pose.position[0],
                collider.pose.position[2],
                &mut normal,
            )?;
            if ground == MISS_HEIGHT {
                services.deactivate()?;
            } else {
                if state.raised == 0 {
                    let matrix = collider.external_basis.ok_or(-4)?;
                    let local = matrix.world_to_local(normal);
                    let angle = math::angle(local[1], local[2]).map_err(|_| -2)?;
                    rotate(collider, reference_alias, Axis::X, angle as u32);
                }
                *activity = 0;
                services.settle(collider)?;
            }
        }
    } else if collider.pose.position[1] < state.ground {
        collider.pose.position[1] = state.ground;
        state.velocity[1] = state.velocity[1]
            .wrapping_mul(i32::from(config.elasticity))
            .wrapping_neg()
            >> 7;
        if state.velocity[1] < threshold.wrapping_mul(i32::from(config.elasticity)) {
            state.velocity[1] = 0;
            state.airborne = 0;
        }
    }
    if ground == MISS_HEIGHT {
        let mut normal = [0; 3];
        if services.fallback(collider.pose.position, &mut normal)? {
            let dot = trunc_shift(normal[0].wrapping_mul(state.velocity[0]), 11)
                .wrapping_add(trunc_shift(normal[2].wrapping_mul(state.velocity[2]), 11));
            if dot < 0 {
                let x =
                    state.velocity[0].wrapping_sub(trunc_shift(dot.wrapping_mul(normal[0]), 12));
                state.velocity[0] = x.wrapping_mul(i32::from(config.elasticity)) >> 7;
                state.velocity[2] = state.velocity[2]
                    .wrapping_sub(trunc_shift(dot.wrapping_mul(normal[2]), 12))
                    .wrapping_mul(i32::from(config.elasticity))
                    >> 7;
                let local = collider
                    .external_basis
                    .ok_or(-4)?
                    .world_to_local(state.velocity);
                let angle = math::angle(local[0], local[2]).map_err(|_| -2)?;
                rotate(
                    collider,
                    reference_alias,
                    Axis::Y,
                    angle.wrapping_neg() as u32,
                );
            }
        }
    }
    state.velocity[1] = state.velocity[1].wrapping_sub(config.gravity.wrapping_mul(dt));
    for i in [0, 2] {
        state.velocity[i] = state.velocity[i]
            .wrapping_sub(i32::from(config.friction).wrapping_mul(state.velocity[i]) >> 7);
    }
    Ok(())
}
