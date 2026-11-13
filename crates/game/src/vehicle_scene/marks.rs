//! Track endpoints, joining quads and the source's 68-handle FIFO (43A930).
use crate::{
    effect_pool::{Pool, Quad, QuadHandle},
    fixed::trunc_shift,
    math::Matrix,
    vehicle,
};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct State {
    pub active: [u8; 2],
    pub endpoints: [[[i32; 3]; 2]; 2],
    pub movement: [i32; 3],
    pub handles: [QuadHandle; 68],
    pub count: u32,
    pub head: u32,
    pub tail: u32,
}
impl Default for State {
    fn default() -> Self {
        Self {
            active: [0; 2],
            endpoints: [[[0; 3]; 2]; 2],
            movement: [0; 3],
            handles: [QuadHandle(0); 68],
            count: 0,
            head: 0,
            tail: 0,
        }
    }
}
#[derive(Debug, Clone, Copy)]
pub struct Input {
    pub player: bool,
    pub deformable: bool,
    pub mode: i32,
    pub dt: i32,
    pub lateral_velocity: i32,
    pub track_slip: i32,
    pub velocity: [i32; 3],
    pub materials: [i32; 2],
    pub travel: [i32; 4],
    pub kinds: [i32; 4],
    pub contacts: [[i32; 3]; 4],
    pub tracks: [Matrix; 2],
}
impl Input {
    pub fn after_pose(vehicle: &vehicle::AfterPose<'_>) -> Self {
        Self {
            player: vehicle.player,
            deformable: vehicle.collision.deformable.is_some(),
            mode: vehicle.frame.mode,
            dt: vehicle.frame.dt,
            lateral_velocity: vehicle.motion.rates.lateral_velocity,
            track_slip: vehicle.motion.rates.track_slip,
            velocity: vehicle.motion.velocity,
            materials: vehicle.contact.sampling.materials,
            travel: vehicle.motion.travel,
            kinds: vehicle
                .contact
                .sampling
                .primary
                .points
                .each_ref()
                .map(|p| p.kind),
            contacts: vehicle.motion.contacts.map(|p| p.world),
            tracks: vehicle.motion.poses.tracks.map(|p| p.matrix),
        }
    }
}
impl State {
    fn push(&mut self, points: [[i32; 3]; 4], pool: &mut Pool) -> Result<(), i32> {
        if self.count == 68 {
            let handle = *self.handles.get(self.head as usize).ok_or(-1)?;
            pool.release_quad(handle).map_err(|_| -1)?;
            self.head = self.head.wrapping_add(1);
            if self.head == 68 {
                self.head = 0;
            }
            self.count = self.count.wrapping_sub(1);
        }
        let handle = pool.allocate_quad(Quad {
            points,
            material: 0,
            style: [0, 6, 6, 6, 1, 1, 63, 1, 63, 63, 1, 63],
        });
        if handle != QuadHandle::EXHAUSTED {
            *self.handles.get_mut(self.tail as usize).ok_or(-1)? = handle;
            self.tail = self.tail.wrapping_add(1);
            if self.tail == 68 {
                self.tail = 0;
            }
            self.count = self.count.wrapping_add(1);
        }
        Ok(())
    }
    pub fn step(&mut self, input: &Input, pool: &mut Pool) -> Result<(), i32> {
        if !input.player || input.deformable || input.mode == 2 || input.mode == 3 {
            return Ok(());
        }
        if input.lateral_velocity.wrapping_abs() < 80000 && input.track_slip.wrapping_abs() < 25000
        {
            self.active = [0; 2];
            return Ok(());
        }
        self.movement = input
            .velocity
            .map(|v| trunc_shift(v.wrapping_mul(input.dt), 14));
        for side in 0..2 {
            let old = self.active[side] != 0;
            if input.materials[side] != 1 {
                continue;
            }
            let [front, rear] = [side * 2, side * 2 + 1];
            let eligible = input.travel[front].max(input.travel[rear]) >= 40001
                && [front, rear]
                    .into_iter()
                    .any(|i| matches!(input.kinds[i] as i16, 0 | 1));
            let points = if eligible {
                let previous = self.endpoints[side];
                self.active[side] = 1;
                let matrix = input.tracks[side].0;
                let axis: [i32; 3] =
                    std::array::from_fn(|i| trunc_shift(matrix[i].wrapping_mul(200), 10));
                let offset: [i32; 3] = std::array::from_fn(|i| trunc_shift(matrix[6 + i], 3));
                let mut rear_point = input.contacts[rear];
                rear_point[1] = rear_point[1].min(input.contacts[front][1]);
                rear_point = std::array::from_fn(|i| {
                    rear_point[i]
                        .wrapping_add(offset[i])
                        .wrapping_add(self.movement[i])
                });
                let mut edge = std::array::from_fn(|i| {
                    if side == 0 {
                        rear_point[i].wrapping_add(axis[i])
                    } else {
                        rear_point[i].wrapping_sub(axis[i])
                    }
                });
                edge[1] = rear_point[1];
                self.endpoints[side] = [edge, rear_point];
                if !old {
                    continue;
                }
                [previous[0], previous[1], rear_point, edge]
            } else if old {
                self.active[side] = 0;
                let previous = self.endpoints[side];
                [
                    previous[0],
                    previous[1],
                    std::array::from_fn(|i| previous[1][i].wrapping_add(self.movement[i])),
                    std::array::from_fn(|i| previous[0][i].wrapping_add(self.movement[i])),
                ]
            } else {
                continue;
            };
            self.push(points, pool)?;
        }
        Ok(())
    }
}
