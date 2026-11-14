//! Borrowing adapter for Bounce motion and shared lifecycle methods. Matrices,
//! grid membership, tick/stop/destroy queues and RNG keep their actual owners.
use super::lifecycle::{Base, Membership};
use super::{View, dispatch_code, spatial_code, valid_limit};
use crate::{
    bounce,
    entity_factory::{self as factory, Environment, Invocation},
    object_query::{Collider, EntityId, ObstacleId},
    scene_entity::Handle,
    scene_loading,
    spatial::{self, Cell},
    terrain,
};
use tankracer_assets::terrain::TerrainData;

pub(super) struct Services<'a, 'e> {
    pub terrain: Option<&'a TerrainData>,
    pub context: factory::Context<'a, Handle>,
    pub environment: Environment<'e>,
    pub entity: EntityId,
    pub membership: Membership<'a>,
    pub cell: &'a mut Option<Cell>,
    pub has_mesh: bool,
}
impl bounce::Services for Services<'_, '_> {
    fn activate(&mut self) -> Result<(), i32> {
        self.context
            .dispatch(
                self.entity,
                scene_loading::ACTIVATE,
                None,
                self.environment,
                &mut Base,
            )
            .map_err(dispatch_code)?;
        Ok(())
    }
    fn deactivate(&mut self) -> Result<(), i32> {
        self.context
            .dispatch(
                self.entity,
                scene_loading::DEACTIVATE,
                None,
                self.environment,
                &mut self.membership,
            )
            .map_err(dispatch_code)?;
        Ok(())
    }
    fn settle(&mut self, collider: &Collider) -> Result<(), i32> {
        spatial::remove(
            self.membership.grid,
            self.membership.id,
            *self.cell,
            self.environment.work_limit,
        )
        .map_err(spatial_code)?;
        let cell = spatial::insert(
            self.membership.grid,
            self.membership.id,
            collider,
            self.has_mesh.then_some(collider.radius),
        );
        *self.cell = Some(cell);
        self.membership.cell = Some(cell);
        self.context
            .queues
            .queue(
                self.context.ticks,
                self.entity,
                false,
                self.environment.work_limit,
            )
            .map_err(|e| crate::crush::code(crate::crush::Error::Lifecycle(e)))
    }
    fn height(&mut self, x: i32, z: i32) -> Result<i32, i32> {
        let mut height = terrain::MISS_HEIGHT;
        terrain::height(self.terrain.ok_or(-1)?, x, z, &mut height).map_err(code)?;
        Ok(height)
    }
    fn normal_height(&mut self, x: i32, z: i32, normal: &mut [i32; 3]) -> Result<i32, i32> {
        let mut height = terrain::MISS_HEIGHT;
        terrain::normal_height(self.terrain.ok_or(-1)?, x, z, &mut height, normal).map_err(code)?;
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
fn code(e: terrain::Error) -> i32 {
    if e == terrain::Error::Arithmetic {
        -2
    } else {
        -1
    }
}
impl View<'_> {
    pub(super) fn tick_bounce(
        &mut self,
        id: ObstacleId,
        call: Invocation<'_, Handle>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        valid_limit(env.work_limit)?;
        let component = self
            .components
            .bounces
            .get_mut(id.0)
            .and_then(Option::as_mut)
            .ok_or(-1)?;
        let object = self
            .components
            .objects
            .get_mut(id.0)
            .and_then(Option::as_mut)
            .ok_or(-1)?;
        let cell = object.data.cell;
        bounce::tick(
            bounce::Motion {
                state: &mut component.state,
                collider: &mut self.world.colliders[id.0],
                activity: &mut self.world.objects[id.0].activity,
                threshold: object.data.impact_threshold,
                reference_alias: component.reference_alias,
            },
            component.config,
            self.components.dt,
            &mut Services {
                terrain: self.components.terrain.as_deref(),
                context: call.context,
                environment: env,
                entity: call.entity,
                membership: Membership {
                    grid: &mut self.world.grid,
                    id,
                    cell,
                    ordinary: &mut self.components.ordinary,
                    special: &mut self.components.special,
                    parent: self.components.parents.generic,
                },
                cell: &mut object.data.cell,
                has_mesh: object.data.mesh.is_some(),
            },
        )?;
        *call.result = 0;
        Ok(())
    }
}
