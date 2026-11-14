//! Collision callbacks borrow the very components and factory used by updates.
//! Traversal owns the world/vehicle/globals borrow; no flags, queues or RNG mirror.
use super::{Components, Execution, View};
use crate::scene_entity::Handle;
use crate::{
    breakable::{self, Environment as HitEnvironment, Metadata, Motion, Owner},
    dispatch::MethodId,
    entity_factory::{self as factory, Lifecycle, Methods, Payload},
    object_query::ObstacleId,
    obstacle_collision::{Invocation, World},
    scene_collision::DEFORMABLE_HIT,
    shatter::{self, BlastTarget},
    symbols::Binding,
};
use std::sync::Arc;

pub struct Collision<'a> {
    pub components: &'a mut Components,
    pub lifecycle: Lifecycle<'a, Handle>,
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
    /// Resource resolution may involve I/O; private copying and fragment state
    /// creation are ordinary Rust operations on the scene's shared mesh store.
    fn auxiliary_model(
        &mut self,
        _name: &str,
    ) -> Result<Option<Arc<tankracer_assets::model::Model>>, i32> {
        Err(-5)
    }
    fn auxiliary_height(
        &mut self,
        scene: &mut Collision<'_>,
        call: Invocation<'_>,
    ) -> Result<i32, i32> {
        let position = call
            .world
            .colliders
            .get(call.target.0)
            .ok_or(-1)?
            .pose
            .position;
        let mut height = crate::terrain::MISS_HEIGHT;
        crate::terrain::height(
            scene.components.terrain.as_deref().ok_or(-1)?,
            position[0],
            position[2],
            &mut height,
        )
        .map_err(|e| match e {
            crate::terrain::Error::Arithmetic => -2,
            _ => -1,
        })?;
        Ok(height)
    }
    fn paired_vertex_tail(&mut self) -> Option<u16> {
        None
    }
    /// Vehicle slots are read separately because an earlier immediate impulse
    /// may change a later slot or target. These game bindings must be supplied.
    fn blast_target(
        &mut self,
        _slot: usize,
        _scene: &mut Collision<'_>,
        _call: Invocation<'_>,
    ) -> Result<Option<BlastTarget>, i32> {
        Err(-5)
    }
    fn blast_impulse(
        &mut self,
        _target: crate::object_query::EntityId,
        _scene: &mut Collision<'_>,
        _call: Invocation<'_>,
    ) -> Result<(), i32> {
        Err(-5)
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
            super::AUXILIARY_HIT => self.activate_mesh_fragments(call, services),
            super::DEBRIS_HIT => self.activate_debris(call, services),
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
    fn activate_debris(
        &mut self,
        mut call: Invocation<'_>,
        services: &mut impl Services,
    ) -> Result<(), i32> {
        let id = call.target;
        let entity = call.world.colliders.get(id.0).ok_or(-1)?.entity;
        let limit = self.environment.work_limit;
        let object = self.components.object_mut(id)?;
        let debris = object.data.debris.as_mut().ok_or(-1)?;
        let Some(activation) = shatter::begin(debris, call.vehicle.velocity, limit)? else {
            *call.result = entity.0;
            return Ok(());
        };
        if debris.flags & 4 != 0 {
            for slot in 0..9 {
                if let Some(target) = services.blast_target(slot, self, reborrow(&mut call))?
                    && target.affected(call.world.colliders[id.0].pose.position)
                {
                    services.blast_impulse(target.entity, self, reborrow(&mut call))?;
                }
            }
        }
        let position = call.world.colliders[id.0].pose.position;
        shatter::activate(
            self.components
                .object_mut(id)?
                .data
                .debris
                .as_mut()
                .ok_or(-1)?,
            position,
            activation,
            call.random,
            limit,
        )?;
        crate::spatial::remove_first(&mut self.components.ordinary, &id, limit)
            .map_err(super::spatial_code)?;
        if !crate::spatial::contains(&self.components.special, &id, limit)
            .map_err(super::spatial_code)?
        {
            self.components.special.push(id);
        }
        *call.result = entity.0;
        Ok(())
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
            vehicles: None,
            execution: Execution::Simulation {
                random: call.random,
            },
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
pub(super) fn reborrow<'a>(call: &'a mut Invocation<'_>) -> Invocation<'a> {
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
        self.scene
            .lifecycle
            .entities
            .record(entity)?
            .object?
            .obstacle()
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
