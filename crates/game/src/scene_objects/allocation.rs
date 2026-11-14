//! Shared scene-object allocation, without implicit memberships or RNG.
use super::{Object, View};
use crate::{
    dispatch::ClassId,
    entity_factory::Invocation,
    geometry::Bounds,
    math::Matrix,
    object_query::{Collider, ObstacleId},
    obstacle_collision,
    obstacle_startup::PlacedObstacle,
    pose::Pose,
    scene_entity::Handle,
    scene_startup::Visibility,
    spatial::Cell,
};
impl View<'_> {
    pub(crate) fn allocate_object(
        &mut self,
        call: &mut Invocation<'_, Handle>,
        is_static: bool,
    ) -> Result<(ClassId, ObstacleId), i32> {
        let class = call
            .context
            .entities
            .record(call.entity)
            .and_then(|v| v.class)
            .ok_or(-1)?;
        let id = ObstacleId(self.world.colliders.len());
        if self.world.objects.len() != id.0 || self.components.objects.len() != id.0 {
            return Err(-1);
        }
        self.world.colliders.push(Collider {
            entity: call.entity,
            pose: Pose {
                position: [0; 3],
                matrix: Matrix([0; 9]),
            },
            external_basis: None,
            radius: 0,
            bounds: Bounds {
                min: [0; 3],
                max: [0; 3],
            },
        });
        self.world.objects.push(obstacle_collision::Object {
            class,
            weight: 0,
            threshold: 0,
            activity: 0,
            flags: 0,
            mesh_bounds: [[0; 3]; 2],
        });
        self.components.objects.push(Some(Object {
            impact_sound: None,
            data: PlacedObstacle {
                id,
                cell: Some(Cell { x: 0, z: 0 }),
                source: None,
                mesh: None,
                visibility: Visibility {
                    distance: 0,
                    add_lap: 0,
                    remove_lap: 0,
                    entered: false,
                },
                sort_position: 0,
                impact_threshold: 0,
                collision_margin: 0,
                auxiliary: None,
                debris: None,
            },
            auxiliary_mesh: None,
            auxiliary_active: 0,
            auxiliary_motion: None,
            ground_height: None,
        }));
        call.context
            .entities
            .record_mut(call.entity)
            .ok_or(-1)?
            .object = Some(Handle::Obstacle(id));
        call.context
            .ticks
            .flags
            .resize(call.context.entities.capacity() as usize, 0);
        call.context.ticks.flags[call.entity.0 as usize] = if is_static { 8 } else { 0 };
        Ok((class, id))
    }
}
