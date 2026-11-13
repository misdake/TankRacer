use tankracer_render::gpu::Renderer;
use wasm_bindgen::prelude::*;
use web_sys::HtmlCanvasElement;

#[wasm_bindgen]
pub struct App {
    pub(crate) renderer: Renderer,
    canvas: HtmlCanvasElement,
    world: Option<crate::world::WorldView>,
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
        })
    }

    pub fn render(&mut self) -> Result<(), JsValue> {
        let size = [self.canvas.width(), self.canvas.height()];
        if size.contains(&0) {
            return Ok(());
        }
        if let Some(world) = &mut self.world {
            return world.draw(&mut self.renderer, size);
        }
        self.renderer.begin_frame(None, size, [0., 0., 0., 1.]);
        Ok(())
    }

    pub fn load_world(&mut self, bytes: &[u8]) -> Result<String, JsValue> {
        let world = crate::world::WorldView::load(&self.renderer, bytes)?;
        let stats = world.stats();
        self.world = Some(world);
        Ok(stats)
    }

    pub fn move_camera(&mut self, seconds: f32, x: f32, y: f32, z: f32, fast: bool) {
        if let Some(world) = &mut self.world {
            world.move_camera(seconds, x, y, z, fast);
        }
    }

    pub fn look(&mut self, dx: f32, dy: f32) {
        if let Some(world) = &mut self.world {
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

    pub fn frame_stats(&self) -> String {
        let s = self.renderer.stats();
        format!(
            "{{\"draws\":{},\"triangles\":{},\"pipeline_changes\":{},\"index_upload_bytes\":{}}}",
            s.draws, s.triangles, s.pipeline_changes, s.index_upload_bytes
        )
    }
}
