//! GPU adapter for the native-testable, read-only vehicle scene.
use glam::Mat4;
use tankracer_game::vehicle_startup::Player;
use tankracer_render::gpu::{Frame, GpuMesh, Renderer, Texture};
use tankracer_scene::{
    transparency,
    vehicle::VehicleScene,
    world::{BatchKind, TransparentTriangle},
};
use wasm_bindgen::JsValue;

struct Pass {
    mesh: GpuMesh,
    vertices: usize,
    indices: usize,
}
struct Part {
    revision: u64,
    passes: [Option<Pass>; 5],
}
pub struct VehicleView {
    pub scene: VehicleScene,
    atlas: Texture,
    parts: Vec<Part>,
}
fn error(e: impl std::fmt::Display) -> JsValue {
    JsValue::from_str(&e.to_string())
}

impl VehicleView {
    pub fn load(
        renderer: &Renderer,
        player: &Player,
        vehicle: &[u8],
        shared: &[u8],
    ) -> Result<Self, JsValue> {
        let mut scene = VehicleScene::load(&player.model, vehicle, shared).map_err(error)?;
        let atlas = renderer.upload_rgba(
            scene.materials.atlas.width,
            scene.materials.atlas.height,
            &scene.materials.atlas.premultiplied_rgba,
        )?;
        scene.materials.release_pixels();
        let parts = scene
            .parts
            .iter()
            .map(|p| {
                let mut passes = std::array::from_fn(|_| None);
                for (pass, data) in passes.iter_mut().zip(&p.geometry.batches) {
                    if !data.indices.is_empty() {
                        *pass = Some(Pass {
                            mesh: renderer.upload_mesh(data)?,
                            vertices: data.vertices.len(),
                            indices: data.indices.len(),
                        });
                    }
                }
                Ok(Part {
                    revision: p.revision,
                    passes,
                })
            })
            .collect::<Result<Vec<_>, JsValue>>()?;
        Ok(Self {
            scene,
            atlas,
            parts,
        })
    }

    pub fn refresh(&mut self, renderer: &mut Renderer, player: &Player) -> Result<(), JsValue> {
        self.scene
            .refresh(&player.model, &player.physics.motion.poses)
            .map_err(error)?;
        for (gpu, part) in self.parts.iter_mut().zip(&self.scene.parts) {
            if gpu.revision == part.revision {
                continue;
            }
            for (pass, data) in gpu.passes.iter_mut().zip(&part.geometry.batches) {
                if data.indices.is_empty() {
                    *pass = None;
                } else if let Some(pass) = pass
                    && pass.vertices == data.vertices.len()
                    && pass.indices == data.indices.len()
                {
                    renderer.update_vertices(&mut pass.mesh, &data.vertices)?;
                    // Local preparation emits contiguous triangle indices. The
                    // blended bank is reordered by the joint sorter below.
                } else {
                    *pass = Some(Pass {
                        mesh: renderer.upload_mesh(data)?,
                        vertices: data.vertices.len(),
                        indices: data.indices.len(),
                    });
                }
            }
            gpu.revision = part.revision;
        }
        Ok(())
    }

    pub fn opaque(&self, frame: &mut Frame<'_>, vp: Mat4) {
        for (gpu, part) in self.parts.iter().zip(&self.scene.parts) {
            for kind in BatchKind::ALL
                .into_iter()
                .filter(|k| *k != BatchKind::Transparent)
            {
                if let Some(pass) = &gpu.passes[kind.slot()] {
                    frame.draw_model(
                        &pass.mesh,
                        &crate::world::material(kind),
                        Some(&self.atlas),
                        vp,
                        part.transform,
                    );
                }
            }
        }
    }

    // The caller assigns geometry slots after the current scenery banks. Vehicle
    // faces participate in the same depth order as the world and other actors.
    pub fn transparent_objects(&self, base: usize, output: &mut Vec<transparency::Object>) {
        for (i, part) in self.scene.parts.iter().enumerate() {
            if !part.geometry.transparent.is_empty() {
                output.push(transparency::Object {
                    geometry: base + i,
                    model: part.transform,
                });
            }
        }
    }
    pub fn transparent(&self, part: usize) -> &[TransparentTriangle] {
        &self.scene.parts[part].geometry.transparent
    }
    pub fn update_indices(
        &mut self,
        renderer: &mut Renderer,
        part: usize,
        indices: &[u32],
    ) -> Result<(), JsValue> {
        let mesh = &mut self.parts[part].passes[BatchKind::Transparent.slot()]
            .as_mut()
            .ok_or_else(|| error("Missing blended vehicle part"))?
            .mesh;
        renderer.update_indices(mesh, indices)
    }
    pub fn draw_transparent(
        &self,
        frame: &mut Frame<'_>,
        part: usize,
        vp: Mat4,
        range: std::ops::Range<u32>,
    ) -> Result<(), JsValue> {
        let mesh = &self.parts[part].passes[BatchKind::Transparent.slot()]
            .as_ref()
            .unwrap()
            .mesh;
        frame.draw_range(
            mesh,
            &crate::world::material(BatchKind::Transparent),
            Some(&self.atlas),
            [vp, self.scene.parts[part].transform],
            range,
        )
    }
    pub fn triangles(&self) -> usize {
        self.parts
            .iter()
            .flat_map(|p| p.passes.iter().flatten())
            .map(|p| p.indices / 3)
            .sum()
    }
    pub fn opaque_draws(&self) -> usize {
        self.parts
            .iter()
            .map(|p| {
                p.passes[..BatchKind::Transparent.slot()]
                    .iter()
                    .flatten()
                    .count()
            })
            .sum()
    }
    pub fn resident_bytes(&self) -> usize {
        std::mem::size_of::<Self>() - std::mem::size_of::<VehicleScene>()
            + self.scene.resident_bytes()
            + self.parts.capacity() * std::mem::size_of::<Part>()
    }
}
