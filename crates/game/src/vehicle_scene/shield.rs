//! Shared shield-orbit strips, including exact bootstrap RNG consumption.
use crate::{
    fixed::trunc_shift,
    math::{self, Matrix},
    pose, random, vehicle,
};

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Orbit {
    pub pitch: i32,
    pub pitch_target: i32,
    pub yaw: i32,
    pub yaw_rate: i32,
    pub yaw_rate_target: i32,
    pub size: i32,
    pub growing: u8,
    pub spin: u16,
    pub position: [i32; 3],
}
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Bank {
    pub orbits: [Orbit; 4],
    pub tail: u32,
}
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct State {
    pub banks: [Bank; 9],
}
#[derive(Debug, Clone, Copy)]
pub struct Input {
    pub enabled: bool,
    pub bank: usize,
    pub dt: i32,
    pub position: [i32; 3],
    pub basis: Option<Matrix>,
}
impl Input {
    pub fn after_pose(vehicle: &vehicle::AfterPose<'_>, network_bank: Option<usize>) -> Self {
        let basis = match vehicle.pose_input.external {
            pose::ExternalBasis::None => None,
            pose::ExternalBasis::Body => Some(vehicle.motion.poses.reference),
            pose::ExternalBasis::Custom(matrix) => Some(matrix),
        };
        Self {
            enabled: vehicle.contact.sampling.effect.active != 0,
            bank: if vehicle.player {
                1
            } else {
                network_bank.unwrap_or(0)
            },
            dt: vehicle.frame.dt,
            position: vehicle.motion.poses.base_position,
            basis,
        }
    }
}
fn seek(value: &mut i32, target: &mut i32, step: i32, mask: u32, offset: i32, random: &mut u32) {
    let reached = if *value < *target {
        *value = value.wrapping_add(step);
        if *value >= *target {
            *value = *target;
            true
        } else {
            false
        }
    } else if *value > *target {
        *value = value.wrapping_sub(step);
        if *value <= *target {
            *value = *target;
            true
        } else {
            false
        }
    } else {
        true
    };
    if reached {
        *target = ((crate::random::next(random) >> 4) & mask) as i32 + offset;
    }
}
impl State {
    /// Exactly five shared LCG draws per orbit, nine strips. Position payload
    /// survives reinitialization, matching the source's selective stores.
    pub fn initialize(&mut self, seed: &mut u32) {
        for (index, bank) in self.banks.iter_mut().enumerate() {
            bank.tail = 0;
            for orbit in &mut bank.orbits {
                orbit.pitch = ((random::next(seed) >> 4) & 16383) as i32;
                orbit.pitch_target = ((random::next(seed) >> 4) & 16383) as i32;
                orbit.yaw = ((random::next(seed) >> 4) & 65535) as i32;
                orbit.yaw_rate = ((random::next(seed) >> 4) & 65535) as i32 - 32768;
                orbit.yaw_rate_target = ((random::next(seed) >> 4) & 65535) as i32 - 32768;
                orbit.size = 200 + index as i32 * 100;
                orbit.growing = 0;
                orbit.spin = 0;
            }
        }
    }
    pub fn tick(&mut self, input: Input, seed: &mut u32) -> Result<(), i32> {
        if !input.enabled {
            return Ok(());
        }
        let bank = self.banks.get_mut(input.bank).ok_or(-1)?;
        for orbit in &mut bank.orbits {
            orbit.yaw = orbit
                .yaw
                .wrapping_add(trunc_shift(input.dt.wrapping_mul(orbit.yaw_rate), 8));
            seek(
                &mut orbit.yaw_rate,
                &mut orbit.yaw_rate_target,
                input.dt.wrapping_mul(4),
                65535,
                -32768,
                seed,
            );
            seek(
                &mut orbit.pitch,
                &mut orbit.pitch_target,
                input.dt.wrapping_mul(16),
                16383,
                0,
                seed,
            );
            let pitch = orbit.pitch as u32 & 65532;
            let yaw = orbit.yaw as u32 & 65532;
            let radius = math::cosine(pitch).wrapping_mul(1500) >> 12;
            let mut point = [
                radius.wrapping_mul(math::cosine(yaw)) >> 12,
                math::sine(pitch).wrapping_mul(1500) >> 12,
                radius.wrapping_mul(math::sine(yaw)) >> 12,
            ];
            if let Some(matrix) = input.basis {
                point = matrix.local_to_world(point);
            }
            orbit.position = std::array::from_fn(|i| point[i].wrapping_add(input.position[i]));
            if orbit.growing == 0 {
                if orbit.size < 201 {
                    orbit.size = 200;
                    orbit.growing = 1;
                } else {
                    orbit.size = orbit.size.wrapping_sub(input.dt.wrapping_mul(2));
                }
            } else if orbit.size < 500 {
                orbit.size = orbit.size.wrapping_add(input.dt.wrapping_mul(2));
            } else {
                orbit.size = 500;
                orbit.growing = 0;
            }
            orbit.spin = orbit.spin.wrapping_add(input.dt.wrapping_mul(8) as u16);
        }
        Ok(())
    }
}
