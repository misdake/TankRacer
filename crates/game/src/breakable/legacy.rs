//! Standalone component owner for module qualification and embedded callers.
//! Runtime scene integration uses the shared factory, never this separate store.
use super::*;
use crate::{lifecycle::Ticks, symbols::Symbols};
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

struct Adapter<'a, S> {
    store: &'a mut Store,
    services: &'a mut S,
}
impl<S: Services> Owner for Adapter<'_, S> {
    fn motion(&mut self, id: ObstacleId) -> Result<Motion<'_>, i32> {
        let state = state_mut(self.store, id)?;
        Ok(Motion {
            angular_velocity: &mut state.angular_velocity,
            rotation: &mut state.rotation,
            turn_rate: &mut state.turn_rate,
            last_vehicle: &mut state.last_vehicle,
        })
    }
    fn metadata(&self, id: ObstacleId) -> Result<Metadata, i32> {
        let s = state(self.store, id)?;
        Ok(Metadata {
            has_sound: s.has_sound,
            auxiliary: s.auxiliary,
            effect_flags: s.effect_flags,
            cell: s.cell,
        })
    }
    fn attached(&self, id: ObstacleId) -> Result<Option<Arc<[Binding]>>, i32> {
        Ok(Some(Arc::clone(&state(self.store, id)?.attached)))
    }
    fn resolve(&self, binding: Binding, world: &World) -> Option<ObstacleId> {
        binding
            .resolve(&self.store.symbols)
            .and_then(|id| entity(world, self.store, id))
    }
    fn method(
        &mut self,
        method: MethodId,
        call: Invocation<'_>,
        env: Environment,
        has_payload: bool,
    ) -> Result<(), i32> {
        if method == ENABLE_TICK {
            if env.work_limit == 0 {
                return Err(-1);
            }
            *call.result = u32::from(self.store.ticks.enable(call.target).map_err(|_| -1)?);
            Ok(())
        } else {
            self.services.method(method, self.store, call, has_payload)
        }
    }
    fn sound(&mut self, call: Invocation<'_>) -> Result<(), i32> {
        self.services.sound(self.store, call)
    }
    fn feedback(&mut self, intensity: i32, call: Invocation<'_>) -> Result<(), i32> {
        self.services.feedback(intensity, self.store, call)
    }
}
pub fn collide(
    store: &mut Store,
    call: Invocation<'_>,
    env: Environment,
    services: &mut impl Services,
) -> Result<(), i32> {
    hit(&mut Adapter { store, services }, call, env)
}
