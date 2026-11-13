//! Synchronous class initialization/destruction and authored name definitions.
//! Component allocation is a class method, not an implicit default object.
use crate::{
    dispatch::{self, Class, ClassId, MethodId},
    entities::{self, Entities},
    lifecycle::{self, Event, Queues, Ticks},
    object_query::EntityId,
    symbols::{self, SymbolId, Symbols},
};
use std::sync::Arc;

#[derive(Debug, Clone, Copy)]
pub struct Slots {
    pub allocate: u32,
    pub initialize: u32,
    pub destroy: u32,
    pub release: u32,
}
#[derive(Clone, Copy)]
pub struct Environment<'a> {
    pub classes: &'a [Arc<Class>],
    pub slots: Slots,
    pub work_limit: u32,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    Entities(entities::Error),
    Symbols(symbols::Error),
    Dispatch(dispatch::Error),
    MissingClass(ClassId),
    Uninitialized(EntityId),
    InvalidReservation(EntityId),
    Lifecycle(lifecycle::Error<EntityId>),
}
/// The shared identity/scheduling state. Class-owned component stores live in
/// Methods, and borrow this same context for every synchronous nested call.
pub struct State<T> {
    pub entities: Entities<T>,
    pub symbols: Symbols,
    pub ticks: Ticks<EntityId>,
    pub queues: Queues<EntityId>,
    pub current_slot: u32,
}
impl<T: Copy> State<T> {
    pub fn new(capacity: u32) -> Result<Self, Error> {
        Ok(Self {
            entities: Entities::new(capacity).map_err(Error::Entities)?,
            symbols: Symbols::default(),
            ticks: Ticks {
                flags: vec![0; capacity as usize],
                ..Ticks::default()
            },
            queues: Queues::default(),
            current_slot: 0,
        })
    }
    pub fn context(&mut self) -> Context<'_, T> {
        Context {
            entities: &mut self.entities,
            symbols: &mut self.symbols,
            ticks: &mut self.ticks,
            queues: &mut self.queues,
            current_slot: &mut self.current_slot,
        }
    }
}
/// Borrow the same tables and current slot for nested method calls. No copied
/// dispatch context or deferred recursion changes constructor/update ordering.
pub struct Context<'a, T> {
    pub entities: &'a mut Entities<T>,
    pub symbols: &'a mut Symbols,
    pub current_slot: &'a mut u32,
    pub ticks: &'a mut Ticks<EntityId>,
    pub queues: &'a mut Queues<EntityId>,
}
pub struct Invocation<'a, T> {
    pub context: Context<'a, T>,
    pub entity: EntityId,
    pub payload: Option<Payload>,
    pub result: &'a mut u32,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Payload {
    Value(u32),
    Entity(EntityId),
    Position([i32; 3]),
}
pub trait Methods<T> {
    fn invoke(
        &mut self,
        method: MethodId,
        call: Invocation<'_, T>,
        env: Environment<'_>,
    ) -> Result<(), i32>;
}
/// Qualified common methods. The class registry maps its logical MethodIds to
/// these bindings; allocation/pose/subclass methods require real component data.
#[derive(Debug, Clone, Copy)]
pub enum BaseMethod {
    Identity,
    Zero,
    One,
    EnableTick,
    DisableTick,
    QueueDestroy,
    ReleaseId,
    Detach,
}
impl BaseMethod {
    pub fn invoke<T: Copy>(self, call: Invocation<'_, T>, env: Environment<'_>) -> Result<(), i32> {
        let id = call.entity;
        let result = match self {
            Self::Identity => id.0,
            Self::Zero => 0,
            Self::One => 1,
            Self::EnableTick => {
                if env.work_limit == 0 {
                    return Err(-1);
                }
                u32::from(call.context.ticks.enable(id).map_err(lifecycle_code)?)
            }
            Self::DisableTick => u32::from(
                call.context
                    .ticks
                    .disable(id, env.work_limit)
                    .map_err(lifecycle_code)?,
            ),
            Self::QueueDestroy => {
                call.context
                    .queues
                    .queue(call.context.ticks, id, true, env.work_limit)
                    .map_err(lifecycle_code)?;
                id.0
            }
            Self::ReleaseId => {
                call.context.entities.release_id(id).map_err(entity_code)?;
                1
            }
            Self::Detach => {
                call.context.entities.detach(id).map_err(entity_code)?;
                1
            }
        };
        *call.result = result;
        Ok(())
    }
}
fn entity_code(error: entities::Error) -> i32 {
    match error {
        entities::Error::Limit => -3,
        entities::Error::Capacity => -4,
        _ => -1,
    }
}
fn lifecycle_code(error: lifecycle::Error<EntityId>) -> i32 {
    match error {
        lifecycle::Error::Spatial(crate::spatial::Error::Limit) => -3,
        lifecycle::Error::Method(code) => code,
        _ => -1,
    }
}
#[derive(Debug, Clone, Copy)]
pub struct Definition {
    pub entity: EntityId,
    pub symbol: SymbolId,
}
impl<T: Copy> Context<'_, T> {
    pub fn dispatch(
        &mut self,
        entity: EntityId,
        slot: u32,
        payload: Option<Payload>,
        env: Environment<'_>,
        methods: &mut impl Methods<T>,
    ) -> Result<u32, Error> {
        let class = self
            .entities
            .record(entity)
            .and_then(|v| v.class)
            .ok_or(Error::Uninitialized(entity))?;
        let class = Arc::clone(env.classes.get(class.0).ok_or(Error::MissingClass(class))?);
        let mut result = 0;
        dispatch::call_entity(
            &class,
            slot,
            self.current_slot,
            &mut result,
            |method, current_slot, result| {
                methods.invoke(
                    method,
                    Invocation {
                        context: Context {
                            entities: self.entities,
                            symbols: self.symbols,
                            current_slot,
                            ticks: self.ticks,
                            queues: self.queues,
                        },
                        entity,
                        payload,
                        result,
                    },
                    env,
                )
            },
        )
        .map_err(Error::Dispatch)?;
        Ok(result)
    }
    pub fn class_dispatch(
        &mut self,
        class: ClassId,
        entity: EntityId,
        payload: Option<Payload>,
        env: Environment<'_>,
        methods: &mut impl Methods<T>,
    ) -> Result<u32, Error> {
        let class = Arc::clone(env.classes.get(class.0).ok_or(Error::MissingClass(class))?);
        let mut result = 0;
        dispatch::call_class(
            &class,
            self.current_slot,
            &mut result,
            |method, current_slot, result| {
                methods.invoke(
                    method,
                    Invocation {
                        context: Context {
                            entities: self.entities,
                            symbols: self.symbols,
                            ticks: self.ticks,
                            queues: self.queues,
                            current_slot,
                        },
                        entity,
                        payload,
                        result,
                    },
                    env,
                )
            },
        )
        .map_err(Error::Dispatch)?;
        Ok(result)
    }
    pub fn create(
        &mut self,
        class: ClassId,
        env: Environment<'_>,
        methods: &mut impl Methods<T>,
    ) -> Result<EntityId, Error> {
        let id = self
            .entities
            .create_record(env.work_limit)
            .map_err(Error::Entities)?;
        self.entities
            .record_mut(id)
            .ok_or(Error::Uninitialized(id))?
            .class = Some(class);
        self.initialize(id, env, methods)?;
        Ok(id)
    }
    pub fn fill_reserved(
        &mut self,
        id: EntityId,
        class: ClassId,
        env: Environment<'_>,
        methods: &mut impl Methods<T>,
    ) -> Result<(), Error> {
        let record = self
            .entities
            .record_mut(id)
            .filter(|v| id.0 != 0 && v.class.is_none() && v.object.is_none())
            .ok_or(Error::InvalidReservation(id))?;
        // A failed first/second method retains this class and prior writes.
        record.class = Some(class);
        self.initialize(id, env, methods)
    }
    fn initialize(
        &mut self,
        id: EntityId,
        env: Environment<'_>,
        methods: &mut impl Methods<T>,
    ) -> Result<(), Error> {
        self.dispatch(id, env.slots.allocate, None, env, methods)?;
        self.dispatch(id, env.slots.initialize, None, env, methods)?;
        Ok(())
    }
    pub fn destroy(
        &mut self,
        id: EntityId,
        env: Environment<'_>,
        methods: &mut impl Methods<T>,
    ) -> Result<(), Error> {
        if self.entities.record(id).is_none() {
            return Ok(());
        }
        self.dispatch(id, env.slots.destroy, None, env, methods)?;
        self.dispatch(id, env.slots.release, None, env, methods)?;
        self.entities.finish_destroy();
        Ok(())
    }
    pub fn tick(
        &mut self,
        slot: u32,
        env: Environment<'_>,
        methods: &mut impl Methods<T>,
    ) -> Result<(), Error> {
        let Self {
            entities,
            symbols,
            current_slot,
            ticks,
            queues,
        } = self;
        let mut method_error = None;
        let status = ticks.run(env.work_limit, |ticks, id| {
            let mut context = Context {
                entities,
                symbols,
                current_slot,
                ticks,
                queues,
            };
            context
                .dispatch(id, slot, None, env, methods)
                .map(|_| ())
                .map_err(|error| {
                    method_error = Some(error);
                    -1
                })
        });
        if let Some(error) = method_error {
            return Err(error);
        }
        status.map_err(Error::Lifecycle)
    }
    pub fn drain(
        &mut self,
        stop_slot: u32,
        env: Environment<'_>,
        methods: &mut impl Methods<T>,
    ) -> Result<(), Error> {
        let Self {
            entities,
            symbols,
            current_slot,
            ticks,
            queues,
        } = self;
        let mut method_error = None;
        let status = queues.drain(ticks, env.work_limit, |queues, ticks, event| {
            let mut context = Context {
                entities,
                symbols,
                current_slot,
                ticks,
                queues,
            };
            let result = match event {
                Event::Stop(id) => context
                    .dispatch(id, stop_slot, None, env, methods)
                    .map(|_| ()),
                Event::Destroy(id) => context.destroy(id, env, methods),
            };
            result.map_err(|error| {
                method_error = Some(error);
                -1
            })
        });
        if let Some(error) = method_error {
            return Err(error);
        }
        status.map_err(Error::Lifecycle)
    }
    /// World loading publishes a name only after allocation and initialization
    /// succeed, before its authored reader runs. Interning alone allocates no ID.
    /// A defined name must refer to a reserved record; ordinary duplicates fail.
    pub fn define(
        &mut self,
        name: &str,
        class: ClassId,
        env: Environment<'_>,
        methods: &mut impl Methods<T>,
    ) -> Result<Definition, Error> {
        let symbol = self.symbols.intern(name).map_err(Error::Symbols)?;
        let entity = if let Some(id) = self.symbols.entity(symbol) {
            self.fill_reserved(id, class, env, methods)?;
            id
        } else {
            self.create(class, env, methods)?
        };
        self.symbols
            .bind_entity(symbol, entity)
            .map_err(Error::Symbols)?;
        Ok(Definition { entity, symbol })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::symbols::Binding;

    #[derive(Default)]
    struct Components {
        next: u32,
        fail: Option<u16>,
    }
    impl Methods<u32> for Components {
        fn invoke(
            &mut self,
            method: MethodId,
            call: Invocation<'_, u32>,
            env: Environment<'_>,
        ) -> Result<(), i32> {
            if method.0 == 0 {
                self.next += 1;
                call.context
                    .entities
                    .record_mut(call.entity)
                    .unwrap()
                    .object = Some(self.next);
                call.context
                    .ticks
                    .flags
                    .resize(call.context.entities.capacity() as usize, 0);
                call.context.ticks.flags[call.entity.0 as usize] = 0;
            }
            if self.fail == Some(method.0) {
                return Err(-13);
            }
            if method.0 == 3 {
                return BaseMethod::ReleaseId.invoke(call, env);
            }
            *call.result = call.entity.0;
            Ok(())
        }
    }
    fn classes() -> Vec<Arc<Class>> {
        vec![Arc::new(Class {
            maximum_slot: 3,
            methods: (0..4).map(|v| Some(MethodId(v))).collect(),
        })]
    }
    fn environment(classes: &[Arc<Class>]) -> Environment<'_> {
        Environment {
            classes,
            slots: Slots {
                allocate: 0,
                initialize: 1,
                destroy: 2,
                release: 3,
            },
            work_limit: 10000,
        }
    }
    #[test]
    fn authored_names_fill_reservations_reject_duplicates_and_observe_id_reuse() {
        let registry = classes();
        let env = environment(&registry);
        let mut state = State::new(8).unwrap();
        let mut methods = Components::default();
        let name = state.symbols.intern("RoadSign").unwrap();
        let binding = Binding::Named(name);
        assert_eq!(state.entities.record_count(), 0);
        let defined = state
            .context()
            .define("roadsign", ClassId(0), env, &mut methods)
            .unwrap();
        assert_eq!(defined.symbol, name);
        assert_eq!(binding.resolve(&state.symbols), Some(defined.entity));
        assert!(matches!(
            state
                .context()
                .define("ROADSIGN", ClassId(0), env, &mut methods),
            Err(Error::InvalidReservation(_))
        ));
        assert_eq!(state.entities.record_count(), 1);
        let reserved = state.entities.new_record(EntityId(5), 10000).unwrap();
        let reserved_name = state.symbols.intern("reserved").unwrap();
        state.symbols.bind_entity(reserved_name, reserved).unwrap();
        assert_eq!(
            state
                .context()
                .define("reserved", ClassId(0), env, &mut methods)
                .unwrap()
                .entity,
            reserved
        );
        assert_eq!(state.entities.record_count(), 2);
        let first_nil = state
            .context()
            .define("nil", ClassId(0), env, &mut methods)
            .unwrap();
        let second_nil = state
            .context()
            .define("NIL", ClassId(0), env, &mut methods)
            .unwrap();
        assert_ne!(first_nil.entity, second_nil.entity);
        assert_eq!(state.symbols.entity(first_nil.symbol), None);
        state
            .context()
            .destroy(defined.entity, env, &mut methods)
            .unwrap();
        let reused = state
            .context()
            .create(ClassId(0), env, &mut methods)
            .unwrap();
        assert_eq!(reused, defined.entity);
        assert_eq!(binding.resolve(&state.symbols), Some(reused));
    }
    #[test]
    fn failed_construction_keeps_record_and_sequence_but_does_not_publish_name() {
        let registry = classes();
        let env = environment(&registry);
        let mut state = State::new(4).unwrap();
        state.entities.sequential = true;
        state.entities.next_requested = EntityId(1);
        let mut methods = Components {
            next: 0,
            fail: Some(1),
        };
        assert_eq!(
            state
                .context()
                .define("Sign", ClassId(0), env, &mut methods)
                .unwrap_err(),
            Error::Dispatch(dispatch::Error::Method(-13))
        );
        let name = state.symbols.intern("Sign").unwrap();
        assert_eq!(state.symbols.entity(name), None);
        assert_eq!(state.entities.record_count(), 1);
        assert_eq!(state.entities.next_requested, EntityId(2));
        assert_eq!(state.entities.record(EntityId(1)).unwrap().object, Some(1));
        assert_eq!(state.current_slot, 1);
        methods.fail = None;
        let defined = state
            .context()
            .define("Sign", ClassId(0), env, &mut methods)
            .unwrap();
        assert_eq!(defined.entity, EntityId(2));
        methods.fail = Some(3);
        assert!(
            state
                .context()
                .destroy(defined.entity, env, &mut methods)
                .is_err()
        );
        assert_eq!(state.entities.record_count(), 2);
        assert!(state.entities.record(defined.entity).is_some());
        assert_eq!(state.current_slot, 3);
        methods.fail = None;
        state
            .context()
            .destroy(defined.entity, env, &mut methods)
            .unwrap();
        assert_eq!(state.entities.record_count(), 1);
        assert!(state.entities.record(defined.entity).is_none());
    }
}
