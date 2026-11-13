//! Actual deformable collision method (408170), including shared RNG consumption.
use crate::{
    fixed::trunc_shift,
    object_query::{Collider, EntityId},
    random,
};

#[derive(Debug, Clone, Copy)]
pub struct Config {
    pub floor: i32,
    pub compression: i32,
    pub drift_shift: u32,
    pub material: i16,
    pub has_sound: bool,
}
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct State {
    pub dirty: u8,
    pub last_vehicle: Option<EntityId>,
    pub sound_timer: i32,
}
pub struct Mesh<'a> {
    pub vertices: &'a mut [[i16; 3]],
    pub flags: &'a [u8],
}
pub struct Vehicle<'a> {
    pub entity: EntityId,
    pub position: [i32; 3],
    pub material: &'a mut i32,
    pub random: &'a mut u32,
    pub dt: i32,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    MissingVertexFlag(usize),
}

impl State {
    /// Return whether the sound trigger fired. Device playback is external;
    /// both original random draws still happen here, even with silent audio.
    pub fn hit(
        &mut self,
        config: Config,
        collider: &Collider,
        mesh: Mesh<'_>,
        vehicle: Vehicle<'_>,
    ) -> Result<bool, Error> {
        self.dirty = 1;
        self.last_vehicle = Some(vehicle.entity);
        let relative =
            std::array::from_fn(|i| vehicle.position[i].wrapping_sub(collider.pose.position[i]));
        let local = collider
            .external_basis
            .map_or(relative, |m| m.world_to_local(relative))
            .map(|v| trunc_shift(v, 3));
        let mut changed = false;
        for (index, vertex) in mesh.vertices.iter_mut().enumerate() {
            let dx = local[0].wrapping_sub(i32::from(vertex[0]));
            let dz = local[2].wrapping_sub(i32::from(vertex[2]));
            if dx <= -256 || dx >= 256 || dz <= -256 || dz >= 256 {
                continue;
            }
            let height = i32::from(vertex[1]);
            if height <= config.floor {
                continue;
            }
            let level = (height >> 5).clamp(0, 14);
            let amount = config
                .compression
                .wrapping_mul(level + 1)
                .wrapping_mul(vehicle.dt)
                >> 6;
            vertex[1] = height.wrapping_sub(amount) as i16;
            if i32::from(vertex[1]) > config.floor {
                changed = true;
            } else {
                vertex[1] = config.floor as i16;
            }
            let flag = *mesh
                .flags
                .get(index)
                .ok_or(Error::MissingVertexFlag(index))?;
            if flag & 4 != 0 {
                let amount = vehicle.dt.wrapping_shr(config.drift_shift) as i16;
                for (axis, bit) in [(0, 1), (2, 2)] {
                    vertex[axis] = if flag & bit != 0 {
                        vertex[axis].wrapping_sub(amount)
                    } else {
                        vertex[axis].wrapping_add(amount)
                    };
                }
            }
        }
        *vehicle.material = i32::from(config.material);
        let mut triggered = false;
        if config.has_sound {
            self.sound_timer = if self.sound_timer < vehicle.dt {
                0
            } else {
                self.sound_timer.wrapping_sub(vehicle.dt)
            };
            if self.sound_timer == 0 && changed {
                random::next(vehicle.random);
                let word = random::next(vehicle.random);
                self.sound_timer = (((word >> 4) & 511) + 400) as i32;
                triggered = true;
            }
        }
        Ok(triggered)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{geometry::Bounds, pose::Pose};
    #[test]
    fn strict_windows_and_missing_flags_preserve_the_written_prefix() {
        let collider = Collider {
            entity: EntityId(1),
            pose: Pose::default(),
            external_basis: None,
            radius: 1,
            bounds: Bounds {
                min: [-1; 3],
                max: [1; 3],
            },
        };
        let config = Config {
            floor: 0,
            compression: 64,
            drift_shift: 1,
            material: -7,
            has_sound: true,
        };
        let mut vertices = [[256, 200, 0], [255, 200, 0]];
        let (mut material, mut seed) = (99, 1);
        let mut state = State::default();
        let result = state.hit(
            config,
            &collider,
            Mesh {
                vertices: &mut vertices,
                flags: &[],
            },
            Vehicle {
                entity: EntityId(2),
                position: [0; 3],
                material: &mut material,
                random: &mut seed,
                dt: 1,
            },
        );
        assert_eq!(result, Err(Error::MissingVertexFlag(1)));
        assert_eq!(vertices, [[256, 200, 0], [255, 193, 0]]);
        assert_eq!(state.dirty, 1);
        assert_eq!(state.last_vehicle, Some(EntityId(2)));
        assert_eq!((material, seed), (99, 1));
    }
}
