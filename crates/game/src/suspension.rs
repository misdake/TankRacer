//! Four-point spring response, aggregation and contact-dependent pivots.
use crate::{contact, fixed::trunc_shift};
use std::convert::Infallible;

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Point {
    /// 0 penetrating, 1 touching, 2 airborne, 3 no terrain.
    pub kind: i32,
    pub gap: i32,
    pub corner: u32,
    pub travel: i32,
    pub rate: i32,
    pub output: i32,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct State {
    pub points: [Point; 4],
    pub angular: [i32; 2],
    pub tilt: [i32; 2],
    pub penetrating_input_sum: i32,
    pub travel_sum: i32,
    pub vertical_delta: i32,
    pub world_vy: i32,
    pub position_y: i32,
    pub timer: i32,
    pub auxiliary_type: [i32; 2],
    pub auxiliary_cached: [i32; 2],
    pub max_penetration: i32,
    pub last_penetration: i32,
}

#[derive(Debug, Clone, Copy, Default)]
pub struct Environment {
    pub dt: i32,
    pub environment_flag: i32,
    pub session_active: i32,
    pub mode: i32,
    pub is_player: bool,
    pub base: [i32; 2],
    pub region_min: [i32; 2],
    pub region_max: [i32; 2],
    pub player_multiplier: u16,
    pub player_shift: u32,
    pub other_multiplier: u16,
    pub other_shift: u32,
    pub penetration_shift: u32,
    pub auxiliary_query_type: [i32; 2],
}

impl State {
    /// Convenient entry for already-known geometry classifications. The resolved
    /// entry below is used by the world to preserve lazy query order and errors.
    pub fn step(&mut self, config: &contact::Config, env: &Environment) {
        let result: Result<(), Infallible> = self.step_resolved(config, env, |side, kind| {
            *kind = env.auxiliary_query_type[side];
            Ok(())
        });
        match result {
            Ok(()) => {}
            Err(impossible) => match impossible {},
        }
    }

    pub fn step_resolved<E>(
        &mut self,
        config: &contact::Config,
        env: &Environment,
        mut resolve: impl FnMut(usize, &mut i32) -> Result<(), E>,
    ) -> Result<(), E> {
        let half = env.environment_flag != 0 && env.session_active == 0;
        let region =
            (0..2).all(|i| env.base[i] >= env.region_min[i] && env.base[i] <= env.region_max[i]);
        let modified = (env.mode == 5 && region) || half;
        self.tilt = [0; 2];
        self.penetrating_input_sum = 0;
        self.travel_sum = 0;
        self.max_penetration = 0;
        self.last_penetration = 0;

        for (i, point) in self.points.iter_mut().enumerate() {
            let mut force = 0_i32;
            if point.kind == 2 {
                force = if modified {
                    let (multiplier, shift) = if env.is_player {
                        (env.player_multiplier, env.player_shift)
                    } else {
                        (env.other_multiplier, env.other_shift)
                    };
                    let force = i32::from(multiplier)
                        .wrapping_mul(4915200)
                        .wrapping_shr(shift)
                        .wrapping_mul(env.dt)
                        .wrapping_neg();
                    if half { force >> 1 } else { force }
                } else {
                    (-4915200_i32).wrapping_mul(env.dt)
                };
            }
            if point.kind == 0 {
                let penetration = (-75_i32).wrapping_sub(point.gap);
                point.travel = point
                    .travel
                    .wrapping_add(penetration.wrapping_shl(env.penetration_shift));
                if point.travel > config.upper {
                    self.last_penetration = trunc_shift(
                        point.travel.wrapping_sub(config.upper),
                        env.penetration_shift,
                    );
                    self.max_penetration = self.max_penetration.max(self.last_penetration);
                    point.travel = config.upper;
                }
                force = force.wrapping_add(penetration.min(400).wrapping_mul(10240));
                self.penetrating_input_sum = self.penetrating_input_sum.wrapping_add(force);
            }
            let mut contact = contact::State {
                travel: point.travel,
                rate: point.rate,
                output: point.output,
                tilt: self.tilt,
            };
            let side = i / 2;
            if force <= 0 && self.auxiliary_cached[side] == 0 {
                resolve(side, &mut self.auxiliary_type[side])?;
                self.auxiliary_cached[side] = 1;
            }
            contact.step(
                config,
                force,
                env.dt,
                point.corner,
                self.auxiliary_type[side],
            );
            point.travel = contact.travel;
            point.rate = contact.rate;
            point.output = contact.output;
            self.tilt = contact.tilt;
            self.travel_sum = self.travel_sum.wrapping_add(point.travel);
        }
        self.combine();
        if self.max_penetration > 0 {
            self.vertical_delta = self.vertical_delta.max(0);
            if self.world_vy < 0 {
                self.world_vy = trunc_shift(self.world_vy, 1);
            }
            // Maximum gates this branch; LAST supplies the actual lift amount.
            self.position_y = self
                .position_y
                .wrapping_add(self.last_penetration.wrapping_add(1));
        }
        if self.timer > 0 {
            self.timer = self.timer.wrapping_sub(env.dt);
        }
        Ok(())
    }

    pub fn combine(&mut self) {
        let [a, b, c, d] = self.points.map(|point| point.output);
        self.angular[0] = self.angular[0].wrapping_sub(self.tilt[0]);
        self.angular[1] = self.angular[1].wrapping_sub(self.tilt[1]);
        self.vertical_delta = agree(a.wrapping_add(c), b.wrapping_add(d))
            .wrapping_add(agree(a.wrapping_add(b), c.wrapping_add(d)));
    }
}

/// Same sign, smaller magnitude; opposite signs produce no shared response.
pub fn agree(a: i32, b: i32) -> i32 {
    if a > 0 && b > 0 {
        a.min(b)
    } else if a < 0 && b < 0 {
        a.max(b)
    } else {
        0
    }
}

pub fn layout(kinds: [i32; 4], config: [i32; 5]) -> [i32; 3] {
    let [g0, g1, g2, g3] = kinds.map(|kind| kind == 0 || kind == 1);
    let (left, right, front, rear) = (g0 || g1, g2 || g3, g0 || g2, g1 || g3);
    [
        if left && !right {
            config[0]
        } else if right && !left {
            config[3]
        } else {
            0
        },
        if left || right { config[1] } else { 0 },
        if rear && !front {
            config[2]
        } else if front && !rear {
            config[4]
        } else {
            0
        },
    ]
}

/// Grounded-point load, evaluated BEFORE the suspension changes travel.
pub fn load(
    kinds: [i32; 4],
    travel: [i32; 4],
    base: i32,
    floor: i32,
    multiplier: u16,
    shift: u32,
) -> i32 {
    let mut total = 0_i32;
    for (kind, travel) in kinds.into_iter().zip(travel) {
        if kind == 0 || kind == 1 {
            let value = base.wrapping_add(travel);
            if value > 0 {
                total = total.wrapping_add(trunc_shift(
                    value.wrapping_mul(i32::from(multiplier)),
                    shift,
                ));
            }
            total = total.wrapping_add(floor);
        }
    }
    total
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lift_uses_last_penetration_and_halves_downward_velocity_towards_zero() {
        let mut state = State {
            points: [
                Point {
                    kind: 0,
                    gap: -275,
                    ..Point::default()
                },
                Point {
                    kind: 0,
                    gap: -185,
                    ..Point::default()
                },
                Point {
                    kind: 3,
                    ..Point::default()
                },
                Point {
                    kind: 3,
                    ..Point::default()
                },
            ],
            world_vy: -5,
            position_y: 1000,
            timer: 1,
            ..State::default()
        };
        state.step(
            &contact::Config {
                lower: -100,
                upper: 100,
                ..contact::Config::default()
            },
            &Environment {
                dt: 3,
                ..Environment::default()
            },
        );
        assert_eq!((state.max_penetration, state.last_penetration), (100, 10));
        assert_eq!(state.position_y, 1011);
        assert_eq!(state.world_vy, -2);
        assert_eq!(state.timer, -2); // Original subtracts dt; does not clamp at zero.
    }

    #[test]
    fn resolver_is_lazy_and_error_preserves_earlier_point_updates() {
        let mut state = State {
            points: std::array::from_fn(|i| Point {
                kind: if i == 0 { 0 } else { 3 },
                gap: -175,
                travel: 1,
                corner: i as u32,
                ..Point::default()
            }),
            auxiliary_type: [2, 1],
            ..State::default()
        };
        let mut calls = Vec::new();
        let result = state.step_resolved(
            &contact::Config {
                lower: -10000,
                upper: 10000,
                ..contact::Config::default()
            },
            &Environment::default(),
            |side, kind| {
                calls.push(side);
                *kind = 3;
                if side == 1 { Err(-9) } else { Ok(()) }
            },
        );
        assert_eq!(result, Err(-9));
        assert_eq!(calls, [0, 1]); // Point 0's positive force used stale auxiliary 2.
        assert_eq!(state.points[0].travel, 101);
        assert_eq!(state.travel_sum, 102); // Points 0/1 committed, point 2 did not.
        assert_eq!(state.auxiliary_type, [3, 3]); // Failed resolver's write survives.
        assert_eq!(state.auxiliary_cached, [1, 0]);
    }
}
