//! Fragment updates share the caller's random stream and real deactivation.
use super::{Execution, View, dispatch_code, valid_limit};
use crate::scene_entity::Handle;
use crate::{
    entity_factory::{Environment, Invocation},
    object_query::ObstacleId,
    shatter,
};
impl View<'_> {
    pub(super) fn tick_debris(
        &mut self,
        id: ObstacleId,
        mut call: Invocation<'_, Handle>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        valid_limit(env.work_limit)?;
        let Execution::Simulation { random } = &mut self.execution else {
            return Err(-1);
        };
        let (dt, group) = (self.components.dt, self.components.frame_group);
        let debris = self
            .components
            .object_mut(id)?
            .data
            .debris
            .as_mut()
            .ok_or(-1)?;
        let deactivate = shatter::tick(debris, call.entity, dt, group, random, env.work_limit)?;
        if deactivate {
            call.context
                .dispatch(
                    call.entity,
                    self.components.interfaces.deactivate,
                    None,
                    env,
                    self,
                )
                .map_err(dispatch_code)?;
        }
        *call.result = call.entity.0;
        Ok(())
    }
}
