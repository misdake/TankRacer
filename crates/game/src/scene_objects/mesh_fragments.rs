//! Mesh-break resources and virtual lifetime policy on the authoritative owner.
use super::{
    View,
    collision::{Collision, Services},
    dispatch_code, spatial_code, valid_limit,
};
use crate::scene_entity::Handle;
use crate::{
    entity_factory::{Environment, Invocation},
    mesh_break,
    object_query::ObstacleId,
    obstacle_collision, spatial,
};
impl Collision<'_> {
    pub(super) fn activate_mesh_fragments(
        &mut self,
        mut call: obstacle_collision::Invocation<'_>,
        services: &mut impl Services,
    ) -> Result<(), i32> {
        let limit = self.environment.work_limit;
        valid_limit(limit)?;
        let id = call.target;
        let entity = call.world.colliders.get(id.0).ok_or(-1)?.entity;
        if self.components.object(id)?.auxiliary_active != 0 {
            *call.result = entity.0;
            return Ok(());
        }
        let prepared = self
            .components
            .object(id)?
            .auxiliary_motion
            .as_ref()
            .is_some_and(|s| s.prepared);
        if !prepared {
            let name = self
                .components
                .object(id)?
                .data
                .auxiliary
                .as_ref()
                .ok_or(-1)?
                .shape_name
                .clone();
            let model = services.auxiliary_model(&name)?.ok_or(-4)?;
            let shared = self.components.surfaces.meshes.bind(&name, &model);
            let private = self
                .components
                .surfaces
                .meshes
                .private_copy(shared)
                .ok_or(-4)?;
            if let Some(old) = self.components.object(id)?.auxiliary_mesh {
                self.components
                    .surfaces
                    .meshes
                    .release_private(old)
                    .ok_or(-1)?;
            }
            let object = self.components.object_mut(id)?;
            object.auxiliary_mesh = Some(private);
            object.auxiliary_motion = Some(mesh_break::State::new(model.geometry.triangles.len()));
        }
        let height = services.auxiliary_height(self, super::collision::reborrow(&mut call))?;
        let basis = call.world.colliders[id.0].external_basis;
        let object = self
            .components
            .objects
            .get_mut(id.0)
            .and_then(Option::as_mut)
            .ok_or(-1)?;
        let mesh = object.auxiliary_mesh.ok_or(-1)?;
        mesh_break::activate(
            object.auxiliary_motion.as_mut().ok_or(-1)?,
            self.components.surfaces.meshes.fragments(mesh).ok_or(-1)?,
            object.data.auxiliary.as_ref().ok_or(-1)?,
            mesh_break::Hit {
                velocity: (call.vehicle.entity.0 != 0).then_some(call.vehicle.velocity),
                basis,
                height,
                paired_tail: services.paired_vertex_tail(),
            },
            call.random,
            limit,
        )?;
        object.data.mesh = Some(mesh);
        object.auxiliary_active = 1;
        call.world.colliders[id.0].radius = 5120;
        spatial::remove(&mut call.world.grid, id, object.data.cell, limit).map_err(spatial_code)?;
        *call.result = entity.0;
        Ok(())
    }
}
impl View<'_> {
    pub(super) fn tick_mesh_fragments(
        &mut self,
        id: ObstacleId,
        mut call: Invocation<'_, Handle>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        valid_limit(env.work_limit)?;
        let object = self
            .components
            .objects
            .get_mut(id.0)
            .and_then(Option::as_mut)
            .ok_or(-1)?;
        let mesh = object.auxiliary_mesh.ok_or(-1)?;
        let done = mesh_break::tick(
            object.auxiliary_motion.as_mut().ok_or(-1)?,
            self.components.surfaces.meshes.fragments(mesh).ok_or(-1)?,
            object.data.auxiliary.as_ref().ok_or(-1)?,
            self.world.colliders[id.0].pose.position,
            self.components.dt,
            env.work_limit,
        )?;
        if done {
            object.auxiliary_active = 0;
            object.data.mesh = None;
            self.world.colliders[id.0].radius = 0;
            let slot = if self.components.mode == 0 || self.components.mode == 1 {
                self.components.interfaces.deactivate
            } else {
                self.components.interfaces.auxiliary_complete
            };
            call.context
                .dispatch(call.entity, slot, None, env, self)
                .map_err(dispatch_code)?;
        }
        *call.result = call.entity.0;
        Ok(())
    }
}
