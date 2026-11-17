//! Original 4057F0 Chasecam, with typed state and no writes to the vehicle.
//! Audio/listener publication and world membership remain platform/session work.
use crate::{
    fixed::trunc_shift,
    math::{self, Axis, Matrix},
    vehicle_startup::Player,
};
pub use tankracer_assets::object_catalog::Camera as Config;

#[derive(Default, Debug, Clone, Copy)]
pub struct Keys {
    pub cycle: bool,
    pub rear: bool,
}
#[derive(Default, Debug, Clone, Copy)]
pub struct Frame {
    pub dt: i32,
    pub keys: Keys,
    pub starting: bool,
    pub overhead: bool,
    pub hold_displacement: bool,
    pub force_restore: bool,
}
#[derive(Debug, Clone, Copy)]
pub struct Subject {
    pub base: [i32; 3],
    pub physical: [i32; 3],
    pub basis: Matrix,
}
impl From<&Player> for Subject {
    fn from(player: &Player) -> Self {
        let poses = &player.physics.motion.poses;
        Self {
            base: poses.base_position,
            physical: poses.physical_position,
            basis: poses.reference,
        }
    }
}
#[derive(Default, Debug, Clone, Copy, PartialEq, Eq)]
pub struct Shake {
    pub amplitude: i32,
    pub position: i32,
    pub speed: i32,
    pub remaining: i16,
    pub duration: i16,
    pub rising: bool,
}
impl Shake {
    /// 4055D0 keeps the oscillator phase when another impulse arrives.
    pub fn start(&mut self, duration_ticks: u32, amplitude: i32) {
        self.amplitude = amplitude;
        self.duration = (duration_ticks as i16).min(2000);
        self.remaining = duration_ticks as i16;
    }
    fn step(&mut self, dt: i32) -> Result<(), math::Error> {
        let scale = i32::from(self.remaining)
            .wrapping_mul(1024)
            .checked_div(i32::from(self.duration))
            .ok_or(math::Error::Arithmetic)?
            .min(1024);
        let limit = self.amplitude.wrapping_mul(scale) >> 10;
        if self.remaining != 1 {
            self.remaining = self.remaining.wrapping_sub(dt as i16).max(1);
        }
        self.speed = if self.rising {
            self.speed.wrapping_add(dt.wrapping_mul(256)).min(3000)
        } else {
            self.speed.wrapping_sub(dt.wrapping_mul(256)).max(-3000)
        };
        self.position = self.position.wrapping_add(dt.wrapping_mul(self.speed) >> 8);
        if (self.rising && self.position > 0) || (!self.rising && self.position < 0) {
            self.rising = !self.rising;
            if self.remaining == 1 {
                self.remaining = 0;
                self.position = 0;
                self.speed = 0;
            }
        }
        self.position = self.position.max(limit.wrapping_neg()).min(limit);
        Ok(())
    }
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct State {
    pub position: [i32; 3],
    pub matrix: Matrix,
    pub mode: u32,
    pub angles: [i32; 3],
    pub pitch_offset: i32,
    pub displacement: [i32; 3],
    pub cockpit_angles: [i32; 3],
    pub cockpit_local: [i32; 3],
    pub cycle_held: bool,
    pub rear: bool,
    pub shake: Shake,
}
fn wrap(value: i32) -> i32 {
    let mut n = i64::from(value);
    if n > 32760 {
        n -= ((n + 32759) / 65520) * 65520;
    }
    if n < -32760 {
        n += ((-n + 32759) / 65520) * 65520;
    }
    n as i32
}
fn approach(value: i32, target: i32, packed: u32, dt: i32) -> i32 {
    let amount = packed.wrapping_mul(u32::from(dt as u16)) as i16 as i32;
    let factor = ((packed >> 16) as u8).wrapping_mul(dt as u8);
    let mut delta = value.wrapping_sub(target);
    delta = if delta > amount {
        delta.wrapping_sub(amount)
    } else if delta < amount.wrapping_neg() {
        delta.wrapping_add(amount)
    } else {
        0
    };
    let before = delta;
    if delta != 0 {
        delta = delta.wrapping_sub(trunc_shift(
            delta.wrapping_mul(i32::from(factor)),
            packed >> 24,
        ));
        if (before < 0 && delta > 0) || (before > 0 && delta < 0) {
            delta = 0;
        }
    }
    delta.wrapping_add(target)
}
fn body_angles(basis: Matrix) -> Result<[i32; 3], math::Error> {
    let m = basis.0;
    Ok([
        math::angle(m[7], math::length2(m[6], m[8]))?,
        math::angle(m[6], m[8])?.wrapping_neg(),
        math::angle(m[1], math::length2(m[0], m[2]))?.wrapping_neg(),
    ])
}
impl State {
    pub fn new(config: &Config, subject: Subject) -> Result<Self, math::Error> {
        let target = body_angles(subject.basis)?;
        let angles = [target[0], target[1], 0];
        let matrix = Matrix::from_angles([
            angles[0].wrapping_add(config.pitch[0]) as u32,
            angles[1] as u32,
            0,
        ]);
        let offset = matrix.local_to_world(config.near);
        Ok(Self {
            position: std::array::from_fn(|i| subject.base[i].wrapping_add(offset[i])),
            matrix,
            mode: 0,
            angles,
            pitch_offset: config.pitch[0],
            displacement: config.near,
            cockpit_angles: [0; 3],
            cockpit_local: [0; 3],
            cycle_held: false,
            rear: false,
            shake: Shake::default(),
        })
    }
    pub fn tick(
        &mut self,
        config: &Config,
        subject: Subject,
        frame: Frame,
    ) -> Result<(), math::Error> {
        let Frame {
            dt,
            keys,
            starting,
            overhead,
            hold_displacement,
            force_restore,
        } = frame;
        if dt <= 0 {
            return Err(math::Error::Arithmetic);
        }
        if keys.cycle && !starting {
            if !self.cycle_held {
                self.mode = (self.mode + 1) % 3;
                self.pitch_offset = config.pitch[0];
            }
            self.cycle_held = true;
        } else {
            self.cycle_held = false;
        }
        self.rear = keys.rear && !starting;
        let [pitch, yaw, roll] = body_angles(subject.basis)?;
        let view_mode = usize::from(self.mode == 1);
        let goal_y = if self.mode == 2 {
            config.near[1]
        } else {
            [config.near[1], config.far[1]][view_mode]
        };
        let goal_z = if overhead {
            config.overhead[2]
        } else if self.mode == 2 {
            config.near[2]
        } else {
            [config.near[2], config.far[2]][view_mode]
        };
        let goal_pitch = if overhead {
            config.overhead_pitch
        } else {
            config.pitch[view_mode]
        };
        let (origin, offset) = if self.mode == 2 {
            self.cockpit_angles[0] =
                approach(self.cockpit_angles[0], pitch, config.cockpit_drag[0], dt);
            self.cockpit_angles[1] = yaw;
            self.cockpit_angles[2] =
                approach(self.cockpit_angles[2], roll, config.cockpit_drag[1], dt);
            let mut angles = self.cockpit_angles;
            if self.shake.remaining != 0 {
                self.shake.step(dt)?;
                for a in &mut angles {
                    *a = a.wrapping_add(self.shake.position);
                }
            }
            self.matrix = Matrix::from_angles(angles.map(|a| a as u32));
            if self.rear {
                self.matrix.rotate(Axis::Y, 32760);
            }
            let local =
                std::array::from_fn(|i| self.cockpit_local[i].wrapping_add(config.cockpit[i]));
            (subject.physical, self.matrix.local_to_world(local))
        } else {
            let delta = wrap(self.angles[1].wrapping_sub(yaw));
            if delta != 0 {
                let correction = delta
                    .wrapping_mul(dt)
                    .wrapping_mul(config.rotation_speed[1])
                    .checked_div(config.yaw_scale)
                    .ok_or(math::Error::Arithmetic)?;
                self.angles[1] = self.angles[1].wrapping_sub(correction);
                let next = wrap(self.angles[1].wrapping_sub(yaw));
                if (delta > 0 && next < 0) || (delta < 0 && next > 0) {
                    self.angles[1] = yaw;
                }
            }
            let delta = wrap(self.angles[0].wrapping_sub(pitch));
            if delta != 0 {
                let temp = delta
                    .wrapping_mul(dt)
                    .wrapping_mul(config.rotation_speed[0]);
                let high = ((i64::from(temp) * -966603629_i64) >> 32) as i32;
                let correction = (high >> 10).wrapping_add(((high as u32) >> 31) as i32);
                self.angles[0] = self.angles[0].wrapping_add(correction);
                let next = wrap(self.angles[0].wrapping_sub(pitch));
                if (delta > 0 && next < 0) || (delta < 0 && next > 0) {
                    self.angles[0] = pitch;
                }
            }
            let mut angles = self.angles;
            if self.rear {
                angles[0] = goal_pitch.wrapping_sub(angles[0]);
                angles[1] = angles[1].wrapping_add(32760);
            } else {
                angles[0] = angles[0].wrapping_add(self.pitch_offset);
            }
            if self.shake.remaining != 0 {
                self.shake.step(dt)?;
                for a in &mut angles {
                    *a = a.wrapping_add(self.shake.position);
                }
            }
            self.matrix = Matrix::from_angles(angles.map(|a| a as u32));
            (subject.base, self.matrix.local_to_world(self.displacement))
        };
        self.position = std::array::from_fn(|i| {
            origin[i].wrapping_add(offset[i]).wrapping_add(
                self.shake.position
                    >> if self.mode == 2 {
                        if i == 1 { 1 } else { 2 }
                    } else if i == 1 {
                        0
                    } else {
                        1
                    },
            )
        });
        if !hold_displacement || force_restore {
            if self.displacement[2] != goal_z {
                self.displacement[2] = approach(self.displacement[2], goal_z, 0x09010001, dt);
            }
            if self.displacement[1] != goal_y {
                self.displacement[1] = approach(self.displacement[1], goal_y, 0x09010001, dt);
            }
            if self.mode != 2 && self.pitch_offset != goal_pitch {
                self.pitch_offset = approach(self.pitch_offset, goal_pitch, 0x09010001, dt);
            }
        }
        Ok(())
    }
    /// 433D10 camera+78 feedback is consumed only by the following camera tick.
    pub fn feedback(&mut self, turret_offset: [i32; 3]) {
        if self.mode == 2 {
            self.cockpit_local = turret_offset;
        }
    }
    pub fn published_position(&self) -> [i32; 3] {
        self.position.map(|p| p >> 3)
    }
    pub fn forward(&self) -> [i32; 3] {
        [self.matrix.0[6], self.matrix.0[7], self.matrix.0[8]]
    }
}
