//! Real generic-obstacle hit (40A3E0), with live activation and grid membership.
use crate::{
    dispatch::{self, MethodId},
    fixed::trunc_shift,
    geometry::approximate_distance,
    lifecycle::Ticks,
    object_query::{EntityId, ObstacleId},
    obstacle_collision::{Invocation, World},
    spatial::{self, Cell},
    symbols::{Binding, Symbols},
};
use std::sync::Arc;

pub const BREAKABLE_HIT: MethodId = MethodId(1);
pub const ENABLE_TICK: MethodId = MethodId(2);
#[derive(Debug, Clone)]
pub struct State {
    pub angular_velocity: i32,
    pub rotation: i32,
    pub turn_rate: i32,
    pub last_vehicle: Option<EntityId>,
    pub has_sound: bool,
    pub auxiliary: bool,
    pub effect_flags: Option<u8>,
    pub cell: Option<Cell>,
    /// Bindings retain original order; resolve names immediately before each
    /// dispatch so an earlier nested callback can change a later target.
    pub attached: Arc<[Binding]>,
}
pub struct Store {
    pub states: Vec<Option<State>>,
    pub live: Vec<bool>,
    pub ticks: Ticks,
    pub symbols: Symbols,
}
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
/// Auxiliary/effect methods remain explicit gameplay bindings. Sound and force
/// feedback are host services. Callbacks observe and can change the same world.
pub trait Services {
    fn method(
        &mut self,
        method: MethodId,
        store: &mut Store,
        call: Invocation<'_>,
        has_payload: bool,
    ) -> Result<(), i32>;
    fn sound(&mut self, store: &mut Store, call: Invocation<'_>) -> Result<(), i32>;
    fn feedback(
        &mut self,
        intensity: i32,
        store: &mut Store,
        call: Invocation<'_>,
    ) -> Result<(), i32>;
}
fn state(store: &Store, id: ObstacleId) -> Result<&State, i32> {
    store.states.get(id.0).and_then(Option::as_ref).ok_or(-1)
}
fn state_mut(store: &mut Store, id: ObstacleId) -> Result<&mut State, i32> {
    store
        .states
        .get_mut(id.0)
        .and_then(Option::as_mut)
        .ok_or(-1)
}
fn entity(world: &World, store: &Store, id: EntityId) -> Option<ObstacleId> {
    world
        .colliders
        .iter()
        .enumerate()
        .find(|(i, c)| c.entity == id && store.live.get(*i) == Some(&true))
        .map(|(i, _)| ObstacleId(i))
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
    store: &mut Store,
    call: &mut Invocation<'_>,
    target: ObstacleId,
    op: Operation,
    env: Environment,
    services: &mut impl Services,
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
            BREAKABLE_HIT => collide(store, nested, env, services),
            ENABLE_TICK => {
                if env.work_limit == 0 {
                    return Err(-1);
                }
                *nested.result = u32::from(store.ticks.enable(target).map_err(|_| -1)?);
                Ok(())
            }
            method => services.method(method, store, nested, op.has_payload),
        }
    };
    if op.entity_dispatch {
        dispatch::call_entity(&table, op.slot, current_slot, &mut ignored, run)
    } else {
        dispatch::call(&table, op.slot, current_slot, &mut ignored, run)
    }
    .map_err(dispatch_error)
}
fn inner(
    store: &mut Store,
    call: &mut Invocation<'_>,
    env: Environment,
    services: &mut impl Services,
) -> Result<(), i32> {
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
    state_mut(store, id)?.angular_velocity = rate;
    if call.vehicle.forward_velocity < 0 {
        state_mut(store, id)?.angular_velocity = rate.wrapping_neg();
        call.world.objects[id.0].flags |= 8;
    }
    if call.world.objects[id.0].activity == 0 {
        if state(store, id)?.has_sound {
            let mut result = 0;
            services
                .sound(store, child(call, id, &mut result))
                .map_err(scene_error)?;
        }
        call.world.objects[id.0].activity = 1;
        invoke(
            store,
            call,
            id,
            Operation {
                slot: env.activate_slot,
                entity_dispatch: true,
                has_payload: false,
            },
            env,
            services,
        )
        .map_err(scene_error)?;
    }
    let flags = call.world.objects[id.0].flags | 2;
    call.world.objects[id.0].flags = flags;
    {
        let s = state_mut(store, id)?;
        s.rotation = 0;
        s.last_vehicle = Some(call.vehicle.entity);
    }
    let rate = trunc_shift(call.vehicle.forward_velocity.wrapping_abs(), 3);
    state_mut(store, id)?.turn_rate = if rate < env.minimum_turn_rate {
        env.minimum_turn_rate
    } else if rate > env.maximum_turn_rate {
        env.maximum_turn_rate
    } else {
        rate
    };
    call.world.objects[id.0].flags = flags & !16;
    if state(store, id)?.auxiliary {
        invoke(
            store,
            call,
            id,
            Operation {
                slot: env.auxiliary_slot,
                entity_dispatch: false,
                has_payload: true,
            },
            env,
            services,
        )?;
        call.world.objects[id.0].activity = 0;
    }
    if state(store, id)?.effect_flags.is_some() {
        invoke(
            store,
            call,
            id,
            Operation {
                slot: env.effect_slot,
                entity_dispatch: false,
                has_payload: true,
            },
            env,
            services,
        )?;
        if state(store, id)?.effect_flags.unwrap_or(0) & 16 == 0 {
            call.world.objects[id.0].activity = 0;
        }
        spatial::remove(
            &mut call.world.grid,
            id,
            state(store, id)?.cell,
            env.work_limit,
        )
        .map_err(|e| match e {
            spatial::Error::Limit => -3,
            spatial::Error::InvalidLimit => -1,
        })?;
    }
    let attached = Arc::clone(&state(store, id)?.attached);
    for (i, target) in attached.iter().enumerate() {
        if i == 65536 {
            return Err(-3);
        }
        if let Some(target) = target
            .resolve(&store.symbols)
            .and_then(|target| entity(call.world, store, target))
        {
            invoke(
                store,
                call,
                target,
                Operation {
                    slot: env.collision_slot,
                    entity_dispatch: false,
                    has_payload: true,
                },
                env,
                services,
            )?;
        }
    }
    if call.vehicle.player && env.feedback {
        let radius = call.world.colliders.get(id.0).ok_or(-1)?.radius;
        let mut result = 0;
        services
            .feedback(i32::from(radius), store, child(call, id, &mut result))
            .map_err(scene_error)?;
    }
    Ok(())
}
/// This method's result is zero even on failure, unlike the deformable method.
pub fn collide(
    store: &mut Store,
    mut call: Invocation<'_>,
    env: Environment,
    services: &mut impl Services,
) -> Result<(), i32> {
    let result = inner(store, &mut call, env, services);
    *call.result = 0;
    result
}
