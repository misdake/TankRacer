//! Collision callbacks borrow the very components and factory used by updates.
//! Traversal owns the world/vehicle/globals borrow; no flags, queues or RNG mirror.
use super::{Components, View};
use crate::{
    breakable::{self, Environment as HitEnvironment, Metadata, Motion, Owner},
    dispatch::MethodId,
    entity_factory::{self as factory, Lifecycle, Methods, Payload},
    object_query::ObstacleId,
    obstacle_collision::{Invocation, World},
    scene_collision::DEFORMABLE_HIT,
    symbols::Binding,
};
use std::sync::Arc;

pub struct Collision<'a> {
    pub components: &'a mut Components,
    pub lifecycle: Lifecycle<'a, ObstacleId>,
    pub environment: factory::Environment<'a>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Sound {
    Obstacle,
    Deformable,
}
/// Device operations remain explicit. Override `method` for additional game
/// bindings, delegating built-ins to `scene_method`; missing methods fail.
pub trait Services: Sized {
    fn method(
        &mut self,
        method: MethodId,
        scene: &mut Collision<'_>,
        call: Invocation<'_>,
        has_payload: bool,
    ) -> Result<(), i32> {
        scene.scene_method(method, call, has_payload)
    }
    fn sound(
        &mut self,
        kind: Sound,
        scene: &mut Collision<'_>,
        call: Invocation<'_>,
    ) -> Result<(), i32>;
    fn feedback(
        &mut self,
        intensity: i32,
        scene: &mut Collision<'_>,
        call: Invocation<'_>,
    ) -> Result<(), i32>;
}
impl Collision<'_> {
    pub fn method(
        &mut self,
        method: MethodId,
        call: Invocation<'_>,
        hit: HitEnvironment,
        services: &mut impl Services,
    ) -> Result<(), i32> {
        self.invoke(method, call, hit, services, true)
    }
    fn invoke(
        &mut self,
        method: MethodId,
        mut call: Invocation<'_>,
        hit: HitEnvironment,
        services: &mut impl Services,
        has_payload: bool,
    ) -> Result<(), i32> {
        match method {
            breakable::BREAKABLE_HIT => breakable::hit(
                &mut Adapter {
                    scene: self,
                    services,
                },
                call,
                hit,
            ),
            DEFORMABLE_HIT => {
                // The method result/RNG/timer commit precedes the host sound.
                let sound = self.components.surfaces.collide(reborrow(&mut call))?;
                if sound {
                    services.sound(Sound::Deformable, self, call)?;
                }
                Ok(())
            }
            _ => services.method(method, self, call, has_payload),
        }
    }
    pub fn scene_method(
        &mut self,
        method: MethodId,
        call: Invocation<'_>,
        has_payload: bool,
    ) -> Result<(), i32> {
        let entity = call.world.colliders.get(call.target.0).ok_or(-1)?.entity;
        let payload = has_payload.then_some(Payload::Entity(call.vehicle.entity));
        self.components.dt = call.dt;
        View {
            world: call.world,
            components: self.components,
        }
        .invoke(
            method,
            factory::Invocation {
                context: self.lifecycle.context(call.current_slot),
                entity,
                payload,
                result: call.result,
            },
            self.environment,
        )
    }
}
fn reborrow<'a>(call: &'a mut Invocation<'_>) -> Invocation<'a> {
    Invocation {
        dt: call.dt,
        target: call.target,
        vehicle: call.vehicle,
        world: call.world,
        random: call.random,
        current_slot: call.current_slot,
        result: call.result,
    }
}
struct Adapter<'a, 'scene, S> {
    scene: &'a mut Collision<'scene>,
    services: &'a mut S,
}
impl<S: Services> Owner for Adapter<'_, '_, S> {
    fn motion(&mut self, id: ObstacleId) -> Result<Motion<'_>, i32> {
        let state = self
            .scene
            .components
            .falls
            .get_mut(id.0)
            .and_then(Option::as_mut)
            .ok_or(-1)?;
        Ok(Motion {
            angular_velocity: &mut state.angular_velocity,
            rotation: &mut state.rotation,
            turn_rate: &mut state.turn_rate,
            last_vehicle: &mut state.last_vehicle,
        })
    }
    fn metadata(&self, id: ObstacleId) -> Result<Metadata, i32> {
        let object = self.scene.components.object(id)?;
        Ok(Metadata {
            has_sound: object.impact_sound.is_some(),
            auxiliary: object.data.auxiliary.is_some(),
            effect_flags: object.data.debris.as_ref().map(|d| d.flags as u8),
            cell: object.data.cell,
        })
    }
    fn attached(&self, id: ObstacleId) -> Result<Option<Arc<[Binding]>>, i32> {
        Ok(self
            .scene
            .components
            .falls
            .get(id.0)
            .and_then(Option::as_ref)
            .ok_or(-1)?
            .attached
            .clone())
    }
    fn resolve(&self, binding: Binding, _: &World) -> Option<ObstacleId> {
        let entity = binding.resolve(self.scene.lifecycle.symbols)?;
        self.scene.lifecycle.entities.record(entity)?.object
    }
    fn method(
        &mut self,
        method: MethodId,
        call: Invocation<'_>,
        env: HitEnvironment,
        has_payload: bool,
    ) -> Result<(), i32> {
        self.scene
            .invoke(method, call, env, self.services, has_payload)
    }
    fn sound(&mut self, call: Invocation<'_>) -> Result<(), i32> {
        self.services.sound(Sound::Obstacle, self.scene, call)
    }
    fn feedback(&mut self, intensity: i32, call: Invocation<'_>) -> Result<(), i32> {
        self.services.feedback(intensity, self.scene, call)
    }
}
