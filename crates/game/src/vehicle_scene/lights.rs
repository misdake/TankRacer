//! Reverse/brake ground-light quads and ownership (4427E0).
use crate::{
    effect_pool::{Pool, Quad, QuadHandle},
    math::Matrix,
    pose, vehicle,
};

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct State {
    pub brake_mode: u8,
    pub active: [u8; 2],
    pub handles: [QuadHandle; 2],
}
#[derive(Debug, Clone, Copy)]
pub struct Settings {
    pub active: bool,
    pub visible: i32,
    pub queued: bool,
    pub reverse_braking: bool,
    pub material: u32,
}
#[derive(Debug, Clone, Copy)]
pub struct Input {
    pub player: bool,
    pub disabled: bool,
    pub settings: Settings,
    pub throttle: i32,
    pub brake: i32,
    pub forward_velocity: i32,
    pub position: [i32; 3],
    pub basis: Option<Matrix>,
    pub materials: [i32; 2],
    pub raw_heights: [i32; 4],
    pub offset: [i32; 3],
}
impl Input {
    pub fn after_pose(
        vehicle: &vehicle::AfterPose<'_>,
        settings: Settings,
        offset: [i32; 3],
    ) -> Self {
        let basis = match vehicle.pose_input.external {
            pose::ExternalBasis::None => None,
            pose::ExternalBasis::Body => Some(vehicle.motion.poses.reference),
            pose::ExternalBasis::Custom(matrix) => Some(matrix),
        };
        Self {
            player: vehicle.player,
            disabled: vehicle.collision.disabled,
            settings,
            throttle: vehicle.frame.input.throttle,
            brake: vehicle.frame.input.brake,
            forward_velocity: vehicle.powertrain.local_velocity[2],
            position: vehicle.motion.poses.physical_position,
            basis,
            materials: vehicle.contact.sampling.materials,
            raw_heights: vehicle
                .contact
                .sampling
                .primary
                .points
                .each_ref()
                .map(|p| p.raw_height),
            offset,
        }
    }
}
impl State {
    fn release(&mut self, side: usize, pool: &mut Pool) -> Result<(), i32> {
        if self.active[side] != 0 {
            pool.release_quad(self.handles[side]).map_err(|_| -1)?;
            self.active[side] = 0;
        }
        Ok(())
    }
    fn release_both(&mut self, pool: &mut Pool) -> Result<(), i32> {
        self.release(0, pool)?;
        self.release(1, pool)
    }
    pub fn step(&mut self, input: &Input, pool: &mut Pool) -> Result<(), i32> {
        let settings = input.settings;
        let stop = if input.player {
            settings.visible != 1
        } else {
            !settings.active || settings.visible != 1 || input.disabled
        };
        if stop {
            return self.release_both(pool);
        }
        let brake = input.brake != 0;
        // This source condition releases even in steady braking when throttle
        // is zero. Preserve allocation order shared with other effect owners.
        if (!brake && self.brake_mode != 0)
            || (brake && self.brake_mode == 0)
            || input.throttle >= 0
        {
            self.release_both(pool)?;
        }
        if settings.queued {
            return Ok(());
        }
        let gray = if !brake {
            if input.throttle >= 0 {
                return Ok(());
            }
            self.brake_mode = 0;
            128
        } else if (input.forward_velocity > 1000 && input.throttle == 0)
            || (settings.reverse_braking && input.forward_velocity < -1000)
        {
            self.brake_mode = 1;
            0
        } else {
            return self.release_both(pool);
        };
        let height = input.raw_heights.into_iter().fold(0_i32, i32::wrapping_add) >> 2;
        for side in 0..2 {
            if input.materials[side] != 1 || input.position[1].wrapping_sub(height) > 2999 {
                self.release(side, pool)?;
                continue;
            }
            let x = input.offset[0];
            let (x1, x2) = if side == 0 {
                (
                    x.wrapping_neg().wrapping_sub(325),
                    x.wrapping_neg().wrapping_add(125),
                )
            } else {
                (x.wrapping_sub(125), x.wrapping_add(325))
            };
            let mut points = [[0; 3]; 4];
            for i in 0..4 {
                let mut v = [
                    if i == 0 || i == 3 { x2 } else { x1 },
                    -1500,
                    if i < 2 {
                        input.offset[2]
                    } else {
                        input.offset[2].wrapping_add(1200)
                    },
                ];
                if let Some(matrix) = input.basis {
                    v = matrix.local_to_world(v);
                }
                points[3 - i] = std::array::from_fn(|j| v[j].wrapping_add(input.position[j]));
            }
            if self.active[side] == 0 {
                self.handles[side] = pool.allocate_quad(Quad {
                    points,
                    material: settings.material,
                    style: [1, 128, gray, gray, 1, 1, 31, 1, 31, 31, 1, 31],
                });
                self.active[side] = 1;
            } else {
                pool.update_quad(self.handles[side], points)
                    .map_err(|_| -1)?;
            }
        }
        Ok(())
    }
}
