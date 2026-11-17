//! GPU adapter for the native-testable, read-only vehicle scene.
use glam::Mat4;
use tankracer_game::vehicle_startup::Player;
use tankracer_render::gpu::{Frame, GpuInstances, GpuMesh, Renderer, Texture};
use tankracer_render::material::{Blend, Cull, Material, Pipeline};
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
struct Aerial {
    passes: [Option<Pass>; 5],
    instances: GpuInstances,
    uploaded: Option<[[f32; 16]; 4]>,
}
pub struct VehicleView {
    pub scene: VehicleScene,
    atlas: Texture,
    parts: Vec<Part>,
    shadow: GpuMesh,
    shadow_revision: u64,
    sight: GpuMesh,
    aerial: Option<Aerial>,
    #[cfg(feature = "gpu-tests")]
    pub shadow_enabled: bool,
    #[cfg(feature = "gpu-tests")]
    pub shade_override: Option<bool>,
    #[cfg(feature = "gpu-tests")]
    pub sight_enabled: bool,
    #[cfg(feature = "gpu-tests")]
    pub aerial_enabled: bool,
    #[cfg(feature = "gpu-tests")]
    pub layer_enabled: bool,
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
                for (((pass, data), scroll), normals) in passes
                    .iter_mut()
                    .zip(&p.geometry.batches)
                    .zip(&p.geometry.uv_scroll)
                    .zip(&p.layer_normals)
                {
                    if !data.indices.is_empty() {
                        *pass = Some(Pass {
                            mesh: renderer.upload_mesh_streams(data, scroll, normals)?,
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
        let shadow = renderer.upload_mesh(&scene.shadow.geometry)?;
        let shadow_revision = scene.shadow.revision;
        let sight = renderer.upload_mesh(&scene.sight)?;
        let aerial = scene
            .aerial
            .as_ref()
            .map(|a| -> Result<_, JsValue> {
                let mut passes = std::array::from_fn(|_| None);
                for (pass, data) in passes.iter_mut().zip(&a.geometry.batches) {
                    if !data.indices.is_empty() {
                        *pass = Some(Pass {
                            mesh: renderer.upload_mesh(data)?,
                            vertices: data.vertices.len(),
                            indices: data.indices.len(),
                        });
                    }
                }
                Ok(Aerial {
                    passes,
                    instances: renderer.instances()?,
                    uploaded: None,
                })
            })
            .transpose()?;
        Ok(Self {
            scene,
            atlas,
            parts,
            shadow,
            shadow_revision,
            sight,
            aerial,
            #[cfg(feature = "gpu-tests")]
            shadow_enabled: true,
            #[cfg(feature = "gpu-tests")]
            shade_override: None,
            #[cfg(feature = "gpu-tests")]
            sight_enabled: true,
            #[cfg(feature = "gpu-tests")]
            aerial_enabled: true,
            #[cfg(feature = "gpu-tests")]
            layer_enabled: true,
        })
    }

    pub fn refresh(&mut self, renderer: &mut Renderer, player: &Player) -> Result<(), JsValue> {
        self.scene.refresh_player(player).map_err(error)?;
        if self.aerial_visible()
            && let (Some(gpu), Some(scene)) = (&mut self.aerial, &self.scene.aerial)
        {
            let poses = scene.poses.map(|p| {
                tankracer_scene::transform::physics_pose(p.position, p.matrix.0).to_cols_array()
            });
            if gpu.uploaded != Some(poses) {
                renderer.update_instances(&mut gpu.instances, &poses)?;
                gpu.uploaded = Some(poses);
            }
        }
        if self.shadow_revision != self.scene.shadow.revision {
            renderer.update_vertices(&mut self.shadow, &self.scene.shadow.geometry.vertices)?;
            self.shadow_revision = self.scene.shadow.revision;
        }
        for (gpu, part) in self.parts.iter_mut().zip(&self.scene.parts) {
            if gpu.revision == part.revision {
                continue;
            }
            for (((pass, data), scroll), normals) in gpu
                .passes
                .iter_mut()
                .zip(&part.geometry.batches)
                .zip(&part.geometry.uv_scroll)
                .zip(&part.layer_normals)
            {
                if data.indices.is_empty() {
                    *pass = None;
                } else if let Some(pass) = pass
                    && pass.vertices == data.vertices.len()
                    && pass.indices == data.indices.len()
                {
                    renderer.update_vertices(&mut pass.mesh, &data.vertices)?;
                    renderer.update_uv_scroll(&mut pass.mesh, scroll)?;
                    renderer.update_layer_normals(&mut pass.mesh, normals)?;
                    // Local preparation emits contiguous triangle indices. The
                    // blended bank is reordered by the joint sorter below.
                } else {
                    *pass = Some(Pass {
                        mesh: renderer.upload_mesh_streams(data, scroll, normals)?,
                        vertices: data.vertices.len(),
                        indices: data.indices.len(),
                    });
                }
            }
            gpu.revision = part.revision;
        }
        Ok(())
    }

    pub fn opaque(&self, frame: &mut Frame<'_>, vp: Mat4, view: tankracer_game::math::Matrix) {
        for (gpu, part) in self.parts.iter().zip(&self.scene.parts) {
            if !self.scene.visible(part) {
                continue;
            }
            for kind in BatchKind::ALL
                .into_iter()
                .filter(|k| *k != BatchKind::Transparent)
            {
                if let Some(pass) = &gpu.passes[kind.slot()] {
                    frame.draw_model(
                        &pass.mesh,
                        &Material {
                            uv_phase: part.uv_phase,
                            shaded: self.shaded(),
                            normal_layer: self.layer(part, view),
                            ..crate::world::material(kind)
                        },
                        Some(&self.atlas),
                        vp,
                        part.transform,
                    );
                }
            }
        }
        if self.aerial_visible() {
            let aerial = self.aerial.as_ref().unwrap();
            for (pass, kind) in aerial.passes.iter().zip(BatchKind::ALL) {
                if let Some(pass) = pass {
                    frame.draw_instanced(
                        &pass.mesh,
                        &Material {
                            shaded: self.shaded(),
                            ..crate::world::material(kind)
                        },
                        Some(&self.atlas),
                        vp,
                        &aerial.instances,
                    );
                }
            }
        }
    }

    // The caller assigns geometry slots after the current scenery banks. Vehicle
    // faces participate in the same depth order as the world and other actors.
    pub fn transparent_objects(&self, base: usize, output: &mut Vec<transparency::Object>) {
        for (i, part) in self.scene.parts.iter().enumerate() {
            if self.scene.visible(part) && !part.geometry.transparent.is_empty() {
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
        view: tankracer_game::math::Matrix,
    ) -> Result<(), JsValue> {
        let mesh = &self.parts[part].passes[BatchKind::Transparent.slot()]
            .as_ref()
            .unwrap()
            .mesh;
        frame.draw_range(
            mesh,
            &Material {
                uv_phase: self.scene.parts[part].uv_phase,
                shaded: self.shaded(),
                normal_layer: self.layer(&self.scene.parts[part], view),
                ..crate::world::material(BatchKind::Transparent)
            },
            Some(&self.atlas),
            [vp, self.scene.parts[part].transform],
            range,
        )
    }
    pub fn triangles(&self) -> usize {
        self.parts
            .iter()
            .zip(&self.scene.parts)
            .filter(|(_, p)| self.scene.visible(p))
            .flat_map(|(p, _)| p.passes.iter().flatten())
            .map(|p| p.indices / 3)
            .sum::<usize>()
            + self.shadow_draws() * 2
            + self.sight_draws() * 2
            + if self.aerial_visible() {
                self.aerial
                    .as_ref()
                    .unwrap()
                    .passes
                    .iter()
                    .flatten()
                    .map(|p| p.indices / 3 * 4)
                    .sum::<usize>()
            } else {
                0
            }
    }
    pub fn advance_tracks(&mut self, player: &Player) {
        self.scene.tracks.advance(
            player.physics.powertrain.track_speed,
            player.physics.motion.angular[1],
            player.brake.reverse_braking,
        );
    }
    fn shaded(&self) -> bool {
        #[cfg(feature = "gpu-tests")]
        if let Some(value) = self.shade_override {
            return value;
        }
        self.scene.shaded
    }
    fn layer(
        &self,
        part: &tankracer_scene::vehicle::PartView,
        view: tankracer_game::math::Matrix,
    ) -> Option<tankracer_render::material::NormalLayer> {
        #[cfg(feature = "gpu-tests")]
        if !self.layer_enabled {
            return None;
        }
        self.scene.layer(part, view, self.shaded())
    }
    pub fn aerial_visible(&self) -> bool {
        #[cfg(feature = "gpu-tests")]
        if !self.aerial_enabled {
            return false;
        }
        self.scene.aerial_visible()
    }
    pub fn visible_parts(&self) -> usize {
        self.scene
            .parts
            .iter()
            .filter(|p| self.scene.visible(p))
            .count()
            + usize::from(self.aerial_visible()) * 4
    }
    pub fn shadow_draws(&self) -> usize {
        #[cfg(feature = "gpu-tests")]
        if !self.shadow_enabled {
            return 0;
        }
        usize::from(self.scene.shadow.visible && !self.scene.cockpit)
    }
    pub fn sight_draws(&self) -> usize {
        #[cfg(feature = "gpu-tests")]
        if !self.sight_enabled {
            return 0;
        }
        usize::from(self.scene.cockpit)
    }
    pub fn sight(&self, frame: &mut Frame<'_>, vp: Mat4, camera: &tankracer_scene::camera::Camera) {
        if self.sight_draws() != 0 {
            frame.draw_model(
                &self.sight,
                &crate::world::material(self.scene.sight_kind),
                Some(&self.atlas),
                vp,
                self.scene.sight_transform(camera),
            );
        }
    }
    pub fn shadow(&self, frame: &mut Frame<'_>, vp: Mat4) {
        if self.shadow_draws() == 0 {
            return;
        }
        let material = Material {
            pipeline: Pipeline {
                depth_write: false,
                blend: Some(Blend::STRAIGHT_ALPHA),
                cull: Cull::None,
                depth_bias: Some([-1, -1]),
                ..Pipeline::OPAQUE
            },
            ..Material::opaque([1., 1., 1., 128. / 255.])
        };
        frame.draw_model(
            &self.shadow,
            &material,
            None,
            vp,
            self.scene.shadow.transform,
        );
    }
    pub fn opaque_draws(&self) -> usize {
        self.parts
            .iter()
            .zip(&self.scene.parts)
            .filter(|(_, p)| self.scene.visible(p))
            .map(|(p, _)| {
                p.passes[..BatchKind::Transparent.slot()]
                    .iter()
                    .flatten()
                    .count()
            })
            .sum::<usize>()
            + if self.aerial_visible() {
                self.aerial
                    .as_ref()
                    .unwrap()
                    .passes
                    .iter()
                    .flatten()
                    .count()
            } else {
                0
            }
    }
    pub fn resident_bytes(&self) -> usize {
        std::mem::size_of::<Self>() - std::mem::size_of::<VehicleScene>()
            + self.scene.resident_bytes()
            + self.parts.capacity() * std::mem::size_of::<Part>()
    }
}
