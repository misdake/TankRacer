//! Platform-independent fixed-update boundary. This owns only the existing
//! driving slice; race lifecycle and future game features are not implemented here.
use crate::{
    chase_camera, drive, player_input,
    vehicle_animation::Tracks,
    vehicle_runtime::{self, Devices, SinglePlayer},
    vehicle_startup::Player,
};
use tankracer_assets::{config::Part, object_catalog::Camera};
pub mod loading;

#[derive(Debug)]
pub enum Error {
    Camera(crate::math::Error),
    World(vehicle_runtime::Error),
}
impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "Session update: {self:?}")
    }
}
impl std::error::Error for Error {}

/// Direct controls remain available for physics qualification. Both routes use
/// the same camera/world/animation/feedback order.
pub enum Controls {
    Keys(player_input::Keys),
    Direct(drive::Input),
}
pub struct Input {
    pub dt: i32,
    pub controls: Controls,
    pub camera: chase_camera::Keys,
}

pub struct Session {
    world: SinglePlayer,
    camera: chase_camera::State,
    camera_config: Camera,
    tracks: Tracks,
}
impl Session {
    pub fn new(world: SinglePlayer, camera_config: Camera) -> Result<Self, Error> {
        let player = world.vehicles.entries[world.player.0]
            .as_ref()
            .ok_or(Error::World(vehicle_runtime::Error::MissingPlayer))?;
        let camera =
            chase_camera::State::new(&camera_config, player.into()).map_err(Error::Camera)?;
        Ok(Self {
            world,
            camera,
            camera_config,
            tracks: Tracks::default(),
        })
    }
    pub fn world(&self) -> &SinglePlayer {
        &self.world
    }
    pub fn camera(&self) -> &chase_camera::State {
        &self.camera
    }
    pub fn tracks(&self) -> Tracks {
        self.tracks
    }
    pub fn player(&self) -> &Player {
        self.world.vehicles.entries[self.world.player.0]
            .as_ref()
            .expect("session owns its player")
    }

    pub fn tick(&mut self, input: Input, devices: &mut impl Devices) -> Result<(), Error> {
        self.camera
            .tick(
                &self.camera_config,
                self.player().into(),
                chase_camera::Frame {
                    dt: input.dt,
                    keys: input.camera,
                    hold_displacement: self.player().prefix.boost.active != 0,
                    ..Default::default()
                },
            )
            .map_err(Error::Camera)?;
        self.world.camera_position = self.camera.published_position();
        self.world.camera_forward = self.camera.forward();
        match input.controls {
            Controls::Keys(keys) => self.world.step_keys(keys, input.dt, true, 0, devices),
            Controls::Direct(controls) => self.world.step(controls, input.dt, true, devices),
        }
        .map_err(Error::World)?;
        let player = self.player();
        let (speed, yaw, locked, offset) = (
            player.physics.powertrain.track_speed,
            player.physics.motion.angular[1],
            player.brake.reverse_braking,
            player.model.offsets[Part::Turret.slot()],
        );
        self.tracks.advance(speed, yaw, locked);
        self.camera.feedback(offset);
        Ok(())
    }
}
