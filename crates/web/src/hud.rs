//! Read-only HUD atlas and ordered batches. No game events are consumed by draws.
use glam::Mat4;
use tankracer_assets::{archive::Archive, surface::Surface};
use tankracer_game::session::Session;
use tankracer_render::{
    geometry::{MeshData, Vertex},
    gpu::{Frame, GpuMesh, Renderer, Texture},
};
use tankracer_scene::{
    atlas::{Atlas, Rect},
    hud::{self, Quad, Snapshot},
};
use wasm_bindgen::JsValue;
struct Batch {
    blend: bool,
    mesh: Option<GpuMesh>,
}
pub struct Hud {
    texture: Texture,
    size: [f32; 2],
    regions: [Option<Rect>; 21],
    plan: Vec<Quad>,
    batches: Vec<Batch>,
    visible: bool,
}
fn error(e: impl std::fmt::Display) -> JsValue {
    JsValue::from_str(&e.to_string())
}
impl Hud {
    pub fn load(
        renderer: &Renderer,
        common: &[u8],
        shared: &[u8],
        level: &[u8],
        map: usize,
    ) -> Result<Self, JsValue> {
        let common = Archive::parse(common).map_err(error)?;
        let shared = Archive::parse(shared).map_err(error)?;
        let level = Archive::parse(level).map_err(error)?;
        let map_name = hud::MAPS.get(map).ok_or("Invalid HUD map")?;
        let images = hud::RESOURCES
            .iter()
            .map(|(id, name)| {
                let name = if *id == 5 { *map_name } else { name };
                let bytes = common
                    .get(name)
                    .or_else(|| shared.get(name))
                    .or_else(|| level.get(name))
                    .ok_or_else(|| error(format!("Missing HUD {name}")))?;
                Surface::open(bytes)
                    .map_err(error)?
                    .decode(0)
                    .map_err(error)
            })
            .collect::<Result<Vec<_>, JsValue>>()?;
        let atlas = Atlas::pack(&images).map_err(error)?;
        let texture = renderer.upload_rgba(atlas.width, atlas.height, &atlas.premultiplied_rgba)?;
        let mut regions = [None; 21];
        for ((id, _), rect) in hud::RESOURCES.iter().zip(atlas.regions) {
            regions[*id] = Some(rect);
        }
        Ok(Self {
            texture,
            size: [atlas.width as f32, atlas.height as f32],
            regions,
            plan: vec![],
            batches: vec![],
            visible: false,
        })
    }
    pub fn refresh(&mut self, renderer: &mut Renderer, session: &Session) -> Result<(), JsValue> {
        let Some(snapshot) = Snapshot::from_session(session) else {
            self.visible = false;
            return Ok(());
        };
        self.visible = true;
        let plan = hud::plan(snapshot).map_err(error)?;
        if plan == self.plan {
            return Ok(());
        }
        let mut meshes: Vec<(bool, MeshData)> = vec![];
        for q in &plan {
            if meshes.last().is_none_or(|(blend, _)| *blend != q.blend) {
                meshes.push((q.blend, MeshData::default()));
            }
            let mesh = &mut meshes.last_mut().ok_or("Missing HUD run")?.1;
            let rect = self.regions[q.resource].ok_or("Unbound HUD texture")?;
            let start = mesh.vertices.len() as u32;
            for (pos, uv) in q.positions.iter().zip(q.uv) {
                mesh.vertices.push(Vertex {
                    position: [pos[0], pos[1], 0.],
                    color: q.color.map(|v| f32::from(v) / 255.),
                    uv: [
                        (rect.x as f32 + uv[0] * rect.width as f32) / self.size[0],
                        (rect.y as f32 + uv[1] * rect.height as f32) / self.size[1],
                    ],
                    billboard_offset: 0.,
                    double_sided: 1.,
                });
            }
            mesh.indices
                .extend([start, start + 1, start + 2, start, start + 2, start + 3]);
        }
        self.batches.resize_with(meshes.len(), || Batch {
            blend: false,
            mesh: None,
        });
        for (batch, (blend, mesh)) in self.batches.iter_mut().zip(meshes) {
            renderer.sync_mesh(&mut batch.mesh, &mesh, Default::default())?;
            batch.blend = blend;
        }
        self.plan = plan;
        Ok(())
    }
    pub fn draw(&self, frame: &mut Frame<'_>) {
        if !self.visible {
            return;
        }
        let projection = Mat4::orthographic_rh_gl(0., 640., 480., 0., -1., 1.);
        for batch in &self.batches {
            let Some(mesh) = &batch.mesh else {
                continue;
            };
            let material = hud::material(batch.blend);
            frame.draw(mesh, &material, Some(&self.texture), projection);
        }
    }
}
