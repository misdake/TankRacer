//! Authored linear movers: player proximity starts them; camera proximity gates
//! movement. A collision can hand control to the shared Spin implementation.
use crate::{
    math::{self, Axis},
    spin,
    terrain::MISS_HEIGHT,
};
use tankracer_assets::{object_catalog::Catalog, parameters::Parameters};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct State {
    pub flags: u32,
    pub distance: i32,
    pub travel: i32,
    pub speed: i32,
    pub activation_range: i32,
}
impl Default for State {
    fn default() -> Self {
        Self {
            flags: 60,
            distance: 10240,
            travel: 0,
            speed: 1310,
            activation_range: 51200,
        }
    }
}
impl State {
    /// Runs after the real Spin constructor. Missing boolean keys clear bits,
    /// even if the initializer or preset set them.
    pub fn construct(&mut self, p: Parameters<'_>, catalog: &Catalog) {
        if let Some(t) = p
            .find(":type")
            .or_else(|| p.find(":Type"))
            .and_then(|mut v| v.token_word())
            .and_then(|n| catalog.get(n))
        {
            self.flags = (self.flags & !60) | (t.flags & 60);
        }
        if let Some(mut v) = p.find(":dist").or_else(|| p.find(":Dist")) {
            self.distance = v.integer().unwrap_or(self.distance).wrapping_shl(10);
        }
        if let Some(mut v) = p.find(":speed").or_else(|| p.find(":Speed")) {
            self.speed = v.integer().unwrap_or(self.speed).wrapping_shl(18) / 1000;
        }
        if let Some(mut v) = p.find(":move_range").or_else(|| p.find(":Move_range")) {
            self.activation_range = v
                .integer()
                .unwrap_or(self.activation_range)
                .wrapping_shl(10);
        }
        for (name, upper, mask) in [
            (":snap_to_track", ":Snap_to_track", 8),
            (":collidable", ":Collidable", 4),
            (":oscillating", ":Oscillating", 16),
            (":flip_180", ":Flip_180", 32),
            (":explode", ":Explode", 128),
        ] {
            let enabled = p
                .find(name)
                .or_else(|| p.find(upper))
                .and_then(|mut v| v.token_word())
                .is_some_and(|v| v.eq_ignore_ascii_case("true"));
            self.flags = if enabled {
                self.flags | mask
            } else {
                self.flags & !mask
            };
        }
    }
}

pub fn set_velocity(state: &State, motion: &mut spin::Motion<'_>) {
    if let Some(matrix) = motion.collider.external_basis {
        motion.state.velocity =
            std::array::from_fn(|i| state.speed.wrapping_mul(matrix.0[6 + i]) >> 12);
    }
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Tick {
    None,
    Spin,
    Debris,
}
#[derive(Clone, Copy)]
pub struct Inputs {
    pub dt: i32,
    /// Original camera coordinates, in physics position units divided by eight.
    pub camera: [i32; 3],
    pub players: [Option<[i32; 3]>; 2],
}
pub trait Services {
    fn normal_height(&mut self, x: i32, z: i32, normal: &mut [i32; 3]) -> Result<i32, i32>;
    fn stop(&mut self) -> Result<(), i32>;
}
pub fn near_camera(position: [i32; 3], camera: [i32; 3]) -> bool {
    [0, 2].into_iter().all(|i| {
        let coordinate = position[i] >> 3;
        let delta = coordinate.wrapping_sub(camera[i]);
        let delta = if delta < 0 {
            camera[i].wrapping_sub(coordinate)
        } else {
            delta
        };
        delta < 12800
    })
}
pub fn tick(
    state: &mut State,
    mut motion: spin::Motion<'_>,
    config: spin::Config,
    inputs: Inputs,
    debris_active: bool,
    services: &mut impl Services,
) -> Result<Tick, i32> {
    let flags = state.flags;
    if *motion.activity == 0 {
        return Ok(if flags & 64 != 0 && debris_active {
            Tick::Debris
        } else {
            Tick::None
        });
    }
    if flags & 64 != 0 {
        return Ok(Tick::Spin);
    }
    if flags & 2 != 0 {
        return Ok(Tick::None);
    }
    let timer = motion.state.timer;
    if flags & 1 == 0 {
        motion.state.timer = timer.wrapping_add(1);
        if timer.is_multiple_of(5)
            && inputs.players.into_iter().any(|p| {
                let range = p.map_or(10240000, |p| {
                    let a = p[0]
                        .wrapping_sub(motion.collider.pose.position[0])
                        .wrapping_abs();
                    let b = p[2]
                        .wrapping_sub(motion.collider.pose.position[2])
                        .wrapping_abs();
                    if b <= a {
                        a.wrapping_add(b.wrapping_sub(b >> 1))
                    } else {
                        b.wrapping_add(a.wrapping_sub(a >> 1))
                    }
                });
                range < state.activation_range
            })
        {
            state.flags = flags | 257;
        }
        return Ok(Tick::None);
    }
    if !near_camera(motion.collider.pose.position, inputs.camera) {
        state.flags = flags & !256;
        return Ok(Tick::None);
    }
    state.flags = flags | 256;
    let dt = inputs.dt;
    for i in [0, 2] {
        motion.collider.pose.position[i] = motion.collider.pose.position[i]
            .wrapping_add(dt.wrapping_mul(motion.state.velocity[i]) >> 8);
    }
    if flags & 8 != 0 {
        motion.state.timer = timer.wrapping_add(1);
        if timer.is_multiple_of(3) {
            let mut normal = [0; 3];
            let ground = services.normal_height(
                motion.collider.pose.position[0],
                motion.collider.pose.position[2],
                &mut normal,
            )?;
            if ground != MISS_HEIGHT {
                motion.state.normal = normal;
                motion.collider.pose.position[1] = ground;
                let local = motion
                    .collider
                    .external_basis
                    .ok_or(-4)?
                    .world_to_local(normal);
                let pitch = math::angle(local[1], local[2])
                    .map_err(|_| -2)?
                    .wrapping_sub(16384);
                let roll = math::angle(local[0].wrapping_neg(), local[1])
                    .map_err(|_| -2)?
                    .wrapping_neg();
                for (axis, angle) in [(Axis::X, pitch), (Axis::Z, roll)] {
                    let step = angle
                        .wrapping_mul(i32::from(config.flatten.multiplier as i16))
                        .wrapping_shr(u32::from(config.flatten.shift as u8));
                    rotate(&mut motion, axis, step);
                }
            }
        }
    }
    state.travel = state.travel.wrapping_add(dt.wrapping_mul(state.speed) >> 8);
    if state.travel.wrapping_abs() > state.distance {
        if state.flags & 16 == 0 {
            state.flags &= !1;
            if state.flags & 2 == 0 {
                state.flags |= 2;
                *motion.activity = 0;
                services.stop()?;
            }
        } else {
            state.travel = if state.travel > 0 {
                state.travel.wrapping_sub(state.distance)
            } else if state.travel < 0 {
                state.travel.wrapping_add(state.distance)
            } else {
                0
            };
            if state.flags & 32 != 0 {
                rotate(&mut motion, Axis::Y, 32760);
            } else {
                state.speed = state.speed.wrapping_neg();
            }
            set_velocity(state, &mut motion);
        }
    }
    Ok(Tick::None)
}
fn rotate(motion: &mut spin::Motion<'_>, axis: Axis, angle: i32) {
    if let Some(matrix) = &mut motion.collider.external_basis {
        matrix.rotate(axis, angle as u32);
        if motion.reference_alias {
            motion.collider.pose.matrix = *matrix;
        }
    }
}
