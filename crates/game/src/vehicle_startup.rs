//! Fresh player dynamics from authored spawn and assembled resources.
//! Identity and scheduling remain the scene factory's responsibility.
use crate::{
    body, contact_pipeline,
    dispatch::ClassId,
    math, motion,
    object_query::EntityId,
    pose, powertrain, vehicle,
    vehicle_model::VehicleModel,
    vehicle_prefix,
    vehicle_scene::{debris, lights, marks, smoke, spray},
};

#[derive(Debug, Clone, Copy)]
pub struct Environment {
    pub ambient_wobble: bool,
    pub session_active: bool,
    pub ambient_phase: u32,
    pub gun_pivot: [i32; 3],
}
/// One owner for vehicle dynamics, prefix and assembled model. Immutable model
/// metadata initializes contacts/hull; subsequent hull vertices belong to motion.
pub struct Player {
    pub physics: vehicle::State,
    pub input: crate::player_input::State,
    pub brake: crate::player_brake::State,
    pub route: crate::player_route::State,
    pub race: crate::race_clock::Racer,
    pub turret: crate::turret::State,
    pub prefix: vehicle_prefix::State,
    pub model: VehicleModel,
    pub lights: lights::State,
    pub marks: marks::State,
    pub spray: spray::State,
    pub smoke: smoke::State,
    pub debris: debris::Vehicle,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    ShadowGeometry,
    Pose(math::Error),
}
impl Player {
    /// Tank initialization -> authored transform -> model-load pose -> player
    /// constructor pose. The two pose calls are observable when wobble is on.
    pub fn construct(
        entity: EntityId,
        class: ClassId,
        model: VehicleModel,
        spawn: pose::Pose,
        environment: Environment,
    ) -> Result<Self, Error> {
        let pose = model.pose_config(environment.gun_pivot);
        let mut motion = motion::State::default();
        motion.poses.physical_position = spawn.position;
        motion.poses.base_position = spawn.position;
        motion.poses.reference = spawn.matrix;
        motion.poses.body.matrix = spawn.matrix;
        motion.hull = body::Hull {
            vertices: model
                .shadow
                .geometry()
                .vertices
                .get(..4)
                .ok_or(Error::ShadowGeometry)?
                .try_into()
                .map_err(|_| Error::ShadowGeometry)?,
            radius: model.shadow.radius,
            ..body::Hull::default()
        };
        let mut contact = contact_pipeline::Pipeline::default();
        for (i, point) in contact.sampling.primary.points.iter_mut().enumerate() {
            point.local = model.primary_local[i];
            contact.response.points[i].corner = i as u32;
        }
        let pose_input = motion::PoseInput {
            timer: 0,
            ambient_wobble: environment.ambient_wobble,
            session_active: environment.session_active,
            ambient_phase: environment.ambient_phase,
            recoil_mode: 0,
            turret_yaw: 0,
            external: pose::ExternalBasis::Body,
        };
        let pose_environment = pose_input.environment(&motion.articulation);
        for _ in 0..2 {
            motion
                .poses
                .update(&pose, motion.travel, &pose_environment)
                .map_err(Error::Pose)?;
        }
        let prefix = vehicle_prefix::State::from_model(&model);
        let input = crate::player_input::State {
            drive: crate::drive::Input {
                alternate_lateral: model.alternate_control[0],
                alternate_rotation: model.alternate_control[1],
                ..Default::default()
            },
            ..Default::default()
        };
        Ok(Self {
            physics: vehicle::State {
                entity,
                class,
                player: true,
                motion,
                powertrain: powertrain::State::default(),
                contact,
                pose,
                pose_input,
                yaw_lock: 0,
                collision: vehicle::CollisionState::default(),
                blast: crate::blast::State {
                    angular_enabled: true,
                    ..Default::default()
                },
            },
            prefix,
            input,
            brake: crate::player_brake::State::default(),
            route: crate::player_route::State::default(),
            race: crate::race_clock::Racer::default(),
            turret: Default::default(),
            model,
            lights: lights::State::default(),
            marks: marks::State::default(),
            spray: spray::State::default(),
            smoke: smoke::State::default(),
            debris: debris::Vehicle::default(),
        })
    }
}
