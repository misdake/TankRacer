//! Obstacle/deformable query -> response -> synchronous gameplay dispatch.
use crate::{
    collision::{self, Scale},
    dispatch::{self, Class, ClassId, MethodId},
    fixed::trunc_shift,
    geometry::approximate_distance,
    math,
    object_query::{self, Collider, EntityId, Hit, ObstacleId},
    random,
};
use std::sync::Arc;

#[derive(Debug, Clone, Copy)]
pub struct Object {
    pub class: ClassId,
    pub weight: i32,
    pub threshold: i32,
    pub activity: i32,
    pub flags: u32,
    pub mesh_bounds: [[i16; 3]; 2],
}
pub struct Grid {
    pub origin: [i32; 2],
    pub shifts: [u32; 2],
    pub cells: [Vec<ObstacleId>; 64],
}
impl Grid {
    fn view(&self) -> object_query::Grid<'_> {
        object_query::Grid {
            origin: self.origin,
            shifts: self.shifts,
            cells: self.cells.each_ref().map(|v| v.as_slice()),
        }
    }
}
/// Stable parallel handles. Scene methods may change objects and candidate lists
/// synchronously; removing an entity must not renumber remaining handles.
pub struct World {
    pub colliders: Vec<Collider>,
    pub objects: Vec<Object>,
    pub classes: Vec<Arc<Class>>,
    pub grid: Grid,
    pub deformables: Vec<ObstacleId>,
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct State {
    pub entity: EntityId,
    pub class: ClassId,
    /// Base position used by game methods; query position below is physical +78.
    pub base_position: [i32; 3],
    pub position: [i32; 3],
    pub previous_position: [i32; 3],
    pub velocity: [i32; 3],
    pub forward_velocity: i32,
    pub player: bool,
    pub contacts: [[i32; 3]; 4],
    pub impact_accumulator: i32,
    pub collision_size: u32,
    pub material_tag: i32,
    pub deformable: Option<EntityId>,
}
#[derive(Debug, Clone, Copy)]
pub struct Tuning {
    pub local_z: Scale,
    pub final_scale: Scale,
}
impl Default for Tuning {
    fn default() -> Self {
        Self {
            local_z: Scale {
                multiplier: 1,
                shift: 1,
            },
            final_scale: Scale {
                multiplier: 1024,
                shift: 10,
            },
        }
    }
}
#[derive(Debug, Clone, Copy)]
pub struct Environment {
    pub mode: i32,
    pub dt: i32,
    pub feedback: bool,
    pub player_class: ClassId,
    pub breakable_class: ClassId,
    pub obstacle_slot: u32,
    pub deformable_slot: u32,
    pub tuning: Tuning,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    Query(object_query::Error),
    Arithmetic(math::Error),
    Dispatch(dispatch::Error),
    MissingObject(ObstacleId),
    MissingClass(ClassId),
    Feedback(i32),
    Impact(i32),
}
pub struct Shared {
    pub random: u32,
    pub current_slot: u32,
}
pub struct Invocation<'a> {
    pub dt: i32,
    pub target: ObstacleId,
    pub vehicle: &'a mut State,
    pub world: &'a mut World,
    pub random: &'a mut u32,
    pub current_slot: &'a mut u32,
    pub result: &'a mut u32,
}
/// No default game methods: consumers must implement actual stateful operations.
/// The full collision world is writable so methods can update memberships and
/// subsequent contact tests observe their writes in the same frame.
pub trait Effects {
    fn method(&mut self, method: MethodId, call: Invocation<'_>) -> Result<(), i32>;
    fn feedback(
        &mut self,
        intensity: i32,
        vehicle: &mut State,
        random: &mut u32,
    ) -> Result<(), i32>;
    fn impact(
        &mut self,
        vehicle: &mut State,
        contact: usize,
        kind: i32,
        random: &mut u32,
    ) -> Result<(), i32>;
}
fn object(world: &World, id: ObstacleId) -> Result<(&Collider, &Object), Error> {
    Ok((
        world.colliders.get(id.0).ok_or(Error::MissingObject(id))?,
        world.objects.get(id.0).ok_or(Error::MissingObject(id))?,
    ))
}
fn invoke(
    world: &mut World,
    vehicle: &mut State,
    id: ObstacleId,
    slot: u32,
    dt: i32,
    shared: &mut Shared,
    effects: &mut impl Effects,
) -> Result<(), Error> {
    let class = object(world, id)?.1.class;
    let table = Arc::clone(
        world
            .classes
            .get(class.0)
            .ok_or(Error::MissingClass(class))?,
    );
    let mut result = 0;
    dispatch::call(
        &table,
        slot,
        &mut shared.current_slot,
        &mut result,
        |method, current_slot, result| {
            effects.method(
                method,
                Invocation {
                    dt,
                    target: id,
                    vehicle,
                    world,
                    random: &mut shared.random,
                    current_slot,
                    result,
                },
            )
        },
    )
    .map_err(Error::Dispatch)
}
fn segment(state: &State) -> ([i32; 3], [i32; 3]) {
    (
        state.position,
        std::array::from_fn(|i| {
            state.position[i]
                .wrapping_mul(2)
                .wrapping_sub(state.previous_position[i])
        }),
    )
}
fn reflect(state: &mut State, normal: [i32; 3], env: Environment) -> Result<(), Error> {
    let r = collision::respond(
        normal,
        state.velocity,
        Some(env.tuning.local_z),
        Some(env.tuning.final_scale),
        env.mode,
        false,
    )
    .map_err(Error::Arithmetic)?;
    state.velocity = r.velocity;
    Ok(())
}
fn blend(state: &mut State, old: [i32; 3], weight: i32) {
    for (i, value) in old.into_iter().enumerate() {
        state.velocity[i] = trunc_shift(
            weight
                .wrapping_mul(state.velocity[i])
                .wrapping_add(128_i32.wrapping_sub(weight).wrapping_mul(value)),
            7,
        );
    }
}
pub fn obstacles(
    world: &mut World,
    state: &mut State,
    env: Environment,
    shared: &mut Shared,
    effects: &mut impl Effects,
) -> Result<Option<ObstacleId>, Error> {
    let (a, b) = segment(state);
    let mut hit = Hit::default();
    object_query::obstacles(
        &world.grid.view(),
        &world.colliders,
        state.entity,
        a,
        b,
        &mut hit,
    )
    .map_err(Error::Query)?;
    let Some(id) = hit.object else {
        return Ok(None);
    };
    let (old, threshold) = (state.velocity, object(world, id)?.1.threshold);
    reflect(state, hit.normal, env)?;
    if threshold != 0 && approximate_distance(old) < threshold {
        return Ok(Some(id));
    }
    let (collider, properties) = object(world, id)?;
    blend(state, old, properties.weight);
    if state.class == env.player_class
        && properties.weight != 128
        && approximate_distance(old) > 200000
        && (properties.class != env.breakable_class
            || properties.activity == 0
            || properties.flags == 0)
    {
        state.impact_accumulator = state.impact_accumulator.wrapping_add(70);
        state.collision_size = state
            .collision_size
            .wrapping_add(u32::from(collider.radius >> 5));
    }
    invoke(world, state, id, env.obstacle_slot, env.dt, shared, effects)?;
    Ok(Some(id))
}
fn inside(world: &World, id: ObstacleId, point: [i32; 3]) -> Result<bool, Error> {
    let (collider, properties) = object(world, id)?;
    let local = collider
        .pose
        .matrix
        .world_to_local(std::array::from_fn(|i| {
            point[i].wrapping_sub(collider.pose.position[i])
        }));
    let low = properties.mesh_bounds[0].map(|v| i32::from(v) * 8);
    let high = properties.mesh_bounds[1].map(|v| i32::from(v) * 8);
    Ok(local[0] > low[0] && local[2] > low[2] && local[0] < high[0] && local[2] < high[2])
}
pub fn deformables(
    world: &mut World,
    state: &mut State,
    env: Environment,
    shared: &mut Shared,
    effects: &mut impl Effects,
) -> Result<Option<ObstacleId>, Error> {
    let (a, b) = segment(state);
    state.material_tag = 0;
    let mut hit = Hit::default();
    object_query::deformables(&world.deformables, &world.colliders, a, b, &mut hit)
        .map_err(Error::Query)?;
    state.deformable = hit.entity;
    let Some(id) = hit.object else {
        return Ok(None);
    };
    let old = state.velocity;
    if state.player && env.feedback {
        effects
            .feedback(10, state, &mut shared.random)
            .map_err(|code| Error::Feedback(if code < 0 { code } else { -7 }))?;
    }
    reflect(state, hit.normal, env)?;
    blend(state, old, object(world, id)?.1.weight);
    invoke(
        world,
        state,
        id,
        env.deformable_slot,
        env.dt,
        shared,
        effects,
    )?;
    for i in 0..4 {
        if inside(world, id, state.contacts[i])?
            && state.player
            && approximate_distance(state.velocity) > 200000
            && env.mode != 2
            && env.mode != 3
            && random::next(&mut shared.random) & 0x30 == 0
        {
            let first = usize::from(state.forward_velocity >= 0);
            effects
                .impact(state, first, 5, &mut shared.random)
                .map_err(|code| Error::Impact(if code < 0 { code } else { -5 }))?;
            effects
                .impact(state, first + 2, 6, &mut shared.random)
                .map_err(|code| Error::Impact(if code < 0 { code } else { -5 }))?;
            state.impact_accumulator = state.impact_accumulator.wrapping_add(env.dt);
        }
    }
    Ok(Some(id))
}
