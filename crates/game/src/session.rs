//! Platform-independent fixed-update boundary. This owns only the existing
//! driving slice and original countdown/clock. Lap/lifecycle ownership follows.
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
    InvalidElapsed,
    AlreadyStarted,
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
    clock: Option<crate::race_clock::Clock>,
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
            clock: None,
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
    /// Start control for this runnable world. Complete actor-runtime readiness
    /// is separate; this does not promote the cold world to a runnable race.
    pub fn begin_race(&mut self) -> Result<(), Error> {
        if self.clock.is_some() || self.world.frame != 0 {
            return Err(Error::AlreadyStarted);
        }
        self.clock = Some(Default::default());
        self.world.race_active = true;
        for player in self.world.vehicles.entries.iter_mut().flatten() {
            player.race = Default::default();
        }
        Ok(())
    }
    pub fn clock(&self) -> Option<&crate::race_clock::Clock> {
        self.clock.as_ref()
    }
    pub fn player(&self) -> &Player {
        self.world.vehicles.entries[self.world.player.0]
            .as_ref()
            .expect("session owns its player")
    }

    pub fn tick(&mut self, mut input: Input, devices: &mut impl Devices) -> Result<(), Error> {
        let (driving, stage) = if let Some(clock) = &mut self.clock {
            if !(10..=1000).contains(&input.dt) {
                return Err(Error::InvalidElapsed);
            }
            // Original race timing precedes every entity, including Player's
            // input and Tank prefix. The accepted camera/physics order follows.
            clock.tick(
                input.dt,
                self.world
                    .vehicles
                    .entries
                    .iter_mut()
                    .flatten()
                    .map(|p| &mut p.race),
            );
            input.dt = crate::race_clock::physics_dt(input.dt);
            (
                self.world.vehicles.entries[self.world.player.0]
                    .as_ref()
                    .ok_or(Error::World(vehicle_runtime::Error::MissingPlayer))?
                    .race
                    .driving,
                clock.display,
            )
        } else {
            (true, 0)
        };
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
            Controls::Keys(keys) => self
                .world
                .step_keys(keys, input.dt, driving, stage, devices),
            Controls::Direct(controls) => self.world.step(controls, input.dt, driving, devices),
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
