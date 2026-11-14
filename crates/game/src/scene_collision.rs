//! Concrete gameplay bindings for collision interfaces. No original code pointers.
pub use crate::breakable::{BREAKABLE_HIT, ENABLE_TICK};
use crate::{
    contact_pipeline, crush, deformable_hit, deformation,
    dispatch::MethodId,
    object_query::{Collider, EntityId},
    obstacle_collision::Invocation,
    scene_meshes::{MeshId, Meshes},
};

/// A logical binding assigned by the Rust class catalogue, unrelated to PE IDs.
pub const DEFORMABLE_HIT: MethodId = MethodId(0);
pub struct Sampling {
    /// Original pre-expansion local clamp bounds (+BA..C6).
    pub bounds: [[i32; 2]; 2],
    pub edge_cap: i32,
    /// Record activation's dirty byte (+CC) is distinct from mesh dirty (+CA).
    pub state: deformation::State,
}
/// One authoritative mutable mesh: rendering and surface sampling must read its
/// current vertices rather than copying the original model each frame.
pub struct Deformable {
    pub config: deformable_hit::Config,
    pub state: deformable_hit::State,
    pub mesh: MeshId,
    pub vertex_flags: Vec<u8>,
    pub sampling: Sampling,
    pub runtime: crush::State,
}

/// Scene components parallel the collision handles; absent entries belong to
/// other classes. Contact views end before methods regain mutable mesh access.
#[derive(Default)]
pub struct Surfaces {
    pub entries: Vec<Option<Deformable>>,
    pub meshes: Meshes,
}
impl Surfaces {
    pub fn with_sampling<R>(
        &mut self,
        entity: Option<EntityId>,
        colliders: &[Collider],
        run: impl FnOnce(Option<&mut contact_pipeline::Deformable<'_>>) -> R,
    ) -> Result<R, i32> {
        let Some(entity) = entity else {
            return Ok(run(None));
        };
        let index = colliders
            .iter()
            .position(|c| c.entity == entity)
            .ok_or(-1)?;
        let collider = &colliders[index];
        let surface = self
            .entries
            .get_mut(index)
            .and_then(Option::as_mut)
            .ok_or(-1)?;
        surface.sampling.state.pose = deformation::Pose {
            origin: collider.pose.position,
            inverse_basis: collider.pose.matrix.0,
        };
        let mut view = contact_pipeline::Deformable {
            geometry: deformation::Geometry {
                sample_bounds: surface.sampling.bounds,
                edges: [
                    collider.bounds.min[0],
                    collider.bounds.max[0],
                    collider.bounds.min[2],
                    collider.bounds.max[2],
                ],
                edge_cap: surface.sampling.edge_cap,
                vertices: &self.meshes.get(surface.mesh).ok_or(-1)?.vertices,
            },
            state: &mut surface.sampling.state,
        };
        Ok(run(Some(&mut view)))
    }
    pub fn collide(&mut self, call: Invocation<'_>) -> Result<bool, i32> {
        self.entries
            .get_mut(call.target.0)
            .and_then(Option::as_mut)
            .ok_or(-1)?
            .collide(&mut self.meshes, call)
    }
}
impl Deformable {
    /// Execute the real method and return its sound-trigger event. Playback is
    /// a host operation; the game-owned RNG/timer writes have already committed.
    pub fn collide(&mut self, meshes: &mut Meshes, call: Invocation<'_>) -> Result<bool, i32> {
        let collider = call.world.colliders.get(call.target.0).ok_or(-1)?;
        let sound = self
            .state
            .hit(
                self.config,
                collider,
                deformable_hit::Mesh {
                    vertices: &mut meshes.get_mut(self.mesh).ok_or(-1)?.vertices,
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
    use tankracer_assets::mesh::Geometry;
    struct Methods {
        surfaces: Surfaces,
        sounds: usize,
    }
    impl Effects for Methods {
        fn method(&mut self, method: MethodId, call: Invocation<'_>) -> Result<(), i32> {
            assert_eq!(method, DEFORMABLE_HIT);
            self.sounds += usize::from(self.surfaces.collide(call)?);
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
        let mut meshes = Meshes::default();
        let mesh = meshes.insert(Geometry {
            vertices: vec![[0, 200, 0]],
            triangles: Vec::new(),
        });
        let mut methods = Methods {
            surfaces: Surfaces {
                meshes,
                entries: vec![Some(Deformable {
                    runtime: crush::State::default(),
                    config: deformable_hit::Config {
                        floor: 0,
                        compression: 64,
                        drift_shift: 0,
                        material: -7,
                        has_sound: true,
                    },
                    state: deformable_hit::State::default(),
                    mesh,
                    vertex_flags: vec![4],
                    sampling: Sampling {
                        bounds: [[-500, 500]; 2],
                        edge_cap: 65536,
                        state: deformation::State {
                            records_attached: true,
                            records: vec![deformation::Record {
                                world_position: [0; 3],
                                timer: 0,
                                position: [0; 3],
                                state: 0,
                                color: [9; 3],
                            }],
                            ..Default::default()
                        },
                    },
                })],
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
            reference_basis: crate::math::Matrix::IDENTITY,
            external_basis: None,
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
        let (vertices, first) = methods
            .surfaces
            .with_sampling(Some(EntityId(2)), &world.colliders, |view| {
                let view = view.unwrap();
                (
                    view.geometry.vertices.as_ptr(),
                    view.state.sample(&view.geometry, [0; 3]),
                )
            })
            .unwrap();
        assert_eq!(first.vertex_height, 200);
        assert_eq!(first.activated_records, 1);
        let surface = methods.surfaces.entries[0].as_ref().unwrap();
        assert_eq!(surface.state.dirty, 0);
        assert_eq!(surface.sampling.state.dirty, 1);
        assert_eq!(
            obstacle::deformables(&mut world, &mut vehicle, env, &mut shared, &mut methods),
            Ok(Some(ObstacleId(0)))
        );
        let surface = methods.surfaces.entries[0].as_ref().unwrap();
        assert_eq!(
            methods.surfaces.meshes.get(surface.mesh).unwrap().vertices,
            [[1, 193, 1]]
        );
        assert_eq!(surface.state.last_vehicle, Some(EntityId(1)));
        assert_eq!(surface.state.dirty, 1);
        let second = methods
            .surfaces
            .with_sampling(Some(EntityId(2)), &world.colliders, |view| {
                let view = view.unwrap();
                assert_eq!(view.geometry.vertices.as_ptr(), vertices);
                view.state.sample(&view.geometry, [0; 3])
            })
            .unwrap();
        assert_eq!(second.vertex_height, 193);
        assert_eq!(second.activated_records, 0);
        assert_eq!(vehicle.material_tag, -7);
        assert_eq!(vehicle.deformable, Some(EntityId(2)));
        assert_eq!(methods.sounds, 1);
        assert_eq!(shared.random, 1586005467);
        assert_eq!(shared.current_slot, 9);
        assert_eq!(vehicle.velocity, [0; 3]);
        assert_eq!(
            methods
                .surfaces
                .with_sampling(Some(EntityId(99)), &world.colliders, |_| panic!(
                    "missing entity must not enter contact"
                )),
            Err::<(), _>(-1)
        );
    }
}
