//! Common scene lifecycle adapters, independent of any motion subclass.
use super::{DEACTIVATE, dispatch_code, spatial_code, valid_limit};
use crate::{
    dispatch::{ClassId, MethodId},
    entity_factory::{BaseMethod, Environment, Invocation, Methods},
    object_query::ObstacleId,
    obstacle_collision::Grid,
    scene_entity::Handle,
    spatial::{self, Cell},
};

pub(super) struct Base;
impl Methods<Handle> for Base {
    fn invoke(
        &mut self,
        method: MethodId,
        call: Invocation<'_, Handle>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        match method {
            crate::breakable::ENABLE_TICK => BaseMethod::EnableTick,
            super::QUEUE_DESTROY => BaseMethod::QueueDestroy,
            super::ZERO => BaseMethod::Zero,
            super::ONE => BaseMethod::One,
            super::IDENTITY => BaseMethod::Identity,
            _ => return Err(-5),
        }
        .invoke(call, env)
    }
}
/// The common Obstacle deactivate implementation also used by ordinary View.
/// Its parent dispatch runs before spatial/list removal, including on errors.
pub(super) struct Membership<'a> {
    pub grid: &'a mut Grid,
    pub id: ObstacleId,
    pub cell: Option<Cell>,
    pub ordinary: &'a mut Vec<ObstacleId>,
    pub special: &'a mut Vec<ObstacleId>,
    pub parent: ClassId,
}
impl Methods<Handle> for Membership<'_> {
    fn invoke(
        &mut self,
        method: MethodId,
        mut call: Invocation<'_, Handle>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        if method != DEACTIVATE {
            return Base.invoke(method, call, env);
        }
        valid_limit(env.work_limit)?;
        call.context
            .class_dispatch(self.parent, call.entity, None, env, self)
            .map_err(dispatch_code)?;
        spatial::remove(self.grid, self.id, self.cell, env.work_limit).map_err(spatial_code)?;
        spatial::remove_first(self.ordinary, &self.id, env.work_limit).map_err(spatial_code)?;
        spatial::remove_first(self.special, &self.id, env.work_limit).map_err(spatial_code)?;
        *call.result = 0;
        Ok(())
    }
}
