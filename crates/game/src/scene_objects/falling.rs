//! Fall bindings borrow the scene's terrain, memberships and shared scheduler.
use super::{View, dispatch_code, spatial_code, valid_limit};
use crate::scene_entity::Handle;
use crate::{
    crush,
    entity_factory::{Environment, Invocation},
    fall,
    math::Matrix,
    object_query::ObstacleId,
    spatial::{self, Cell},
};
use std::sync::Arc;
use tankracer_assets::terrain::TerrainData;
impl View<'_> {
    pub(super) fn destroy_fall(
        &mut self,
        id: ObstacleId,
        mut call: Invocation<'_, Handle>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        let entity = call.entity;
        valid_limit(env.work_limit)?;
        let state = self
            .components
            .falls
            .get_mut(id.0)
            .and_then(Option::as_mut)
            .ok_or(-1)?;
        if let Some(attached) = &state.attached
            && attached.len() > env.work_limit as usize
        {
            state.attached = Some(Arc::from(&attached[env.work_limit as usize..]));
            return Err(-3);
        }
        state.attached = None;
        call.context
            .class_dispatch(self.components.parents.obstacle, entity, None, env, self)
            .map_err(dispatch_code)?;
        *call.result = 0;
        Ok(())
    }
    pub(super) fn tick_fall(
        &mut self,
        id: ObstacleId,
        mut call: Invocation<'_, Handle>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        let entity = call.entity;
        valid_limit(env.work_limit)?;
        let peer = self
            .components
            .falls
            .get(id.0)
            .and_then(Option::as_ref)
            .ok_or(-1)?
            .last_vehicle;
        let vehicle_basis =
            if self.world.objects[id.0].activity != 0 && self.world.objects[id.0].flags & 2 != 0 {
                let peer = peer
                    .and_then(|p| call.context.entities.record(p))
                    .and_then(|r| r.object)
                    .ok_or(-1)?;
                match peer {
                    Handle::Obstacle(id) => self.world.colliders.get(id.0).ok_or(-1)?.pose.matrix,
                    Handle::Vehicle(id) => {
                        self.vehicles
                            .ok_or(-1)?
                            .entries
                            .get(id.0)
                            .and_then(Option::as_ref)
                            .ok_or(-1)?
                            .physics
                            .motion
                            .poses
                            .reference
                    }
                }
            } else {
                Matrix::IDENTITY
            };
        let effect_active = self
            .components
            .object(id)?
            .data
            .debris
            .as_ref()
            .is_some_and(|d| d.flags & 1 != 0);
        let cell = self.components.object(id)?.data.cell;
        let collider = self.world.colliders.get_mut(id.0).ok_or(-1)?;
        let properties = self.world.objects.get_mut(id.0).ok_or(-1)?;
        if properties.activity != 0 && collider.external_basis.is_none() {
            return Err(-1);
        }
        let mut matrix = collider.external_basis.unwrap_or(collider.pose.matrix);
        let alias = collider.external_basis == Some(collider.pose.matrix);
        let width = (i32::from(properties.mesh_bounds[1][1])
            - i32::from(properties.mesh_bounds[0][1]))
        .wrapping_mul(8);
        let mut services = FallServices {
            terrain: self.components.terrain.as_deref(),
            grid: &mut self.world.grid,
            id,
            entity,
            cell,
            ticks: call.context.ticks,
            queues: call.context.queues,
            limit: env.work_limit,
        };
        let status = fall::tick(
            fall::Motion {
                state: self
                    .components
                    .falls
                    .get_mut(id.0)
                    .and_then(Option::as_mut)
                    .ok_or(-1)?,
                matrix: &mut matrix,
                flags: &mut properties.flags,
                activity: &mut properties.activity,
                position: collider.pose.position,
                width,
                vehicle_basis,
                dt: self.components.dt,
                effect_active,
            },
            self.components.fall_tuning,
            &mut services,
        );
        if collider.external_basis.is_some() {
            collider.external_basis = Some(matrix);
        }
        if alias {
            collider.pose.matrix = matrix;
        }
        status?;
        // These remain real virtual calls, not silently dropped effects.
        if self.components.object(id)?.data.auxiliary.is_some()
            && self.components.object(id)?.auxiliary_active != 0
        {
            call.context
                .dispatch(
                    entity,
                    self.components.interfaces.auxiliary_tick,
                    None,
                    env,
                    self,
                )
                .map_err(dispatch_code)?;
        }
        if self
            .components
            .object(id)?
            .data
            .debris
            .as_ref()
            .is_some_and(|d| d.flags & 1 != 0)
        {
            call.context
                .dispatch(
                    entity,
                    self.components.interfaces.debris_tick,
                    None,
                    env,
                    self,
                )
                .map_err(dispatch_code)?;
        }
        *call.result = 0;
        Ok(())
    }
}

struct FallServices<'a> {
    terrain: Option<&'a TerrainData>,
    grid: &'a mut crate::obstacle_collision::Grid,
    id: ObstacleId,
    entity: crate::object_query::EntityId,
    cell: Option<Cell>,
    ticks: &'a mut crate::lifecycle::Ticks<crate::object_query::EntityId>,
    queues: &'a mut crate::lifecycle::Queues<crate::object_query::EntityId>,
    limit: u32,
}
impl fall::Services for FallServices<'_> {
    fn height(&mut self, x: i32, z: i32) -> Result<i32, i32> {
        let mut height = crate::terrain::MISS_HEIGHT;
        crate::terrain::height(self.terrain.ok_or(-1)?, x, z, &mut height).map_err(|error| {
            match error {
                crate::terrain::Error::Arithmetic => -2,
                crate::terrain::Error::InvalidBucket => -1,
            }
        })?;
        Ok(height)
    }
    fn stop(&mut self) -> Result<(), i32> {
        spatial::remove(self.grid, self.id, self.cell, self.limit).map_err(spatial_code)?;
        self.queues
            .queue(self.ticks, self.entity, false, self.limit)
            .map_err(|error| crush::code(crush::Error::Lifecycle(error)))
    }
}
