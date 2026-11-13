//! Raw WebGL 2 backend. Resources own their handles; frame state is explicit.

mod resources;
mod state;

use glam::Mat4;
pub use resources::{GpuMesh, RenderTarget, Texture};
use wasm_bindgen::{JsCast, JsValue};
use web_sys::{HtmlCanvasElement, WebGl2RenderingContext as Gl, WebGlUniformLocation};

use crate::{
    geometry::MeshData,
    material::{Material, Pipeline},
};
use resources::Program;

#[derive(Debug, Default, Clone, Copy)]
pub struct FrameStats {
    pub draws: u32,
    pub triangles: u32,
    pub pipeline_changes: u32,
}

pub struct Renderer {
    gl: Gl,
    program: Program,
    matrix: WebGlUniformLocation,
    color: WebGlUniformLocation,
    cutoff: WebGlUniformLocation,
    textured: WebGlUniformLocation,
    fallback: Texture,
    pipeline: Option<Pipeline>,
    mesh: Option<web_sys::WebGlVertexArrayObject>,
    texture: Option<web_sys::WebGlTexture>,
    stats: FrameStats,
}

/// A live frame exclusively borrows the renderer. Resource uploads cannot
/// invalidate VAO/texture/framebuffer caches between draws in this frame.
pub struct Frame<'a> {
    renderer: &'a mut Renderer,
}

impl Frame<'_> {
    pub fn draw(
        &mut self,
        mesh: &GpuMesh,
        material: &Material,
        texture: Option<&Texture>,
        matrix: Mat4,
    ) {
        self.renderer.draw(mesh, material, texture, matrix);
    }

    pub fn read_pixel(&self, x: i32, y: i32) -> Result<[u8; 4], JsValue> {
        self.renderer.read_pixel(x, y)
    }
}

impl Renderer {
    pub fn new(canvas: &HtmlCanvasElement) -> Result<Self, JsValue> {
        let options = js_sys::Object::new();
        for (name, enabled) in [
            ("alpha", false),
            ("premultipliedAlpha", false),
            ("antialias", false),
            ("depth", true),
            ("stencil", false),
        ] {
            js_sys::Reflect::set(&options, &name.into(), &enabled.into())?;
        }
        let gl: Gl = canvas
            .get_context_with_context_options("webgl2", &options)?
            .ok_or_else(|| JsValue::from_str("WebGL 2 is unavailable"))?
            .dyn_into()?;
        gl.pixel_storei(Gl::UNPACK_ALIGNMENT, 1);
        gl.pixel_storei(Gl::UNPACK_FLIP_Y_WEBGL, 0);
        gl.pixel_storei(Gl::UNPACK_PREMULTIPLY_ALPHA_WEBGL, 0);
        gl.pixel_storei(Gl::UNPACK_COLORSPACE_CONVERSION_WEBGL, Gl::NONE as i32);
        let program = Program::new(&gl, include_str!("world.vert"), include_str!("world.frag"))?;
        let uniform = |name| {
            gl.get_uniform_location(&program.raw, name)
                .ok_or_else(|| JsValue::from_str(&format!("Missing shader uniform: {name}")))
        };
        let matrix = uniform("uMatrix")?;
        let color = uniform("uColor")?;
        let cutoff = uniform("uCutoff")?;
        let textured = uniform("uTextured")?;
        let fallback = Texture::rgba(&gl, 1, 1, &[255; 4])?;
        gl.use_program(Some(&program.raw));
        gl.uniform1i(Some(&uniform("uTexture")?), 0);
        Ok(Self {
            gl,
            program,
            matrix,
            color,
            cutoff,
            textured,
            fallback,
            pipeline: None,
            mesh: None,
            texture: None,
            stats: FrameStats::default(),
        })
    }

    pub fn upload_mesh(&self, data: &MeshData) -> Result<GpuMesh, JsValue> {
        GpuMesh::new(&self.gl, data)
    }

    pub fn upload_rgba(&self, width: u32, height: u32, bytes: &[u8]) -> Result<Texture, JsValue> {
        Texture::rgba(&self.gl, width, height, bytes)
    }

    pub fn target(&self, width: u32, height: u32) -> Result<RenderTarget, JsValue> {
        RenderTarget::new(&self.gl, width, height)
    }

    pub fn begin_frame<'a>(
        &'a mut self,
        target: Option<&'a RenderTarget>,
        size: [u32; 2],
        clear: [f32; 4],
    ) -> Frame<'a> {
        let gl = &self.gl;
        let size = target.map_or(size, |t| [t.width, t.height]);
        gl.bind_framebuffer(Gl::FRAMEBUFFER, target.and_then(|t| t.framebuffer.as_ref()));
        gl.viewport(0, 0, size[0] as i32, size[1] as i32);
        gl.disable(Gl::SCISSOR_TEST);
        gl.disable(Gl::STENCIL_TEST);
        gl.disable(Gl::POLYGON_OFFSET_FILL);
        gl.disable(Gl::SAMPLE_ALPHA_TO_COVERAGE);
        gl.disable(Gl::DITHER);
        gl.color_mask(true, true, true, true);
        gl.depth_mask(true);
        gl.depth_range(0., 1.);
        gl.clear_depth(1.);
        gl.clear_color(clear[0], clear[1], clear[2], clear[3]);
        gl.clear(Gl::COLOR_BUFFER_BIT | Gl::DEPTH_BUFFER_BIT);
        gl.front_face(Gl::CCW);
        gl.active_texture(Gl::TEXTURE0);
        gl.use_program(Some(&self.program.raw));
        // Clearing overrides depth/color masks. Force a complete first pipeline.
        // Upload helpers can also change bindings between frames.
        self.pipeline = None;
        self.mesh = None;
        self.texture = None;
        self.stats = FrameStats::default();
        Frame { renderer: self }
    }

    fn draw(
        &mut self,
        mesh: &GpuMesh,
        material: &Material,
        texture: Option<&Texture>,
        matrix: Mat4,
    ) {
        self.apply_pipeline(material.pipeline);
        let gl = &self.gl;
        if self.mesh != mesh.vao {
            gl.bind_vertex_array(mesh.vao.as_ref());
            self.mesh = mesh.vao.clone();
        }
        let image = texture.unwrap_or(&self.fallback);
        if self.texture.as_ref() != Some(&image.raw) {
            gl.bind_texture(Gl::TEXTURE_2D, Some(&image.raw));
            self.texture = Some(image.raw.clone());
        }
        gl.uniform_matrix4fv_with_f32_array(Some(&self.matrix), false, &matrix.to_cols_array());
        gl.uniform4fv_with_f32_array(Some(&self.color), &material.color);
        gl.uniform1f(Some(&self.cutoff), material.alpha_cutoff.unwrap_or(-1.));
        gl.uniform1i(Some(&self.textured), i32::from(texture.is_some()));
        gl.draw_elements_with_i32(Gl::TRIANGLES, mesh.count, Gl::UNSIGNED_INT, 0);
        self.stats.draws += 1;
        self.stats.triangles += mesh.count as u32 / 3;
    }

    pub fn stats(&self) -> FrameStats {
        self.stats
    }

    /// Synchronous readback belongs to test/inspection paths, never game frames.
    fn read_pixel(&self, x: i32, y: i32) -> Result<[u8; 4], JsValue> {
        let mut pixel = [0; 4];
        self.gl.read_pixels_with_opt_u8_array(
            x,
            y,
            1,
            1,
            Gl::RGBA,
            Gl::UNSIGNED_BYTE,
            Some(&mut pixel),
        )?;
        self.check_error()?;
        Ok(pixel)
    }

    pub fn check_error(&self) -> Result<(), JsValue> {
        let error = self.gl.get_error();
        if error == Gl::NO_ERROR {
            Ok(())
        } else {
            Err(JsValue::from_str(&format!("WebGL error: 0x{error:x}")))
        }
    }
}
