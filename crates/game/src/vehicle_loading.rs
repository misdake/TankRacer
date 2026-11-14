//! Fresh single-player installation into the common entity factory and ticks.
//! Resource/model preparation precedes this boundary; no captured state is used.
use crate::{
    dispatch::{Class, MethodId},
    entity_factory::{self as factory, BaseMethod, Environment, Invocation, Methods},
    object_query::{EntityId, TankId},
    pose::Pose,
    scene_entity::Handle,
    scene_loading::{self, Registry},
    spatial,
    vehicle_model::VehicleModel,
    vehicle_startup::{self, Player},
};

const ALLOCATE: MethodId = MethodId(72);
const INITIALIZE: MethodId = MethodId(73);
pub const TICK: MethodId = MethodId(74);
const UNSUPPORTED: MethodId = MethodId(75);
pub const APPLY_BOOST: MethodId = MethodId(76);
pub const BOOST_SLOT: u32 = 28;

/// Creation, activation and stop bindings. The frame/remaining lifecycle calls
/// require their real runtime owner; unsupported methods fail.
pub(crate) fn class() -> Class {
    let mut methods = vec![Some(UNSUPPORTED); BOOST_SLOT as usize + 1];
    for (slot, method) in [
        (scene_loading::ALLOCATE, ALLOCATE),
        (scene_loading::INITIALIZE, INITIALIZE),
        (scene_loading::ACTIVATE, crate::breakable::ENABLE_TICK),
        (scene_loading::STOP, crate::scene_objects::DISABLE_TICK),
        (scene_loading::TICK, TICK),
        (BOOST_SLOT, APPLY_BOOST),
    ] {
        methods[slot as usize] = Some(method);
    }
    Class {
        maximum_slot: BOOST_SLOT as i32,
        methods,
    }
}
pub struct Request {
    pub model: VehicleModel,
    pub spawn: Pose,
    pub environment: vehicle_startup::Environment,
    pub static_allocation: bool,
}
#[derive(Default)]
pub struct Store {
    pub entries: Vec<Option<Player>>,
    /// Ordered actor drawing memberships, in the vehicle component domain.
    /// Creating the fallback player prepends it.
    pub visible: Vec<TankId>,
    pub hidden: Vec<TankId>,
}
#[derive(Debug)]
pub enum Error {
    Factory(factory::Error),
    Construct(vehicle_startup::Error),
    MissingVehicle,
    Membership(spatial::Error),
}
impl Store {
    pub fn create_single_player(
        &mut self,
        state: &mut factory::State<Handle>,
        registry: &Registry,
        request: Request,
        work_limit: u32,
    ) -> Result<(EntityId, TankId), Error> {
        let Request {
            model,
            spawn,
            environment,
            static_allocation,
        } = request;
        let env = registry.environment(work_limit);
        let mut loader = Loader {
            store: self,
            model: Some(model),
            spawn,
            environment,
            static_allocation,
            failure: None,
        };
        let created = state.context().create(registry.player, env, &mut loader);
        if let Some(error) = loader.failure {
            return Err(Error::Construct(error));
        }
        let entity = created.map_err(Error::Factory)?;
        let id = state
            .entities
            .record(entity)
            .and_then(|r| r.object)
            .and_then(Handle::vehicle)
            .ok_or(Error::MissingVehicle)?;
        self.visible.insert(0, id);
        spatial::remove_first(&mut self.hidden, &id, work_limit).map_err(Error::Membership)?;
        state
            .context()
            .dispatch(entity, scene_loading::ACTIVATE, None, env, &mut Lifecycle)
            .map_err(Error::Factory)?;
        Ok((entity, id))
    }
}
struct Loader<'a> {
    store: &'a mut Store,
    model: Option<VehicleModel>,
    spawn: Pose,
    environment: vehicle_startup::Environment,
    static_allocation: bool,
    failure: Option<vehicle_startup::Error>,
}
impl Methods<Handle> for Loader<'_> {
    fn invoke(
        &mut self,
        method: MethodId,
        call: Invocation<'_, Handle>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        match method {
            ALLOCATE => {
                let id = TankId(self.store.entries.len());
                self.store.entries.push(None);
                call.context
                    .entities
                    .record_mut(call.entity)
                    .ok_or(-1)?
                    .object = Some(Handle::Vehicle(id));
                call.context
                    .ticks
                    .flags
                    .resize(call.context.entities.capacity() as usize, 0);
                call.context.ticks.flags[call.entity.0 as usize] =
                    if self.static_allocation { 8 } else { 0 };
                *call.result = 1;
                Ok(())
            }
            INITIALIZE => {
                let record = call.context.entities.record(call.entity).ok_or(-1)?;
                let id = record.object.and_then(Handle::vehicle).ok_or(-1)?;
                let player = Player::construct(
                    call.entity,
                    record.class.ok_or(-1)?,
                    self.model.take().ok_or(-5)?,
                    self.spawn,
                    self.environment,
                )
                .map_err(|e| {
                    self.failure = Some(e);
                    -1
                })?;
                *self.store.entries.get_mut(id.0).ok_or(-1)? = Some(player);
                *call.result = call.entity.0;
                Ok(())
            }
            _ => Lifecycle.invoke(method, call, env),
        }
    }
}
/// Creation and runtime dispatch share these actual scheduler bindings.
/// This deliberately does not acknowledge an unimplemented frame/destroy call.
pub struct Lifecycle;
impl Methods<Handle> for Lifecycle {
    fn invoke(
        &mut self,
        method: MethodId,
        call: Invocation<'_, Handle>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        match method {
            crate::breakable::ENABLE_TICK => BaseMethod::EnableTick.invoke(call, env),
            crate::scene_objects::DISABLE_TICK => BaseMethod::DisableTick.invoke(call, env),
            _ => Err(-5),
        }
    }
}
