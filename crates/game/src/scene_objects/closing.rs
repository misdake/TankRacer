//! Closing barrier phase dispatch on the authoritative scene/factory.
use super::{View, dispatch_code, valid_limit};
use crate::{
    closer::{self, Phase},
    entity_factory::{Environment, Invocation},
    object_query::ObstacleId,
    scene_entity::Handle,
};
impl View<'_> {
    pub(super) fn tick_closer(
        &mut self,
        id: ObstacleId,
        mut call: Invocation<'_, Handle>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        valid_limit(env.work_limit)?;
        let component = self
            .components
            .closers
            .get_mut(id.0)
            .and_then(Option::as_mut)
            .ok_or(-1)?;
        match component.state.phase {
            Phase::Uninitialized => return Err(-5),
            Phase::Waiting => {
                let player = self
                    .vehicles
                    .and_then(|s| s.entries.first())
                    .and_then(Option::as_ref)
                    .map(|p| p.physics.motion.poses.base_position);
                closer::poll(
                    &mut component.state,
                    call.entity,
                    self.components.frame_group,
                    self.world.colliders[id.0].pose.position,
                    player,
                );
            }
            Phase::Closing => {
                let collider = &mut self.world.colliders[id.0];
                let mut matrix = collider.external_basis.ok_or(-4)?;
                let result = closer::close(
                    &mut component.state,
                    &mut matrix,
                    self.components.dt,
                    env.work_limit,
                    || {
                        call.context
                            .queues
                            .queue(call.context.ticks, call.entity, false, env.work_limit)
                            .map_err(|e| crate::crush::code(crate::crush::Error::Lifecycle(e)))
                    },
                );
                collider.external_basis = Some(matrix);
                if component.reference_alias {
                    collider.pose.matrix = matrix;
                }
                result?;
            }
            Phase::Fragments => {
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
        }
        *call.result = 0;
        Ok(())
    }
}
