//! Vehicle part poses (42F220), independent of renderer and resource addresses.
use crate::{
    fixed::trunc_shift,
    math::{self, Axis, Error, Matrix},
    vector,
};
use tankracer_assets::config::{Gain, Part, Tank};

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Pose {
    pub position: [i32; 3],
    pub matrix: Matrix,
}

#[derive(Debug, Clone, Copy)]
pub enum ExternalBasis {
    None,
    Body,
    Custom(Matrix),
}
#[derive(Debug, Clone, Copy)]
pub struct Environment {
    pub timer: u16,
    pub ambient_wobble: bool,
    pub session_active: bool,
    pub ambient_phase: u32,
    pub recoil_mode: i32,
    pub turret_yaw: i16,
    pub recoil: i32,
    pub gun_pitch: i32,
    pub external: ExternalBasis,
}
#[derive(Debug, Clone)]
pub struct Config {
    pub track_offsets: [[i32; 3]; 2],
    pub turret_offset: [i32; 3],
    pub gun_offset: [i32; 3],
    pub attachment_offsets: [[i32; 3]; 2],
    pub bottom_offset: [i32; 3],
    pub track_length: i32,
    pub travel_gain: Gain,
    pub gun_pivot: [i32; 3],
}
impl Config {
    pub fn from_tank(tank: &Tank, track_length: i32, gun_pivot: [i32; 3]) -> Self {
        let body = tank.nodes[Part::Body.slot()].offset;
        let relative = |part: Part| {
            std::array::from_fn(|i| tank.nodes[part.slot()].offset[i].wrapping_sub(body[i]))
        };
        let turret = relative(Part::Turret);
        let gun = relative(Part::Gun);
        Self {
            track_offsets: [relative(Part::LeftTrack), relative(Part::RightTrack)],
            turret_offset: turret,
            gun_offset: std::array::from_fn(|i| gun[i].wrapping_sub(turret[i])),
            attachment_offsets: [relative(Part::Under), relative(Part::End)],
            bottom_offset: tank.nodes[Part::Bottom.slot()].offset,
            track_length,
            travel_gain: tank.suspension.susp_to_shape,
            gun_pivot,
        }
    }
}
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct State {
    pub physical_position: [i32; 3],
    pub reference: Matrix,
    pub base_position: [i32; 3],
    pub body: Pose,
    pub tracks: [Pose; 2],
    pub turret: Pose,
    pub gun: Pose,
    pub attachments: [Pose; 2],
    pub bottom_position: [i32; 3],
    pub recoil_factor: u8,
    pub recoil_shift: u8,
}
fn add(a: [i32; 3], b: [i32; 3]) -> [i32; 3] {
    std::array::from_fn(|i| a[i].wrapping_add(b[i]))
}
impl State {
    pub fn update(
        &mut self,
        config: &Config,
        travel: [i32; 4],
        env: &Environment,
    ) -> Result<(), Error> {
        self.body.position = self.physical_position;
        self.reference = self.body.matrix;
        let mut wobble = 0;
        if env.timer != 0 {
            wobble = math::sine(u32::from(env.timer & 511) * 128).wrapping_mul(3) >> 7;
            self.body.position[1] = self.body.position[1].wrapping_add(wobble);
        }
        if env.ambient_wobble && !env.session_active {
            wobble = math::sine((env.ambient_phase & 511) * 128).wrapping_mul(3) >> 7;
            self.body.position[1] = self.body.position[1].wrapping_add(wobble);
        }
        let origin = self.body.position;
        for side in 0..2 {
            let shift = u32::from(config.travel_gain.shift);
            let front = trunc_shift(travel[side * 2], shift);
            let rear = trunc_shift(travel[side * 2 + 1], shift);
            let mut angle =
                math::angle(front.wrapping_sub(rear).wrapping_abs(), config.track_length)?;
            if front < rear {
                angle = angle.wrapping_neg();
            }
            let track = &mut self.tracks[side];
            track.matrix = self.body.matrix;
            let mut local = config.track_offsets[side];
            local[1] = local[1]
                .wrapping_add(front.wrapping_add(rear) >> 1)
                .wrapping_sub(wobble);
            track.position = add(track.matrix.local_to_world(local), origin);
            track.matrix.rotate(Axis::X, angle as u32);
        }
        self.turret.matrix = self.body.matrix;
        let (shift, factor) = match env.recoil_mode {
            3 => (2, 3),
            5 => (0, 1),
            _ => (1, 1),
        };
        self.recoil_shift = shift;
        self.recoil_factor = factor;
        let mut recoil = vector::yaw(
            [
                0,
                0,
                trunc_shift(env.recoil, u32::from(shift)).wrapping_mul(i32::from(factor)),
            ],
            env.turret_yaw as u32,
        );
        recoil = match env.external {
            ExternalBasis::None => recoil,
            ExternalBasis::Body => self.reference.local_to_world(recoil),
            ExternalBasis::Custom(m) => m.local_to_world(recoil),
        };
        self.turret.position = add(
            add(
                self.turret.matrix.local_to_world(config.turret_offset),
                origin,
            ),
            recoil,
        );
        self.turret.matrix.rotate(Axis::Y, env.turret_yaw as u32);
        self.gun.matrix = self.turret.matrix;
        let mut gun_local = config.gun_offset;
        gun_local[2] = gun_local[2].wrapping_add(env.recoil);
        self.gun.matrix.rotate_pivot(
            &mut gun_local,
            config.gun_pivot,
            [env.gun_pitch.wrapping_neg(), 0, 0],
        );
        self.gun.position = add(
            self.turret.matrix.local_to_world(gun_local),
            self.turret.position,
        );
        for (pose, offset) in self.attachments.iter_mut().zip(config.attachment_offsets) {
            pose.matrix = self.body.matrix;
            pose.position = add(pose.matrix.local_to_world(offset), origin);
        }
        self.base_position = self.physical_position;
        let sum = travel.into_iter().fold(0_i32, |sum, value| {
            sum.wrapping_add(trunc_shift(
                value.wrapping_mul(i32::from(config.travel_gain.multiplier)),
                u32::from(config.travel_gain.shift),
            ))
        });
        self.base_position[1] = self.base_position[1].wrapping_add(sum >> 3);
        self.physical_position = self.body.position;
        self.bottom_position = add(
            self.body.matrix.local_to_world(config.bottom_offset),
            origin,
        );
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn config() -> Config {
        Config {
            track_offsets: [[0; 3]; 2],
            turret_offset: [0; 3],
            gun_offset: [0; 3],
            attachment_offsets: [[0; 3]; 2],
            bottom_offset: [0; 3],
            track_length: 1,
            travel_gain: Gain::default(),
            gun_pivot: [0; 3],
        }
    }
    fn environment() -> Environment {
        Environment {
            timer: 1,
            ambient_wobble: true,
            session_active: false,
            ambient_phase: 2,
            recoil_mode: 0,
            turret_yaw: 0,
            recoil: 0,
            gun_pitch: 0,
            external: ExternalBasis::Body,
        }
    }
    #[test]
    fn body_accumulates_both_wobbles_but_tracks_remove_only_the_last_one() {
        let mut state = State::default();
        state.update(&config(), [0; 4], &environment()).unwrap();
        assert_eq!(state.body.position, [0, 3, 0]);
        assert_eq!(state.physical_position, [0, 3, 0]);
        assert_eq!(state.tracks[0].position, [0, 1, 0]);
        assert_eq!(state.base_position, [0; 3]);
        state.update(&config(), [0; 4], &environment()).unwrap();
        assert_eq!(state.physical_position, [0, 6, 0]);
        assert_eq!(state.tracks[0].position, [0, 4, 0]);
        assert_eq!(state.base_position, [0, 3, 0]);
    }
    #[test]
    fn second_track_error_preserves_first_track_and_defers_physical_position_commit() {
        let mut config = config();
        config.track_offsets = [[1, 0, 0], [2, 0, 0]];
        let mut env = environment();
        env.ambient_wobble = false;
        let old = Pose {
            position: [99; 3],
            matrix: Matrix([55; 9]),
        };
        let mut state = State {
            physical_position: [10, 20, 30],
            tracks: [old; 2],
            turret: old,
            ..State::default()
        };
        assert_eq!(
            state.update(&config, [0, 0, i32::MIN, 0], &env),
            Err(Error::Arithmetic)
        );
        assert_eq!(state.body.position, [10, 21, 30]);
        assert_eq!(state.physical_position, [10, 20, 30]);
        assert_eq!(state.tracks[0].position, [11, 20, 30]);
        assert_eq!(state.tracks[1], old);
        assert_eq!(state.turret, old);
    }
}
