//! World/body velocity -> drive -> rearweight yaw -> world/suspension velocity.
use crate::{drive, motion, velocity};
use tankracer_assets::config::{Drive, Surfaces};

/// Persistent drive fields not owned by motion. Angular rates, acceleration,
/// slip and lateral speed have one authoritative owner in `motion::State`.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct State {
    pub local_velocity: [i32; 3],
    pub track_speed: i32,
    pub boost_target: i32,
    pub engine_display: i32,
    pub was_active: u8,
    pub gear: i8,
    pub load: i32,
}
pub struct Config<'a> {
    pub drive: &'a Drive,
    pub surfaces: &'a Surfaces,
    pub tuning: &'a drive::Tuning,
    pub yaw: velocity::YawConfig,
}
pub struct Frame<'a> {
    pub input: &'a drive::Input,
    pub drive: &'a drive::Environment,
    pub vertical_delta: i32,
    pub yaw_lock: i32,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    InvalidTimeStep,
    Drive(drive::Error),
}
impl Error {
    pub fn status(self) -> i32 {
        match self {
            Self::InvalidTimeStep => -2,
            Self::Drive(drive::Error::GearLimit) => -3,
            Self::Drive(drive::Error::InvalidWeather) => -4,
            Self::Drive(drive::Error::MissingSurface(_)) => -1,
        }
    }
}
impl State {
    /// Read-only stage view for diagnostics and the qualified drive operation.
    /// Do not retain a second mutable copy alongside authoritative motion.
    pub fn drive_state(&self, motion: &motion::State) -> drive::State {
        drive::State {
            velocity: self.local_velocity,
            angular: motion.angular,
            track_speed: self.track_speed,
            boost_target: self.boost_target,
            engine_display: self.engine_display,
            slip: motion.rates.track_slip,
            was_active: self.was_active,
            gear: self.gear,
            lateral_velocity: motion.rates.lateral_velocity,
            acceleration: motion.acceleration,
            load: self.load,
        }
    }
    fn publish(&mut self, stage: drive::State, motion: &mut motion::State) {
        self.local_velocity = stage.velocity;
        self.track_speed = stage.track_speed;
        self.boost_target = stage.boost_target;
        self.engine_display = stage.engine_display;
        self.was_active = stage.was_active;
        self.gear = stage.gear;
        self.load = stage.load;
        motion.angular = stage.angular;
        motion.rates.track_slip = stage.slip;
        motion.rates.lateral_velocity = stage.lateral_velocity;
        motion.acceleration = stage.acceleration;
    }
    pub fn step(
        &mut self,
        motion: &mut motion::State,
        config: &Config<'_>,
        frame: &Frame<'_>,
    ) -> Result<(), Error> {
        if frame.drive.dt <= 0 {
            return Err(Error::InvalidTimeStep);
        }
        // Precision is chosen from the incoming world velocity ONCE; drive can
        // cross the threshold before the return transform, without reselecting.
        let large = velocity::large(motion.velocity);
        self.local_velocity = velocity::transform(
            motion.velocity,
            motion.poses.reference,
            velocity::Direction::WorldToBody,
            large,
        );
        let mut stage = self.drive_state(motion);
        let result = stage.step(
            config.drive,
            config.surfaces,
            config.tuning,
            frame.input,
            frame.drive,
        );
        // Unlike the attitude adapter, drive commits an error prefix directly.
        // Publish it before returning, but do not convert world velocity yet.
        self.publish(stage, motion);
        result.map_err(Error::Drive)?;
        if !frame.input.alternate && frame.yaw_lock < 1 {
            motion.angular[1] = velocity::correct_yaw(
                self.local_velocity,
                motion.acceleration[1],
                motion.angular[1],
                &config.yaw,
            );
        }
        motion.velocity = velocity::transform(
            self.local_velocity,
            motion.poses.reference,
            velocity::Direction::BodyToWorld,
            large,
        );
        motion.velocity[1] = motion.velocity[1].wrapping_add(frame.vertical_delta);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn drive_error_keeps_the_incoming_world_and_the_already_transformed_local_velocity() {
        let mut power = State {
            track_speed: 73,
            ..State::default()
        };
        let mut motion = motion::State {
            velocity: [17, 0, -17],
            angular: [1, 2, 3],
            ..motion::State::default()
        };
        let drive = Drive::default();
        let surfaces = Surfaces::default();
        let tuning = drive::Tuning::default();
        let input = drive::Input {
            action_flags: 1,
            ..drive::Input::default()
        };
        let environment = drive::Environment {
            dt: 16,
            player: true,
            ..drive::Environment::default()
        };
        let config = Config {
            drive: &drive,
            surfaces: &surfaces,
            tuning: &tuning,
            yaw: (&drive).into(),
        };
        let frame = Frame {
            input: &input,
            drive: &environment,
            vertical_delta: 123,
            yaw_lock: 0,
        };
        assert_eq!(
            power.step(&mut motion, &config, &frame),
            Err(Error::Drive(drive::Error::InvalidWeather))
        );
        assert_eq!(motion.velocity, [17, 0, -17]);
        assert_eq!(motion.angular, [1, 2, 3]);
        assert_eq!(power.local_velocity, [16, 0, -32]);
        assert_eq!(power.track_speed, 73);
    }
}
