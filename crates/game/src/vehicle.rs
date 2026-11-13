//! Active vehicle physics, after the timer/ability prefix and before game scheduling.
use crate::{
    body, collision_effects, contact_pipeline,
    dispatch::ClassId,
    geometry::approximate_distance,
    ground_collision,
    math::Matrix,
    motion,
    object_query::{EntityId, Tank, TankId},
    obstacle_collision as obstacle, pose, powertrain, tank_collision,
};

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct CollisionState {
    pub disabled: bool,
    pub timer: i32,
    pub impact_accumulator: i32,
    pub collision_size: u32,
    pub material: i32,
    pub deformable: Option<EntityId>,
    pub obstacle_impulse: i32,
    pub tank_delta: [i32; 3],
    pub ground_delta: [i32; 3],
}
#[derive(Debug, Clone)]
pub struct State {
    pub entity: EntityId,
    pub class: ClassId,
    pub player: bool,
    pub motion: motion::State,
    pub powertrain: powertrain::State,
    pub contact: contact_pipeline::Pipeline,
    pub pose: pose::Config,
    pub pose_input: motion::PoseInput,
    pub yaw_lock: i32,
    pub collision: CollisionState,
}
pub struct Config<'a> {
    pub contact: contact_pipeline::Config<'a>,
    pub powertrain: powertrain::Config<'a>,
    pub attitude: &'a crate::attitude::Config,
    pub body: &'a body::Tuning,
    pub obstacles: obstacle::Environment,
    pub separation_limit: u32,
}
pub struct Frame {
    pub dt: i32,
    pub number: u32,
    pub mode: i32,
    pub input: crate::drive::Input,
    pub drive: crate::drive::Environment,
}
pub struct World<'a, 'mesh> {
    pub terrain: &'a contact_pipeline::World<'a>,
    pub objects: &'a mut obstacle::World,
    /// Resource bounds/slot metadata persist; poses/contacts are refreshed from
    /// the corresponding vehicle before queries and after each completed stage.
    pub tanks: &'a mut [Tank],
    pub slots: &'a [Option<TankId>; 9],
    pub peer: &'a mut Option<tank_collision::PeerSnapshot>,
    pub shared: &'a mut obstacle::Shared,
    pub effects: collision_effects::Bank<'a, 'mesh>,
    pub player_effects: &'a mut [u32],
}
pub struct Resources<'a, 'geometry> {
    pub gun: Option<body::GunMesh<'a>>,
    pub deformable: Option<&'a mut contact_pipeline::Deformable<'geometry>>,
}
/// Current simulation inputs for scene effects, after pose but before display
/// rescaling. Effects can update the impact accumulator without a second copy,
/// and read the newly sampled surface/traction data rather than stale caches.
pub struct AfterPose<'a> {
    pub entity: EntityId,
    pub class: ClassId,
    pub player: bool,
    pub motion: &'a mut motion::State,
    pub collision: &'a mut CollisionState,
    pub contact: &'a contact_pipeline::Pipeline,
    pub powertrain: &'a powertrain::State,
    pub pose_input: &'a motion::PoseInput,
    pub frame: &'a Frame,
}
/// These are scene-owned operations, with no default or deferred substitute.
/// Collision response, RNG, particles and damage execute in this module's flow.
pub trait Services: collision_effects::ObjectServices {
    fn contact_feedback(&mut self, vehicle: &mut State, random: &mut u32) -> Result<(), i32>;
    fn after_pose(
        &mut self,
        vehicle: AfterPose<'_>,
        effects: &mut collision_effects::Bank<'_, '_>,
        random: &mut u32,
    ) -> Result<(), i32>;
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Error {
    InvalidWorld,
    MissingTank(TankId),
    InactiveVehicle,
    InvalidTimeStep,
    Contact(contact_pipeline::Error),
    ContactFeedback(i32),
    Powertrain(powertrain::Error),
    Obstacle(obstacle::Error),
    Tank(tank_collision::Error),
    Ground(ground_collision::Error),
    Motion(motion::Error),
}
impl Error {
    pub fn status(&self) -> i32 {
        use crate::{dispatch, object_query, terrain};
        let query = |e: &object_query::Error| match e {
            object_query::Error::Arithmetic => -2,
            object_query::Error::Limit => -3,
            _ => -1,
        };
        match self {
            Self::InvalidWorld => -1,
            Self::MissingTank(_) | Self::InvalidTimeStep => -2,
            Self::InactiveVehicle => -5,
            Self::Contact(contact_pipeline::Error::Terrain(terrain::Error::Arithmetic)) => -2,
            Self::Contact(_) => -1,
            Self::ContactFeedback(c) => *c,
            Self::Powertrain(e) => e.status(),
            Self::Obstacle(obstacle::Error::Arithmetic(_)) => -2,
            Self::Obstacle(obstacle::Error::Query(e))
            | Self::Tank(tank_collision::Error::Query(e)) => query(e),
            Self::Obstacle(
                obstacle::Error::Feedback(c)
                | obstacle::Error::Impact(c)
                | obstacle::Error::Dispatch(dispatch::Error::Method(c)),
            ) => *c,
            Self::Obstacle(_) => -1,
            Self::Tank(tank_collision::Error::Limit) => -3,
            Self::Tank(tank_collision::Error::Effect(c)) => *c,
            Self::Tank(_) => -1,
            Self::Ground(ground_collision::Error::Arithmetic(_)) => -2,
            Self::Ground(ground_collision::Error::Effect(c)) => *c,
            Self::Motion(e) => e.status(),
        }
    }
}
fn external(state: &State) -> Option<Matrix> {
    resolve_external(state.pose_input.external, state.motion.poses.reference)
}
fn resolve_external(binding: pose::ExternalBasis, reference: Matrix) -> Option<Matrix> {
    match binding {
        pose::ExternalBasis::None => None,
        pose::ExternalBasis::Body => Some(reference),
        pose::ExternalBasis::Custom(m) => Some(m),
    }
}
fn refresh(index: usize, state: &State, world: &mut World<'_, '_>) {
    let geometry = &mut world.tanks[index];
    geometry.collider.entity = state.entity;
    geometry.collider.pose = pose::Pose {
        position: state.motion.poses.base_position,
        matrix: state.motion.poses.reference,
    };
    geometry.collider.external_basis = external(state);
    geometry.contacts = state.motion.contacts.map(|p| p.world);
    geometry.disabled = state.collision.disabled;
    geometry.ready = state.motion.ready;
    let effect = &mut world.effects.vehicles[index];
    effect.entity = state.entity;
    effect.contacts = geometry.contacts;
    effect.forward_velocity = state.powertrain.local_velocity[2];
    effect.external_basis = geometry.collider.external_basis;
    world.effects.corners[index] = state.contact.response.points.map(|p| p.corner);
    let damage = &mut world.effects.damage[index];
    damage.player = state.player;
    damage.body_pose = state.motion.poses.body;
}
fn obstacle_state(s: &State) -> obstacle::State {
    obstacle::State {
        entity: s.entity,
        class: s.class,
        base_position: s.motion.poses.base_position,
        position: s.motion.poses.physical_position,
        previous_position: s.motion.previous_position,
        velocity: s.motion.velocity,
        forward_velocity: s.powertrain.local_velocity[2],
        player: s.player,
        contacts: s.motion.contacts.map(|p| p.world),
        impact_accumulator: s.collision.impact_accumulator,
        collision_size: s.collision.collision_size,
        material_tag: s.collision.material,
        deformable: s.collision.deformable,
    }
}
fn publish_obstacle(s: &mut State, view: obstacle::State) {
    s.entity = view.entity;
    s.class = view.class;
    s.player = view.player;
    s.motion.poses.base_position = view.base_position;
    s.motion.poses.physical_position = view.position;
    s.motion.previous_position = view.previous_position;
    s.motion.velocity = view.velocity;
    s.powertrain.local_velocity[2] = view.forward_velocity;
    for (contact, point) in s.motion.contacts.iter_mut().zip(view.contacts) {
        contact.world = point;
    }
    s.collision.impact_accumulator = view.impact_accumulator;
    s.collision.collision_size = view.collision_size;
    s.collision.material = view.material_tag;
    s.collision.deformable = view.deformable;
}
fn tank_state(s: &State) -> tank_collision::State {
    tank_collision::State {
        velocity: s.motion.velocity,
        local_velocity: s.powertrain.local_velocity,
        previous_position: s.motion.previous_position,
        collision_timer: s.collision.timer,
        player: s.player,
    }
}
fn publish_tank(s: &mut State, view: tank_collision::State) {
    s.motion.velocity = view.velocity;
    s.powertrain.local_velocity = view.local_velocity;
    s.motion.previous_position = view.previous_position;
    s.collision.timer = view.collision_timer;
    s.player = view.player;
}
fn blend_pair(velocity: &mut [i32; 3], before: [i32; 2]) {
    for (i, old) in [0, 2].into_iter().zip(before) {
        velocity[i] = old.wrapping_add(velocity[i].wrapping_sub(old).wrapping_mul(624) >> 10);
    }
}
fn scene_error(code: i32) -> i32 {
    if code < 0 { code } else { -8 }
}

/// Run contact -> powertrain -> obstacle -> tank -> deformable -> ground ->
/// motion, retaining every stage's original error prefix. Peers are writable
/// canonical vehicles: a later peer update cannot restore a stale pre-hit speed.
pub fn step_physics(
    vehicles: &mut [State],
    own: TankId,
    world: &mut World<'_, '_>,
    config: &Config<'_>,
    frame: &Frame,
    resources: Resources<'_, '_>,
    services: &mut impl Services,
) -> Result<(), Error> {
    let n = vehicles.len();
    if own.0 >= n {
        return Err(Error::MissingTank(own));
    }
    if n > 9
        || world.tanks.len() != n
        || world.effects.vehicles.len() != n
        || world.effects.damage.len() != n
        || world.effects.corners.len() != n
    {
        return Err(Error::InvalidWorld);
    }
    if frame.dt <= 0 {
        return Err(Error::InvalidTimeStep);
    }
    if !world.tanks[own.0].active {
        return Err(Error::InactiveVehicle);
    }
    world.effects.frame = frame.number;
    for (i, vehicle) in vehicles.iter().enumerate() {
        refresh(i, vehicle, world);
    }
    let result = run(vehicles, own, world, config, frame, resources, services);
    // Even failed stages can alter a peer's velocity, own contact normal or
    // pose prefix. Refresh views after publishing, without rolling any back.
    for (i, vehicle) in vehicles.iter().enumerate() {
        refresh(i, vehicle, world);
    }
    result
}
fn run(
    vehicles: &mut [State],
    own: TankId,
    world: &mut World<'_, '_>,
    config: &Config<'_>,
    frame: &Frame,
    resources: Resources<'_, '_>,
    services: &mut impl Services,
) -> Result<(), Error> {
    let state = &mut vehicles[own.0];
    let mut sampling = *config.contact.sampling;
    sampling.dt = frame.dt;
    sampling.frame = frame.number;
    sampling.player = u32::from(state.player);
    let feedback = state
        .contact
        .step_vehicle(
            contact_pipeline::Vehicle {
                motion: &mut state.motion,
                powertrain: &mut state.powertrain,
                pose: &mut state.pose,
            },
            world.terrain,
            &contact_pipeline::Config {
                sampling: &sampling,
                suspension: config.contact.suspension,
                contact: config.contact.contact,
                layout: config.contact.layout,
            },
            &mut world.shared.random,
            resources.deformable,
            |slot| {
                if let Some(value) = world.player_effects.get_mut(usize::from(slot)) {
                    *value = 0;
                    true
                } else {
                    false
                }
            },
        )
        .map_err(Error::Contact)?;
    if feedback {
        services
            .contact_feedback(state, &mut world.shared.random)
            .map_err(|c| Error::ContactFeedback(scene_error(c)))?;
    }
    let mut drive = frame.drive;
    drive.dt = frame.dt;
    drive.mode = frame.mode;
    drive.player = state.player;
    drive.materials = state.contact.sampling.materials.map(|m| m as usize);
    drive.kinds = state
        .contact
        .sampling
        .primary
        .points
        .each_ref()
        .map(|p| p.kind);
    drive.contact_y = state.motion.contacts.map(|p| p.world[1]);
    drive.airborne = state.contact.sampling.primary.airborne_count as i8;
    state
        .powertrain
        .step(
            &mut state.motion,
            &config.powertrain,
            &powertrain::Frame {
                input: &frame.input,
                drive: &drive,
                vertical_delta: state.contact.response.vertical_delta,
                yaw_lock: state.yaw_lock,
            },
        )
        .map_err(Error::Powertrain)?;
    refresh(own.0, state, world);
    let mut obstacle_env = config.obstacles;
    obstacle_env.mode = frame.mode;
    obstacle_env.dt = frame.dt;
    let before = state.motion.velocity;
    let mut view = obstacle_state(state);
    let result = obstacle::obstacles(
        world.objects,
        &mut view,
        obstacle_env,
        world.shared,
        &mut collision_effects::ObjectEffects {
            bank: &mut world.effects,
            services,
            own,
        },
    );
    publish_obstacle(state, view);
    result.map_err(Error::Obstacle)?;
    state.collision.obstacle_impulse = approximate_distance(std::array::from_fn(|i| {
        state.motion.velocity[i].wrapping_sub(before[i])
    }));
    let before = state.motion.velocity;
    if !state.collision.disabled {
        for (i, vehicle) in vehicles.iter().enumerate() {
            refresh(i, vehicle, world);
        }
        // Stack-only temporary SoA, bounded by the game's nine tank slots.
        let empty = tank_state(&vehicles[own.0]);
        let mut states = [empty; 9];
        for (dst, src) in states.iter_mut().zip(vehicles.iter()) {
            *dst = tank_state(src);
        }
        if frame.mode == 4 || frame.mode == 5 {
            *world.peer = None;
        }
        let result = tank_collision::step(
            own,
            world.tanks,
            &mut states[..vehicles.len()],
            world.slots,
            tank_collision::Environment {
                mode: frame.mode,
                dt: frame.dt,
                separation_limit: config.separation_limit,
            },
            world.peer,
            &mut collision_effects::TankEffects {
                bank: &mut world.effects,
                random: &mut world.shared.random,
                own,
            },
        );
        if result.is_ok() && (frame.mode == 4 || frame.mode == 5) {
            blend_pair(&mut states[own.0].velocity, [before[0], before[2]]);
            if let Some(peer) = *world.peer {
                blend_pair(&mut states[peer.tank.0].velocity, peer.velocity_xz);
            }
        }
        for (dst, src) in vehicles.iter_mut().zip(states) {
            publish_tank(dst, src);
        }
        result.map_err(Error::Tank)?;
    }
    let state = &mut vehicles[own.0];
    state.collision.tank_delta =
        std::array::from_fn(|i| state.motion.velocity[i].wrapping_sub(before[i]));
    refresh(own.0, state, world);
    let mut view = obstacle_state(state);
    let result = obstacle::deformables(
        world.objects,
        &mut view,
        obstacle_env,
        world.shared,
        &mut collision_effects::ObjectEffects {
            bank: &mut world.effects,
            services,
            own,
        },
    );
    publish_obstacle(state, view);
    result.map_err(Error::Obstacle)?;
    refresh(own.0, state, world);
    let before = state.motion.velocity;
    let mut ground = ground_collision::State {
        velocity: before,
        yaw: state.motion.angular[1],
    };
    let mut contacts = std::array::from_fn(|i| ground_collision::Contact {
        kind: state.contact.sampling.primary.points[i].kind as i16,
        corner: state.contact.response.points[i].corner as i32,
        normal: state.contact.sampling.primary.points[i].normal,
    });
    let result = ground.step(
        &mut contacts,
        ground_collision::Environment {
            mode: frame.mode,
            player: state.player,
            forward_velocity: state.powertrain.local_velocity[2],
            position: state.motion.poses.base_position,
            external_basis: external(state),
        },
        &mut collision_effects::TankEffects {
            bank: &mut world.effects,
            random: &mut world.shared.random,
            own,
        },
    );
    state.motion.velocity = ground.velocity;
    state.motion.angular[1] = ground.yaw;
    for (dst, src) in state
        .contact
        .sampling
        .primary
        .points
        .iter_mut()
        .zip(contacts)
    {
        dst.normal = src.normal;
    }
    result.map_err(Error::Ground)?;
    state.collision.ground_delta =
        std::array::from_fn(|i| state.motion.velocity[i].wrapping_sub(before[i]));
    let entity = state.entity;
    let class = state.class;
    let binding = state.pose_input.external;
    let player = state.player;
    let forward = state.powertrain.local_velocity[2];
    state
        .motion
        .finish(
            &motion::Config {
                attitude: config.attitude,
                body: config.body,
                pose: &state.pose,
            },
            &motion::Frame {
                dt: frame.dt,
                number: frame.number,
                player: state.player,
                mode: frame.mode,
                pose: state.pose_input,
            },
            resources.gun,
            |motion| {
                // Body-relative offsets follow the NEW pose reference, not
                // the collision view built before attitude/pose processing.
                let view = &mut world.effects.vehicles[own.0];
                view.entity = entity;
                view.contacts = motion.contacts.map(|p| p.world);
                view.forward_velocity = forward;
                view.external_basis = resolve_external(binding, motion.poses.reference);
                world.effects.damage[own.0].player = player;
                world.effects.damage[own.0].body_pose = motion.poses.body;
                services.after_pose(
                    AfterPose {
                        entity,
                        class,
                        player,
                        motion,
                        collision: &mut state.collision,
                        contact: &state.contact,
                        powertrain: &state.powertrain,
                        pose_input: &state.pose_input,
                        frame,
                    },
                    &mut world.effects,
                    &mut world.shared.random,
                )
            },
        )
        .map_err(Error::Motion)
}
