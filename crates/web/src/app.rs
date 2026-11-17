use tankracer_render::gpu::Renderer;
use wasm_bindgen::prelude::*;
use web_sys::HtmlCanvasElement;

#[wasm_bindgen]
pub struct App {
    pub(crate) renderer: Renderer,
    canvas: HtmlCanvasElement,
    world: Option<crate::world::WorldView>,
    vehicle: Option<crate::vehicle::Session>,
    follow_camera: bool,
    hud: Option<crate::hud::Hud>,
    #[cfg(feature = "gpu-tests")]
    hud_enabled: bool,
}

#[wasm_bindgen]
impl App {
    #[wasm_bindgen(constructor)]
    pub fn new(canvas: HtmlCanvasElement) -> Result<Self, JsValue> {
        let renderer = Renderer::new(&canvas)?;
        Ok(Self {
            renderer,
            canvas,
            world: None,
            vehicle: None,
            follow_camera: true,
            hud: None,
            #[cfg(feature = "gpu-tests")]
            hud_enabled: true,
        })
    }

    pub fn render(&mut self) -> Result<(), JsValue> {
        let size = [self.canvas.width(), self.canvas.height()];
        if size.contains(&0) {
            return Ok(());
        }
        if let Some(vehicle) = &mut self.vehicle {
            vehicle.view.scene.cockpit = self.follow_camera && vehicle.chase().mode == 2;
        }
        if let Some(world) = &mut self.world {
            let show_hud = self.follow_camera;
            #[cfg(feature = "gpu-tests")]
            let show_hud = show_hud && self.hud_enabled;
            return world.draw(
                &mut self.renderer,
                size,
                self.vehicle.as_mut(),
                if show_hud { self.hud.as_mut() } else { None },
            );
        }
        self.renderer.begin_frame(None, size, [0., 0., 0., 1.]);
        Ok(())
    }

    pub fn load_world(&mut self, bytes: &[u8]) -> Result<String, JsValue> {
        let world = crate::world::WorldView::load(&self.renderer, bytes)?;
        let stats = world.stats();
        self.world = Some(world);
        self.vehicle = None;
        self.hud = None;
        Ok(stats)
    }

    pub fn load_player(
        &mut self,
        data: &[u8],
        bronze: &[u8],
        shared: &[u8],
        level: &[u8],
    ) -> Result<(), JsValue> {
        self.install_player(
            crate::vehicle::Files {
                data,
                bronze,
                shared,
                level,
            },
            crate::vehicle::Start::Authored,
        )
    }

    #[cfg(feature = "gpu-tests")]
    pub fn load_collision(
        &mut self,
        data: &[u8],
        bronze: &[u8],
        shared: &[u8],
        level: &[u8],
        target: u32,
    ) -> Result<(), JsValue> {
        self.install_player(
            crate::vehicle::Files {
                data,
                bronze,
                shared,
                level,
            },
            crate::vehicle::Start::Collision(target),
        )
    }

    #[cfg(feature = "gpu-tests")]
    pub fn load_handbrake(
        &mut self,
        data: &[u8],
        bronze: &[u8],
        shared: &[u8],
        level: &[u8],
    ) -> Result<(), JsValue> {
        self.install_player(
            crate::vehicle::Files {
                data,
                bronze,
                shared,
                level,
            },
            crate::vehicle::Start::Handbrake,
        )
    }

    fn install_player(
        &mut self,
        files: crate::vehicle::Files<'_>,
        start: crate::vehicle::Start,
    ) -> Result<(), JsValue> {
        let world = self
            .world
            .as_mut()
            .ok_or_else(|| JsValue::from_str("Load a world first"))?;
        let vehicle = crate::vehicle::Session::load(
            &self.renderer,
            std::sync::Arc::clone(&world.terrain),
            files,
            start,
        )?;
        self.follow_camera = true;
        world.reset_dynamic();
        world.camera = vehicle.camera();
        self.vehicle = Some(vehicle);
        Ok(())
    }

    pub fn step_keys(
        &mut self,
        dt: i32,
        left: bool,
        right: bool,
        throttle: bool,
        brake: bool,
        auxiliary: bool,
    ) -> Result<(), JsValue> {
        if let (Some(vehicle), Some(world)) = (&mut self.vehicle, &mut self.world) {
            vehicle.step_keys(
                tankracer_game::player_input::Keys {
                    left,
                    right,
                    throttle,
                    brake,
                    auxiliary,
                    ..Default::default()
                },
                dt,
            )?;
            if self.follow_camera {
                world.camera = vehicle.camera();
            }
        }
        Ok(())
    }

    pub fn begin_race(&mut self) -> Result<(), JsValue> {
        self.vehicle
            .as_mut()
            .ok_or("Load a player first")?
            .runtime
            .begin_race()
            .map_err(|e| JsValue::from_str(&e.to_string()))
    }

    pub fn load_hud(&mut self, common: &[u8], shared: &[u8], level: &[u8]) -> Result<(), JsValue> {
        let vehicle = self.vehicle.as_ref().ok_or("Load a player first")?;
        self.hud = Some(crate::hud::Hud::load(
            &self.renderer,
            common,
            shared,
            level,
            vehicle.runtime.world().settings().map as usize,
        )?);
        Ok(())
    }

    #[cfg(feature = "gpu-tests")]
    pub fn set_hud_probe(&mut self, visible: bool) {
        self.hud_enabled = visible;
    }

    #[cfg(feature = "gpu-tests")]
    pub fn race_state(&self) -> Vec<i32> {
        self.vehicle.as_ref().map_or_else(Vec::new, |v| {
            let Some(c) = v.runtime.clock() else {
                return vec![];
            };
            let p = v.runtime.player();
            [
                c.countdown,
                c.release,
                c.display,
                p.race.elapsed,
                i32::from(p.race.laps),
                i32::from(p.race.driving),
                i32::from(p.input.start_held),
                i32::from(p.input.fast_start),
                p.prefix.countdown_throttle,
                p.prefix.countdown_rpm,
            ]
            .into_iter()
            .chain(p.physics.motion.poses.physical_position)
            .chain(p.physics.motion.velocity)
            .chain([
                v.runtime.world().shared.random as i32,
                v.runtime.world().frame as i32,
            ])
            .collect()
        })
    }

    #[cfg(feature = "gpu-tests")]
    pub fn step_vehicle(
        &mut self,
        dt: i32,
        throttle: i32,
        brake: i32,
        steering: i32,
    ) -> Result<(), JsValue> {
        if let (Some(vehicle), Some(world)) = (&mut self.vehicle, &mut self.world) {
            vehicle.step(
                tankracer_game::drive::Input {
                    throttle,
                    brake,
                    steering,
                    ..Default::default()
                },
                dt,
            )?;
            if self.follow_camera {
                world.camera = vehicle.camera();
            }
        }
        Ok(())
    }

    pub fn toggle_camera(&mut self) {
        self.follow_camera = !self.follow_camera;
        if self.follow_camera
            && let (Some(vehicle), Some(world)) = (&self.vehicle, &mut self.world)
        {
            world.camera = vehicle.camera();
        }
    }
    /// Input routing queries this only when assigning a newly pressed key.
    pub fn inspection_active(&self) -> bool {
        self.vehicle.is_none() || !self.follow_camera
    }
    pub fn set_camera_keys(&mut self, cycle: bool, rear: bool) {
        if let Some(vehicle) = &mut self.vehicle {
            vehicle.camera_keys = tankracer_game::chase_camera::Keys { cycle, rear };
        }
    }
    pub fn set_turret_keys(&mut self, left: bool, right: bool) {
        if let Some(vehicle) = &mut self.vehicle {
            vehicle.turret_keys = [left, right];
        }
    }
    #[cfg(feature = "gpu-tests")]
    pub fn turret_state(&self) -> Vec<i32> {
        self.vehicle.as_ref().map_or_else(Vec::new, |v| {
            let p = v.runtime.player();
            let t = p.turret;
            [
                i32::from(p.physics.pose_input.turret_yaw),
                t.velocity,
                t.cooldown,
                i32::from(t.centering),
                i32::from(t.moving),
                i32::from(t.center_lock),
                i32::from(t.pressed[0]),
                i32::from(t.pressed[1]),
            ]
            .into_iter()
            .chain(p.physics.motion.poses.turret.matrix.0)
            .chain(p.physics.motion.poses.gun.position)
            .chain(p.physics.motion.poses.physical_position)
            .chain([
                v.runtime.world().shared.random as i32,
                v.runtime.world().frame as i32,
            ])
            .collect()
        })
    }
    #[cfg(feature = "gpu-tests")]
    pub fn camera_state(&self) -> Vec<i32> {
        self.vehicle.as_ref().map_or_else(Vec::new, |v| {
            let c = v.chase();
            [c.mode as i32, i32::from(c.rear), i32::from(c.cycle_held)]
                .into_iter()
                .chain(c.position)
                .chain(c.matrix.0)
                .chain(c.cockpit_local)
                .collect()
        })
    }

    #[cfg(feature = "gpu-tests")]
    pub fn set_vehicle_sight_probe(&mut self, enabled: bool) {
        if let Some(v) = &mut self.vehicle {
            v.view.sight_enabled = enabled;
        }
    }

    #[cfg(feature = "gpu-tests")]
    pub fn set_vehicle_attachment_probe(&mut self, aerial: bool, layer: bool, starting: bool) {
        if let Some(v) = &mut self.vehicle {
            v.view.aerial_enabled = aerial;
            v.view.layer_enabled = layer;
            v.view.scene.presentation.starting = starting;
        }
    }

    #[cfg(feature = "gpu-tests")]
    pub fn scenery_state(&self) -> Vec<u32> {
        self.vehicle.as_ref().map_or_else(Vec::new, |v| {
            let scene = &v.runtime.world().objects;
            vec![
                scene
                    .world
                    .objects
                    .iter()
                    .filter(|p| p.class == v.runtime.world().registry.fall && p.flags & 1 != 0)
                    .count() as u32,
                scene
                    .components
                    .surfaces
                    .entries
                    .iter()
                    .flatten()
                    .filter(|s| s.state.dirty != 0)
                    .count() as u32,
                scene
                    .components
                    .bounces
                    .iter()
                    .flatten()
                    .filter(|b| b.state.age != 0 || b.state.airborne != 0)
                    .count() as u32,
                scene
                    .components
                    .closers
                    .iter()
                    .flatten()
                    .filter(|c| c.state.phase == tankracer_game::closer::Phase::Closing)
                    .count() as u32,
                scene
                    .components
                    .closers
                    .iter()
                    .flatten()
                    .filter(|c| c.state.phase == tankracer_game::closer::Phase::Fragments)
                    .count() as u32,
                scene
                    .components
                    .spins
                    .iter()
                    .flatten()
                    .filter(|s| s.state.timer != 0)
                    .count() as u32,
                scene
                    .components
                    .movers
                    .iter()
                    .flatten()
                    .filter(|m| m.travel != 0)
                    .count() as u32,
                scene
                    .components
                    .objects
                    .iter()
                    .flatten()
                    .filter(|o| o.data.debris.as_ref().is_some_and(|d| d.flags & 1 != 0))
                    .count() as u32,
                scene.components.explosion_world.count as u32,
            ]
        })
    }

    #[cfg(feature = "gpu-tests")]
    pub fn vehicle_position(&self) -> Vec<i32> {
        self.vehicle.as_ref().map_or_else(Vec::new, |v| {
            v.runtime.world().vehicles.entries[0]
                .as_ref()
                .unwrap()
                .physics
                .motion
                .poses
                .physical_position
                .to_vec()
        })
    }

    #[cfg(feature = "gpu-tests")]
    pub fn vehicle_state(&self) -> Vec<i32> {
        self.vehicle.as_ref().map_or_else(Vec::new, |v| {
            let state = &v.runtime.world().vehicles.entries[0]
                .as_ref()
                .unwrap()
                .physics;
            state
                .motion
                .poses
                .physical_position
                .into_iter()
                .chain(state.motion.velocity)
                .chain([
                    v.runtime.world().shared.random as i32,
                    v.runtime.world().frame as i32,
                ])
                .collect()
        })
    }

    /// Complete cold construction probe; it never changes the driving session.
    #[cfg(feature = "gpu-tests")]
    #[allow(clippy::too_many_arguments)] // Mirrors the immutable archive/selection boundary.
    pub fn complete_startup(
        &self,
        data: &[u8],
        vehicles: &[u8],
        shared: &[u8],
        level: &[u8],
        map: usize,
        cup: usize,
        car: u32,
    ) -> Result<Vec<i64>, JsValue> {
        use tankracer_game::{
            session::loading::{Files, Selection},
            world_startup::Constructed,
        };
        let constructed = Constructed::load(
            Files {
                data,
                vehicles: &[vehicles],
                shared,
                level,
            },
            Selection {
                map,
                cup,
                vehicle: car,
            },
        )
        .map_err(|e| JsValue::from_str(&e.to_string()))?;
        let world = constructed.world();
        let p = world.vehicles.entries[world.player.0]
            .as_ref()
            .ok_or("Missing Player")?;
        Ok(vec![
            i64::from(world.shared.random),
            i64::from(p.physics.entity.0),
            i64::from(constructed.camera.entity.0),
            i64::from(constructed.finish.entity.0),
            constructed.scenery().construction.len() as i64,
            constructed.scenery().actors.entries.len() as i64,
            i64::from(constructed.camera.variant),
            i64::from(constructed.camera.pose.position[0]),
            i64::from(constructed.camera.pose.position[1]),
            i64::from(constructed.camera.pose.position[2]),
            i64::from(p.route.checkpoint.map_or(0, |e| e.0)),
            i64::from(p.route.passed),
            world.identity.ticks.order.len() as i64,
            world.identity.queues.notifications.len() as i64,
            i64::from(world.frame),
        ])
    }

    /// Initial Player route binding for native/Wasm startup checks.
    #[cfg(feature = "gpu-tests")]
    pub fn player_route(&self) -> Vec<u32> {
        self.vehicle.as_ref().map_or_else(Vec::new, |v| {
            let route = v.runtime.player().route;
            vec![
                u32::from(route.checkpoint.is_some()),
                route.checkpoint.map_or(0, |id| id.0),
                route.passed,
            ]
        })
    }

    /// Exact diagnostic text compared inside browser tests, never in production.
    #[cfg(feature = "gpu-tests")]
    pub fn simulation_probe(&self) -> String {
        self.vehicle.as_ref().map_or_else(String::new, |v| {
            let game = v.runtime.world();
            let p = v.runtime.player();
            format!(
                "{:?}|{:?}|{:?}|{:?}|{:?}|{:?}|{:?}|{:?}|{:?}|{:?}|{:?}|{}|{}",
                p.physics,
                p.input,
                p.brake,
                p.prefix,
                p.route,
                game.objects.world.colliders,
                game.objects.world.objects,
                game.objects.components.movers,
                game.objects
                    .components
                    .spins
                    .iter()
                    .map(|s| s.as_ref().map(|s| &s.state))
                    .collect::<Vec<_>>(),
                v.runtime.camera(),
                v.runtime.tracks(),
                game.shared.random,
                game.frame
            )
        })
    }

    #[cfg(feature = "gpu-tests")]
    pub fn vehicle_tracks(&self) -> Vec<u32> {
        self.vehicle
            .as_ref()
            .map_or_else(Vec::new, |v| v.runtime.tracks().travel.to_vec())
    }

    #[cfg(feature = "gpu-tests")]
    pub fn set_vehicle_render_probe(&mut self, shadow: bool, shade: i32) -> Result<(), JsValue> {
        if !(-1..=1).contains(&shade) {
            return Err("Invalid shade probe".into());
        }
        if let Some(vehicle) = &mut self.vehicle {
            vehicle.view.shadow_enabled = shadow;
            vehicle.view.shade_override = if shade < 0 { None } else { Some(shade != 0) };
        }
        Ok(())
    }

    #[cfg(feature = "gpu-tests")]
    pub fn player_controls(&self) -> Vec<i32> {
        self.vehicle.as_ref().map_or_else(Vec::new, |v| {
            let p = v.runtime.world().vehicles.entries[0].as_ref().unwrap();
            vec![
                p.input.drive.throttle,
                p.input.drive.brake,
                p.input.drive.steering,
                i32::from(p.input.drive.alternate),
                i32::from(p.input.drive.alternate_lateral),
                i32::from(p.input.drive.alternate_rotation),
                i32::from(p.brake.saved_turn),
                i32::from(p.brake.reverse_braking),
                p.model.config.tank.drive.max_turn,
            ]
        })
    }

    pub fn move_camera(&mut self, seconds: f32, x: f32, y: f32, z: f32, fast: bool) {
        if (self.vehicle.is_none() || !self.follow_camera)
            && let Some(world) = &mut self.world
        {
            world.move_camera(seconds, x, y, z, fast);
        }
    }

    pub fn look(&mut self, dx: f32, dy: f32) {
        if (self.vehicle.is_none() || !self.follow_camera)
            && let Some(world) = &mut self.world
        {
            world.camera.look(dx, dy);
        }
    }

    pub fn camera_position(&self) -> Vec<f32> {
        self.world
            .as_ref()
            .map_or_else(|| vec![0.; 3], |w| w.camera.position.to_array().to_vec())
    }

    pub fn camera_forward(&self) -> Vec<f32> {
        self.world.as_ref().map_or_else(
            || vec![0., 0., -1.],
            |w| w.camera.forward().to_array().to_vec(),
        )
    }

    pub fn scene_stats(&self) -> String {
        self.world
            .as_ref()
            .map_or_else(|| "{}".into(), |w| w.stats())
    }

    #[cfg(feature = "gpu-tests")]
    pub fn run_gpu_checks(&mut self) -> Result<String, JsValue> {
        crate::checks::run(&mut self.renderer)
    }

    #[cfg(feature = "gpu-tests")]
    pub fn check_graphics(&self) -> Result<(), JsValue> {
        self.renderer.check_error()
    }

    pub fn frame_stats(&self) -> String {
        let s = self.renderer.stats();
        format!(
            "{{\"draws\":{},\"triangles\":{},\"pipeline_changes\":{},\"index_upload_bytes\":{},\"vertex_upload_bytes\":{},\"instance_upload_bytes\":{}}}",
            s.draws,
            s.triangles,
            s.pipeline_changes,
            s.index_upload_bytes,
            s.vertex_upload_bytes,
            s.instance_upload_bytes
        )
    }
}
