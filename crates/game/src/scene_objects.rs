//! Ordered scene-object lifecycle on the same components used by collision.
//! Class tables stay immutable; parent calls and nested activation are immediate.
use crate::scene_entity::Handle;
mod allocation;
mod bouncing;
mod closing;
pub mod collision;
mod explosions;
mod falling;
mod fragments;
mod lifecycle;
mod mesh_fragments;
mod moving;
mod spinning;
use crate::{
    crush,
    dispatch::{ClassId, MethodId},
    entity_factory::{self as factory, BaseMethod, Environment, Invocation, Methods, Payload},
    fall,
    math::Matrix,
    object_query::ObstacleId,
    obstacle_collision::World,
    obstacle_startup::{Obstacle, PlacedObstacle},
    scene_collision::Surfaces,
    scene_meshes::MeshId,
    scene_startup::{self, Crush},
    spatial::{self, Cell},
};
use std::sync::Arc;
use tankracer_assets::terrain::TerrainData;

pub const ZERO: MethodId = MethodId(32);
pub const ONE: MethodId = MethodId(33);
pub const IDENTITY: MethodId = MethodId(34);
pub const DISABLE_TICK: MethodId = MethodId(35);
pub const QUEUE_DESTROY: MethodId = MethodId(36);
pub const DETACH: MethodId = MethodId(37);
pub const RELEASE_ID: MethodId = MethodId(38);
pub const RELEASE_OBJECT: MethodId = MethodId(39);
pub const RESET_POSE: MethodId = MethodId(40);
pub const WORLD_INITIALIZE: MethodId = MethodId(41);
pub const OBSTACLE_INITIALIZE: MethodId = MethodId(42);
pub const ENTER: MethodId = MethodId(43);
pub const LEAVE: MethodId = MethodId(44);
pub const DEACTIVATE: MethodId = MethodId(45);
pub const SET_MODE: MethodId = MethodId(46);
pub const MOVE: MethodId = MethodId(47);
pub const COLLIDABLE: MethodId = MethodId(48);
pub const DESTROY: MethodId = MethodId(49);
pub const NOTIFY: MethodId = MethodId(50);
pub const ACTIVATE: MethodId = MethodId(51);
pub const CRUSH_ENTER: MethodId = MethodId(52);
pub const CRUSH_LEAVE: MethodId = MethodId(53);
pub const CRUSH_DESTROY: MethodId = MethodId(54);
pub const CRUSH_HIGHLIGHT: MethodId = MethodId(55);
pub const CRUSH_INITIALIZE: MethodId = MethodId(56);
pub const DESTROY_REQUEST: MethodId = MethodId(57);
pub const FALL_TICK: MethodId = MethodId(65);
pub const FALL_DESTROY: MethodId = MethodId(66);
pub const AUXILIARY_TICK: MethodId = MethodId(68);
pub const DEBRIS_TICK: MethodId = MethodId(69);
pub const AUXILIARY_HIT: MethodId = MethodId(70);
pub const DEBRIS_HIT: MethodId = MethodId(71);
pub const BOUNCE_INITIALIZE: MethodId = MethodId(77);
pub const BOUNCE_HIT: MethodId = MethodId(79);
pub const BOUNCE_TICK: MethodId = MethodId(80);
pub const CLOSER_HIT: MethodId = MethodId(82);
pub const CLOSER_TICK: MethodId = MethodId(83);
pub const SPIN_INITIALIZE: MethodId = MethodId(84);
pub const SPIN_HIT: MethodId = MethodId(86);
pub const SPIN_TICK: MethodId = MethodId(87);
pub const MOVER_INITIALIZE: MethodId = MethodId(88);
pub const MOVER_TICK: MethodId = MethodId(90);
pub const MOVER_HIT: MethodId = MethodId(91);
pub const EXPLOSION_ALLOCATE: MethodId = MethodId(92);
pub const EXPLOSION_TICK: MethodId = MethodId(93);

#[derive(Clone, Copy)]
pub struct Parents {
    pub world: ClassId,
    pub generic: ClassId,
    pub obstacle: ClassId,
    pub spin: ClassId,
}
#[derive(Clone, Copy)]
pub struct Interfaces {
    pub activate: u32,
    pub deactivate: u32,
    pub auxiliary_tick: u32,
    pub debris_tick: u32,
    pub auxiliary_complete: u32,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SoundId(pub u32);
pub struct Object {
    /// Optional host-owned sound resource; the disabled C loader creates none.
    pub impact_sound: Option<SoundId>,
    pub data: PlacedObstacle,
    /// Owned replacement geometry and per-face motion share renderer storage.
    pub auxiliary_mesh: Option<MeshId>,
    /// Live auxiliary mode word; mesh ownership alone does not mean it ticks.
    pub auxiliary_active: i16,
    pub auxiliary_motion: Option<crate::mesh_break::State>,
    pub ground_height: Option<i32>,
}
/// Collision traversal borrows `world` while callbacks borrow `components`.
/// Both views write the same owners used by factory updates and rendering.
pub struct Scene {
    pub world: World,
    pub components: Components,
}
/// Lifecycle-only calls need no random stream. Frame execution must borrow the
/// caller's authoritative stream; missing simulation context is an explicit error.
pub enum Execution<'a> {
    Lifecycle,
    Simulation { random: &'a mut u32 },
}
pub struct View<'a> {
    pub execution: Execution<'a>,
    pub world: &'a mut World,
    pub components: &'a mut Components,
    pub vehicles: Option<&'a crate::vehicle_loading::Store>,
}
pub struct Components {
    pub surfaces: Surfaces,
    pub objects: Vec<Option<Object>>,
    pub ordinary: Vec<ObstacleId>,
    pub special: Vec<ObstacleId>,
    pub lap_objects: Vec<ObstacleId>,
    pub signs: Vec<ObstacleId>,
    pub sign_meshes: [Option<MeshId>; 2],
    pub falls: Vec<Option<fall::State>>,
    pub bounces: Vec<Option<crate::bounce::Component>>,
    pub closers: Vec<Option<crate::closer::Component>>,
    pub spins: Vec<Option<crate::spin::Component>>,
    pub movers: Vec<Option<crate::mover::State>>,
    pub explosions: Vec<Option<crate::explosion::State>>,
    pub explosion_world: crate::explosion::World,
    pub explosion_resources: crate::explosion::Resources,
    pub explosion_class: Option<ClassId>,
    pub waypoints: crate::waypoint::Routes,
    pub camera_position: [i32; 3],
    pub gravity: i32,
    pub umbrella_mesh: Option<MeshId>,
    pub fall_tuning: fall::Tuning,
    pub(crate) terrain: Option<Arc<TerrainData>>,
    pub(crate) pending_crush: Vec<(ObstacleId, crush::Initial)>,
    pub parents: Parents,
    pub interfaces: Interfaces,
    pub dt: i32,
    pub frame_group: u32,
    pub mode: i32,
}
impl Scene {
    pub fn spawn_explosion(
        &mut self,
        identity: &mut factory::State<Handle>,
        position: [i32; 3],
        random: &mut u32,
        environment: Environment<'_>,
    ) -> Result<crate::object_query::EntityId, i32> {
        self.view()
            .spawn_explosion(&mut identity.context(), position, random, environment)
    }
    pub fn new(world: World, surfaces: Surfaces, parents: Parents, interfaces: Interfaces) -> Self {
        Self {
            world,
            components: Components {
                surfaces,
                objects: Vec::new(),
                ordinary: Vec::new(),
                special: Vec::new(),
                lap_objects: Vec::new(),
                signs: Vec::new(),
                sign_meshes: [None; 2],
                falls: Vec::new(),
                bounces: Vec::new(),
                closers: Vec::new(),
                spins: Vec::new(),
                movers: Vec::new(),
                explosions: Vec::new(),
                explosion_world: crate::explosion::World::default(),
                explosion_resources: crate::explosion::Resources::default(),
                explosion_class: None,
                waypoints: crate::waypoint::Routes::default(),
                camera_position: [0; 3],
                gravity: 5,
                umbrella_mesh: None,
                fall_tuning: fall::Tuning::default(),
                terrain: None,
                pending_crush: Vec::new(),
                parents,
                interfaces,
                dt: 0,
                frame_group: 0,
                mode: 0,
            },
        }
    }
    pub fn view(&mut self) -> View<'_> {
        View {
            execution: Execution::Lifecycle,
            world: &mut self.world,
            components: &mut self.components,
            vehicles: None,
        }
    }
    pub fn updates<'a>(&'a mut self, random: &'a mut u32) -> View<'a> {
        View {
            world: &mut self.world,
            components: &mut self.components,
            execution: Execution::Simulation { random },
            vehicles: None,
        }
    }
    pub fn updates_with_vehicles<'a>(
        &'a mut self,
        random: &'a mut u32,
        vehicles: &'a crate::vehicle_loading::Store,
    ) -> View<'a> {
        let mut view = self.updates(random);
        view.vehicles = Some(vehicles);
        view
    }
    pub fn install_obstacle(
        &mut self,
        obstacle: Obstacle,
        class: ClassId,
    ) -> Result<ObstacleId, scene_startup::Error> {
        self.view().install_obstacle(obstacle, class)
    }
    pub fn install_crush(
        &mut self,
        crush: Crush,
        class: ClassId,
    ) -> Result<ObstacleId, scene_startup::Error> {
        self.view().install_crush(crush, class)
    }
    pub fn move_object(
        &mut self,
        id: ObstacleId,
        position: [i32; 3],
        limit: u32,
    ) -> Result<u32, i32> {
        self.view().move_object(id, position, limit)
    }
}
impl Components {
    pub(crate) fn object(&self, id: ObstacleId) -> Result<&Object, i32> {
        self.objects.get(id.0).and_then(Option::as_ref).ok_or(-1)
    }
    pub(crate) fn object_mut(&mut self, id: ObstacleId) -> Result<&mut Object, i32> {
        self.objects
            .get_mut(id.0)
            .and_then(Option::as_mut)
            .ok_or(-1)
    }
    pub(crate) fn crush_initial(&self, id: ObstacleId) -> Result<crush::Initial, i32> {
        self.pending_crush
            .iter()
            .find(|(target, _)| *target == id)
            .map(|(_, initial)| *initial)
            .ok_or(-1)
    }
    pub(crate) fn finish_crush_initial(&mut self, id: ObstacleId) {
        self.pending_crush.retain(|(target, _)| *target != id);
    }
}
impl View<'_> {
    fn adopt(&mut self, object: Object) -> Result<ObstacleId, scene_startup::Error> {
        let id = object.data.id;
        if self
            .components
            .objects
            .get(id.0)
            .is_some_and(Option::is_some)
        {
            return Err(scene_startup::Error::InvalidWorld);
        }
        if self.components.objects.len() <= id.0 {
            self.components.objects.resize_with(id.0 + 1, || None);
        }
        self.components.objects[id.0] = Some(object);
        Ok(id)
    }
    pub fn install_obstacle(
        &mut self,
        obstacle: Obstacle,
        class: ClassId,
    ) -> Result<ObstacleId, scene_startup::Error> {
        let data = obstacle.install_components(self.world, class)?;
        self.adopt(Object {
            impact_sound: None,
            data,
            auxiliary_mesh: None,
            auxiliary_active: 0,
            auxiliary_motion: None,
            ground_height: None,
        })
    }
    pub fn install_crush(
        &mut self,
        crush: Crush,
        class: ClassId,
    ) -> Result<ObstacleId, scene_startup::Error> {
        let placed = crush.install_components(self.world, &mut self.components.surfaces, class)?;
        let mesh = self.components.surfaces.entries[placed.id.0]
            .as_ref()
            .ok_or(scene_startup::Error::InvalidWorld)?
            .mesh;
        self.adopt(Object {
            impact_sound: None,
            data: PlacedObstacle {
                id: placed.id,
                // Crush initialize does not call the ordinary parent initializer.
                cell: Some(Cell { x: 0, z: 0 }),
                source: Some(placed.source),
                mesh: Some(mesh),
                visibility: placed.visibility,
                sort_position: 0,
                impact_threshold: 0,
                collision_margin: 1536,
                auxiliary: None,
                debris: None,
            },
            auxiliary_mesh: None,
            auxiliary_active: 0,
            auxiliary_motion: None,
            ground_height: Some(placed.ground_height),
        })
    }
    fn crush_records_attached(&self, id: ObstacleId) -> Result<bool, i32> {
        if let Some(surface) = self
            .components
            .surfaces
            .entries
            .get(id.0)
            .and_then(Option::as_ref)
        {
            Ok(surface.sampling.state.records_attached)
        } else {
            self.components.crush_initial(id).map(|_| false)
        }
    }
    fn insert_grid(&mut self, id: ObstacleId) -> Result<(), i32> {
        let mesh = self.components.object(id)?.data.mesh;
        let radius = mesh
            .and_then(|mesh| self.components.surfaces.meshes.get(mesh))
            .map(|_| self.world.colliders[id.0].radius);
        let cell = spatial::insert(
            &mut self.world.grid,
            id,
            &self.world.colliders[id.0],
            radius,
        );
        self.components.object_mut(id)?.data.cell = Some(cell);
        Ok(())
    }
    fn remove_grid(&mut self, id: ObstacleId, limit: u32) -> Result<(), i32> {
        let cell = self.components.object(id)?.data.cell;
        spatial::remove(&mut self.world.grid, id, cell, limit).map_err(spatial_code)
    }
    fn clear_owned(&mut self, id: ObstacleId) -> Result<(), i32> {
        if let Some(mesh) = self.components.object(id)?.auxiliary_mesh {
            self.components
                .surfaces
                .meshes
                .release_private(mesh)
                .ok_or(-1)?;
        }
        let object = self.components.object_mut(id)?;
        object.auxiliary_mesh = None;
        object.auxiliary_active = 0;
        object.auxiliary_motion = None;
        object.data.auxiliary = None;
        object.data.debris = None;
        Ok(())
    }
    pub fn move_object(
        &mut self,
        id: ObstacleId,
        position: [i32; 3],
        limit: u32,
    ) -> Result<u32, i32> {
        valid_limit(limit)?;
        self.world.colliders.get_mut(id.0).ok_or(-1)?.pose.position = position;
        self.remove_grid(id, limit)?;
        self.insert_grid(id)?;
        Ok(position[1] as u32)
    }
}
fn valid_limit(limit: u32) -> Result<(), i32> {
    if limit == 0 { Err(-1) } else { Ok(()) }
}
fn spatial_code(error: spatial::Error) -> i32 {
    match error {
        spatial::Error::InvalidLimit => -1,
        spatial::Error::Limit => -3,
    }
}
fn dispatch_code(error: factory::Error) -> i32 {
    match error {
        factory::Error::Dispatch(crate::dispatch::Error::Method(code)) => code,
        _ => -1,
    }
}
fn value(payload: Option<Payload>) -> Result<u32, i32> {
    match payload {
        Some(Payload::Value(v)) => Ok(v),
        _ => Err(-1),
    }
}
impl Methods<Handle> for Scene {
    fn invoke(
        &mut self,
        method: MethodId,
        call: Invocation<'_, Handle>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        self.view().invoke(method, call, env)
    }
}
impl Methods<Handle> for View<'_> {
    fn invoke(
        &mut self,
        method: MethodId,
        mut call: Invocation<'_, Handle>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        if method == EXPLOSION_ALLOCATE {
            let (_, id) = self.allocate_object(&mut call, false)?;
            self.components.explosions.resize_with(id.0 + 1, || None);
            self.components.explosions[id.0] = Some(crate::explosion::State::default());
            *call.result = 1;
            return Ok(());
        }
        let base = match method {
            ZERO => Some(BaseMethod::Zero),
            ONE => Some(BaseMethod::One),
            IDENTITY => Some(BaseMethod::Identity),
            crate::breakable::ENABLE_TICK => Some(BaseMethod::EnableTick),
            DISABLE_TICK => Some(BaseMethod::DisableTick),
            QUEUE_DESTROY => Some(BaseMethod::QueueDestroy),
            DETACH => Some(BaseMethod::Detach),
            RELEASE_ID => Some(BaseMethod::ReleaseId),
            _ => None,
        };
        if let Some(base) = base {
            let target = call
                .context
                .entities
                .record(call.entity)
                .and_then(|r| r.object)
                .and_then(Handle::obstacle);
            let retiring = matches!(method, DETACH | RELEASE_ID);
            base.invoke(call, env)?;
            if retiring && let Some(id) = target {
                self.components.finish_crush_initial(id);
                if let Some(object) = self.components.objects.get_mut(id.0) {
                    *object = None;
                }
                if let Some(surface) = self.components.surfaces.entries.get_mut(id.0) {
                    *surface = None;
                }
                if let Some(state) = self.components.falls.get_mut(id.0) {
                    *state = None;
                }
                if let Some(state) = self.components.bounces.get_mut(id.0) {
                    *state = None;
                }
                if let Some(state) = self.components.closers.get_mut(id.0) {
                    *state = None;
                }
                if let Some(state) = self.components.spins.get_mut(id.0) {
                    *state = None;
                }
                if let Some(state) = self.components.movers.get_mut(id.0) {
                    *state = None;
                }
                if let Some(state) = self.components.explosions.get_mut(id.0) {
                    *state = None;
                }
                if let Some(state) = self.components.waypoints.points.get_mut(id.0) {
                    *state = None;
                }
            }
            return Ok(());
        }
        let entity = call.entity;
        let target = call
            .context
            .entities
            .record(entity)
            .and_then(|r| r.object)
            .and_then(Handle::obstacle);
        if method == RELEASE_OBJECT {
            *call.result = u32::from(
                target.is_some()
                    && call
                        .context
                        .ticks
                        .flags
                        .get(entity.0 as usize)
                        .copied()
                        .ok_or(-1)?
                        & 8
                        == 0,
            );
            return Ok(());
        }
        let id = target.ok_or(-1)?;
        match method {
            FALL_DESTROY => return self.destroy_fall(id, call, env),
            FALL_TICK => return self.tick_fall(id, call, env),
            BOUNCE_TICK => return self.tick_bounce(id, call, env),
            CLOSER_TICK => return self.tick_closer(id, call, env),
            SPIN_TICK => return self.tick_spin(id, call, env),
            MOVER_TICK => return self.tick_mover(id, call, env),
            EXPLOSION_TICK => return self.tick_explosion(id, call, env),
            DEBRIS_TICK => return self.tick_debris(id, call, env),
            AUXILIARY_TICK => return self.tick_mesh_fragments(id, call, env),
            RESET_POSE => {
                let pose = &mut self.world.colliders.get_mut(id.0).ok_or(-1)?.pose;
                pose.matrix = Matrix::IDENTITY;
                self.world.colliders[id.0].external_basis = Some(Matrix::IDENTITY);
                *call.result = entity.0;
            }
            WORLD_INITIALIZE => {
                call.context
                    .class_dispatch(self.components.parents.world, entity, None, env, self)
                    .map_err(dispatch_code)?;
                self.components.object_mut(id)?.data.sort_position = -8;
                *call.result = 0;
            }
            OBSTACLE_INITIALIZE => {
                valid_limit(env.work_limit)?;
                call.context
                    .class_dispatch(self.components.parents.generic, entity, None, env, self)
                    .map_err(dispatch_code)?;
                self.components.object_mut(id)?.data.cell = None;
                *call.result = entity.0;
            }
            CRUSH_INITIALIZE => {
                self.world.objects.get_mut(id.0).ok_or(-1)?.weight = 8;
                if let Some(surface) = self
                    .components
                    .surfaces
                    .entries
                    .get_mut(id.0)
                    .and_then(Option::as_mut)
                {
                    surface.config.floor = 50;
                    surface.config.compression = 1;
                    surface.sampling.edge_cap = 45000;
                    surface.config.drift_shift = 1;
                } else if let Some((_, initial)) = self
                    .components
                    .pending_crush
                    .iter_mut()
                    .find(|(target, _)| *target == id)
                {
                    *initial = crush::Initial {
                        highlighted: initial.highlighted,
                        ..Default::default()
                    };
                } else {
                    self.components
                        .pending_crush
                        .push((id, crush::Initial::default()));
                }
                *call.result = entity.0;
            }
            ENTER => {
                valid_limit(env.work_limit)?;
                self.components.ordinary.push(id);
                self.insert_grid(id)?;
                *call.result = 0;
            }
            MOVER_INITIALIZE => {
                call.context
                    .class_dispatch(self.components.parents.spin, entity, None, env, self)
                    .map_err(dispatch_code)?;
                let state = self
                    .components
                    .movers
                    .get_mut(id.0)
                    .and_then(Option::as_mut)
                    .ok_or(-1)?;
                state.distance = 10240;
                state.speed = 1310;
                state.flags |= 60;
                state.activation_range = 51200;
                *call.result = 0;
            }
            SPIN_INITIALIZE => {
                call.context
                    .class_dispatch(self.components.parents.obstacle, entity, None, env, self)
                    .map_err(dispatch_code)?;
                let config = &mut self
                    .components
                    .spins
                    .get_mut(id.0)
                    .and_then(Option::as_mut)
                    .ok_or(-1)?
                    .config;
                *config = crate::spin::Config {
                    deflection: config.deflection,
                    ..Default::default()
                };
                self.components.object_mut(id)?.data.impact_threshold = 10;
                self.world.objects[id.0].weight = 20;
                *call.result = 0;
            }
            BOUNCE_INITIALIZE => {
                call.context
                    .class_dispatch(self.components.parents.obstacle, entity, None, env, self)
                    .map_err(dispatch_code)?;
                let config = &mut self
                    .components
                    .bounces
                    .get_mut(id.0)
                    .and_then(Option::as_mut)
                    .ok_or(-1)?
                    .config;
                config.elasticity = 35;
                config.friction = 10;
                config.height_factor = 20;
                self.components.object_mut(id)?.data.impact_threshold = 10;
                self.world.objects[id.0].weight = 0;
                *call.result = 0;
            }
            LEAVE => {
                valid_limit(env.work_limit)?;
                spatial::remove_first(&mut self.components.ordinary, &id, env.work_limit)
                    .map_err(spatial_code)?;
                self.remove_grid(id, env.work_limit)?;
                *call.result = 0;
            }
            DEACTIVATE => {
                lifecycle::Membership {
                    grid: &mut self.world.grid,
                    id,
                    cell: self.components.objects[id.0].as_mut().ok_or(-1)?.data.cell,
                    ordinary: &mut self.components.ordinary,
                    special: &mut self.components.special,
                    parent: self.components.parents.generic,
                }
                .invoke(method, call, env)?;
            }
            SET_MODE => {
                valid_limit(env.work_limit)?;
                let mode = value(call.payload)?;
                let (from, to) = if mode == 1 {
                    (&mut self.components.ordinary, &mut self.components.special)
                } else {
                    (&mut self.components.special, &mut self.components.ordinary)
                };
                spatial::remove_first(from, &id, env.work_limit).map_err(spatial_code)?;
                to.push(id);
                *call.result = 0;
            }
            MOVE => {
                let Some(Payload::Position(position)) = call.payload else {
                    return Err(-1);
                };
                *call.result = self.move_object(id, position, env.work_limit)?;
            }
            COLLIDABLE => {
                valid_limit(env.work_limit)?;
                if value(call.payload)? == 1 {
                    self.insert_grid(id)?;
                } else {
                    self.remove_grid(id, env.work_limit)?;
                }
                *call.result = 0;
            }
            DESTROY => {
                valid_limit(env.work_limit)?;
                self.clear_owned(id)?;
                self.remove_grid(id, env.work_limit)?;
                call.context
                    .class_dispatch(self.components.parents.generic, entity, None, env, self)
                    .map_err(dispatch_code)?;
                *call.result = entity.0;
            }
            NOTIFY => {
                call.context
                    .queues
                    .queue(call.context.ticks, entity, false, env.work_limit)
                    .map_err(|e| crush::code(crush::Error::Lifecycle(e)))?;
                *call.result = 0;
            }
            ACTIVATE => {
                call.context
                    .dispatch(entity, self.components.interfaces.activate, None, env, self)
                    .map_err(dispatch_code)?;
                *call.result = 1;
            }
            DESTROY_REQUEST => {
                *call.result = call
                    .context
                    .dispatch(
                        entity,
                        self.components.interfaces.deactivate,
                        None,
                        env,
                        self,
                    )
                    .map_err(dispatch_code)?;
            }
            CRUSH_ENTER => {
                let records_attached = self.crush_records_attached(id)?;
                let activate = crush::enter(
                    id,
                    &mut self.world.deformables,
                    &mut self.components.special,
                    records_attached,
                    env.work_limit,
                )
                .map_err(crush::code)?;
                if activate {
                    call.context
                        .dispatch(entity, self.components.interfaces.activate, None, env, self)
                        .map_err(dispatch_code)?;
                }
                *call.result = u32::from(activate);
            }
            CRUSH_LEAVE => {
                let records_attached = self.crush_records_attached(id)?;
                *call.result = u32::from(
                    crush::leave(
                        id,
                        entity,
                        crush::Membership {
                            collision: &mut self.world.deformables,
                            drawing: &mut self.components.special,
                        },
                        records_attached,
                        call.context.ticks,
                        call.context.queues,
                        env.work_limit,
                    )
                    .map_err(crush::code)?,
                );
            }
            CRUSH_DESTROY => {
                valid_limit(env.work_limit)?;
                if let Some(surface) = self
                    .components
                    .surfaces
                    .entries
                    .get_mut(id.0)
                    .and_then(Option::as_mut)
                {
                    let private = surface.runtime.private_mesh;
                    crush::destroy_owned(
                        surface,
                        &mut self.components.surfaces.meshes,
                        env.work_limit,
                    )
                    .map_err(crush::code)?;
                    if private {
                        self.components.object_mut(id)?.data.mesh = None;
                    }
                } else {
                    self.components.crush_initial(id)?;
                }
                self.components.finish_crush_initial(id);
                call.context
                    .class_dispatch(self.components.parents.obstacle, entity, None, env, self)
                    .map_err(dispatch_code)?;
                *call.result = entity.0;
            }
            CRUSH_HIGHLIGHT => {
                let mode = value(call.payload)?;
                if let Some(surface) = self
                    .components
                    .surfaces
                    .entries
                    .get_mut(id.0)
                    .and_then(Option::as_mut)
                {
                    crush::set_highlight(surface, mode);
                } else {
                    self.components
                        .pending_crush
                        .iter_mut()
                        .find(|(target, _)| *target == id)
                        .ok_or(-1)?
                        .1
                        .highlighted = mode == 1;
                }
                *call.result = id.0 as u32 + 1;
            }
            crush::TICK => crush::invoke_tick(
                self.components
                    .surfaces
                    .entries
                    .get_mut(id.0)
                    .and_then(Option::as_mut)
                    .ok_or(-1)?,
                call,
                env,
                self.components.dt,
            )?,
            _ => return Err(-5),
        }
        Ok(())
    }
}
