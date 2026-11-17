//! Startup-only actor construction bindings. Unknown runtime calls fail rather
//! than pretending to update an actor whose runtime has not been integrated.
use super::*;
use crate::actor_startup::{self, Actor, Kind};
pub(super) fn register(registry: &mut Registry) {
    for (index, kind) in Kind::ALL.into_iter().enumerate() {
        let parent = if kind == Kind::Powerup {
            registry.obstacle
        } else {
            registry.generic
        };
        let mut class = (*registry.classes[parent.0]).clone();
        class.methods[CONSTRUCT as usize] = Some(MethodId(100 + index as u16));
        class.methods[TICK as usize] = Some(MethodId(109));
        if matches!(
            kind,
            Kind::Powerup | Kind::Train | Kind::Flock | Kind::Background
        ) {
            class.methods[ENTER as usize] = Some(MethodId(112));
            class.methods[LEAVE as usize] = Some(MethodId(113));
        }
        if kind == Kind::Powerup {
            class.methods[AFTER_CONSTRUCT as usize] = Some(methods::IDENTITY);
        }
        let id = ClassId(registry.classes.len());
        registry.classes.push(Arc::new(class));
        registry.actor_classes.push((kind, id));
    }
}
impl<S: ShapeSource> Loader<'_, S> {
    pub(super) fn actor_membership(
        &mut self,
        method: MethodId,
        mut call: Invocation<'_, Handle>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        let record = call.context.entities.record(call.entity).ok_or(-1)?;
        let class = record.class.ok_or(-1)?;
        let kind = self
            .registry
            .actor_classes
            .iter()
            .find(|(_, id)| *id == class)
            .map(|(kind, _)| *kind)
            .ok_or(-1)?;
        let id = record.object.and_then(Handle::obstacle).ok_or(-1)?;
        if method == MethodId(112) {
            if matches!(kind, Kind::Train | Kind::Flock) {
                self.scene.components.special.push(id);
            }
            call.context
                .dispatch(call.entity, ACTIVATE, None, env, self)
                .map_err(factory_code)?;
        } else {
            spatial::remove_first(&mut self.scene.components.special, &id, env.work_limit)
                .map_err(spatial_code)?;
            if kind == Kind::Powerup {
                let cell = self.scene.components.object(id)?.data.cell;
                spatial::remove(&mut self.scene.world.grid, id, cell, env.work_limit)
                    .map_err(spatial_code)?;
            }
            call.context
                .queues
                .queue(call.context.ticks, call.entity, false, env.work_limit)
                .map_err(|_| -3)?;
        }
        *call.result = 0;
        Ok(())
    }
    pub(super) fn construct_actor(
        &mut self,
        method: MethodId,
        mut call: Invocation<'_, Handle>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        let kind = Kind::ALL[usize::from(method.0 - 100)];
        let parent = if kind == Kind::Powerup {
            self.registry.obstacle
        } else {
            self.registry.generic
        };
        call.context
            .class_dispatch(parent, call.entity, None, env, self)
            .map_err(factory_code)?;
        let id = call
            .context
            .entities
            .record(call.entity)
            .and_then(|r| r.object)
            .and_then(Handle::obstacle)
            .ok_or(-1)?;
        let mut pose = self.scene.world.colliders[id.0].pose;
        let record = self.record.ok_or(-1)?;
        let value = actor_startup::construct(
            record,
            &mut pose,
            &mut actor_startup::Resources {
                source: self.resources.source,
                models: self.resources.models,
                terrain: self.resources.terrain,
                random: self.resources.random,
                camera: self.resources.catalog.camera,
            },
            self.actors.as_mut().ok_or(-5)?,
        )
        .map_err(|e| scene_startup::Error::Actor(e.to_string()));
        let value = self.resource(value)?;
        if let Actor::Background {
            target: Some(name), ..
        } = &value
        {
            call.context.symbols.intern(name).map_err(|_| -1)?;
        }
        self.scene.world.colliders[id.0].pose = pose;
        self.scene.world.colliders[id.0].external_basis = Some(pose.matrix);
        if let Actor::Powerup(pickup) = &value {
            let entered = pickup.phase != 2 && pickup.phase != 3;
            {
                let data = &mut self.scene.components.object_mut(id)?.data;
                data.visibility.add_lap = pickup.added_lap;
                data.visibility.remove_lap = pickup.phase as i16;
                data.visibility.distance = 150;
            }
            call.context
                .dispatch(
                    call.entity,
                    if entered { ENTER } else { LEAVE },
                    None,
                    env,
                    self,
                )
                .map_err(factory_code)?;
            self.scene
                .components
                .object_mut(id)?
                .data
                .visibility
                .entered = entered;
            self.scene.components.lap_objects.push(id);
        }
        if matches!(value, Actor::Smoker { .. } | Actor::Lamps(..)) {
            self.scene.components.special.push(id);
        }
        if let Actor::Train(train) = &value
            && matches!(train.motion, actor_startup::train::Motion::Lander)
            && !self.scene.components.special.contains(&id)
        {
            self.scene.components.special.push(id);
        }
        if kind == Kind::Trackcam {
            self.actors.as_mut().ok_or(-5)?.track_cameras.insert(0, id);
            call.context
                .queues
                .queue(call.context.ticks, call.entity, false, env.work_limit)
                .map_err(|_| -3)?;
        }
        self.actors.as_mut().ok_or(-5)?.entries.push((id, value));
        *call.result = 0;
        Ok(())
    }
}
