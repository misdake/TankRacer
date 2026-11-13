//! Real generic-obstacle hit (40A3E0), with live activation and grid membership.
use crate::{
    dispatch::{self, MethodId},
    fixed::trunc_shift,
    geometry::approximate_distance,
    object_query::{EntityId, ObstacleId},
    obstacle_collision::{Invocation, World},
    spatial::{self, Cell},
    symbols::Binding,
};
use std::sync::Arc;
mod legacy;
pub use legacy::{Services, State, Store, collide};

pub const BREAKABLE_HIT: MethodId = MethodId(1);
pub const ENABLE_TICK: MethodId = MethodId(2);
#[derive(Debug, Clone, Copy)]
pub struct Environment {
    pub minimum_turn_rate: i32,
    pub maximum_turn_rate: i32,
    pub activate_slot: u32,
    pub auxiliary_slot: u32,
    pub effect_slot: u32,
    pub collision_slot: u32,
    pub feedback: bool,
    pub work_limit: u32,
}
/// Direct mutable fields, so collision activation and later ticks share motion.
pub struct Motion<'a> {
    pub angular_velocity: &'a mut i32,
    pub rotation: &'a mut i32,
    pub turn_rate: &'a mut i32,
    pub last_vehicle: &'a mut Option<EntityId>,
}
#[derive(Clone, Copy)]
pub struct Metadata {
    pub has_sound: bool,
    pub auxiliary: bool,
    pub effect_flags: Option<u8>,
    pub cell: Option<Cell>,
}
/// Owners supply authoritative components and immediate nested methods. Metadata
/// is read again after callbacks; names resolve immediately before dispatch.
pub trait Owner {
    fn motion(&mut self, id: ObstacleId) -> Result<Motion<'_>, i32>;
    fn metadata(&self, id: ObstacleId) -> Result<Metadata, i32>;
    fn attached(&self, id: ObstacleId) -> Result<Option<Arc<[Binding]>>, i32>;
    fn resolve(&self, binding: Binding, world: &World) -> Option<ObstacleId>;
    fn method(
        &mut self,
        method: MethodId,
        call: Invocation<'_>,
        env: Environment,
        has_payload: bool,
    ) -> Result<(), i32>;
    fn sound(&mut self, call: Invocation<'_>) -> Result<(), i32>;
    fn feedback(&mut self, intensity: i32, call: Invocation<'_>) -> Result<(), i32>;
}
fn child<'a>(
    call: &'a mut Invocation<'_>,
    target: ObstacleId,
    result: &'a mut u32,
) -> Invocation<'a> {
    Invocation {
        dt: call.dt,
        target,
        vehicle: &mut *call.vehicle,
        world: &mut *call.world,
        random: &mut *call.random,
        current_slot: &mut *call.current_slot,
        result,
    }
}
fn scene_error(code: i32) -> i32 {
    if code < 0 { code } else { -8 }
}
fn dispatch_error(error: dispatch::Error) -> i32 {
    match error {
        dispatch::Error::Method(c) => c,
        dispatch::Error::MissingSlot(_) => -1,
    }
}
#[derive(Clone, Copy)]
struct Operation {
    slot: u32,
    entity_dispatch: bool,
    has_payload: bool,
}
fn invoke(
    owner: &mut impl Owner,
    call: &mut Invocation<'_>,
    target: ObstacleId,
    op: Operation,
    env: Environment,
) -> Result<(), i32> {
    let class = call.world.objects.get(target.0).ok_or(-1)?.class;
    let table = Arc::clone(call.world.classes.get(class.0).ok_or(-1)?);
    let Invocation {
        dt,
        vehicle,
        world,
        random,
        current_slot,
        ..
    } = call;
    let mut ignored = 0;
    let run = |method, slot: &mut u32, result: &mut u32| {
        let nested = Invocation {
            dt: *dt,
            target,
            vehicle,
            world,
            random,
            current_slot: slot,
            result,
        };
        match method {
            BREAKABLE_HIT => hit(owner, nested, env),
            method => owner.method(method, nested, env, op.has_payload),
        }
    };
    if op.entity_dispatch {
        dispatch::call_entity(&table, op.slot, current_slot, &mut ignored, run)
    } else {
        dispatch::call(&table, op.slot, current_slot, &mut ignored, run)
    }
    .map_err(dispatch_error)
}
fn inner(owner: &mut impl Owner, call: &mut Invocation<'_>, env: Environment) -> Result<(), i32> {
    let id = call.target;
    let properties = call.world.objects.get(id.0).ok_or(-1)?;
    if properties.activity != 0 || properties.flags & 1 != 0 {
        return Ok(());
    }
    call.world.objects[id.0].flags = (properties.flags & !12) | 1;
    let rate = trunc_shift(
        approximate_distance(call.vehicle.velocity).wrapping_neg(),
        3,
    );
    *owner.motion(id)?.angular_velocity = rate;
    if call.vehicle.forward_velocity < 0 {
        *owner.motion(id)?.angular_velocity = rate.wrapping_neg();
        call.world.objects[id.0].flags |= 8;
    }
    if call.world.objects[id.0].activity == 0 {
        if owner.metadata(id)?.has_sound {
            let mut result = 0;
            owner
                .sound(child(call, id, &mut result))
                .map_err(scene_error)?;
        }
        call.world.objects[id.0].activity = 1;
        invoke(
            owner,
            call,
            id,
            Operation {
                slot: env.activate_slot,
                entity_dispatch: true,
                has_payload: false,
            },
            env,
        )
        .map_err(scene_error)?;
    }
    let flags = call.world.objects[id.0].flags | 2;
    call.world.objects[id.0].flags = flags;
    {
        let s = owner.motion(id)?;
        *s.rotation = 0;
        *s.last_vehicle = Some(call.vehicle.entity);
    }
    let rate = trunc_shift(call.vehicle.forward_velocity.wrapping_abs(), 3);
    *owner.motion(id)?.turn_rate = if rate < env.minimum_turn_rate {
        env.minimum_turn_rate
    } else if rate > env.maximum_turn_rate {
        env.maximum_turn_rate
    } else {
        rate
    };
    call.world.objects[id.0].flags = flags & !16;
    if owner.metadata(id)?.auxiliary {
        invoke(
            owner,
            call,
            id,
            Operation {
                slot: env.auxiliary_slot,
                entity_dispatch: false,
                has_payload: true,
            },
            env,
        )?;
        call.world.objects[id.0].activity = 0;
    }
    if owner.metadata(id)?.effect_flags.is_some() {
        invoke(
            owner,
            call,
            id,
            Operation {
                slot: env.effect_slot,
                entity_dispatch: false,
                has_payload: true,
            },
            env,
        )?;
        if owner.metadata(id)?.effect_flags.unwrap_or(0) & 16 == 0 {
            call.world.objects[id.0].activity = 0;
        }
        spatial::remove(
            &mut call.world.grid,
            id,
            owner.metadata(id)?.cell,
            env.work_limit,
        )
        .map_err(|e| match e {
            spatial::Error::Limit => -3,
            spatial::Error::InvalidLimit => -1,
        })?;
    }
    if let Some(attached) = owner.attached(id)? {
        for (i, binding) in attached.iter().enumerate() {
            if i == 65536 {
                return Err(-3);
            }
            if let Some(target) = owner.resolve(*binding, call.world) {
                invoke(
                    owner,
                    call,
                    target,
                    Operation {
                        slot: env.collision_slot,
                        entity_dispatch: false,
                        has_payload: true,
                    },
                    env,
                )?;
            }
        }
    }
    if call.vehicle.player && env.feedback {
        let radius = call.world.colliders.get(id.0).ok_or(-1)?.radius;
        let mut result = 0;
        owner
            .feedback(i32::from(radius), child(call, id, &mut result))
            .map_err(scene_error)?;
    }
    Ok(())
}
/// This method's result is zero even on failure, unlike the deformable method.
pub fn hit(owner: &mut impl Owner, mut call: Invocation<'_>, env: Environment) -> Result<(), i32> {
    let result = inner(owner, &mut call, env);
    *call.result = 0;
    result
}
