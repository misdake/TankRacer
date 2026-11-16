//! Read-only live scenery extraction. Shared prototypes are instanced; private
//! Crush/fragment geometry uploads only after its authoritative version changes.
use glam::Mat4;
use tankracer_game::{scene_objects::Scene, scenery::Loaded};
use tankracer_render::gpu::{Frame, GpuInstances, GpuMesh, Renderer, Texture};
use tankracer_scene::{
    model::Materials,
    transform::physics_pose,
    transparency,
    world::{BatchKind, TransparentTriangle},
};
use wasm_bindgen::JsValue;

struct Pass {
    mesh: GpuMesh,
    vertices: usize,
    indices: usize,
}
struct Group {
    version: u64,
    passes: [Option<Pass>; 5],
    transparent: Vec<TransparentTriangle>,
    transforms: Vec<[f32; 16]>,
    uploaded: Vec<[f32; 16]>,
    instances: GpuInstances,
}
pub struct View {
    materials: Materials,
    atlas: Texture,
    groups: Vec<Option<Group>>,
    pub revision: u64,
    pub objects: usize,
    pub triangles: usize,
    pub opaque_draws: usize,
}
fn error(e: impl std::fmt::Display) -> JsValue {
    JsValue::from_str(&e.to_string())
}
impl View {
    pub fn load(
        renderer: &Renderer,
        loaded: &Loaded,
        level: &[u8],
        shared: &[u8],
    ) -> Result<Self, JsValue> {
        let mut materials = Materials::load(
            loaded
                .models
                .models()
                .flat_map(|m| m.materials.iter().map(|m| m.name.as_str())),
            level,
            shared,
        )
        .map_err(error)?;
        let atlas = renderer.upload_rgba(
            materials.atlas.width,
            materials.atlas.height,
            &materials.atlas.premultiplied_rgba,
        )?;
        materials.release_pixels();
        Ok(Self {
            materials,
            atlas,
            groups: Vec::new(),
            revision: 0,
            objects: 0,
            triangles: 0,
            opaque_draws: 0,
        })
    }
    pub fn refresh(&mut self, renderer: &mut Renderer, scene: &Scene) -> Result<(), JsValue> {
        let meshes = &scene.components.surfaces.meshes;
        self.groups.resize_with(meshes.len(), || None);
        for (index, group) in self.groups.iter_mut().enumerate() {
            if let Some(group) = group {
                group.transforms.clear();
            }
            // Retired private meshes release GPU storage without renumbering.
            if meshes.info_at(index).is_none() {
                *group = None;
            }
        }
        self.objects = 0;
        self.triangles = 0;
        for id in scene
            .components
            .ordinary
            .iter()
            .chain(&scene.components.special)
        {
            let object = scene
                .components
                .objects
                .get(id.0)
                .and_then(Option::as_ref)
                .ok_or_else(|| error("Invalid render membership"))?;
            let Some(mesh_id) = object.data.mesh else {
                continue;
            };
            let info = meshes
                .info(mesh_id)
                .ok_or_else(|| error("Missing current scene mesh"))?;
            let entry = &mut self.groups[mesh_id.index()];
            if entry.is_none() {
                *entry = Some(Group {
                    version: 0,
                    passes: std::array::from_fn(|_| None),
                    transparent: Vec::new(),
                    transforms: Vec::new(),
                    uploaded: Vec::new(),
                    instances: renderer.instances()?,
                });
            }
            let group = entry.as_mut().unwrap();
            if group.version != info.version {
                let prepared = self
                    .materials
                    .prepare(info.geometry, info.materials)
                    .map_err(error)?;
                for (pass, data) in group.passes.iter_mut().zip(&prepared.batches) {
                    if data.indices.is_empty() {
                        *pass = None;
                        continue;
                    }
                    if let Some(pass) = pass
                        && pass.vertices == data.vertices.len()
                        && pass.indices == data.indices.len()
                    {
                        renderer.update_vertices(&mut pass.mesh, &data.vertices)?;
                    } else {
                        *pass = Some(Pass {
                            mesh: renderer.upload_mesh(data)?,
                            vertices: data.vertices.len(),
                            indices: data.indices.len(),
                        });
                    }
                }
                group.transparent = prepared.transparent;
                group.version = info.version;
                self.revision = self.revision.wrapping_add(1);
            }
            let collider = scene
                .world
                .colliders
                .get(id.0)
                .ok_or_else(|| error("Missing scene pose"))?;
            let basis = collider.external_basis.unwrap_or(collider.pose.matrix);
            group
                .transforms
                .push(physics_pose(collider.pose.position, basis.0).to_cols_array());
            self.objects += 1;
            self.triangles += group
                .passes
                .iter()
                .flatten()
                .map(|p| p.indices / 3)
                .sum::<usize>();
        }
        for group in self.groups.iter_mut().flatten() {
            if group.transforms != group.uploaded {
                renderer.update_instances(&mut group.instances, &group.transforms)?;
                group.uploaded.clear();
                group.uploaded.extend_from_slice(&group.transforms);
            }
        }
        self.opaque_draws = self
            .groups
            .iter()
            .flatten()
            .filter(|g| !g.transforms.is_empty())
            .map(|g| {
                g.passes[..BatchKind::Transparent.slot()]
                    .iter()
                    .flatten()
                    .count()
            })
            .sum();
        Ok(())
    }
    pub fn opaque(&self, frame: &mut Frame<'_>, vp: Mat4) {
        for group in self
            .groups
            .iter()
            .flatten()
            .filter(|g| !g.transforms.is_empty())
        {
            for kind in BatchKind::ALL
                .into_iter()
                .filter(|k| *k != BatchKind::Transparent)
            {
                if let Some(pass) = &group.passes[kind.slot()] {
                    frame.draw_instanced(
                        &pass.mesh,
                        &crate::world::material(kind),
                        Some(&self.atlas),
                        vp,
                        &group.instances,
                    );
                }
            }
        }
    }
    pub fn transparent_objects(&self, output: &mut Vec<transparency::Object>) {
        for (i, group) in self
            .groups
            .iter()
            .enumerate()
            .filter_map(|(i, g)| g.as_ref().map(|g| (i, g)))
        {
            if group.transparent.is_empty() {
                continue;
            }
            output.extend(group.transforms.iter().map(|m| transparency::Object {
                geometry: i + 1,
                model: Mat4::from_cols_array(m),
            }));
        }
    }
    pub fn transparent(&self, geometry: usize) -> &[TransparentTriangle] {
        self.groups
            .get(geometry - 1)
            .and_then(Option::as_ref)
            .map_or(&[], |g| &g.transparent)
    }
    pub fn geometry_count(&self) -> usize {
        self.groups.len()
    }
    pub fn update_indices(
        &mut self,
        renderer: &mut Renderer,
        geometry: usize,
        indices: &[u32],
    ) -> Result<(), JsValue> {
        let pass = self.groups[geometry - 1]
            .as_mut()
            .and_then(|g| g.passes[BatchKind::Transparent.slot()].as_mut())
            .ok_or_else(|| error("Missing transparent prototype"))?;
        renderer.update_indices(&mut pass.mesh, indices)
    }
    pub fn draw_transparent(
        &self,
        frame: &mut Frame<'_>,
        object: transparency::Object,
        vp: Mat4,
        range: std::ops::Range<u32>,
    ) -> Result<(), JsValue> {
        let mesh = &self.groups[object.geometry - 1].as_ref().unwrap().passes
            [BatchKind::Transparent.slot()]
        .as_ref()
        .unwrap()
        .mesh;
        frame.draw_range(
            mesh,
            &crate::world::material(BatchKind::Transparent),
            Some(&self.atlas),
            [vp, object.model],
            range,
        )
    }
    pub fn resident_bytes(&self) -> usize {
        std::mem::size_of::<Self>() - std::mem::size_of::<Materials>()
            + self.materials.resident_bytes()
            + self.groups.capacity() * std::mem::size_of::<Option<Group>>()
            + self
                .groups
                .iter()
                .flatten()
                .map(|g| {
                    g.transparent.capacity() * std::mem::size_of::<TransparentTriangle>()
                        + (g.transforms.capacity() + g.uploaded.capacity()) * 64
                })
                .sum::<usize>()
    }
}
