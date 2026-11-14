//! Spin borrows the authoritative collider and queue; terrain is immutable.
use super::{View, dispatch_code, valid_limit};
use crate::{
    entity_factory::{Environment, Invocation},
    object_query::{EntityId, ObstacleId},
    scene_entity::Handle,
    spin, terrain,
};
use tankracer_assets::terrain::TerrainData;

pub(super) struct Services<'a> {
    pub terrain: Option<&'a TerrainData>,
    pub ticks: &'a mut crate::lifecycle::Ticks<EntityId>,
    pub queues: &'a mut crate::lifecycle::Queues<EntityId>,
    pub entity: EntityId,
    pub limit: u32,
}
impl crate::mover::Services for Services<'_> {
    fn normal_height(&mut self, x: i32, z: i32, n: &mut [i32; 3]) -> Result<i32, i32> {
        spin::Services::normal_height(self, x, z, n)
    }
    fn stop(&mut self) -> Result<(), i32> {
        spin::Services::stop(self)
    }
}
fn terrain_code(e: terrain::Error) -> i32 {
    if e == terrain::Error::Arithmetic {
        -2
    } else {
        -1
    }
}
impl spin::Services for Services<'_> {
    fn stop(&mut self) -> Result<(), i32> {
        self.queues
            .queue(self.ticks, self.entity, false, self.limit)
            .map_err(|e| crate::crush::code(crate::crush::Error::Lifecycle(e)))
    }
    fn normal_height(&mut self, x: i32, z: i32, normal: &mut [i32; 3]) -> Result<i32, i32> {
        let mut height = terrain::MISS_HEIGHT;
        terrain::normal_height(self.terrain.ok_or(-1)?, x, z, &mut height, normal)
            .map_err(terrain_code)?;
        Ok(height)
    }
    fn height(&mut self, x: i32, z: i32) -> Result<i32, i32> {
        let mut height = terrain::MISS_HEIGHT;
        terrain::height(self.terrain.ok_or(-1)?, x, z, &mut height).map_err(terrain_code)?;
        Ok(height)
    }
    fn fallback(&mut self, position: [i32; 3], normal: &mut [i32; 3]) -> Result<bool, i32> {
        Ok(crate::geometry::fallback(
            self.terrain.ok_or(-1)?,
            position,
            0,
            normal,
        ))
    }
}
impl View<'_> {
    pub(super) fn tick_spin(
        &mut self,
        id: ObstacleId,
        mut call: Invocation<'_, Handle>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        valid_limit(env.work_limit)?;
        let bounds = self.world.objects[id.0]
            .mesh_bounds
            .map(|v| v.map(i32::from));
        let component = self
            .components
            .spins
            .get_mut(id.0)
            .and_then(Option::as_mut)
            .ok_or(-1)?;
        spin::tick(
            spin::Motion {
                state: &mut component.state,
                collider: &mut self.world.colliders[id.0],
                activity: &mut self.world.objects[id.0].activity,
                reference_alias: component.reference_alias,
            },
            component.config,
            self.components.objects[id.0]
                .as_ref()
                .ok_or(-1)?
                .data
                .impact_threshold,
            bounds,
            self.components.gravity,
            self.components.dt,
            &mut Services {
                terrain: self.components.terrain.as_deref(),
                ticks: call.context.ticks,
                queues: call.context.queues,
                entity: call.entity,
                limit: env.work_limit,
            },
        )?;
        if self.components.object(id)?.data.auxiliary.is_some()
            && self.components.object(id)?.auxiliary_active != 0
        {
            call.context
                .dispatch(
                    call.entity,
                    self.components.interfaces.auxiliary_tick,
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
