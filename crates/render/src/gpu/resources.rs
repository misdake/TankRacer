use crate::geometry::{MeshData, Vertex};
use wasm_bindgen::JsValue;
use web_sys::{
    WebGl2RenderingContext as Gl, WebGlBuffer, WebGlFramebuffer, WebGlProgram, WebGlRenderbuffer,
    WebGlShader, WebGlTexture, WebGlVertexArrayObject,
};

pub struct GpuMesh {
    gl: Gl,
    pub(super) vao: Option<WebGlVertexArrayObject>,
    vertices: Option<WebGlBuffer>,
    indices: Option<WebGlBuffer>,
    pub(super) count: i32,
}

impl GpuMesh {
    pub(super) fn new(gl: &Gl, mesh: &MeshData) -> Result<Self, JsValue> {
        if mesh.indices.len() > i32::MAX as usize
            || !mesh.indices.len().is_multiple_of(3)
            || mesh
                .indices
                .iter()
                .any(|&i| i as usize >= mesh.vertices.len())
        {
            return Err("Invalid triangle mesh indices".into());
        }
        let mut result = Self {
            gl: gl.clone(),
            vao: None,
            vertices: None,
            indices: None,
            count: mesh.indices.len() as i32,
        };
        result.vao = Some(gl.create_vertex_array().ok_or("Cannot allocate mesh VAO")?);
        result.vertices = Some(gl.create_buffer().ok_or("Cannot allocate vertex buffer")?);
        result.indices = Some(gl.create_buffer().ok_or("Cannot allocate index buffer")?);
        gl.bind_vertex_array(result.vao.as_ref());
        gl.bind_buffer(Gl::ARRAY_BUFFER, result.vertices.as_ref());
        // Copy only during resource upload; no unsafe views across memory growth.
        let vertices = js_sys::Uint8Array::from(bytemuck::cast_slice::<Vertex, u8>(&mesh.vertices));
        gl.buffer_data_with_array_buffer_view(Gl::ARRAY_BUFFER, &vertices, Gl::STATIC_DRAW);
        gl.bind_buffer(Gl::ELEMENT_ARRAY_BUFFER, result.indices.as_ref());
        let indices = js_sys::Uint32Array::from(mesh.indices.as_slice());
        gl.buffer_data_with_array_buffer_view(Gl::ELEMENT_ARRAY_BUFFER, &indices, Gl::STATIC_DRAW);
        for (slot, size, offset) in [(0, 3, 0), (1, 4, 12), (2, 2, 28)] {
            gl.enable_vertex_attrib_array(slot);
            gl.vertex_attrib_pointer_with_i32(
                slot,
                size,
                Gl::FLOAT,
                false,
                std::mem::size_of::<Vertex>() as i32,
                offset,
            );
        }
        gl.bind_vertex_array(None);
        Ok(result)
    }
}

impl Drop for GpuMesh {
    fn drop(&mut self) {
        self.gl.delete_vertex_array(self.vao.as_ref());
        self.gl.delete_buffer(self.vertices.as_ref());
        self.gl.delete_buffer(self.indices.as_ref());
    }
}

pub struct Texture {
    gl: Gl,
    pub(super) raw: WebGlTexture,
}

impl Texture {
    pub(super) fn rgba(gl: &Gl, width: u32, height: u32, bytes: &[u8]) -> Result<Self, JsValue> {
        if width == 0
            || height == 0
            || width > i32::MAX as u32
            || height > i32::MAX as u32
            || u64::from(width) * u64::from(height) * 4 != bytes.len() as u64
        {
            return Err("Invalid RGBA texture dimensions".into());
        }
        let texture = Self {
            gl: gl.clone(),
            raw: gl.create_texture().ok_or("Cannot allocate texture")?,
        };
        gl.bind_texture(Gl::TEXTURE_2D, Some(&texture.raw));
        gl.tex_image_2d_with_i32_and_i32_and_i32_and_format_and_type_and_opt_u8_array(
            Gl::TEXTURE_2D,
            0,
            Gl::RGBA8 as i32,
            width as i32,
            height as i32,
            0,
            Gl::RGBA,
            Gl::UNSIGNED_BYTE,
            Some(bytes),
        )?;
        for (parameter, value) in [
            (Gl::TEXTURE_MIN_FILTER, Gl::LINEAR),
            (Gl::TEXTURE_MAG_FILTER, Gl::LINEAR),
            (Gl::TEXTURE_WRAP_S, Gl::CLAMP_TO_EDGE),
            (Gl::TEXTURE_WRAP_T, Gl::CLAMP_TO_EDGE),
        ] {
            gl.tex_parameteri(Gl::TEXTURE_2D, parameter, value as i32);
        }
        Ok(texture)
    }
}

impl Drop for Texture {
    fn drop(&mut self) {
        self.gl.delete_texture(Some(&self.raw));
    }
}

pub struct RenderTarget {
    gl: Gl,
    pub(super) framebuffer: Option<WebGlFramebuffer>,
    color: Option<WebGlTexture>,
    depth: Option<WebGlRenderbuffer>,
    pub width: u32,
    pub height: u32,
}

impl RenderTarget {
    pub(super) fn new(gl: &Gl, width: u32, height: u32) -> Result<Self, JsValue> {
        if width == 0 || height == 0 || width > i32::MAX as u32 || height > i32::MAX as u32 {
            return Err("Invalid render target dimensions".into());
        }
        let mut result = Self {
            gl: gl.clone(),
            framebuffer: None,
            color: None,
            depth: None,
            width,
            height,
        };
        result.framebuffer = Some(
            gl.create_framebuffer()
                .ok_or("Cannot allocate framebuffer")?,
        );
        result.color = Some(
            gl.create_texture()
                .ok_or("Cannot allocate framebuffer color")?,
        );
        result.depth = Some(
            gl.create_renderbuffer()
                .ok_or("Cannot allocate framebuffer depth")?,
        );
        gl.bind_texture(Gl::TEXTURE_2D, result.color.as_ref());
        gl.tex_storage_2d(Gl::TEXTURE_2D, 1, Gl::RGBA8, width as i32, height as i32);
        gl.bind_renderbuffer(Gl::RENDERBUFFER, result.depth.as_ref());
        gl.renderbuffer_storage(
            Gl::RENDERBUFFER,
            Gl::DEPTH_COMPONENT24,
            width as i32,
            height as i32,
        );
        gl.bind_framebuffer(Gl::FRAMEBUFFER, result.framebuffer.as_ref());
        gl.framebuffer_texture_2d(
            Gl::FRAMEBUFFER,
            Gl::COLOR_ATTACHMENT0,
            Gl::TEXTURE_2D,
            result.color.as_ref(),
            0,
        );
        gl.framebuffer_renderbuffer(
            Gl::FRAMEBUFFER,
            Gl::DEPTH_ATTACHMENT,
            Gl::RENDERBUFFER,
            result.depth.as_ref(),
        );
        if gl.check_framebuffer_status(Gl::FRAMEBUFFER) != Gl::FRAMEBUFFER_COMPLETE {
            return Err("Incomplete RGBA8/depth24 framebuffer".into());
        }
        Ok(result)
    }
}

impl Drop for RenderTarget {
    fn drop(&mut self) {
        self.gl.delete_framebuffer(self.framebuffer.as_ref());
        self.gl.delete_texture(self.color.as_ref());
        self.gl.delete_renderbuffer(self.depth.as_ref());
    }
}

struct Shader {
    gl: Gl,
    raw: WebGlShader,
}
impl Shader {
    fn new(gl: &Gl, kind: u32, source: &str) -> Result<Self, JsValue> {
        let shader = Self {
            gl: gl.clone(),
            raw: gl.create_shader(kind).ok_or("Cannot allocate shader")?,
        };
        gl.shader_source(&shader.raw, source);
        gl.compile_shader(&shader.raw);
        if !gl
            .get_shader_parameter(&shader.raw, Gl::COMPILE_STATUS)
            .as_bool()
            .unwrap_or(false)
        {
            return Err(gl
                .get_shader_info_log(&shader.raw)
                .unwrap_or_else(|| "Shader compilation failed".into())
                .into());
        }
        Ok(shader)
    }
}
impl Drop for Shader {
    fn drop(&mut self) {
        self.gl.delete_shader(Some(&self.raw));
    }
}

pub(super) struct Program {
    gl: Gl,
    pub raw: WebGlProgram,
}
impl Program {
    pub fn new(gl: &Gl, vertex: &str, fragment: &str) -> Result<Self, JsValue> {
        let vertex = Shader::new(gl, Gl::VERTEX_SHADER, vertex)?;
        let fragment = Shader::new(gl, Gl::FRAGMENT_SHADER, fragment)?;
        let program = Self {
            gl: gl.clone(),
            raw: gl.create_program().ok_or("Cannot allocate program")?,
        };
        gl.attach_shader(&program.raw, &vertex.raw);
        gl.attach_shader(&program.raw, &fragment.raw);
        gl.link_program(&program.raw);
        if !gl
            .get_program_parameter(&program.raw, Gl::LINK_STATUS)
            .as_bool()
            .unwrap_or(false)
        {
            return Err(gl
                .get_program_info_log(&program.raw)
                .unwrap_or_else(|| "Shader link failed".into())
                .into());
        }
        Ok(program)
    }
}
impl Drop for Program {
    fn drop(&mut self) {
        self.gl.delete_program(Some(&self.raw));
    }
}
