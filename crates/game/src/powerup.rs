//! Powerup's actual activation/fall/ground-spin phases (frame_powerup_tick).
//! Grid membership and auxiliary dispatch are immediate caller-owned operations.
use crate::{math::Axis, pose::Pose, terrain};
use tankracer_assets::terrain::TerrainData;
pub const TICK: crate::dispatch::MethodId = crate::dispatch::MethodId(114);
#[derive(Debug, Default)]
pub struct Flight {
    pub angular: [i32; 3],
    pub normal: [i32; 3],
    pub parachute: Option<Pose>,
    pub blink_time: i32,
    pub blink_count: u32,
    pub delay: i32,
    pub disabled: bool,
}
#[derive(Clone, Copy)]
pub struct Frame {
    pub dt: i32,
    pub number: u32,
    pub entity: u32,
    pub player: [i32; 3],
    pub camera: [i32; 3],
    pub visibility: u16,
    pub auxiliary_active: bool,
    pub separation: i32,
    pub diameter: u32,
}
#[derive(Debug, PartialEq, Eq)]
pub enum Action {
    None,
    Activate,
    Auxiliary,
}
const FALL_SPEED: i32 = -1310;
const SPIN_SPEED: i32 = 4659;
fn pivot_rotate(pose: &mut Pose, angles: [i32; 3], height: i32) {
    let pivot = [0, height, 0];
    let before = pose.matrix.local_to_world(pivot);
    for axis in [Axis::Y, Axis::X, Axis::Z] {
        let index = match axis {
            Axis::X => 0,
            Axis::Y => 1,
            Axis::Z => 2,
        };
        pose.matrix.rotate(axis, angles[index] as u32);
    }
    let after = pose.matrix.local_to_world(pivot);
    for i in 0..3 {
        pose.position[i] = pose.position[i].wrapping_add(before[i].wrapping_sub(after[i]));
    }
}
fn terrain_code(e: terrain::Error) -> i32 {
    match e {
        terrain::Error::Arithmetic => -2,
        _ => -1,
    }
}
impl crate::actor_startup::pickup::Pickup {
    pub fn phase(
        &mut self,
        pose: &mut Pose,
        flags: &mut u32,
        frame: Frame,
        data: &TerrainData,
    ) -> Result<Action, i32> {
        let old = *flags;
        let f = &mut self.flight;
        let dt = frame.dt;
        if old & 1 == 0 {
            if frame.auxiliary_active {
                return Ok(Action::Auxiliary);
            }
            if f.disabled {
                return Ok(Action::None);
            }
            if self.regeneration != -1 && f.delay != 0 {
                f.delay = f.delay.wrapping_sub(dt);
                if f.delay > 0 {
                    return Ok(Action::None);
                }
                f.delay = 0;
            }
            if [0, 2].into_iter().all(|i| {
                pose.position[i]
                    .wrapping_sub(frame.player[i])
                    .wrapping_abs()
                    < self.fall_range
            }) {
                *flags |= 1;
                return Ok(Action::Activate);
            }
        } else if old & 8 == 0 {
            if old & 4 == 0 {
                let floor = self.ground.wrapping_add(frame.diameter as i32);
                if floor < pose.position[1] {
                    pose.position[1] =
                        pose.position[1].wrapping_add(FALL_SPEED.wrapping_mul(dt) >> 8);
                } else {
                    pose.position[1] = floor;
                    f.parachute = Some(*pose);
                    *flags |= 4;
                    terrain::normal_height(
                        data,
                        pose.position[0],
                        pose.position[2],
                        &mut self.ground,
                        &mut f.normal,
                    )
                    .map_err(terrain_code)?;
                }
                let angle = dt >> 1;
                f.angular[2] = f.angular[2].wrapping_add(if pose.matrix.0[3] > 0 {
                    angle.wrapping_neg()
                } else {
                    angle
                });
                f.angular[0] = f.angular[0].wrapping_add(if pose.matrix.0[5] > 0 {
                    angle
                } else {
                    angle.wrapping_neg()
                });
                pivot_rotate(pose, f.angular, frame.separation);
            } else {
                let local = pose.matrix.world_to_local(f.normal);
                let mut pitch = crate::math::angle(local[1], local[2])
                    .map_err(|_| -2)?
                    .wrapping_sub(16384);
                let mut roll = crate::math::angle(local[0].wrapping_neg(), local[1])
                    .map_err(|_| -2)?
                    .wrapping_neg();
                let cap = dt.wrapping_mul(16);
                if pitch != 0 {
                    if pitch.wrapping_abs() > cap {
                        pitch = if pitch < 0 { cap.wrapping_neg() } else { cap };
                    }
                    pose.matrix.rotate(Axis::X, pitch as u32);
                }
                if roll != 0 {
                    if roll.wrapping_abs() > cap {
                        roll = if roll < 0 { cap.wrapping_neg() } else { cap };
                    }
                    pose.matrix.rotate(Axis::Z, roll as u32);
                }
                let parachute = f.parachute.as_mut().ok_or(-4)?;
                parachute.position[1] =
                    parachute.position[1].wrapping_add(FALL_SPEED.wrapping_mul(dt) >> 10);
                parachute.matrix.rotate(
                    Axis::X,
                    if pitch > 0 { dt.wrapping_neg() } else { dt } as u32,
                );
                parachute.matrix.rotate(
                    Axis::Z,
                    if roll > 0 { dt.wrapping_neg() } else { dt } as u32,
                );
                pose.matrix
                    .rotate(Axis::Y, (SPIN_SPEED.wrapping_mul(dt) >> 8) as u32);
            }
        } else {
            let cap = i32::from(frame.visibility).wrapping_mul(128);
            if [0, 2].into_iter().all(|i| {
                (pose.position[i] >> 3)
                    .wrapping_sub(frame.camera[i])
                    .wrapping_abs()
                    < cap
            }) {
                pose.matrix
                    .rotate(Axis::Y, (SPIN_SPEED.wrapping_mul(dt) >> 8) as u32);
                if (frame.entity ^ frame.number) & 255 == 0 {
                    pose.matrix.normalize().map_err(|_| -2)?;
                }
            }
        }
        Ok(Action::None)
    }
    /// Runs after any immediate activation/auxiliary call, using its updated flags.
    pub fn finish_frame(&mut self, flags: &mut u32, dt: i32) {
        if *flags & 8 == 0 && *flags & 4 != 0 {
            self.flight.blink_time = self.flight.blink_time.wrapping_add(dt);
            if self.flight.blink_time > 30 {
                self.flight.blink_time = 0;
                let before = self.flight.blink_count;
                *flags ^= 2;
                self.flight.blink_count = before.wrapping_add(1);
                if before == 10 {
                    *flags |= 8;
                }
            }
        }
    }
}
