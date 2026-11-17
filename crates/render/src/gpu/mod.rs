//! Raw WebGL 2 backend. Resources own their handles; frame state is explicit.

mod resources;
mod state;

use glam::Mat4;
pub use resources::{GpuInstances, GpuMesh, RenderTarget, Texture};
use wasm_bindgen::{JsCast, JsValue};
use web_sys::{HtmlCanvasElement, WebGl2RenderingContext as Gl, WebGlUniformLocation};

use crate::{
    geometry::MeshData,
    material::{AlphaEncoding, Material, Pipeline},
};
use resources::Program;

#[derive(Debug, Default, Clone, Copy)]
pub struct FrameStats {
    pub draws: u32,
    pub triangles: u32,
    pub pipeline_changes: u32,
    pub index_upload_bytes: u32,
    pub vertex_upload_bytes: u32,
    pub instance_upload_bytes: u32,
}

pub struct Renderer {
    gl: Gl,
    program: Program,
    matrix: WebGlUniformLocation,
    model: WebGlUniformLocation,
    instanced: WebGlUniformLocation,
    color: WebGlUniformLocation,
    cutoff: WebGlUniformLocation,
    textured: WebGlUniformLocation,
    texture_premultiplied: WebGlUniformLocation,
    output_premultiplied: WebGlUniformLocation,
    geometry_culling: WebGlUniformLocation,
    billboard_right: WebGlUniformLocation,
    uv_phase: WebGlUniformLocation,
    shaded: WebGlUniformLocation,
    normal_layer: WebGlUniformLocation,
    layer_rows: WebGlUniformLocation,
    layer_atlas: WebGlUniformLocation,
    layer_gain: WebGlUniformLocation,
    fallback: Texture,
    pipeline: Option<Pipeline>,
    mesh: Option<web_sys::WebGlVertexArrayObject>,
    texture: Option<web_sys::WebGlTexture>,
    instances: Option<web_sys::WebGlBuffer>,
    stats: FrameStats,
    pending_upload_bytes: u32,
    pending_vertex_bytes: u32,
    pending_instance_bytes: u32,
}

/// A live frame exclusively borrows the renderer. Resource uploads cannot
/// invalidate VAO/texture/framebuffer caches between draws in this frame.
pub struct Frame<'a> {
    renderer: &'a mut Renderer,
}

impl Frame<'_> {
    pub fn set_billboard_right(&mut self, right: [f32; 2]) {
        self.renderer
            .gl
            .uniform2f(Some(&self.renderer.billboard_right), right[0], right[1]);
    }
    pub fn draw(
        &mut self,
        mesh: &GpuMesh,
        material: &Material,
        texture: Option<&Texture>,
        matrix: Mat4,
    ) {
        self.renderer
            .draw(mesh, material, texture, matrix, Mat4::IDENTITY);
    }

    pub fn draw_model(
        &mut self,
        mesh: &GpuMesh,
        material: &Material,
        texture: Option<&Texture>,
        vp: Mat4,
        model: Mat4,
    ) {
        self.renderer.draw(mesh, material, texture, vp, model);
    }

    pub fn draw_range(
        &mut self,
        mesh: &GpuMesh,
        material: &Material,
        texture: Option<&Texture>,
        transforms: [Mat4; 2],
        range: std::ops::Range<u32>,
    ) -> Result<(), JsValue> {
        if range.start > range.end
            || range.end > mesh.count as u32
            || !range.start.is_multiple_of(3)
            || !range.end.is_multiple_of(3)
        {
            return Err("Invalid triangle draw range".into());
        }
        self.renderer
            .bind_draw(mesh, material, texture, transforms[0], transforms[1], false);
        self.renderer.gl.draw_elements_with_i32(
            Gl::TRIANGLES,
            (range.end - range.start) as i32,
            Gl::UNSIGNED_INT,
            range.start as i32 * 4,
        );
        self.renderer.stats.draws += 1;
        self.renderer.stats.triangles += (range.end - range.start) / 3;
        Ok(())
    }

    pub fn draw_instanced(
        &mut self,
        mesh: &GpuMesh,
        material: &Material,
        texture: Option<&Texture>,
        vp: Mat4,
        instances: &GpuInstances,
    ) {
        let r = &mut self.renderer;
        let mesh_changed = r.mesh != mesh.vao;
        r.bind_draw(mesh, material, texture, vp, Mat4::IDENTITY, true);
        if mesh_changed || r.instances.as_ref() != Some(&instances.buffer) {
            r.gl.bind_buffer(Gl::ARRAY_BUFFER, Some(&instances.buffer));
            for column in 0..4 {
                let slot = 5 + column;
                r.gl.enable_vertex_attrib_array(slot);
                r.gl.vertex_attrib_pointer_with_i32(
                    slot,
                    4,
                    Gl::FLOAT,
                    false,
                    64,
                    column as i32 * 16,
                );
                r.gl.vertex_attrib_divisor(slot, 1);
            }
            r.instances = Some(instances.buffer.clone());
        }
        r.gl.uniform1i(Some(&r.instanced), 1);
        r.gl.draw_elements_instanced_with_i32(
            Gl::TRIANGLES,
            mesh.count,
            Gl::UNSIGNED_INT,
            0,
            instances.count,
        );
        r.stats.draws += 1;
        r.stats.triangles += mesh.count as u32 / 3 * instances.count as u32;
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
        gl.vertex_attrib4f(9, 0., 0., 0., 0.);
        gl.vertex_attrib4f(10, 0., 0., 0., 0.);
        let program = Program::new(&gl, include_str!("world.vert"), include_str!("world.frag"))?;
        let uniform = |name| {
            gl.get_uniform_location(&program.raw, name)
                .ok_or_else(|| JsValue::from_str(&format!("Missing shader uniform: {name}")))
        };
        let matrix = uniform("uMatrix")?;
        let model = uniform("uModel")?;
        let instanced = uniform("uInstanced")?;
        let color = uniform("uColor")?;
        let cutoff = uniform("uCutoff")?;
        let textured = uniform("uTextured")?;
        let texture_premultiplied = uniform("uTexturePremultiplied")?;
        let output_premultiplied = uniform("uOutputPremultiplied")?;
        let geometry_culling = uniform("uGeometryCulling")?;
        let billboard_right = uniform("uBillboardRight")?;
        let uv_phase = uniform("uUvPhase")?;
        let shaded = uniform("uShaded")?;
        let normal_layer = uniform("uNormalLayer")?;
        let layer_rows = uniform("uLayerRows[0]")?;
        let layer_atlas = uniform("uLayerAtlas")?;
        let layer_gain = uniform("uLayerGain")?;
        let fallback = Texture::rgba(&gl, 1, 1, &[255; 4])?;
        gl.use_program(Some(&program.raw));
        gl.uniform1i(Some(&uniform("uTexture")?), 0);
        Ok(Self {
            gl,
            program,
            matrix,
            model,
            instanced,
            color,
            cutoff,
            textured,
            texture_premultiplied,
            output_premultiplied,
            geometry_culling,
            billboard_right,
            uv_phase,
            shaded,
            normal_layer,
            layer_rows,
            layer_atlas,
            layer_gain,
            fallback,
            pipeline: None,
            mesh: None,
            texture: None,
            instances: None,
            stats: FrameStats::default(),
            pending_upload_bytes: 0,
            pending_vertex_bytes: 0,
            pending_instance_bytes: 0,
        })
    }

    pub fn upload_mesh(&self, data: &MeshData) -> Result<GpuMesh, JsValue> {
        GpuMesh::new(&self.gl, data)
    }
    /// Update a changed mesh prototype. Revisions belong to its owner; this
    /// shared cache boundary handles topology, optional streams and cleanup.
    pub fn sync_mesh(
        &mut self,
        slot: &mut Option<GpuMesh>,
        data: &MeshData,
        streams: crate::geometry::MeshStreams<'_>,
    ) -> Result<(), JsValue> {
        if data.indices.is_empty() {
            *slot = None;
            return Ok(());
        }
        if let Some(mesh) = slot
            && mesh.vertex_count() == data.vertices.len()
        {
            self.update_vertices(mesh, &data.vertices)?;
            self.update_indices(mesh, &data.indices)?;
            self.update_uv_scroll(mesh, streams.uv_scroll)?;
            self.update_layer_normals(mesh, streams.normals)?;
        } else {
            *slot = Some(self.upload_mesh_streams(data, streams.uv_scroll, streams.normals)?);
        }
        Ok(())
    }

    pub fn upload_animated_mesh(
        &self,
        data: &MeshData,
        scroll: &[[f32; 4]],
    ) -> Result<GpuMesh, JsValue> {
        let mut mesh = self.upload_mesh(data)?;
        mesh.uv_scroll(scroll)?;
        Ok(mesh)
    }

    pub fn upload_mesh_streams(
        &self,
        data: &MeshData,
        scroll: &[[f32; 4]],
        normals: &[[f32; 4]],
    ) -> Result<GpuMesh, JsValue> {
        let mut mesh = self.upload_animated_mesh(data, scroll)?;
        mesh.layer_normals(normals)?;
        Ok(mesh)
    }

    pub fn update_uv_scroll(
        &mut self,
        mesh: &mut GpuMesh,
        scroll: &[[f32; 4]],
    ) -> Result<(), JsValue> {
        mesh.uv_scroll(scroll)?;
        self.pending_vertex_bytes = self
            .pending_vertex_bytes
            .saturating_add(std::mem::size_of_val(scroll) as u32);
        self.mesh = None;
        Ok(())
    }

    pub fn update_layer_normals(
        &mut self,
        mesh: &mut GpuMesh,
        normals: &[[f32; 4]],
    ) -> Result<(), JsValue> {
        mesh.layer_normals(normals)?;
        self.pending_vertex_bytes = self
            .pending_vertex_bytes
            .saturating_add(std::mem::size_of_val(normals) as u32);
        self.mesh = None;
        Ok(())
    }

    pub fn instances(&self) -> Result<GpuInstances, JsValue> {
        GpuInstances::new(&self.gl)
    }

    pub fn update_instances(
        &mut self,
        buffer: &mut GpuInstances,
        transforms: &[[f32; 16]],
    ) -> Result<(), JsValue> {
        buffer.update(transforms)?;
        self.pending_instance_bytes = self
            .pending_instance_bytes
            .saturating_add(std::mem::size_of_val(transforms) as u32);
        Ok(())
    }

    pub fn update_vertices(
        &mut self,
        mesh: &mut GpuMesh,
        vertices: &[crate::geometry::Vertex],
    ) -> Result<(), JsValue> {
        mesh.update_vertices(vertices)?;
        self.pending_vertex_bytes = self
            .pending_vertex_bytes
            .saturating_add(std::mem::size_of_val(vertices) as u32);
        Ok(())
    }

    pub fn upload_rgba(&self, width: u32, height: u32, bytes: &[u8]) -> Result<Texture, JsValue> {
        Texture::rgba(&self.gl, width, height, bytes)
    }

    pub fn target(&self, width: u32, height: u32) -> Result<RenderTarget, JsValue> {
        RenderTarget::new(&self.gl, width, height)
    }

    pub fn update_indices(&mut self, mesh: &mut GpuMesh, indices: &[u32]) -> Result<(), JsValue> {
        mesh.update_indices(indices)?;
        self.pending_upload_bytes = self
            .pending_upload_bytes
            .saturating_add(std::mem::size_of_val(indices) as u32);
        Ok(())
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
        gl.uniform2f(Some(&self.billboard_right), 1., 0.);
        // Clearing overrides depth/color masks. Force a complete first pipeline.
        // Upload helpers can also change bindings between frames.
        self.pipeline = None;
        self.mesh = None;
        self.texture = None;
        self.instances = None;
        self.stats = FrameStats {
            index_upload_bytes: std::mem::take(&mut self.pending_upload_bytes),
            vertex_upload_bytes: std::mem::take(&mut self.pending_vertex_bytes),
            instance_upload_bytes: std::mem::take(&mut self.pending_instance_bytes),
            ..FrameStats::default()
        };
        Frame { renderer: self }
    }

    fn draw(
        &mut self,
        mesh: &GpuMesh,
        material: &Material,
        texture: Option<&Texture>,
        matrix: Mat4,
        model: Mat4,
    ) {
        self.bind_draw(mesh, material, texture, matrix, model, false);
        self.gl
            .draw_elements_with_i32(Gl::TRIANGLES, mesh.count, Gl::UNSIGNED_INT, 0);
        self.stats.draws += 1;
        self.stats.triangles += mesh.count as u32 / 3;
    }

    fn bind_draw(
        &mut self,
        mesh: &GpuMesh,
        material: &Material,
        texture: Option<&Texture>,
        matrix: Mat4,
        model: Mat4,
        instanced: bool,
    ) {
        self.apply_pipeline(material.pipeline);
        let gl = &self.gl;
        let mesh_changed = self.mesh != mesh.vao;
        if mesh_changed {
            gl.bind_vertex_array(mesh.vao.as_ref());
            self.mesh = mesh.vao.clone();
        }
        // Enabled instance streams are VAO state. Ordinary draws must not fetch
        // them (their buffers can be shorter than the vertex/index stream).
        if !instanced && (mesh_changed || self.instances.is_some()) {
            for slot in 5..9 {
                gl.disable_vertex_attrib_array(slot);
            }
            self.instances = None;
        }
        let image = texture.unwrap_or(&self.fallback);
        if self.texture.as_ref() != Some(&image.raw) {
            gl.bind_texture(Gl::TEXTURE_2D, Some(&image.raw));
            self.texture = Some(image.raw.clone());
        }
        gl.uniform_matrix4fv_with_f32_array(Some(&self.matrix), false, &matrix.to_cols_array());
        gl.uniform_matrix4fv_with_f32_array(Some(&self.model), false, &model.to_cols_array());
        gl.uniform1i(Some(&self.instanced), i32::from(instanced));
        gl.uniform4fv_with_f32_array(Some(&self.color), &material.color);
        gl.uniform2fv_with_f32_array(Some(&self.uv_phase), &material.uv_phase);
        gl.uniform1i(Some(&self.shaded), i32::from(material.shaded));
        gl.uniform1i(
            Some(&self.normal_layer),
            i32::from(material.normal_layer.is_some()),
        );
        if let Some(layer) = material.normal_layer {
            let [a, b] = layer.rows;
            gl.uniform3iv_with_i32_array(
                Some(&self.layer_rows),
                &[a[0], a[1], a[2], b[0], b[1], b[2]],
            );
            gl.uniform4fv_with_f32_array(Some(&self.layer_atlas), &layer.atlas);
            gl.uniform1f(Some(&self.layer_gain), layer.gain);
        }
        gl.uniform1f(Some(&self.cutoff), material.alpha_cutoff.unwrap_or(-1.));
        gl.uniform1i(Some(&self.textured), i32::from(texture.is_some()));
        gl.uniform1i(
            Some(&self.texture_premultiplied),
            i32::from(matches!(
                material.texture_alpha,
                AlphaEncoding::Premultiplied
            )),
        );
        gl.uniform1i(
            Some(&self.output_premultiplied),
            i32::from(matches!(
                material.output_alpha,
                AlphaEncoding::Premultiplied
            )),
        );
        gl.uniform1i(
            Some(&self.geometry_culling),
            i32::from(material.geometry_culling),
        );
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
