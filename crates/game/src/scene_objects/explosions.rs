//! Scenery-explosion allocation and retirement use the common scene identity,
//! drawing membership, scheduler and queues. No private synthetic entity IDs.
use super::{Execution, View, dispatch_code, valid_limit};
use crate::{
    entity_factory::{Context, Environment, Invocation},
    explosion,
    object_query::{EntityId, ObstacleId},
    random,
    scene_entity::Handle,
    scene_loading, spatial,
};
impl View<'_> {
    pub(crate) fn spawn_explosion(
        &mut self,
        context: &mut Context<'_, Handle>,
        position: [i32; 3],
        random: &mut u32,
        env: Environment<'_>,
    ) -> Result<EntityId, i32> {
        valid_limit(env.work_limit)?;
        let class = self.components.explosion_class.ok_or(-5)?;
        let entity = context.create(class, env, self).map_err(dispatch_code)?;
        let first = random::next(random) >> 4;
        let id = context
            .entities
            .record(entity)
            .and_then(|r| r.object)
            .and_then(Handle::obstacle)
            .ok_or(-4)?;
        context
            .dispatch(entity, scene_loading::ACTIVATE, None, env, self)
            .map_err(dispatch_code)?;
        self.components.special.push(id);
        self.world.colliders[id.0].pose.position = position;
        explosion::initialize(
            self.components.explosions[id.0].as_mut().ok_or(-4)?,
            position,
            first,
            random,
            self.components.explosion_resources,
        );
        let world = &mut self.components.explosion_world;
        if world.count < 15 {
            world.count = world.count.wrapping_add(1);
            if let Some(index) = world.slots.iter().position(|s| s.entity.is_none()) {
                world.slots[index] = explosion::Slot {
                    entity: Some(entity),
                    frame: self.components.frame_group,
                };
                self.components.explosions[id.0].as_mut().ok_or(-4)?.slot = index as i16;
            }
        } else {
            let mut index = 0;
            for i in 1..15 {
                if world.slots[i].frame < world.slots[index].frame {
                    index = i;
                }
            }
            let old = world.slots[index].entity.ok_or(-1)?;
            let old_id = context
                .entities
                .record(old)
                .and_then(|r| r.object)
                .and_then(Handle::obstacle)
                .ok_or(-1)?;
            spatial::remove_first(&mut self.components.special, &old_id, env.work_limit)
                .map_err(super::spatial_code)?;
            context
                .dispatch(old, scene_loading::DEACTIVATE, None, env, self)
                .map_err(dispatch_code)?;
            self.components.explosion_world.slots[index] = explosion::Slot {
                entity: Some(entity),
                frame: self.components.frame_group,
            };
            self.components.explosions[id.0].as_mut().ok_or(-4)?.slot = index as i16;
            self.components.explosion_world.overflow = 1;
            self.components.explosion_world.fog = self.components.explosion_resources.map_fog;
        }
        explosion::finish_spawn(self.components.explosions[id.0].as_mut().ok_or(-4)?);
        Ok(entity)
    }
    pub(super) fn tick_explosion(
        &mut self,
        id: ObstacleId,
        mut call: Invocation<'_, Handle>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        valid_limit(env.work_limit)?;
        let Execution::Simulation { random } = &mut self.execution else {
            return Err(-1);
        };
        let state = self
            .components
            .explosions
            .get_mut(id.0)
            .and_then(Option::as_mut)
            .ok_or(-1)?;
        let retire = explosion::tick(
            state,
            self.world.colliders[id.0].pose.position,
            self.components.dt,
            random,
            self.components.explosion_resources,
        )?;
        if retire {
            let slot = usize::try_from(state.slot).map_err(|_| -1)?;
            self.components
                .explosion_world
                .slots
                .get_mut(slot)
                .ok_or(-1)?
                .entity = None;
            spatial::remove_first(&mut self.components.special, &id, env.work_limit)
                .map_err(super::spatial_code)?;
            call.context
                .dispatch(call.entity, scene_loading::DEACTIVATE, None, env, self)
                .map_err(dispatch_code)?;
            self.components.explosion_world.count =
                self.components.explosion_world.count.wrapping_sub(1);
        }
        *call.result = 0;
        Ok(())
    }
}
