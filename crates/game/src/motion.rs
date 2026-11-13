//! Post-collision half of 430A30: integrate, articulate, pose and publish contact hull.
use crate::{attitude, body, fixed::trunc_shift, math, pose};

/// Values converted to the source's 40/dt display/update scale only at the end
/// of a successful tick. They are not inputs to position integration.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Rates {
    pub travel_sum: i32,
    pub penetrating_force: i32,
    pub lateral_velocity: i32,
    pub track_slip: i32,
}
impl Rates {
    fn rescale(&mut self, dt: i32) {
        for value in [
            &mut self.travel_sum,
            &mut self.penetrating_force,
            &mut self.lateral_velocity,
            &mut self.track_slip,
        ] {
            *value = value.wrapping_mul(40) / dt;
        }
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct State {
    pub poses: pose::State,
    pub previous_position: [i32; 3],
    pub velocity: [i32; 3],
    pub angular: [i32; 3],
    /// Body-local lateral/longitudinal drive deltas, before end-of-tick scaling.
    pub acceleration: [i32; 2],
    pub pivot: [i32; 3],
    pub articulation: body::Articulation,
    pub lean: body::Lean,
    pub contacts: [body::Contact; 4],
    pub travel: [i32; 4],
    pub hull: body::Hull,
    pub ready: bool,
    pub rates: Rates,
}

pub struct Config<'a> {
    pub attitude: &'a attitude::Config,
    pub body: &'a body::Tuning,
    pub pose: &'a pose::Config,
}
pub struct PoseInput {
    pub timer: u16,
    pub ambient_wobble: bool,
    pub session_active: bool,
    pub ambient_phase: u32,
    pub recoil_mode: i32,
    pub turret_yaw: i16,
    pub external: pose::ExternalBasis,
}
impl PoseInput {
    fn environment(&self, articulation: &body::Articulation) -> pose::Environment {
        pose::Environment {
            timer: self.timer,
            ambient_wobble: self.ambient_wobble,
            session_active: self.session_active,
            ambient_phase: self.ambient_phase,
            recoil_mode: self.recoil_mode,
            turret_yaw: self.turret_yaw,
            recoil: articulation.recoil,
            gun_pitch: articulation.pitch,
            external: self.external,
        }
    }
}
pub struct Frame {
    pub dt: i32,
    pub number: u32,
    pub player: bool,
    pub mode: i32,
    /// Articulation owns recoil/pitch; callers supply only independent inputs.
    pub pose: PoseInput,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    InvalidTimeStep,
    Attitude(math::Error),
    Articulation(body::Error),
    Pose(math::Error),
    AfterPose(i32),
}
impl Error {
    pub fn status(self) -> i32 {
        match self {
            Self::InvalidTimeStep | Self::Attitude(_) | Self::Pose(_) => -2,
            Self::Articulation(_) => -1,
            Self::AfterPose(code) => code,
        }
    }
}
impl State {
    /// Required scene work runs after pose, before hull/readiness/rescaling.
    /// It may change this same vehicle; subsequent work observes those writes.
    /// No whole-tick rollback: failures preserve the original stage boundary.
    pub fn finish(
        &mut self,
        config: &Config<'_>,
        frame: &Frame,
        gun: Option<body::GunMesh<'_>>,
        mut after_pose: impl FnMut(&mut Self) -> Result<(), i32>,
    ) -> Result<(), Error> {
        if frame.dt <= 0 {
            return Err(Error::InvalidTimeStep);
        }
        self.previous_position = self.poses.physical_position;
        for i in 0..3 {
            self.poses.physical_position[i] = self.poses.physical_position[i]
                .wrapping_add(trunc_shift(self.velocity[i].wrapping_mul(frame.dt), 14));
        }
        let mut attitude = attitude::State {
            reference: self.poses.reference,
            matrix: self.poses.body.matrix,
            position: self.poses.body.position,
            angular: self.angular,
            longitudinal_delta: self.acceleration[1],
            lateral_delta: self.acceleration[0],
            pivot: self.pivot,
        };
        // The C adapter commits this local result only on success. A failing
        // normalization must not leak partial attitude writes to the vehicle.
        attitude
            .step(config.attitude, frame.dt, frame.number)
            .map_err(Error::Attitude)?;
        self.poses.body.matrix = attitude.matrix;
        self.poses.body.position = attitude.position;
        self.angular = attitude.angular;
        self.articulation
            .step(frame.dt, config.body, gun)
            .map_err(Error::Articulation)?;
        if frame.player && frame.mode != 2 && frame.mode != 3 {
            self.lean.step(self.acceleration, frame.dt, config.body);
        }
        let pose = frame.pose.environment(&self.articulation);
        self.poses
            .update(config.pose, self.travel, &pose)
            .map_err(Error::Pose)?;
        after_pose(self).map_err(|code| Error::AfterPose(if code < 0 { code } else { -8 }))?;
        self.hull.refresh(
            self.poses.physical_position,
            self.previous_position,
            self.contacts,
        );
        self.ready = true;
        self.rates.rescale(frame.dt);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tankracer_assets::config::Gain;
    #[test]
    fn failed_normalization_keeps_position_integration_but_not_local_attitude_writes() {
        let matrix = math::Matrix([4000, 1, 0, 65407, 339, 0, 0, 0, 4096]);
        let mut state = State {
            poses: pose::State {
                physical_position: [10, 20, 30],
                body: pose::Pose {
                    position: [100; 3],
                    matrix,
                },
                ..pose::State::default()
            },
            velocity: [16384, -16384, 0],
            rates: Rates {
                travel_sum: 73,
                ..Rates::default()
            },
            ..State::default()
        };
        let attitude = attitude::Config::default();
        let body = body::Tuning::default();
        let pose = pose::Config {
            track_offsets: [[0; 3]; 2],
            turret_offset: [0; 3],
            gun_offset: [0; 3],
            attachment_offsets: [[0; 3]; 2],
            bottom_offset: [0; 3],
            track_length: 1,
            travel_gain: Gain::default(),
            gun_pivot: [0; 3],
        };
        let config = Config {
            attitude: &attitude,
            body: &body,
            pose: &pose,
        };
        let frame = Frame {
            dt: 16,
            number: 10,
            player: true,
            mode: 0,
            pose: PoseInput {
                timer: 0,
                ambient_wobble: false,
                session_active: true,
                ambient_phase: 0,
                recoil_mode: 0,
                turret_yaw: 0,
                external: pose::ExternalBasis::Body,
            },
        };
        assert_eq!(
            state.finish(&config, &frame, None, |_| panic!("pose must not run")),
            Err(Error::Attitude(math::Error::Arithmetic))
        );
        assert_eq!(state.previous_position, [10, 20, 30]);
        assert_eq!(state.poses.physical_position, [26, 4, 30]);
        assert_eq!(
            state.poses.body,
            pose::Pose {
                position: [100; 3],
                matrix
            }
        );
        assert!(!state.ready);
        assert_eq!(state.rates.travel_sum, 73);
    }
}
