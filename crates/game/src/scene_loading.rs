//! Fresh scene-object loading through the shared factory and real parent
//! order. Resource construction never installs memberships implicitly.
use crate::{
    breakable, crush,
    dispatch::{Class, ClassId, MethodId},
    entity_factory::{self as factory, Environment, Invocation, Methods, Slots},
    geometry::Bounds,
    math::Matrix,
    object_query::{Collider, EntityId, ObstacleId},
    obstacle_collision::{self, Grid, World},
    obstacle_startup::{self, Initial, Materials, Obstacle, PlacedObstacle},
    pose::Pose,
    scene_collision::{DEFORMABLE_HIT, Surfaces},
    scene_objects::{self as methods, Interfaces, Object, Parents, Scene},
    scene_startup::{self, Visibility},
    spatial::{self, Cell},
    vehicle_model::{ModelBank, ShapeSource, Space},
};
use std::sync::Arc;
use tankracer_assets::{
    level, object_catalog::Catalog, parameters::Parameters, terrain::TerrainData,
};

// Logical interfaces for the Rust registry; no PE addresses or bootstrap RAM.
pub const ALLOCATE: u32 = 0;
pub const INITIALIZE: u32 = 1;
pub const DESTROY: u32 = 2;
pub const RELEASE: u32 = 3;
pub const ACTIVATE: u32 = 4;
pub const STOP: u32 = 5;
pub const ENTER: u32 = 6;
pub const LEAVE: u32 = 7;
pub const DEACTIVATE: u32 = 8;
pub const MODE: u32 = 9;
pub const MOVE: u32 = 10;
pub const COLLIDABLE: u32 = 11;
pub const TICK: u32 = 12;
pub const READ: u32 = 16;
pub const CONSTRUCT: u32 = 17;
pub const BEFORE_READ: u32 = 18;
pub const AFTER_READ: u32 = 19;
pub const AFTER_CONSTRUCT: u32 = 20;
pub const DEFORMABLE_CONTACT: u32 = 21;

const ALLOCATE_OBJECT: MethodId = MethodId(58);
const READ_OBJECT: MethodId = MethodId(59);
const CONSTRUCT_BASE: MethodId = MethodId(60);
const CONSTRUCT_WORLD: MethodId = MethodId(61);
const CONSTRUCT_GENERIC: MethodId = MethodId(62);
const CONSTRUCT_OBSTACLE: MethodId = MethodId(63);
const CONSTRUCT_CRUSH: MethodId = MethodId(64);

pub struct Registry {
    pub classes: Vec<Arc<Class>>,
    pub world: ClassId,
    pub render_world: ClassId,
    pub generic: ClassId,
    pub obstacle: ClassId,
    pub crush: ClassId,
}
impl Default for Registry {
    fn default() -> Self {
        let mut world = Class {
            maximum_slot: DEFORMABLE_CONTACT as i32,
            methods: vec![None; DEFORMABLE_CONTACT as usize + 1],
        };
        for (slot, method) in [
            (ALLOCATE, ALLOCATE_OBJECT),
            (INITIALIZE, methods::RESET_POSE),
            (DESTROY, methods::RELEASE_OBJECT),
            (RELEASE, methods::DETACH),
            (ACTIVATE, breakable::ENABLE_TICK),
            (STOP, methods::DISABLE_TICK),
            (DEACTIVATE, methods::QUEUE_DESTROY),
            (14, methods::ACTIVATE),
            (15, methods::NOTIFY),
            (READ, READ_OBJECT),
            (CONSTRUCT, CONSTRUCT_BASE),
        ] {
            world.methods[slot as usize] = Some(method);
        }
        let mut render_world = world.clone();
        render_world.methods[INITIALIZE as usize] = Some(methods::WORLD_INITIALIZE);
        render_world.methods[CONSTRUCT as usize] = Some(CONSTRUCT_WORLD);
        let mut generic = render_world.clone();
        generic.methods[CONSTRUCT as usize] = Some(CONSTRUCT_GENERIC);
        let mut obstacle = generic.clone();
        for (slot, method) in [
            (INITIALIZE, methods::OBSTACLE_INITIALIZE),
            (DESTROY, methods::DESTROY),
            (ENTER, methods::ENTER),
            (LEAVE, methods::LEAVE),
            (DEACTIVATE, methods::DEACTIVATE),
            (MODE, methods::SET_MODE),
            (MOVE, methods::MOVE),
            (COLLIDABLE, methods::COLLIDABLE),
            (13, methods::DESTROY_REQUEST),
            (CONSTRUCT, CONSTRUCT_OBSTACLE),
            (AFTER_CONSTRUCT, methods::NOTIFY),
        ] {
            obstacle.methods[slot as usize] = Some(method);
        }
        let mut crush = obstacle.clone();
        for (slot, method) in [
            (INITIALIZE, methods::CRUSH_INITIALIZE),
            (CONSTRUCT, CONSTRUCT_CRUSH),
            (DESTROY, methods::CRUSH_DESTROY),
            (ENTER, methods::CRUSH_ENTER),
            (LEAVE, methods::CRUSH_LEAVE),
            (MODE, methods::CRUSH_HIGHLIGHT),
            (TICK, crush::TICK),
            (DEFORMABLE_CONTACT, DEFORMABLE_HIT),
        ] {
            crush.methods[slot as usize] = Some(method);
        }
        Self {
            classes: vec![
                Arc::new(world),
                Arc::new(render_world),
                Arc::new(generic),
                Arc::new(obstacle),
                Arc::new(crush),
            ],
            world: ClassId(0),
            render_world: ClassId(1),
            generic: ClassId(2),
            obstacle: ClassId(3),
            crush: ClassId(4),
        }
    }
}
impl Registry {
    pub fn environment(&self, work_limit: u32) -> Environment<'_> {
        Environment {
            classes: &self.classes,
            slots: Slots {
                allocate: ALLOCATE,
                initialize: INITIALIZE,
                destroy: DESTROY,
                release: RELEASE,
            },
            work_limit,
        }
    }
    pub fn scene(&self, grid: Grid) -> Scene {
        Scene::new(
            World {
                colliders: vec![],
                objects: vec![],
                classes: self.classes.clone(),
                grid,
                deformables: vec![],
            },
            Surfaces::default(),
            Parents {
                world: self.world,
                generic: self.generic,
                obstacle: self.obstacle,
            },
            Interfaces {
                activate: ACTIVATE,
                deactivate: DEACTIVATE,
            },
        )
    }
}

pub struct Resources<'a, S> {
    pub source: &'a S,
    pub models: &'a mut ModelBank,
    pub terrain: &'a TerrainData,
    pub random: &'a mut u32,
    pub catalog: &'a Catalog,
    pub materials: &'a mut dyn Materials,
    pub matrix_tail: Option<[i32; 3]>,
}
#[derive(Debug)]
pub enum Error {
    UnsupportedClass(String),
    Factory(factory::Error),
    Resource(scene_startup::Error),
}
pub struct Loader<'a, S> {
    pub scene: &'a mut Scene,
    pub resources: Resources<'a, S>,
    pub registry: &'a Registry,
    pub static_allocation: bool,
    pub visibility_distance: u16,
    pub work_limit: u32,
    record: Option<level::Object<'a>>,
    failure: Option<scene_startup::Error>,
}
impl<'a, S: ShapeSource> Loader<'a, S> {
    pub fn new(scene: &'a mut Scene, resources: Resources<'a, S>, registry: &'a Registry) -> Self {
        Self {
            scene,
            resources,
            registry,
            static_allocation: true,
            visibility_distance: 150,
            work_limit: 100000,
            record: None,
            failure: None,
        }
    }
    /// Load a supported authored class. Unsupported subclasses are explicit
    /// errors until their real initializers/readers have been implemented.
    pub fn load(
        &mut self,
        state: &mut factory::State<ObstacleId>,
        record: level::Object<'a>,
    ) -> Result<EntityId, Error> {
        let class = if record.class.eq_ignore_ascii_case("Obstacle") {
            self.registry.obstacle
        } else if record.class.eq_ignore_ascii_case("Crush") {
            self.registry.crush
        } else {
            return Err(Error::UnsupportedClass(record.class.into()));
        };
        self.record = Some(record);
        self.failure = None;
        let registry = self.registry;
        let env = registry.environment(self.work_limit);
        let result = (|| {
            let definition = state.context().define(record.name, class, env, self)?;
            state
                .context()
                .dispatch(definition.entity, READ, None, env, self)?;
            Ok(definition.entity)
        })();
        self.record = None;
        match (result, self.failure.take()) {
            (_, Some(error)) => Err(Error::Resource(error)),
            (Err(error), None) => Err(Error::Factory(error)),
            (Ok(entity), None) => Ok(entity),
        }
    }
    fn resource<T>(&mut self, value: Result<T, scene_startup::Error>) -> Result<T, i32> {
        value.map_err(|error| {
            self.failure = Some(error);
            -1
        })
    }
    fn allocate(&mut self, call: Invocation<'_, ObstacleId>) -> Result<(), i32> {
        let class = call
            .context
            .entities
            .record(call.entity)
            .and_then(|v| v.class)
            .ok_or(-1)?;
        let id = ObstacleId(self.scene.world.colliders.len());
        if self.scene.world.objects.len() != id.0 || self.scene.objects.len() != id.0 {
            return Err(-1);
        }
        self.scene.world.colliders.push(Collider {
            entity: call.entity,
            pose: Pose {
                position: [0; 3],
                matrix: Matrix([0; 9]),
            },
            external_basis: None,
            radius: 0,
            bounds: Bounds {
                min: [0; 3],
                max: [0; 3],
            },
        });
        self.scene.world.objects.push(obstacle_collision::Object {
            class,
            weight: 0,
            threshold: 0,
            activity: 0,
            flags: 0,
            mesh_bounds: [[0; 3]; 2],
        });
        self.scene.objects.push(Some(Object {
            data: PlacedObstacle {
                id,
                cell: Some(Cell { x: 0, z: 0 }),
                source: None,
                mesh: None,
                visibility: Visibility {
                    distance: 0,
                    add_lap: 0,
                    remove_lap: 0,
                    entered: false,
                },
                sort_position: 0,
                impact_threshold: 0,
                collision_margin: 0,
                auxiliary: None,
                debris: None,
            },
            auxiliary_mesh: None,
            ground_height: None,
        }));
        call.context
            .entities
            .record_mut(call.entity)
            .ok_or(-1)?
            .object = Some(id);
        call.context
            .ticks
            .flags
            .resize(call.context.entities.capacity() as usize, 0);
        call.context.ticks.flags[call.entity.0 as usize] =
            if self.static_allocation { 8 } else { 0 };
        *call.result = 1;
        Ok(())
    }
    fn read(
        &mut self,
        mut call: Invocation<'_, ObstacleId>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        let record = self.record.ok_or(-1)?;
        call.context
            .dispatch(call.entity, BEFORE_READ, None, env, self)
            .map_err(factory_code)?;
        let id = call
            .context
            .entities
            .record(call.entity)
            .and_then(|r| r.object)
            .ok_or(-1)?;
        let source = if record.shape.eq_ignore_ascii_case("nil") {
            None
        } else {
            let loaded = self
                .resources
                .models
                .load_optional(self.resources.source, Space::Level, record.shape)
                .map_err(scene_startup::Error::Shape);
            self.resource(loaded)?
        };
        let mesh = source
            .as_ref()
            .map(|model| self.scene.surfaces.meshes.bind(record.shape, model));
        let pose = Pose::from_level(&record);
        let collider = &mut self.scene.world.colliders[id.0];
        collider.pose = pose;
        collider.external_basis = Some(pose.matrix);
        collider.radius = source.as_ref().map_or(0, |m| m.radius);
        let data = &mut self.scene.object_mut(id)?.data;
        data.source = source;
        data.mesh = mesh;
        call.context
            .dispatch(call.entity, CONSTRUCT, None, env, self)
            .map_err(factory_code)?;
        call.context
            .dispatch(call.entity, AFTER_READ, None, env, self)
            .map_err(factory_code)?;
        *call.result = call.entity.0;
        Ok(())
    }
    fn construct_generic(
        &mut self,
        mut call: Invocation<'_, ObstacleId>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        let record = self.record.ok_or(-1)?;
        let id = call
            .context
            .entities
            .record(call.entity)
            .and_then(|r| r.object)
            .ok_or(-1)?;
        call.context
            .class_dispatch(self.registry.render_world, call.entity, None, env, self)
            .map_err(factory_code)?;
        spatial::remove_first(&mut self.scene.ordinary, &id, env.work_limit)
            .map_err(spatial_code)?;
        let p = Parameters::new(record.parameters).map_err(|_| -1)?;
        let visibility = scene_startup::visibility(p, self.resources.random);
        let visibility = self.resource(visibility)?;
        let add = visibility.add_lap;
        self.scene.object_mut(id)?.data.visibility.add_lap = add;
        // Leave runs before the delayed list append. Removal rules are independent.
        call.context
            .dispatch(
                call.entity,
                if add != 0 { LEAVE } else { ENTER },
                None,
                env,
                self,
            )
            .map_err(factory_code)?;
        self.scene.object_mut(id)?.data.visibility.entered = add == 0;
        if add != 0 {
            self.scene.lap_objects.push(id);
        }
        if p.find(":lap_to_remove").is_some() {
            self.scene.object_mut(id)?.data.visibility.remove_lap = visibility.remove_lap;
            let position = self.scene.lap_objects.iter().position(|v| *v == id);
            if position.unwrap_or(self.scene.lap_objects.len()) >= env.work_limit as usize {
                return Err(-3);
            }
            if position.is_none() {
                self.scene.lap_objects.push(id);
            }
        }
        *call.result = 0;
        Ok(())
    }
    fn construct_obstacle(
        &mut self,
        mut call: Invocation<'_, ObstacleId>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        call.context
            .class_dispatch(self.registry.generic, call.entity, None, env, self)
            .map_err(factory_code)?;
        let id = call
            .context
            .entities
            .record(call.entity)
            .and_then(|r| r.object)
            .ok_or(-1)?;
        let record = self.record.ok_or(-1)?;
        let data = &self.scene.object(id)?.data;
        let initial = Initial {
            weight: self.scene.world.objects[id.0].weight,
            threshold: data.impact_threshold,
            activity: self.scene.world.objects[id.0].activity,
            sort_position: data.sort_position,
        };
        let visibility = data.visibility;
        let built = Obstacle::build_visible(
            &record,
            call.entity,
            initial,
            visibility,
            &mut scene_startup::Resources {
                source: self.resources.source,
                models: self.resources.models,
                meshes: &mut self.scene.surfaces.meshes,
                terrain: self.resources.terrain,
                random: self.resources.random,
            },
            &mut obstacle_startup::Environment {
                catalog: self.resources.catalog,
                materials: self.resources.materials,
                matrix_tail: self.resources.matrix_tail,
            },
        );
        let built = self.resource(built)?;
        self.scene.world.colliders[id.0] = built.collider;
        let properties = &mut self.scene.world.objects[id.0];
        properties.weight = built.weight;
        properties.threshold = built.minimum_activation;
        properties.activity = built.activity;
        properties.mesh_bounds = built.source.as_ref().map_or([[0; 3]; 2], |m| m.bounds);
        let data = &mut self.scene.object_mut(id)?.data;
        data.source = built.source;
        data.mesh = built.mesh;
        data.visibility = built.visibility;
        data.sort_position = built.sort_position;
        data.impact_threshold = built.threshold;
        data.collision_margin = built.collision_margin;
        data.auxiliary = built.auxiliary;
        data.debris = built.debris;
        call.context
            .dispatch(call.entity, AFTER_CONSTRUCT, None, env, self)
            .map_err(factory_code)?;
        *call.result = call.entity.0;
        Ok(())
    }
    fn construct_crush(
        &mut self,
        mut call: Invocation<'_, ObstacleId>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        call.context
            .class_dispatch(self.registry.obstacle, call.entity, None, env, self)
            .map_err(factory_code)?;
        let id = call
            .context
            .entities
            .record(call.entity)
            .and_then(|r| r.object)
            .ok_or(-1)?;
        let data = &self.scene.object(id)?.data;
        let parent = scene_startup::CrushParent {
            collider: self.scene.world.colliders[id.0],
            source: data.source.clone(),
            mesh: data.mesh,
            visibility: data.visibility,
            weight: self.scene.world.objects[id.0].weight,
            sort_position: data.sort_position,
        };
        let initial = self.scene.crush_initial(id)?;
        let built = scene_startup::Crush::finish(
            &self.record.ok_or(-1)?,
            parent,
            initial,
            &mut scene_startup::Resources {
                source: self.resources.source,
                models: self.resources.models,
                meshes: &mut self.scene.surfaces.meshes,
                terrain: self.resources.terrain,
                random: self.resources.random,
            },
        );
        let built = self.resource(built)?;
        // The parent queued stop before allocating glow records. The subclass
        // cancels it only for an immediate-entry object with a live glow buffer.
        if built.surface.sampling.state.records_attached && built.visibility.add_lap == 0 {
            call.context
                .queues
                .cancel_notification(call.context.ticks, call.entity, env.work_limit)
                .map_err(|e| crush::code(crush::Error::Lifecycle(e)))?;
        }
        let mesh = built.surface.mesh;
        self.scene.world.colliders[id.0] = built.collider;
        self.scene.world.objects[id.0].weight = built.weight;
        self.scene.world.objects[id.0].mesh_bounds = built.source.bounds;
        let object = self.scene.object_mut(id)?;
        object.data.source = Some(built.source);
        object.data.mesh = Some(mesh);
        object.data.visibility = built.visibility;
        object.ground_height = Some(built.ground_height);
        if self.scene.surfaces.entries.len() <= id.0 {
            self.scene.surfaces.entries.resize_with(id.0 + 1, || None);
        }
        self.scene.surfaces.entries[id.0] = Some(built.surface);
        self.scene.finish_crush_initial(id);
        *call.result = call.entity.0;
        Ok(())
    }
}
impl<S: ShapeSource> Methods<ObstacleId> for Loader<'_, S> {
    fn invoke(
        &mut self,
        method: MethodId,
        mut call: Invocation<'_, ObstacleId>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        if env.work_limit == 0 {
            return Err(-1);
        }
        match method {
            ALLOCATE_OBJECT => self.allocate(call),
            READ_OBJECT => self.read(call, env),
            CONSTRUCT_BASE => {
                call.context
                    .dispatch(call.entity, ACTIVATE, None, env, self)
                    .map_err(factory_code)?;
                *call.result = call.entity.0;
                Ok(())
            }
            CONSTRUCT_WORLD => {
                call.context
                    .class_dispatch(self.registry.world, call.entity, None, env, self)
                    .map_err(factory_code)?;
                let record = self.record.ok_or(-1)?;
                let p = Parameters::new(record.parameters).map_err(|_| -1)?;
                let id = call
                    .context
                    .entities
                    .record(call.entity)
                    .and_then(|r| r.object)
                    .ok_or(-1)?;
                let data = &mut self.scene.object_mut(id)?.data;
                data.visibility.distance = self.visibility_distance;
                if let Some(sort) = p.integer(":sortpos") {
                    data.sort_position = sort as i16;
                }
                if let Some(weight) = p.integer(":solidity").or_else(|| p.integer(":Solidity")) {
                    self.scene.world.objects[id.0].weight = weight;
                }
                self.scene.ordinary.push(id);
                *call.result = call.entity.0;
                Ok(())
            }
            CONSTRUCT_GENERIC => self.construct_generic(call, env),
            CONSTRUCT_OBSTACLE => self.construct_obstacle(call, env),
            CONSTRUCT_CRUSH => self.construct_crush(call, env),
            _ => self.scene.invoke(method, call, env),
        }
    }
}
fn factory_code(error: factory::Error) -> i32 {
    match error {
        factory::Error::Dispatch(crate::dispatch::Error::Method(code)) => code,
        _ => -1,
    }
}
fn spatial_code(error: spatial::Error) -> i32 {
    match error {
        spatial::Error::InvalidLimit => -1,
        spatial::Error::Limit => -3,
    }
}
