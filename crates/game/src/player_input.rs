//! CUP Player input before Tank dispatch (41FAEA..420B03).
//! Device bindings/axis normalization are external; ramping and launch rules
//! are gameplay. Deathmatch restoration and the enclosing tick are not this
//! boundary. Countdown RPM belongs to the existing vehicle prefix.
use crate::{drive, fixed::rate_step};

#[derive(Debug, Clone, Copy)]
pub enum Action {
    Left = 1,
    Right = 2,
    Throttle = 5,
    Brake = 6,
    Auxiliary = 15,
}
#[derive(Debug, Clone, Copy)]
pub enum Axis {
    Steering,
    Longitudinal,
}
pub trait Device {
    fn down(&mut self, action: Action) -> Result<bool, i32>;
    fn axis(&mut self, axis: Axis) -> Result<i32, i32>;
}
/// Sample once per fixed update; key repeat/render timing do not own rates.
#[derive(Debug, Clone, Copy, Default)]
pub struct Keys {
    pub left: bool,
    pub right: bool,
    pub throttle: bool,
    pub brake: bool,
    pub auxiliary: bool,
}
impl Device for Keys {
    fn down(&mut self, action: Action) -> Result<bool, i32> {
        Ok(match action {
            Action::Left => self.left,
            Action::Right => self.right,
            Action::Throttle => self.throttle,
            Action::Brake => self.brake,
            Action::Auxiliary => self.auxiliary,
        })
    }
    fn axis(&mut self, _: Axis) -> Result<i32, i32> {
        Ok(0)
    }
}
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct State {
    pub drive: drive::Input,
    pub start_held: u8,
    pub fast_start: u8,
}
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Shared {
    pub steering_analog_latch: bool,
}
#[derive(Debug, Clone, Copy)]
pub struct Tuning {
    pub push_throttle: i32,
    pub push_brake: i32,
    pub push_steer: i32,
    pub return_throttle: i16,
    pub return_brake: i16,
    pub return_steer: i32,
}
impl Default for Tuning {
    fn default() -> Self {
        Self {
            push_throttle: 800,
            push_brake: 70,
            push_steer: 800,
            return_throttle: 720,
            return_brake: 720,
            return_steer: 700,
        }
    }
}
#[derive(Debug, Clone, Copy)]
pub struct Frame {
    pub dt: i32,
    pub driving: bool,
    pub player: i8,
    pub speed: i32,
    pub start_stage: i32,
    pub analog: bool,
    pub analog_longitudinal: bool,
    pub twin_tracks: bool,
    pub track_values: [i32; 2],
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Outcome {
    pub driving: bool,
    pub auxiliary_down: bool,
}
fn down(device: &mut impl Device, player: i8, action: Action) -> Result<bool, i32> {
    if player == 0 {
        device.down(action)
    } else {
        Ok(false)
    }
}
impl State {
    fn start_press(&mut self, stage: i32) {
        if stage == 1 {
            self.start_held = 1;
        } else if stage == 2 && self.start_held == 0 {
            self.fast_start = 1;
        }
    }
    pub fn step(
        &mut self,
        countdown: &mut i32,
        shared: &mut Shared,
        frame: Frame,
        tuning: Tuning,
        device: &mut impl Device,
    ) -> Result<Outcome, i32> {
        let mut outcome = Outcome {
            driving: frame.driving,
            auxiliary_down: false,
        };
        if frame.driving {
            outcome.auxiliary_down = down(device, frame.player, Action::Auxiliary)?;
            self.driving(shared, frame, tuning, device)?;
            self.drive.throttle = self.drive.throttle.clamp(-65535, 65535);
            self.drive.brake = self.drive.brake.clamp(0, 65535);
            self.drive.steering = self.drive.steering.clamp(-65535, 65535);
        } else if frame.start_stage != 4 {
            self.countdown(countdown, frame, tuning, device)?;
        }
        Ok(outcome)
    }
    fn countdown(
        &mut self,
        countdown: &mut i32,
        f: Frame,
        t: Tuning,
        device: &mut impl Device,
    ) -> Result<(), i32> {
        let throttle = down(device, f.player, Action::Throttle)?;
        if throttle {
            *countdown = countdown.wrapping_add(t.push_throttle.wrapping_mul(f.dt));
            self.start_press(f.start_stage);
        } else {
            *countdown = rate_step(*countdown, i32::from(t.return_throttle), f.dt);
            if (1..3).contains(&f.start_stage) && !f.analog {
                self.start_held = 0;
            }
        }
        let brake = down(device, f.player, Action::Brake)?;
        if brake {
            self.drive.brake = self
                .drive
                .brake
                .wrapping_add(t.push_brake.wrapping_mul(f.dt));
        } else {
            self.drive.brake = rate_step(self.drive.brake, i32::from(t.return_brake), f.dt);
        }
        if f.analog_longitudinal && f.analog && !throttle && !brake {
            let value = device.axis(Axis::Longitudinal)?;
            if value != 0 {
                *countdown = value.wrapping_mul(-2);
                if value < -16384 {
                    self.start_press(f.start_stage);
                } else if value < 0 && (1..3).contains(&f.start_stage) {
                    self.start_held = 0;
                }
            }
        }
        // Countdown repairs neither stale brake nor steering.
        *countdown = (*countdown).clamp(0, 65535);
        Ok(())
    }
    fn driving(
        &mut self,
        shared: &mut Shared,
        f: Frame,
        t: Tuning,
        device: &mut impl Device,
    ) -> Result<(), i32> {
        if f.analog && f.twin_tracks {
            let [left, right] = f.track_values;
            let opposite = left.wrapping_add(right).wrapping_neg();
            self.drive.steering = right.wrapping_sub(left);
            self.drive.throttle = 0;
            self.drive.brake = 0;
            if opposite > 0 && f.speed < 0 {
                self.drive.brake = opposite;
            } else if opposite <= 0 && f.speed > 0 {
                self.drive.brake = opposite.wrapping_neg();
            } else {
                self.drive.throttle = opposite;
            }
            return Ok(());
        }
        let reverse = down(device, f.player, Action::Brake)? && f.speed == 0 || f.speed < 0;
        let (push, stop, force) = if reverse {
            (
                Action::Brake,
                Action::Throttle,
                t.push_throttle.wrapping_neg(),
            )
        } else {
            (Action::Throttle, Action::Brake, t.push_throttle)
        };
        let throttle = down(device, f.player, push)?;
        self.drive.throttle = if throttle {
            self.drive.throttle.wrapping_add(force.wrapping_mul(f.dt))
        } else {
            rate_step(self.drive.throttle, i32::from(t.return_throttle), f.dt)
        };
        let brake = down(device, f.player, stop)?;
        self.drive.brake = if brake {
            self.drive
                .brake
                .wrapping_add(t.push_brake.wrapping_mul(f.dt))
        } else {
            rate_step(self.drive.brake, i32::from(t.return_brake), f.dt)
        };
        if f.analog_longitudinal && f.analog && !throttle && !brake {
            let value = device.axis(Axis::Longitudinal)?;
            if f.speed > 0 && value > 0 {
                self.drive.brake = value.wrapping_mul(2);
            } else if f.speed < 0 && value < 0 {
                self.drive.brake = value.wrapping_mul(-2);
            } else if value != 0 {
                self.drive.throttle = value.wrapping_mul(-2);
            }
        }
        // Queried even when this player's analog input is disabled.
        let axis = device.axis(Axis::Steering)?;
        if f.analog {
            if axis != 0 {
                shared.steering_analog_latch = true;
            }
            if shared.steering_analog_latch {
                self.drive.steering = axis.wrapping_mul(2);
            }
        }
        if down(device, f.player, Action::Left)? {
            self.drive.steering = self
                .drive
                .steering
                .wrapping_sub(t.push_steer.wrapping_mul(f.dt));
            shared.steering_analog_latch = false;
        } else if down(device, f.player, Action::Right)? {
            self.drive.steering = self
                .drive
                .steering
                .wrapping_add(t.push_steer.wrapping_mul(f.dt));
            shared.steering_analog_latch = false;
        } else if !shared.steering_analog_latch {
            let steer = self.drive.steering;
            let step = t.return_steer.wrapping_mul(f.dt);
            self.drive.steering = if steer > 0 {
                steer.wrapping_sub(step).max(0)
            } else if steer < 0 {
                steer.wrapping_add(step).min(0)
            } else {
                0
            };
        }
        Ok(())
    }
}
