//! Concrete gameplay bindings for collision interfaces. No original code pointers.
pub use crate::breakable::{BREAKABLE_HIT, ENABLE_TICK};
use crate::{deformable_hit, dispatch::MethodId, obstacle_collision::Invocation};
use tankracer_assets::mesh::Geometry;

/// A logical binding assigned by the Rust class catalogue, unrelated to PE IDs.
pub const DEFORMABLE_HIT: MethodId = MethodId(0);
/// One authoritative mutable mesh: rendering and surface sampling must read its
/// current vertices rather than copying the original model each frame.
pub struct Deformable {
    pub config: deformable_hit::Config,
    pub state: deformable_hit::State,
    pub mesh: Geometry,
    pub vertex_flags: Vec<u8>,
}
impl Deformable {
    /// Execute the real method and return its sound-trigger event. Playback is
    /// a host operation; the game-owned RNG/timer writes have already committed.
    pub fn collide(&mut self, call: Invocation<'_>) -> Result<bool, i32> {
        let collider = call.world.colliders.get(call.target.0).ok_or(-1)?;
        let sound = self
            .state
            .hit(
                self.config,
                collider,
                deformable_hit::Mesh {
                    vertices: &mut self.mesh.vertices,
                    flags: &self.vertex_flags,
                },
                deformable_hit::Vehicle {
                    entity: call.vehicle.entity,
                    position: call.vehicle.base_position,
                    material: &mut call.vehicle.material_tag,
                    random: call.random,
                    dt: call.dt,
                },
            )
            .map_err(|_| -1)?;
        *call.result = collider.entity.0;
        Ok(sound)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        dispatch::{Class, ClassId},
        geometry::Bounds,
        object_query::{Collider, EntityId, ObstacleId},
        obstacle_collision::{
            self as obstacle, Effects, Environment, Grid, Object, Shared, State, Tuning, World,
        },
        pose::Pose,
    };
    use std::sync::Arc;
    struct Methods {
        surface: Deformable,
        sounds: usize,
    }
    impl Effects for Methods {
        fn method(&mut self, method: MethodId, call: Invocation<'_>) -> Result<(), i32> {
            assert_eq!(method, DEFORMABLE_HIT);
            self.sounds += usize::from(self.surface.collide(call)?);
            Ok(())
        }
        fn feedback(&mut self, _: i32, _: &mut State, _: &mut u32) -> Result<(), i32> {
            panic!("feedback disabled")
        }
        fn impact(&mut self, _: &mut State, _: usize, _: i32, _: &mut u32) -> Result<(), i32> {
            panic!("stationary vehicle")
        }
    }
    #[test]
    fn query_dispatch_real_mesh_method_and_shared_rng_compose_using_base_position() {
        let mut methods = Methods {
            surface: Deformable {
                config: deformable_hit::Config {
                    floor: 0,
                    compression: 64,
                    drift_shift: 0,
                    material: -7,
                    has_sound: true,
                },
                state: deformable_hit::State::default(),
                mesh: Geometry {
                    vertices: vec![[0, 200, 0]],
                    triangles: Vec::new(),
                },
                vertex_flags: vec![4],
            },
            sounds: 0,
        };
        let collider = Collider {
            entity: EntityId(2),
            pose: Pose::default(),
            external_basis: None,
            radius: 512,
            bounds: Bounds {
                min: [-4000; 3],
                max: [4000; 3],
            },
        };
        let mut world = World {
            colliders: vec![collider],
            objects: vec![Object {
                class: ClassId(0),
                weight: 0,
                threshold: 0,
                activity: 0,
                flags: 0,
                mesh_bounds: [[-500; 3], [500; 3]],
            }],
            classes: vec![Arc::new(Class {
                maximum_slot: 0,
                methods: vec![Some(DEFORMABLE_HIT)],
            })],
            grid: Grid {
                origin: [0; 2],
                shifts: [0; 2],
                cells: std::array::from_fn(|_| Vec::new()),
            },
            deformables: vec![ObstacleId(0)],
        };
        let mut vehicle = State {
            entity: EntityId(1),
            class: ClassId(1),
            base_position: [0; 3],
            position: [3000, 0, 0],
            previous_position: [3000, 0, 0],
            velocity: [0; 3],
            forward_velocity: 0,
            player: true,
            contacts: [[0; 3]; 4],
            impact_accumulator: 0,
            collision_size: 0,
            material_tag: 99,
            deformable: None,
        };
        let env = Environment {
            mode: 0,
            dt: 1,
            feedback: false,
            player_class: ClassId(1),
            breakable_class: ClassId(2),
            obstacle_slot: 0,
            deformable_slot: 0,
            tuning: Tuning::default(),
        };
        let mut shared = Shared {
            random: 1,
            current_slot: 9,
        };
        assert_eq!(
            obstacle::deformables(&mut world, &mut vehicle, env, &mut shared, &mut methods),
            Ok(Some(ObstacleId(0)))
        );
        assert_eq!(methods.surface.mesh.vertices, [[1, 193, 1]]);
        assert_eq!(vehicle.material_tag, -7);
        assert_eq!(vehicle.deformable, Some(EntityId(2)));
        assert_eq!(methods.surface.state.last_vehicle, Some(EntityId(1)));
        assert_eq!(methods.sounds, 1);
        assert_eq!(shared.random, 1586005467);
        assert_eq!(shared.current_slot, 9);
        assert_eq!(vehicle.velocity, [0; 3]);
    }
}
