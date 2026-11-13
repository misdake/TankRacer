use glam::{Mat4, Vec3};
use tankracer_render::{
    geometry::MeshData,
    gpu::{GpuMesh, Renderer, Texture},
    material::Material,
};
use wasm_bindgen::prelude::*;
use web_sys::HtmlCanvasElement;

#[wasm_bindgen]
pub struct App {
    pub(crate) renderer: Renderer,
    canvas: HtmlCanvasElement,
    cube: GpuMesh,
    checker: Texture,
}

#[wasm_bindgen]
impl App {
    #[wasm_bindgen(constructor)]
    pub fn new(canvas: HtmlCanvasElement) -> Result<Self, JsValue> {
        let renderer = Renderer::new(&canvas)?;
        let cube = renderer.upload_mesh(&MeshData::cube())?;
        let checker = renderer.upload_rgba(
            2,
            2,
            &[
                210, 210, 190, 255, 130, 140, 120, 255, 130, 140, 120, 255, 210, 210, 190, 255,
            ],
        )?;
        Ok(Self {
            renderer,
            canvas,
            cube,
            checker,
        })
    }

    pub fn render(&mut self, seconds: f64) {
        let size = [self.canvas.width(), self.canvas.height()];
        if size.contains(&0) {
            return;
        }
        let mut frame = self
            .renderer
            .begin_frame(None, size, [0.48, 0.62, 0.72, 1.]);
        let projection = Mat4::perspective_rh_gl(
            50_f32.to_radians(),
            size[0] as f32 / size[1] as f32,
            0.1,
            100.,
        );
        let view = Mat4::look_at_rh(Vec3::new(8., 6., 10.), Vec3::new(0., 0.5, 0.), Vec3::Y);
        let vp = projection * view;
        let ground = Mat4::from_translation(Vec3::new(0., -0.3, 0.))
            * Mat4::from_scale(Vec3::new(6., 0.15, 6.));
        frame.draw(
            &self.cube,
            &Material::opaque([0.7, 0.8, 0.6, 1.]),
            Some(&self.checker),
            vp * ground,
        );
        for (i, color) in [
            [0.75, 0.32, 0.15, 1.],
            [0.25, 0.45, 0.65, 1.],
            [0.65, 0.6, 0.2, 1.],
        ]
        .into_iter()
        .enumerate()
        {
            let model = Mat4::from_translation(Vec3::new((i as f32 - 1.) * 2.6, 0.8, 0.))
                * Mat4::from_rotation_y(seconds as f32 * 0.2 + i as f32 * 0.4)
                * Mat4::from_scale(Vec3::new(0.8, 1., 0.8));
            frame.draw(&self.cube, &Material::opaque(color), None, vp * model);
        }
    }

    #[cfg(feature = "gpu-tests")]
    pub fn run_gpu_checks(&mut self) -> Result<String, JsValue> {
        crate::checks::run(&mut self.renderer)
    }

    pub fn frame_stats(&self) -> String {
        let s = self.renderer.stats();
        format!(
            "{{\"draws\":{},\"triangles\":{},\"pipeline_changes\":{}}}",
            s.draws, s.triangles, s.pipeline_changes
        )
    }
}
