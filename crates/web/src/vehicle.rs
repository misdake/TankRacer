//! Terrain/vehicle session. Rendering reads canonical state without changing it.
use glam::Vec3;
use std::sync::Arc;
use tankracer_assets::{
    archive::Archive, config::ResolveContext, level::Level, terrain::TerrainData,
};
use tankracer_game::{
    obstacle_collision::Invocation,
    player_input, pose,
    scene_objects::collision::{Collision, Services, Sound},
    scenery,
    vehicle_loading::Request,
    vehicle_model::BuildContext,
    vehicle_resources,
    vehicle_runtime::{Devices, Settings, SinglePlayer, Startup},
    vehicle_scene::spray,
    vehicle_startup::Environment,
};
use tankracer_render::gpu::Renderer;
use tankracer_scene::{
    camera::Camera,
    transform::{physics_pose, physics_position},
};
use wasm_bindgen::JsValue;

struct SilentDevices<'a>(&'a scenery::Loaded);
impl Services for SilentDevices<'_> {
    fn auxiliary_model(
        &mut self,
        name: &str,
    ) -> Result<Option<Arc<tankracer_assets::model::Model>>, i32> {
        Ok(self.0.auxiliary_model(name))
    }
    fn sound(&mut self, _: Sound, _: &mut Collision<'_>, _: Invocation<'_>) -> Result<(), i32> {
        Ok(())
    }
    fn feedback(&mut self, _: i32, _: &mut Collision<'_>, _: Invocation<'_>) -> Result<(), i32> {
        Ok(())
    }
}
impl Devices for SilentDevices<'_> {
    fn surface_feedback(&mut self) -> Result<(), i32> {
        Ok(())
    }
    fn impact_feedback(&mut self, _: i32) -> Result<(), i32> {
        Ok(())
    }
}
pub struct Session {
    pub game: SinglePlayer,
    pub view: crate::vehicle_view::VehicleView,
    pub scenery: crate::scenery::View,
    loaded: scenery::Loaded,
}
pub struct Files<'a> {
    pub data: &'a [u8],
    pub bronze: &'a [u8],
    pub shared: &'a [u8],
    pub level: &'a [u8],
}
#[derive(Default)]
pub enum Start {
    #[default]
    Authored,
    #[cfg(feature = "gpu-tests")]
    Collision(u32),
    #[cfg(feature = "gpu-tests")]
    Handbrake,
}
impl Session {
    pub fn load(
        renderer: &Renderer,
        terrain: Arc<TerrainData>,
        files: Files<'_>,
        start: Start,
    ) -> Result<Self, JsValue> {
        let Files {
            data,
            bronze,
            shared,
            level,
        } = files;
        let archive = Archive::parse(level).map_err(|e| JsValue::from_str(&e.to_string()))?;
        let maps = [
            "ENGLAND", "MEXICO", "SWAMP", "EAST", "THEME", "BIO", "MONACO", "CIRCUIT",
        ];
        let (map, name) = maps
            .into_iter()
            .enumerate()
            .find(|(_, name)| archive.get(&format!("{}B.DAT", &name[..3])).is_some())
            .ok_or("Unknown level archive")?;
        let cup = if map == 7 {
            2
        } else if [3, 5, 6].contains(&map) {
            1
        } else {
            0
        };
        let resources = vehicle_resources::prepare(
            data,
            &[bronze],
            shared,
            1,
            BuildContext {
                resolve: ResolveContext {
                    player: true,
                    map: map as u32,
                    ..ResolveContext::default()
                },
                ambient_wobble: false,
                session_active: false,
            },
        )
        .map_err(|e| JsValue::from_str(&e.to_string()))?;
        let mut startup = Startup::new(
            terrain,
            Settings {
                map: map as u32,
                mode: 0,
                special: false,
                environment_flag: 0,
                seed: 0,
                spray_materials: spray::Materials {
                    filth: 1,
                    crap: 2,
                    splash: 3,
                },
                light_material: 4,
                hardware: true,
            },
        )
        .map_err(|e| JsValue::from_str(&format!("Vehicle startup: {e:?}")))?;
        let loaded = scenery::load(&mut startup, data, level, shared, name, cup)
            .map_err(|e| JsValue::from_str(&e.to_string()))?;
        let script_name = format!("{}{}.DAT", &name[..3], ["B", "S", "G"][cup]);
        let script = Level::decode(archive.get(&script_name).ok_or("Missing player level")?)
            .map_err(|e| JsValue::from_str(&e.to_string()))?;
        let spawn = script
            .objects
            .iter()
            .find(|o| o.class == "Player")
            .map(pose::Pose::from_level)
            .unwrap_or_default();
        #[cfg(feature = "gpu-tests")]
        let handbrake_probe = matches!(&start, Start::Handbrake);
        let (spawn, velocity) = match start {
            Start::Authored => (spawn, [0; 3]),
            #[cfg(feature = "gpu-tests")]
            Start::Handbrake => (spawn, [20000, 0, 500000]),
            #[cfg(feature = "gpu-tests")]
            Start::Collision(target) => {
                if target > 9 {
                    return Err("Unknown scenery collision probe".into());
                }
                let class = if target >= 8 {
                    startup.registry.mover
                } else if target >= 6 {
                    startup.registry.spin
                } else if target >= 4 {
                    startup.registry.closer
                } else if target == 3 {
                    startup.registry.bounce
                } else if target == 2 {
                    startup.registry.crush
                } else {
                    startup.registry.fall
                };
                let index = startup
                    .objects
                    .world
                    .objects
                    .iter()
                    .enumerate()
                    .filter(|(i, p)| {
                        p.class == class
                            && startup.objects.components.objects[*i]
                                .as_ref()
                                .is_some_and(|o| o.data.visibility.entered)
                    })
                    .nth(usize::from(target == 1))
                    .ok_or("Missing collision probe target")?
                    .0;
                let mut position = startup.objects.world.colliders[index].pose.position;
                position[1] += 1500;
                position[2] -= 500;
                let velocity = if target == 4 {
                    position[0] += 38000;
                    position[2] += 38500;
                    [0; 3]
                } else if target == 7 {
                    position[0] -= 500;
                    position[2] += 500;
                    [600001, 0, 0]
                } else if target == 8 {
                    position[2] -= 37500;
                    [0; 3]
                } else {
                    [0, 0, 600001]
                };
                (
                    pose::Pose {
                        position,
                        ..Default::default()
                    },
                    velocity,
                )
            }
        };
        let mut game = startup
            .finish(
                resources.surfaces,
                Request {
                    model: resources.model,
                    spawn,
                    static_allocation: true,
                    environment: Environment {
                        ambient_wobble: false,
                        session_active: false,
                        ambient_phase: 0,
                        gun_pivot: [0; 3],
                    },
                },
            )
            .map_err(|e| JsValue::from_str(&format!("Vehicle startup: {e:?}")))?;
        let player = game.vehicles.entries[0].as_mut().unwrap();
        player.physics.motion.previous_position = spawn.position;
        player.physics.motion.velocity = velocity;
        #[cfg(feature = "gpu-tests")]
        if handbrake_probe {
            player.physics.powertrain.local_velocity = velocity;
        }
        let scenery = crate::scenery::View::load(renderer, &loaded, level, shared)?;
        let view = crate::vehicle_view::VehicleView::load(renderer, player, bronze, shared)?;
        Ok(Self {
            game,
            view,
            scenery,
            loaded,
        })
    }
    pub fn camera(&self) -> Camera {
        let body = self.game.vehicles.entries[0]
            .as_ref()
            .unwrap()
            .physics
            .motion
            .poses
            .body;
        let transform = physics_pose(body.position, body.matrix.0);
        let position = physics_position(body.position);
        let forward = -transform.z_axis.truncate().normalize_or_zero();
        Camera::looking_at(
            position - forward * 22. + Vec3::Y * 10.,
            position + Vec3::Y * 2.,
        )
    }
    fn prepare_camera(&mut self, camera: &Camera) {
        let p = camera.position * 128.;
        let f = camera.forward() * 4096.;
        self.game.camera_position = [p.x as i32, p.y as i32, -p.z as i32];
        self.game.camera_forward = [f.x as i32, f.y as i32, -f.z as i32];
    }
    pub fn step_keys(
        &mut self,
        keys: player_input::Keys,
        dt: i32,
        camera: &Camera,
    ) -> Result<(), JsValue> {
        self.prepare_camera(camera);
        self.game
            .step_keys(keys, dt, true, 0, &mut SilentDevices(&self.loaded))
            .map_err(|e| JsValue::from_str(&format!("Player input update: {e:?}")))?;
        self.view.advance_tracks(
            self.game.vehicles.entries[self.game.player.0]
                .as_ref()
                .unwrap(),
        );
        Ok(())
    }
    #[cfg(feature = "gpu-tests")]
    pub fn step(
        &mut self,
        input: tankracer_game::drive::Input,
        dt: i32,
        camera: &Camera,
    ) -> Result<(), JsValue> {
        self.prepare_camera(camera);
        self.game
            .step(input, dt, true, &mut SilentDevices(&self.loaded))
            .map_err(|e| JsValue::from_str(&format!("Vehicle update: {e:?}")))?;
        self.view.advance_tracks(
            self.game.vehicles.entries[self.game.player.0]
                .as_ref()
                .unwrap(),
        );
        Ok(())
    }
}
