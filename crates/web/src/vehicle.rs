//! Terrain/vehicle session. Rendering reads canonical state without changing it.
use std::sync::Arc;
use tankracer_assets::terrain::TerrainData;
#[cfg(feature = "gpu-tests")]
use tankracer_game::pose;
use tankracer_game::{
    obstacle_collision::Invocation,
    player_input,
    scene_objects::collision::{Collision, Services, Sound},
    scenery,
    vehicle_runtime::Devices,
};
use tankracer_render::gpu::Renderer;
use tankracer_scene::camera::Camera;
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
    pub runtime: tankracer_game::session::Session,
    pub view: crate::vehicle_view::VehicleView,
    pub scenery: crate::scenery::View,
    loaded: scenery::Loaded,
    pub camera_keys: tankracer_game::chase_camera::Keys,
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
        let selection = tankracer_game::session::loading::Selection::from_archive(level, 1)
            .map_err(|e| JsValue::from_str(&e.to_string()))?;
        let mut prepared = tankracer_game::session::loading::Prepared::load(
            terrain,
            tankracer_game::session::loading::Files {
                data,
                vehicles: &[bronze],
                shared,
                level,
            },
            selection,
            true,
        )
        .map_err(|e| JsValue::from_str(&e.to_string()))?;
        #[cfg(feature = "gpu-tests")]
        let startup = &prepared.startup;
        let spawn = prepared.spawn;
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
        prepared.spawn = spawn;
        let loaded = prepared
            .finish()
            .map_err(|e| JsValue::from_str(&e.to_string()))?;
        let camera_config = loaded.camera;
        let mut game = loaded.world;
        let loaded = loaded.scenery.ok_or("Missing prepared scenery")?;
        let player = game.vehicles.entries[0].as_mut().unwrap();
        player.physics.motion.previous_position = spawn.position;
        player.physics.motion.velocity = velocity;
        #[cfg(feature = "gpu-tests")]
        if handbrake_probe {
            player.physics.powertrain.local_velocity = velocity;
        }
        let scenery = crate::scenery::View::load(renderer, &loaded, level, shared)?;
        let view = crate::vehicle_view::VehicleView::load(renderer, player, bronze, shared)?;
        let runtime = tankracer_game::session::Session::new(game, camera_config)
            .map_err(|e| JsValue::from_str(&e.to_string()))?;
        Ok(Self {
            runtime,
            view,
            scenery,
            loaded,
            camera_keys: Default::default(),
        })
    }
    pub fn chase(&self) -> &tankracer_game::chase_camera::State {
        self.runtime.camera()
    }
    pub fn camera(&self) -> Camera {
        Camera::from_chase(self.runtime.camera())
    }
    pub fn step_keys(&mut self, keys: player_input::Keys, dt: i32) -> Result<(), JsValue> {
        self.runtime
            .tick(
                tankracer_game::session::Input {
                    dt,
                    controls: tankracer_game::session::Controls::Keys(keys),
                    camera: self.camera_keys,
                },
                &mut SilentDevices(&self.loaded),
            )
            .map_err(|e| JsValue::from_str(&e.to_string()))
    }
    #[cfg(feature = "gpu-tests")]
    pub fn step(&mut self, input: tankracer_game::drive::Input, dt: i32) -> Result<(), JsValue> {
        self.runtime
            .tick(
                tankracer_game::session::Input {
                    dt,
                    controls: tankracer_game::session::Controls::Direct(input),
                    camera: self.camera_keys,
                },
                &mut SilentDevices(&self.loaded),
            )
            .map_err(|e| JsValue::from_str(&e.to_string()))
    }
}
