use glam::Vec3;
use tankracer_render::{
    gpu::{GpuMesh, Renderer, Texture},
    material::{AlphaEncoding, Blend, Cull, Material, Pipeline},
};
use tankracer_scene::{
    camera::Camera,
    world::{BatchKind, WorldScene},
};
use wasm_bindgen::JsValue;

pub struct WorldView {
    pub camera: Camera,
    scene: WorldScene,
    meshes: Vec<GpuMesh>,
    atlas: Texture,
}

impl WorldView {
    pub fn load(renderer: &Renderer, bytes: &[u8]) -> Result<Self, JsValue> {
        let mut scene =
            WorldScene::load_archive(bytes).map_err(|e| JsValue::from_str(&e.to_string()))?;
        let camera = scene.inspection_camera();
        let atlas = renderer.upload_rgba(
            scene.atlas.width,
            scene.atlas.height,
            &scene.atlas.premultiplied_rgba,
        )?;
        let meshes = scene
            .batches
            .iter()
            .map(|b| renderer.upload_mesh(&b.geometry))
            .collect::<Result<Vec<_>, _>>()?;
        // Source ownership/index tables stay for later detachable features;
        // static vertex bytes and atlas pixels now live only on the GPU.
        for batch in &mut scene.batches {
            batch.geometry.vertices = Vec::new();
        }
        scene.atlas.premultiplied_rgba = Vec::new();
        renderer.check_error()?;
        Ok(Self {
            scene,
            meshes,
            atlas,
            camera,
        })
    }

    pub fn draw(&mut self, renderer: &mut Renderer, size: [u32; 2]) -> Result<(), JsValue> {
        if self
            .scene
            .order
            .update(&self.scene.transparent, &self.camera)
        {
            renderer.update_indices(
                &mut self.meshes[BatchKind::Transparent.slot()],
                &self.scene.order.indices,
            )?;
        }
        let vp = self.camera.view_projection(size[0] as f32 / size[1] as f32);
        let right = self.camera.right();
        let mut frame = renderer.begin_frame(None, size, [0.48, 0.62, 0.72, 1.]);
        frame.set_billboard_right([right.x, right.z]);
        for (batch, mesh) in self.scene.batches.iter().zip(&self.meshes) {
            if batch.geometry.indices.is_empty() {
                continue;
            }
            let material = material(batch.kind);
            frame.draw(mesh, &material, Some(&self.atlas), vp);
        }
        Ok(())
    }

    pub fn move_camera(&mut self, seconds: f32, x: f32, y: f32, z: f32, fast: bool) {
        self.camera.move_local(Vec3::new(x, y, z), seconds, fast);
    }

    pub fn stats(&self) -> String {
        let s = &self.scene.stats;
        format!(
            "{{\"source_meshes\":{},\"source_triangles\":{},\"rendered_triangles\":{},\"billboard_triangles\":{},\"materials\":{},\"unique_vertices\":{},\"vertex_bytes\":{},\"index_bytes\":{},\"acceleration_bytes\":{},\"atlas_width\":{},\"atlas_height\":{}}}",
            s.source_meshes,
            s.source_triangles,
            s.rendered_triangles,
            s.billboard_triangles,
            self.scene.materials.len(),
            s.unique_vertices,
            s.vertex_bytes,
            s.index_bytes,
            s.acceleration_bytes,
            self.scene.atlas.width,
            self.scene.atlas.height
        )
    }
}

fn material(kind: BatchKind) -> Material {
    let transparent = kind == BatchKind::Transparent;
    let masked = matches!(kind, BatchKind::Masked | BatchKind::MaskedTwoSided);
    let double_sided = matches!(
        kind,
        BatchKind::OpaqueTwoSided | BatchKind::MaskedTwoSided | BatchKind::Transparent
    );
    Material {
        pipeline: Pipeline {
            depth_write: !transparent,
            blend: if transparent {
                Some(Blend::PREMULTIPLIED_ALPHA)
            } else {
                None
            },
            cull: if double_sided { Cull::None } else { Cull::Back },
            ..Pipeline::OPAQUE
        },
        alpha_cutoff: if masked { Some(0.5) } else { None },
        texture_alpha: AlphaEncoding::Premultiplied,
        output_alpha: if transparent {
            AlphaEncoding::Premultiplied
        } else {
            AlphaEncoding::Straight
        },
        geometry_culling: transparent,
        ..Material::opaque([1.; 4])
    }
}
