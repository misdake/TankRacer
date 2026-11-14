//! Mover phases borrow the same Spin state and parent method table.
use super::{View, dispatch_code, valid_limit};
use crate::{
    entity_factory::{Environment, Invocation},
    mover::{self, Tick},
    object_query::ObstacleId,
    scene_entity::Handle,
    spin,
};
impl View<'_> {
    pub(super) fn tick_mover(
        &mut self,
        id: ObstacleId,
        mut call: Invocation<'_, Handle>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        valid_limit(env.work_limit)?;
        let players = std::array::from_fn(|i| {
            self.vehicles
                .and_then(|v| v.entries.get(i))
                .and_then(Option::as_ref)
                .map(|v| v.physics.motion.poses.base_position)
        });
        let debris_active = self
            .components
            .object(id)?
            .data
            .debris
            .as_ref()
            .is_some_and(|d| d.flags & 1 != 0);
        let state = self
            .components
            .movers
            .get_mut(id.0)
            .and_then(Option::as_mut)
            .ok_or(-1)?;
        let spin = self
            .components
            .spins
            .get_mut(id.0)
            .and_then(Option::as_mut)
            .ok_or(-1)?;
        let action = mover::tick(
            state,
            spin::Motion {
                state: &mut spin.state,
                collider: &mut self.world.colliders[id.0],
                activity: &mut self.world.objects[id.0].activity,
                reference_alias: spin.reference_alias,
            },
            spin.config,
            mover::Inputs {
                dt: self.components.dt,
                camera: self.components.camera_position,
                players,
            },
            debris_active,
            &mut super::spinning::Services {
                terrain: self.components.terrain.as_deref(),
                ticks: call.context.ticks,
                queues: call.context.queues,
                entity: call.entity,
                limit: env.work_limit,
            },
        )?;
        match action {
            Tick::None => (),
            Tick::Spin => {
                call.context
                    .class_dispatch(self.components.parents.spin, call.entity, None, env, self)
                    .map_err(dispatch_code)?;
            }
            Tick::Debris => {
                call.context
                    .dispatch(
                        call.entity,
                        self.components.interfaces.debris_tick,
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
