use glam::{Mat4, Vec3};
use std::sync::Arc;
use tankracer_assets::{archive::Archive, budget::MapBudget, terrain::TerrainData};
use tankracer_render::{
    gpu::{GpuMesh, Renderer, Texture},
    material::{AlphaEncoding, Blend, Cull, Material, Pipeline},
};
use tankracer_scene::{
    camera::Camera,
    transparency::{self, Order},
    world::{BatchKind, WorldScene},
};
use wasm_bindgen::JsValue;

pub struct WorldView {
    pub camera: Camera,
    pub terrain: Arc<TerrainData>,
    acceleration_bytes: usize,
    scene: WorldScene,
    meshes: Vec<GpuMesh>,
    atlas: Texture,
    order: Order,
    transparent_objects: Vec<transparency::Object>,
    scenery_bytes: usize,
    route_bytes: usize,
    scenery_objects: usize,
    scenery_triangles: usize,
    scenery_opaque_draws: usize,
    vehicle_parts: usize,
    vehicle_triangles: usize,
    vehicle_opaque_draws: usize,
    vehicle_bytes: usize,
}

impl WorldView {
    pub fn load(renderer: &Renderer, bytes: &[u8]) -> Result<Self, JsValue> {
        let mut scene =
            WorldScene::load_archive(bytes).map_err(|e| JsValue::from_str(&e.to_string()))?;
        let archive = Archive::parse(bytes).map_err(|e| JsValue::from_str(&e.to_string()))?;
        let file = archive
            .entries()
            .iter()
            .find(|e| e.name.ends_with(".BSM"))
            .ok_or_else(|| JsValue::from_str("Level has no terrain"))?;
        let terrain = Arc::new(
            TerrainData::decode_bsm(archive.get(file.name).unwrap())
                .map_err(|e| JsValue::from_str(&e.to_string()))?,
        );
        let mut budget = MapBudget::default();
        budget
            .account(scene.stats.acceleration_bytes)
            .map_err(JsValue::from_str)?;
        budget
            .account(terrain.resident_bytes() + 2 * std::mem::size_of::<usize>())
            .map_err(JsValue::from_str)?;
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
        budget
            .account(
                std::mem::size_of::<Self>() - std::mem::size_of::<WorldScene>()
                    + meshes.capacity() * std::mem::size_of::<GpuMesh>(),
            )
            .map_err(JsValue::from_str)?;
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
            terrain,
            acceleration_bytes: budget.bytes(),
            order: Order::default(),
            transparent_objects: Vec::new(),
            scenery_bytes: 0,
            route_bytes: 0,
            scenery_objects: 0,
            scenery_triangles: 0,
            scenery_opaque_draws: 0,
            vehicle_parts: 0,
            vehicle_triangles: 0,
            vehicle_opaque_draws: 0,
            vehicle_bytes: 0,
        })
    }

    pub fn draw(
        &mut self,
        renderer: &mut Renderer,
        size: [u32; 2],
        mut session: Option<&mut crate::vehicle::Session>,
    ) -> Result<(), JsValue> {
        if let Some(session) = &mut session {
            session.scenery.refresh(renderer, &session.game.objects)?;
            let player = session.game.vehicles.entries[session.game.player.0]
                .as_ref()
                .ok_or("Missing render player")?;
            session.view.refresh(renderer, player)?;
            let vehicle_base = session.scenery.geometry_count() + 1;
            self.transparent_objects.clear();
            self.transparent_objects.push(transparency::Object {
                geometry: 0,
                model: Mat4::IDENTITY,
            });
            session
                .scenery
                .transparent_objects(&mut self.transparent_objects);
            session
                .view
                .transparent_objects(vehicle_base, &mut self.transparent_objects);
            if self.order.update(
                &self.transparent_objects,
                |id| {
                    if id == 0 {
                        &self.scene.transparent
                    } else if id < vehicle_base {
                        session.scenery.transparent(id)
                    } else {
                        session.view.transparent(id - vehicle_base)
                    }
                },
                &self.camera,
                session
                    .scenery
                    .revision
                    .wrapping_add(session.view.scene.revision),
            ) {
                for (geometry, indices) in self
                    .order
                    .banks
                    .iter()
                    .enumerate()
                    .filter(|(_, i)| !i.is_empty())
                {
                    if geometry == 0 {
                        renderer.update_indices(
                            &mut self.meshes[BatchKind::Transparent.slot()],
                            indices,
                        )?;
                    } else if geometry < vehicle_base {
                        session
                            .scenery
                            .update_indices(renderer, geometry, indices)?;
                    } else {
                        session
                            .view
                            .update_indices(renderer, geometry - vehicle_base, indices)?;
                    }
                }
            }
            self.route_bytes = session.game.objects.components.waypoints.resident_bytes();
            self.scenery_bytes = session.scenery.resident_bytes()
                + session
                    .game
                    .objects
                    .components
                    .surfaces
                    .meshes
                    .render_metadata_bytes()
                + self.order.resident_bytes()
                + self.transparent_objects.capacity() * std::mem::size_of::<transparency::Object>();
            let mut budget = MapBudget::default();
            budget
                .account(self.acceleration_bytes + self.scenery_bytes + self.route_bytes)
                .map_err(JsValue::from_str)?;
            self.scenery_objects = session.scenery.objects;
            self.scenery_triangles = session.scenery.triangles;
            self.scenery_opaque_draws = session.scenery.opaque_draws;
            self.vehicle_parts = session.view.scene.parts.len();
            self.vehicle_triangles = session.view.triangles();
            self.vehicle_opaque_draws = session.view.opaque_draws();
            self.vehicle_bytes = session.view.resident_bytes();
        } else {
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
        }
        let vp = self.camera.view_projection(size[0] as f32 / size[1] as f32);
        let right = self.camera.right();
        let mut frame = renderer.begin_frame(None, size, [0.48, 0.62, 0.72, 1.]);
        frame.set_billboard_right([right.x, right.z]);
        for (batch, mesh) in self
            .scene
            .batches
            .iter()
            .zip(&self.meshes)
            .filter(|(b, _)| b.kind != BatchKind::Transparent)
        {
            if batch.geometry.indices.is_empty() {
                continue;
            }
            let material = material(batch.kind);
            frame.draw(mesh, &material, Some(&self.atlas), vp);
        }
        // Dynamic opaque geometry must write depth before blended world faces.
        if let Some(session) = &session {
            session.scenery.opaque(&mut frame, vp);
            session.view.opaque(&mut frame, vp);
        }
        let alpha = BatchKind::Transparent.slot();
        if let Some(session) = &session {
            for run in &self.order.runs {
                let object = self.order.objects[run.object];
                let range = run.first..run.first + run.count;
                if object.geometry == 0 {
                    frame.draw_range(
                        &self.meshes[alpha],
                        &material(BatchKind::Transparent),
                        Some(&self.atlas),
                        [vp, Mat4::IDENTITY],
                        range,
                    )?;
                } else if object.geometry <= session.scenery.geometry_count() {
                    session
                        .scenery
                        .draw_transparent(&mut frame, object, vp, range)?;
                } else {
                    session.view.draw_transparent(
                        &mut frame,
                        object.geometry - session.scenery.geometry_count() - 1,
                        vp,
                        range,
                    )?;
                }
            }
        } else if !self.scene.batches[alpha].geometry.indices.is_empty() {
            frame.draw(
                &self.meshes[alpha],
                &material(BatchKind::Transparent),
                Some(&self.atlas),
                vp,
            );
        }
        Ok(())
    }

    pub fn move_camera(&mut self, seconds: f32, x: f32, y: f32, z: f32, fast: bool) {
        self.camera.move_local(Vec3::new(x, y, z), seconds, fast);
    }

    pub fn reset_dynamic(&mut self) {
        // App replaces the whole WorldView when returning to static inspection.
        // Once a session is installed, only the joint world/actor sorter is used.
        self.acceleration_bytes -= self.scene.release_static_order();
        // A new session owns new GPU prototypes even at identical poses. Never
        // reuse the previous session's sorted-bank cache against those buffers.
        self.order = Order::default();
        self.transparent_objects.clear();
        self.scenery_bytes = 0;
        self.route_bytes = 0;
        self.scenery_objects = 0;
        self.scenery_triangles = 0;
        self.scenery_opaque_draws = 0;
        self.vehicle_parts = 0;
        self.vehicle_triangles = 0;
        self.vehicle_opaque_draws = 0;
        self.vehicle_bytes = 0;
    }

    pub fn stats(&self) -> String {
        let s = &self.scene.stats;
        format!(
            "{{\"source_meshes\":{},\"source_triangles\":{},\"rendered_triangles\":{},\"billboard_triangles\":{},\"materials\":{},\"unique_vertices\":{},\"vertex_bytes\":{},\"index_bytes\":{},\"acceleration_bytes\":{},\"atlas_width\":{},\"atlas_height\":{},\"render_acceleration_bytes\":{},\"collision_acceleration_bytes\":{},\"route_acceleration_bytes\":{},\"scenery_objects\":{},\"scenery_triangles\":{},\"scenery_acceleration_bytes\":{},\"scenery_opaque_draws\":{},\"transparent_runs\":{},\"vehicle_parts\":{},\"vehicle_triangles\":{},\"vehicle_opaque_draws\":{},\"vehicle_render_bytes\":{}}}",
            s.source_meshes,
            s.source_triangles,
            s.rendered_triangles,
            s.billboard_triangles,
            self.scene.materials.len(),
            s.unique_vertices,
            s.vertex_bytes,
            s.index_bytes,
            self.acceleration_bytes + self.scenery_bytes + self.route_bytes,
            self.scene.atlas.width,
            self.scene.atlas.height,
            self.acceleration_bytes + self.scenery_bytes
                - self.terrain.resident_bytes()
                - 2 * std::mem::size_of::<usize>(),
            self.terrain.resident_bytes() + 2 * std::mem::size_of::<usize>(),
            self.route_bytes,
            self.scenery_objects,
            self.scenery_triangles,
            self.scenery_bytes,
            self.scenery_opaque_draws,
            self.order.runs.len(),
            self.vehicle_parts,
            self.vehicle_triangles,
            self.vehicle_opaque_draws,
            self.vehicle_bytes
        )
    }
}

pub(crate) fn material(kind: BatchKind) -> Material {
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
