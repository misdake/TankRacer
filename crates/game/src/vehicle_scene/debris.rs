//! Two shared eight-particle debris banks and their vehicle ownership.
use crate::{fixed::trunc_shift, geometry, object_query::EntityId, random, vehicle};

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Particle {
    pub velocity: [i32; 3],
    pub position: [i32; 3],
    pub color: [u8; 3],
    pub active: u8,
    pub size: u32,
}
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Bank {
    pub particles: [Particle; 8],
    pub floor: i32,
    pub owner: Option<EntityId>,
}
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct State {
    pub banks: [Bank; 2],
}
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Vehicle {
    pub active: u32,
    pub timer: i32,
    pub previous_plane: u8,
}
/// Borrowed canonical per-vehicle state, so stealing a shared bank immediately
/// retires the previous owner rather than sending a deferred notification.
pub struct Owner<'a> {
    pub entity: EntityId,
    pub state: &'a mut Vehicle,
}
#[derive(Debug, Clone, Copy)]
pub struct Input {
    pub dt: i32,
    pub player: bool,
    pub position: [i32; 3],
    pub velocity: [i32; 3],
    pub has_plane: u8,
    pub plane_distance: i32,
    pub materials: [usize; 2],
    pub contacts: [[i32; 3]; 2],
    pub camera_position: [i32; 3],
    pub camera_forward: [i32; 3],
}
impl Input {
    pub fn after_pose(v: &vehicle::AfterPose<'_>, camera: ([i32; 3], [i32; 3])) -> Self {
        Self {
            dt: v.frame.dt,
            player: v.player,
            position: v.motion.poses.base_position,
            velocity: v.motion.velocity,
            has_plane: u8::from(v.contact.sampling.primary.has_extra),
            plane_distance: v.contact.sampling.primary.extra[3],
            materials: v.contact.sampling.materials.map(|m| m as usize),
            contacts: [v.motion.contacts[0].world, v.motion.contacts[2].world],
            camera_position: camera.0,
            camera_forward: camera.1,
        }
    }
}
pub struct Context<'a> {
    pub kinds: &'a [i8; 16],
    pub random: &'a mut u32,
    pub impact_accumulator: &'a mut i32,
    /// Device output retains the random value used by the original audio call,
    /// even if a later scene-owner lookup fails. Playback does not own game RNG.
    pub sound_draw: &'a mut Option<u32>,
}
fn jitter(random: &mut u32, mask: u32) -> i32 {
    ((random::next(random) >> 4) & mask) as i32
}
fn color(random: &mut u32, surface: bool, component: usize) -> u8 {
    // Three discarded draws are observable through the shared RNG sequence.
    for _ in 0..3 {
        random::next(random);
    }
    let value = random::next(random);
    let (mask, shift, factor) = if surface {
        (16383, 6, [33, 18, 10][component])
    } else {
        (4095, 8, 30)
    };
    (((65535 - ((value >> shift) & mask)) * factor) >> 16) as u8
}
impl State {
    /// Inactive-path clear resets only active markers, ownership and cooldown.
    pub fn clear(&mut self, player: bool, vehicle: &mut Vehicle) {
        if vehicle.active == 0 {
            return;
        }
        let bank = &mut self.banks[usize::from(player)];
        for p in &mut bank.particles {
            p.active = 0;
        }
        bank.owner = None;
        vehicle.active = 0;
        vehicle.timer = 0;
    }
    pub fn step(
        &mut self,
        owners: &mut [Owner<'_>],
        own: usize,
        input: &Input,
        context: &mut Context<'_>,
    ) -> Result<(), i32> {
        let bank = &mut self.banks[usize::from(input.player)];
        let owner = owners.get_mut(own).ok_or(-1)?;
        let entity = owner.entity;
        let state = &mut *owner.state;
        if state.active != 0 {
            let gravity = trunc_shift(input.dt.wrapping_mul(5000), 8);
            let mut active = false;
            for p in &mut bank.particles {
                if p.active == 0 {
                    continue;
                }
                p.velocity[1] = p.velocity[1].wrapping_sub(gravity);
                for j in 0..3 {
                    p.position[j] = p.position[j]
                        .wrapping_add(trunc_shift(input.dt.wrapping_mul(p.velocity[j]), 8));
                }
                if p.position[1] < bank.floor {
                    p.active = 0;
                } else {
                    active = true;
                }
            }
            if !active {
                bank.owner = None;
                state.active = 0;
            }
        }
        let speed = geometry::approximate_distance(input.velocity) as u32;
        state.timer = state.timer.wrapping_sub(input.dt).max(0);
        let emit = input.has_plane != 0
            && state.timer == 0
            && !(state.previous_plane != 0
                && (state.active != 0 || input.velocity[1].wrapping_abs() < 102401))
            && speed > 102400;
        if emit {
            *context.sound_draw = Some(random::next(context.random));
            let surface = [
                *context.kinds.get(input.materials[0]).ok_or(-1)? == 1,
                *context.kinds.get(input.materials[1]).ok_or(-1)? == 1,
            ];
            state.active = 1;
            state.timer = 500;
            *context.impact_accumulator = context.impact_accumulator.wrapping_add(100);
            let dot = (0..3).fold(0_i32, |dot, i| {
                dot.wrapping_add(
                    input.position[i]
                        .wrapping_sub(input.camera_position[i].wrapping_mul(8))
                        .wrapping_mul(input.camera_forward[i]),
                )
            });
            if input.player || dot >= 0 {
                if let Some(previous) = bank.owner {
                    owners
                        .iter_mut()
                        .find(|o| o.entity == previous)
                        .ok_or(-1)?
                        .state
                        .active = 0;
                }
                owners[own].state.active = 1;
                bank.owner = Some(entity);
                bank.floor = input.plane_distance.wrapping_mul(-8);
                for (i, p) in bank.particles.iter_mut().enumerate() {
                    p.color = std::array::from_fn(|j| color(context.random, surface[i & 1], j));
                    p.position[0] = input.contacts[i & 1][0]
                        .wrapping_add(jitter(context.random, 2047).wrapping_sub(1024));
                    p.position[2] = input.contacts[i & 1][2]
                        .wrapping_add(jitter(context.random, 2047).wrapping_sub(1024));
                    p.position[1] = bank.floor;
                    for j in [0, 2] {
                        p.velocity[j] = trunc_shift(input.velocity[j], 7)
                            .wrapping_add(jitter(context.random, 2047).wrapping_sub(1024))
                            .wrapping_add(
                                p.position[j]
                                    .wrapping_sub(input.position[j])
                                    .wrapping_mul(2),
                            );
                    }
                    // Source uses SAR for this absolute vertical term, unlike
                    // horizontal velocity's truncation toward zero.
                    let value = input.velocity[1].wrapping_neg();
                    let vertical = if value < 0 {
                        (input.velocity[1] >> 7).wrapping_neg()
                    } else {
                        value >> 7
                    };
                    p.velocity[1] = jitter(context.random, 1023)
                        .wrapping_add((speed >> 8) as i32)
                        .wrapping_add(vertical);
                    p.active = 1;
                    p.size = jitter(context.random, 511) as u32 + 500;
                }
            }
        }
        owners[own].state.previous_plane = input.has_plane;
        Ok(())
    }
}
