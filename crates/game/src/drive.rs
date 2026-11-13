//! Body damping, engine/gears, traction and steering (430C51..431E56).
//! Yaw correction, world-velocity conversion and integration are separate stages.
use crate::fixed::{Coefficient, damp, decay, rate_step, trunc_shift};
use tankracer_assets::config::{Damping, Drive, Gain, Surface, Surfaces};

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Input {
    pub throttle: i32,
    pub brake: i32,
    pub steering: i32,
    pub action_flags: u32,
    pub alternate: bool,
    pub alternate_lateral: i16,
    pub alternate_rotation: i16,
}
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct State {
    pub velocity: [i32; 3],
    pub angular: [i32; 3],
    pub track_speed: i32,
    pub boost_target: i32,
    pub engine_display: i32,
    pub slip: i32,
    pub was_active: u8,
    pub gear: i8,
    pub lateral_velocity: i32,
    pub acceleration: [i32; 2],
    pub load: i32,
}
#[derive(Debug, Clone, Copy, Default)]
pub struct Environment {
    pub dt: i32,
    pub mode: i32,
    pub player: bool,
    pub active: bool,
    pub launch_eligible: bool,
    pub materials: [usize; 2],
    pub kinds: [i32; 4],
    pub contact_y: [i32; 4],
    pub airborne: i8,
    pub boost_charge: i16,
    pub boost_power: i32,
    pub extra_drive: i32,
}
#[derive(Debug, Clone)]
pub struct Tuning {
    pub gear_limits: [i32; 5],
    pub gear_offsets: [i32; 5],
    pub gear_gain: [Gain; 5],
    pub engine_damping: Damping,
    pub boost_approach: Damping,
    pub boost_decay: Damping,
}
impl Default for Tuning {
    fn default() -> Self {
        Self {
            gear_limits: [215000, 165000, 135000, 125000, 110000],
            gear_offsets: [0, 60000, 90000, 120000, 140000],
            gear_gain: [
                Gain {
                    multiplier: 3,
                    shift: 0,
                },
                Gain {
                    multiplier: 1750,
                    shift: 10,
                },
                Gain {
                    multiplier: 1200,
                    shift: 10,
                },
                Gain {
                    multiplier: 1050,
                    shift: 10,
                },
                Gain {
                    multiplier: 1,
                    shift: 0,
                },
            ],
            engine_damping: Damping {
                step: 400,
                fraction: 1,
                shift: 10,
            },
            boost_approach: Damping {
                step: 400,
                fraction: 1,
                shift: 12,
            },
            boost_decay: Damping {
                step: 75,
                fraction: 1,
                shift: 12,
            },
        }
    }
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    InvalidWeather,
    MissingSurface(usize),
    GearLimit,
}

fn gain(value: i32, descriptor: Gain) -> i32 {
    Coefficient::from(descriptor).apply_truncated(value)
}
fn average(value: i32, surfaces: [&Surface; 2], factor: impl Fn(&Surface) -> Gain) -> i32 {
    gain(value, factor(surfaces[0])).wrapping_add(gain(value, factor(surfaces[1]))) >> 1
}
fn with_force(force: i32, descriptor: Damping) -> Damping {
    Damping {
        step: force as i16,
        ..descriptor
    }
}
fn approach(value: i32, target: i32, descriptor: Damping, dt: i32) -> i32 {
    let scaled = u32::from(descriptor.fraction).wrapping_mul(dt as u32) as u8;
    let force = (i32::from(descriptor.step).wrapping_mul(dt) as i16) as i32;
    let remaining = rate_step(value.wrapping_sub(target), force, 1);
    let result = remaining.wrapping_sub(trunc_shift(
        remaining.wrapping_mul(i32::from(scaled)),
        u32::from(descriptor.shift),
    ));
    let result = if (remaining < 0 && result > 0) || (remaining > 0 && result < 0) {
        0
    } else {
        result
    };
    result.wrapping_add(target)
}

impl State {
    fn update_gear(&mut self, tuning: &Tuning, dt: i32) -> Result<(), Error> {
        let mut remaining = self.track_speed.wrapping_abs();
        let mut gear = 0;
        let mut count = 0;
        if remaining > tuning.gear_limits[0] {
            loop {
                remaining = remaining.wrapping_sub(tuning.gear_limits[gear]);
                if gear < 4 {
                    gear += 1;
                }
                count += 1;
                if count > 65536 {
                    return Err(Error::GearLimit);
                }
                if remaining <= tuning.gear_limits[gear] {
                    break;
                }
            }
        }
        self.gear = gear as i8;
        let target = remaining.wrapping_add(tuning.gear_offsets[gear]);
        self.engine_display = target.wrapping_add(damp(
            self.engine_display.wrapping_sub(target),
            tuning.engine_damping,
            dt,
        ));
        Ok(())
    }

    pub fn step(
        &mut self,
        config: &Drive,
        surfaces: &Surfaces,
        tuning: &Tuning,
        input: &Input,
        env: &Environment,
    ) -> Result<(), Error> {
        let [a, b] = env.materials;
        let surfaces = [
            surfaces.entries.get(a).ok_or(Error::MissingSurface(a))?,
            surfaces.entries.get(b).ok_or(Error::MissingSurface(b))?,
        ];
        let dt = env.dt;
        let old_x = self.velocity[0];
        let old_z = self.velocity[2];
        let airborne = i32::from(env.airborne);
        let flags = input.action_flags;
        let lateral = if env.player && input.alternate {
            i32::from(config.sideways_drag.step)
                .wrapping_mul(i32::from(input.alternate_lateral))
                .wrapping_shr(u32::from(surfaces[0].slide[0].shift as u8))
        } else {
            let slide = if env.player && flags & 3 != 0 {
                if flags & 56 == 0 {
                    return Err(Error::InvalidWeather);
                }
                (if flags & 1 != 0 { 1 } else { 4 })
                    + if flags & 8 != 0 {
                        0
                    } else if flags & 16 != 0 {
                        1
                    } else {
                        2
                    }
            } else {
                0
            };
            average(i32::from(config.sideways_drag.step), surfaces, |s| {
                s.slide[slide]
            })
        };
        let force = trunc_shift(
            lateral
                .wrapping_mul(i32::from(config.sidegrip_fraction.multiplier))
                .wrapping_mul(self.load),
            u32::from(config.sidegrip_fraction.shift as u8),
        );
        self.velocity[0] = damp(old_x, with_force(force, config.sideways_drag), dt);
        self.velocity[1] = rate_step(self.velocity[1], i32::from(config.gravity_drag.step), dt);
        let force = average(i32::from(config.forward_drag.step), surfaces, |s| s.drag);
        self.velocity[2] = damp(self.velocity[2], with_force(force, config.forward_drag), dt);
        self.angular[0] = damp(self.angular[0], config.air_rotational_drag, dt);
        self.angular[2] = damp(self.angular[2], config.air_rotational_drag, dt);
        // Alternate control intentionally reuses FORWARD damping's high bytes.
        let descriptor = if input.alternate {
            config.forward_drag
        } else {
            config.rotational_drag
        };
        let force = if input.alternate {
            gain(i32::from(input.alternate_rotation), surfaces[0].grip)
        } else {
            average(i32::from(config.rotational_drag.step), surfaces, |s| s.grip)
        };
        let force = trunc_shift((4_i32).wrapping_sub(airborne).wrapping_mul(force), 2);
        self.angular[1] = damp(self.angular[1], with_force(force, descriptor), dt);
        self.lateral_velocity = self.velocity[0];
        if env.active && input.throttle == 0 && input.brake == 0 {
            let difference = env.contact_y[1]
                .wrapping_add(env.contact_y[3])
                .wrapping_sub(env.contact_y[0].wrapping_add(env.contact_y[2]));
            self.velocity[2] = self.velocity[2].wrapping_add(gain(difference, config.hill_roll));
        }
        let descriptor = if airborne == 4 {
            config.track_air_drag
        } else if self.slip != 0 {
            config.track_spin_drag
        } else {
            with_force(
                average(i32::from(config.track_drag.step), surfaces, |s| s.drag),
                config.track_drag,
            )
        };
        self.track_speed = decay(self.track_speed, descriptor.shift, dt);
        if self.was_active != u8::from(env.active) {
            self.was_active = u8::from(env.active);
            if env.active && env.launch_eligible {
                self.track_speed = 314488;
                self.boost_target = 314488;
                self.velocity[2] = 19687;
            }
        }
        let budget = average(
            (if self.boost_target != 0 {
                20000
            } else {
                config.catchup_traction_limit
            })
            .wrapping_mul(dt),
            surfaces,
            |s| s.grip,
        );
        if self.boost_target != 0 {
            if self.track_speed < self.boost_target {
                self.track_speed = approach(
                    self.track_speed,
                    self.boost_target,
                    tuning.boost_approach,
                    dt,
                );
            }
            self.boost_target = damp(self.boost_target, tuning.boost_decay, dt);
            if self.velocity[2] < 314488 {
                self.velocity[2] = self.velocity[2]
                    .wrapping_add(dt.wrapping_mul(512))
                    .min(314488);
            }
        }
        let v = self.velocity[2];
        let mut drive = self.track_speed;
        if v > 0 && v > drive {
            drive = drive.wrapping_add(v.wrapping_sub(drive).min(budget));
        } else if v < 0 && v.wrapping_neg() > drive.wrapping_neg() {
            drive = drive.wrapping_sub(drive.wrapping_sub(v).min(budget));
        }
        self.track_speed = drive;
        self.update_gear(tuning, dt)?;
        let square = (drive >> 12).wrapping_mul(drive >> 12);
        let power_mul = if env.boost_charge < 2501 {
            config.power_mul
        } else {
            config.turbo_power_mul
        };
        let mut cap = config
            .initial_power
            .wrapping_sub(power_mul.wrapping_mul(square));
        if env.boost_charge >= 2501 {
            cap = cap.wrapping_add(env.boost_power);
        }
        cap = cap.max(0).min(config.max_power);
        let mut term = (cap >> 8)
            .wrapping_mul(input.throttle >> 8)
            .wrapping_mul(dt);
        if airborne == 4 {
            term = term.wrapping_mul(4);
        }
        drive = self
            .track_speed
            .wrapping_add(gain(term >> 15, tuning.gear_gain[self.gear as usize]));
        let base = if input.throttle.wrapping_abs() < 1024 {
            dt.wrapping_mul(16)
        } else {
            0
        };
        let braking = trunc_shift(
            (4_i32)
                .wrapping_sub(airborne)
                .wrapping_mul(config.max_brakes >> 23)
                .wrapping_mul(input.brake >> 8)
                .wrapping_mul(dt),
            2,
        );
        if drive > 0 {
            drive = drive.wrapping_sub(base.wrapping_add(braking)).max(0);
        } else {
            drive = drive.wrapping_add(base.wrapping_add(braking)).min(0);
        }
        self.track_speed = drive.max(config.max_rpm.wrapping_neg()).min(config.max_rpm);
        let delta = if airborne == 4 {
            0
        } else {
            self.track_speed.wrapping_sub(self.velocity[2])
        };
        let fallback = env.kinds.contains(&4);
        let traction = if (env.mode == 4 || env.mode == 5) && fallback {
            400
        } else {
            config.traction_limit
        };
        let budget = average(traction.wrapping_mul(dt), surfaces, |s| s.grip);
        let mut applied;
        if env.player
            && [0, 1, 5].contains(&env.mode)
            && self.velocity[2].wrapping_abs() < 300000
            && input.brake == 0
            && self.boost_target == 0
            && !fallback
        {
            if delta > budget {
                applied = budget.wrapping_mul(2);
                self.slip = delta.wrapping_sub(applied);
            } else if delta < budget.wrapping_neg() {
                applied = budget.wrapping_mul(2).wrapping_neg();
                self.slip = delta.wrapping_sub(applied);
            } else {
                applied = delta;
                self.slip = 0;
            }
        } else {
            applied = if delta > budget {
                budget
            } else if delta < budget.wrapping_neg() {
                budget.wrapping_neg()
            } else {
                delta
            };
            self.slip = delta.wrapping_sub(applied);
        }
        if env.active {
            applied = trunc_shift(
                (4_i32)
                    .wrapping_sub(airborne)
                    .wrapping_mul(applied.wrapping_add(env.extra_drive)),
                2,
            );
        }
        self.velocity[2] = self.velocity[2].wrapping_add(applied);
        self.acceleration = [
            self.velocity[0].wrapping_sub(old_x),
            self.velocity[2].wrapping_sub(old_z),
        ];
        self.load = trunc_shift(self.load, 8);
        let term = trunc_shift(input.steering.wrapping_mul(config.max_turn), 8);
        let term = trunc_shift(
            term.wrapping_mul(i32::from(config.turn_fraction.multiplier))
                .wrapping_mul(self.load),
            u32::from(config.turn_fraction.shift as u8),
        );
        self.angular[1] = self.angular[1]
            .wrapping_sub(dt.wrapping_mul(term))
            .wrapping_add(gain(
                self.velocity[0].wrapping_neg(),
                config.understeer_fraction,
            ));
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn alternate_yaw_uses_forward_descriptor_high_bytes() {
        let mut config = Drive::default();
        config.forward_drag.shift = 14;
        config.rotational_drag.shift = 9;
        let initial = State {
            angular: [0, 100000, 0],
            ..State::default()
        };
        let mut state = initial;
        let env = Environment {
            dt: 17,
            player: true,
            ..Environment::default()
        };
        state
            .step(
                &config,
                &Surfaces::default(),
                &Tuning::default(),
                &Input {
                    alternate: true,
                    ..Input::default()
                },
                &env,
            )
            .unwrap();
        assert_eq!(state.angular[1], 99897);
        let mut state = initial;
        state
            .step(
                &config,
                &Surfaces::default(),
                &Tuning::default(),
                &Input::default(),
                &env,
            )
            .unwrap();
        assert_eq!(state.angular[1], 96680);
    }

    #[test]
    fn gear_threshold_is_strict_and_failure_does_not_commit_display_or_gear() {
        let mut state = State {
            track_speed: 215000,
            ..State::default()
        };
        state.update_gear(&Tuning::default(), 0).unwrap();
        assert_eq!(state.gear, 0);
        state.track_speed = 215001;
        state.update_gear(&Tuning::default(), 0).unwrap();
        assert_eq!(state.gear, 1);
        state.track_speed = 100;
        state.engine_display = 99;
        let tuning = Tuning {
            gear_limits: [0; 5],
            ..Tuning::default()
        };
        assert_eq!(state.update_gear(&tuning, 0), Err(Error::GearLimit));
        assert_eq!(state.gear, 1);
        assert_eq!(state.engine_display, 99);
    }

    #[test]
    fn surface_channels_truncate_independently_then_average_arithmetically() {
        let a = Surface {
            drag: Gain {
                multiplier: 1,
                shift: 0,
            },
            ..Surface::default()
        };
        let b = Surface {
            drag: Gain {
                multiplier: 1,
                shift: 1,
            },
            ..Surface::default()
        };
        assert_eq!(average(-1, [&a, &b], |s| s.drag), -1); // (-1 + 0) >> 1.
    }
}
