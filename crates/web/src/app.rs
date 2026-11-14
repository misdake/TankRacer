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
        })
    }

    pub fn render(&mut self) -> Result<(), JsValue> {
        let size = [self.canvas.width(), self.canvas.height()];
        if size.contains(&0) {
            return Ok(());
        }
        if let Some(world) = &mut self.world {
            return world.draw(&mut self.renderer, size, self.vehicle.as_mut());
        }
        self.renderer.begin_frame(None, size, [0., 0., 0., 1.]);
        Ok(())
    }

    pub fn load_world(&mut self, bytes: &[u8]) -> Result<String, JsValue> {
        let world = crate::world::WorldView::load(&self.renderer, bytes)?;
        let stats = world.stats();
        self.world = Some(world);
        self.vehicle = None;
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
        world.camera = vehicle.view.camera();
        self.vehicle = Some(vehicle);
        Ok(())
    }

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
                &world.camera,
            )?;
            if self.follow_camera {
                world.camera = vehicle.view.camera();
            }
        }
        Ok(())
    }

    pub fn toggle_camera(&mut self) {
        self.follow_camera = !self.follow_camera;
        if self.follow_camera
            && let (Some(vehicle), Some(world)) = (&self.vehicle, &mut self.world)
        {
            world.camera = vehicle.view.camera();
        }
    }

    #[cfg(feature = "gpu-tests")]
    pub fn scenery_state(&self) -> Vec<u32> {
        self.vehicle.as_ref().map_or_else(Vec::new, |v| {
            let scene = &v.game.objects;
            vec![
                scene
                    .world
                    .objects
                    .iter()
                    .filter(|p| p.class == v.game.registry.fall && p.flags & 1 != 0)
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
            ]
        })
    }

    #[cfg(feature = "gpu-tests")]
    pub fn vehicle_position(&self) -> Vec<i32> {
        self.vehicle.as_ref().map_or_else(Vec::new, |v| {
            v.game.vehicles.entries[0]
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
            let state = &v.game.vehicles.entries[0].as_ref().unwrap().physics;
            state
                .motion
                .poses
                .physical_position
                .into_iter()
                .chain(state.motion.velocity)
                .chain([v.game.shared.random as i32, v.game.frame as i32])
                .collect()
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
