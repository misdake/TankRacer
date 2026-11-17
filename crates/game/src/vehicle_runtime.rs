//! Resource-initialized single-player physics stage, driven by the common ticks.
//! Race rules/opponents are later stages; scene collisions are real bindings.
use crate::{
    attitude, body, collision_effects, contact, contact_pipeline, damage,
    dispatch::MethodId,
    drive,
    entity_factory::{self as factory, Methods},
    impact_particles,
    object_query::{Collider, EntityId, Tank, TankId},
    obstacle_collision as obstacle, player_brake, player_input, powertrain,
    sampling::Transform,
    scene_entity::Handle,
    scene_loading::{self, Registry},
    scene_objects, suspension, tank_collision, vehicle,
    vehicle_loading::{self, Request, Store},
    vehicle_prefix::{self as prefix, boost, spin},
    vehicle_scene::{chain, debris, pickup, shield, smoke, spray, temporary},
    velocity,
};
use std::sync::Arc;
use tankracer_assets::{
    config::{Part, Surfaces},
    terrain::TerrainData,
};

/// Device operations do not own the simulation's random stream.
pub trait Devices: scene_objects::collision::Services {
    fn surface_feedback(&mut self) -> Result<(), i32>;
    fn impact_feedback(&mut self, intensity: i32) -> Result<(), i32>;
}
#[derive(Clone, Copy)]
pub struct Settings {
    pub map: u32,
    pub mode: i32,
    pub special: bool,
    pub environment_flag: i32,
    pub seed: u32,
    pub spray_materials: spray::Materials,
    pub light_material: u32,
    pub hardware: bool,
}
#[derive(Debug)]
pub enum Error {
    Factory(factory::Error),
    Player(vehicle_loading::Error),
    Config(tankracer_assets::config::Error),
    MissingPlayer,
    UnsupportedConfiguration,
    Route(i32),
}
/// Shared visual and prefix state has one owner. The two shield tail words are
/// the gameplay effect-owner slots, not an additional independent flag bank.
pub struct Shared {
    pub random: u32,
    pub input: player_input::Shared,
    pub noise: [u16; 16],
    pub pool: crate::effect_pool::Pool,
    pub impacts: impact_particles::State,
    pub impact_vehicle: impact_particles::Vehicle,
    pub damage: damage::State,
    pub debris: debris::State,
    pub pickup: pickup::State,
    pub temporary: temporary::State,
    pub shield: shield::State,
    pub view: prefix::View,
    pub camera: boost::Camera,
    pub spin: spin::Shared,
    pub boost: boost::Shared,
    pub debris_sound: Option<u32>,
}
/// Terrain and surface configuration cannot be replaced after derived state
/// and scene bindings have been constructed.
/// ```compile_fail
/// fn replace(world: &mut tankracer_game::vehicle_runtime::SinglePlayer) {
///     world.terrain = std::sync::Arc::clone(world.terrain());
/// }
/// ```
/// ```compile_fail
/// fn replace(world: &mut tankracer_game::vehicle_runtime::SinglePlayer) {
///     world.surfaces = *world.surfaces();
/// }
/// ```
pub struct SinglePlayer {
    pub identity: factory::State<Handle>,
    pub registry: Registry,
    pub vehicles: Store,
    pub objects: scene_objects::Scene,
    terrain: Arc<TerrainData>,
    surfaces: Surfaces,
    pub shared: Shared,
    pub player: TankId,
    pub frame: u32,
    /// Global race-rule gate, distinct from the Player's driving/countdown flag.
    /// The current proxy starts in an active race; later race ownership sets it.
    pub race_active: bool,
    pub camera_position: [i32; 3],
    pub camera_forward: [i32; 3],
    settings: Settings,
    profile: Profile,
    tank: Tank,
    peer: Option<tank_collision::PeerSnapshot>,
}
/// Derived immutable configuration is built once, not rediscovered every tick.
struct Profile {
    contact: contact::Config,
    sampling: contact_pipeline::Environment,
    suspension: suspension::Environment,
    layout: [i32; 5],
    attitude: attitude::Config,
    drive: drive::Tuning,
    body: body::Tuning,
    spray: spray::Tuning,
    smoke: smoke::Tuning,
    pickup: pickup::Tuning,
    light_offset: [i32; 3],
    bumps: [contact::Coefficient; 10],
}
/// World ownership is established before placing the player. Authored readers
/// and plugins use this same factory, geometry store and RNG during bootstrap.
pub struct Startup {
    pub identity: factory::State<Handle>,
    pub registry: Registry,
    pub objects: scene_objects::Scene,
    terrain: Arc<TerrainData>,
    pub shared: Shared,
    settings: Settings,
}
mod execution;
mod startup;

impl SinglePlayer {
    pub fn step(
        &mut self,
        input: drive::Input,
        dt: i32,
        drive_active: bool,
        devices: &mut impl Devices,
    ) -> Result<(), Error> {
        self.advance(input, None, dt, drive_active, devices)
    }
    /// Authoritative Player input precedes its parent Tank tick, at the same
    /// scheduler position. Direct `step` remains the physics qualification API.
    pub fn step_keys(
        &mut self,
        keys: player_input::Keys,
        dt: i32,
        drive_active: bool,
        start_stage: i32,
        devices: &mut impl Devices,
    ) -> Result<(), Error> {
        self.advance(
            drive::Input::default(),
            Some((keys, start_stage)),
            dt,
            drive_active,
            devices,
        )
    }
}
