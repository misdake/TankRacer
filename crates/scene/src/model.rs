//! Local-space mesh preparation. Current simulation geometry stays authoritative;
//! GPU prototypes and their texture atlas contain no gameplay state.
use crate::{
    atlas::Atlas,
    camera::from_world,
    world::{BatchKind, TransparentTriangle, legacy_color, winding},
};
use glam::Vec3;
use std::error::Error;
use tankracer_assets::{
    archive::Archive,
    mesh::{Geometry, SourceMaterial},
    surface::Surface,
};
use tankracer_render::geometry::{MeshData, Vertex};

pub struct Materials {
    pub atlas: Atlas,
    names: Vec<String>,
    classes: Vec<u32>,
}

pub struct Prepared {
    pub batches: [MeshData; 5],
    pub transparent: Vec<TransparentTriangle>,
    /// Optional wheel/tread V coefficients, atlas origin and byte-wrap period.
    pub uv_scroll: [Vec<[f32; 4]>; 5],
}
#[derive(Clone, Copy)]
pub(crate) struct UvRow {
    pub triangle: usize,
    pub corner: usize,
    pub base: u8,
    pub channel: usize,
}
impl Materials {
    /// Deterministic union includes preloaded replacement models, so a collision
    /// can select a private mesh without texture I/O during the frame.
    pub fn load<'a>(
        names: impl Iterator<Item = &'a str>,
        level: &[u8],
        shared: &[u8],
    ) -> Result<Self, Box<dyn Error>> {
        let level = Archive::parse(level)?;
        let shared = Archive::parse(shared)?;
        let mut names: Vec<_> = names.map(str::to_ascii_uppercase).collect();
        names.sort_unstable();
        names.dedup();
        let images = names
            .iter()
            .map(|name| {
                let file = format!("{name}.SUR");
                let bytes = level
                    .get(&file)
                    .or_else(|| shared.get(&file))
                    .ok_or_else(|| format!("Missing model material: {file}"))?;
                Ok(Surface::open(bytes)?.decode(0)?)
            })
            .collect::<Result<Vec<_>, Box<dyn Error>>>()?;
        let classes = images.iter().map(|i| i.flags & 3).collect();
        Ok(Self {
            atlas: Atlas::pack(&images)?,
            names,
            classes,
        })
    }
    pub fn release_pixels(&mut self) {
        self.atlas.premultiplied_rgba = Vec::new();
    }
    pub fn normalized_rect(&self, name: &str) -> Result<[f32; 4], Box<dyn Error>> {
        let i = self
            .names
            .binary_search(&name.to_ascii_uppercase())
            .map_err(|_| format!("Unbound material: {name}"))?;
        let r = self.atlas.regions[i];
        Ok([
            r.x as f32 / self.atlas.width as f32,
            r.y as f32 / self.atlas.height as f32,
            r.width as f32 / self.atlas.width as f32,
            r.height as f32 / self.atlas.height as f32,
        ])
    }
    /// Sprites use pixel UVs, unlike model faces' byte-normalized UVs.
    pub fn sprite(&self, name: &str, uv: [u8; 4]) -> Result<(MeshData, BatchKind), Box<dyn Error>> {
        let index = self
            .names
            .binary_search(&name.to_ascii_uppercase())
            .map_err(|_| format!("Unbound sprite material: {name}"))?;
        let rect = self.atlas.regions[index];
        let mut mesh = MeshData::quad(0.);
        for (v, pixel) in mesh.vertices.iter_mut().zip([
            [uv[0], uv[3]],
            [uv[2], uv[3]],
            [uv[2], uv[1]],
            [uv[0], uv[1]],
        ]) {
            v.uv = [
                (rect.x + u32::from(pixel[0])) as f32 / self.atlas.width as f32,
                (rect.y + u32::from(pixel[1])) as f32 / self.atlas.height as f32,
            ];
            v.double_sided = 1.;
        }
        Ok((mesh, BatchKind::for_surface(self.classes[index], true)?))
    }
    pub fn resident_bytes(&self) -> usize {
        std::mem::size_of::<Self>()
            + self.names.capacity() * std::mem::size_of::<String>()
            + self.names.iter().map(String::capacity).sum::<usize>()
            + self.classes.capacity() * 4
            + self.atlas.regions.capacity() * std::mem::size_of::<crate::atlas::Rect>()
            + self.atlas.premultiplied_rgba.capacity()
    }
    pub fn prepare(
        &self,
        geometry: &Geometry,
        materials: &[SourceMaterial],
    ) -> Result<Prepared, Box<dyn Error>> {
        self.prepare_animated(geometry, materials, &[])
    }

    pub(crate) fn prepare_animated(
        &self,
        geometry: &Geometry,
        materials: &[SourceMaterial],
        rows: &[UvRow],
    ) -> Result<Prepared, Box<dyn Error>> {
        self.prepare_vehicle(geometry, materials, rows, None)
            .map(|(prepared, _)| prepared)
    }

    pub(crate) fn prepare_vehicle(
        &self,
        geometry: &Geometry,
        materials: &[SourceMaterial],
        rows: &[UvRow],
        normals: Option<&[[i16; 3]]>,
    ) -> Result<(Prepared, crate::vehicle::layer::NormalBanks), Box<dyn Error>> {
        let bindings = materials
            .iter()
            .map(|m| {
                let key = m.name.to_ascii_uppercase();
                self.names
                    .binary_search(&key)
                    .map_err(|_| format!("Unbound material: {}", m.name))
            })
            .collect::<Result<Vec<_>, _>>()?;
        let mut prepared = Prepared {
            batches: std::array::from_fn(|_| MeshData::default()),
            transparent: Vec::new(),
            uv_scroll: std::array::from_fn(|_| Vec::new()),
        };
        let mut normal_banks = std::array::from_fn(|_| Vec::new());
        for (index, triangle) in geometry.triangles.iter().enumerate() {
            if triangle.flags & 64 != 0 {
                continue;
            }
            if triangle
                .indices
                .iter()
                .any(|&i| i as usize >= geometry.vertices.len())
            {
                return Err("Model vertex out of range".into());
            }
            let winding = winding(&geometry.vertices, triangle);
            if winding.is_none() && triangle.billboard.is_none() {
                continue;
            }
            let binding = triangle
                .material
                .map(|id| {
                    bindings
                        .get(id.index())
                        .copied()
                        .ok_or("Model material out of range")
                })
                .transpose()?;
            let class = binding.map_or(0, |i| self.classes[i]);
            if normals.is_some() && class == 2 && triangle.normal[1] > 1500 {
                return Err("Normal layer requires an opaque or keyed base material".into());
            }
            let double_sided = triangle.flags & 1 != 0 || triangle.billboard.is_some();
            let kind = BatchKind::for_surface(class, double_sided)?;
            let region = binding.map_or(self.atlas.white, |i| self.atlas.regions[i]);
            let mut scroll = [[0.; 4]; 3];
            let mut corners = std::array::from_fn::<_, 3, _>(|i| {
                let vertex = geometry.vertices[triangle.indices[i] as usize];
                let local = triangle
                    .billboard
                    .as_ref()
                    .map_or(vertex, |b| [b.center[0], vertex[1], b.center[1]]);
                let mut uv = triangle.uv[i];
                if let Some(row) = rows.iter().find(|r| r.triangle == index && r.corner == i) {
                    uv[1] = row.base;
                    let period = region.height as f32 / self.atlas.height as f32;
                    scroll[i][row.channel] =
                        period / 256. * if row.channel == 0 { 32. } else { 2. };
                    scroll[i][2] = region.y as f32 / self.atlas.height as f32;
                    scroll[i][3] = period;
                }
                Vertex {
                    position: from_world(local.map(i32::from)).to_array(),
                    color: legacy_color(triangle.color, binding.is_some()),
                    uv: self.atlas.uv(region, uv),
                    billboard_offset: triangle.billboard.as_ref().map_or(0., |b| {
                        f32::from(b.half_width) / 128.
                            * if b.positive_vertices & (1 << i) != 0 {
                                1.
                            } else {
                                -1.
                            }
                    }),
                    double_sided: f32::from(u8::from(double_sided)),
                }
            });
            if winding == Some(true) {
                corners.swap(1, 2);
                scroll.swap(1, 2);
            }
            if let Some(normals) = normals {
                let mut stream = [[0.; 4]; 3];
                for (out, index) in stream.iter_mut().zip(triangle.indices) {
                    let n = normals
                        .get(index as usize)
                        .ok_or("Missing authored vertex normal")?;
                    *out = [
                        f32::from(n[0]),
                        f32::from(n[1]),
                        f32::from(n[2]),
                        f32::from(u8::from(triangle.normal[1] > 1500)),
                    ];
                }
                if winding == Some(true) {
                    stream.swap(1, 2);
                }
                normal_banks[kind.slot()].extend(stream);
            }
            let batch = &mut prepared.batches[kind.slot()];
            let first = batch.vertices.len() as u32;
            let indices = [first, first + 1, first + 2];
            if kind == BatchKind::Transparent {
                let center = corners
                    .iter()
                    .map(|v| Vec3::from_array(v.position))
                    .sum::<Vec3>()
                    / 3.;
                prepared
                    .transparent
                    .push(TransparentTriangle { indices, center });
            }
            batch.vertices.extend(corners);
            batch.indices.extend(indices);
            if !rows.is_empty() {
                prepared.uv_scroll[kind.slot()].extend(scroll);
            }
        }
        Ok((prepared, normal_banks))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tankracer_assets::model::{Model, ShapeArchive};
    #[test]
    fn real_level_models_keep_materials_winding_and_current_deformation() {
        let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
        let shared = std::fs::read(root.join("INGAME/FASTFILE.DAT")).unwrap();
        let level = std::fs::read(root.join("LEVELS/ENGLAND/FASTFILE.DAT")).unwrap();
        let archive = Archive::parse(&level).unwrap();
        let mut models = Vec::new();
        for file in archive.entries().iter().filter(|e| e.name.ends_with(".SS")) {
            let shapes = ShapeArchive::parse(archive.get(file.name).unwrap()).unwrap();
            for shape in shapes.entries() {
                let bytes = shapes.get(shape.name).unwrap();
                if !bytes.is_empty() {
                    models.push(Model::decode(bytes).unwrap());
                }
            }
        }
        assert!(!models.is_empty());
        let materials = Materials::load(
            models
                .iter()
                .flat_map(|m| m.materials.iter().map(|m| m.name.as_str())),
            &level,
            &shared,
        )
        .unwrap();
        let mut triangles = 0;
        for model in &models {
            let before = materials
                .prepare(&model.geometry, &model.materials)
                .unwrap();
            let mut current = model.geometry.clone();
            // A private geometry edit must affect the render vertices while the
            // source model and its bindings remain immutable.
            for vertex in &mut current.vertices {
                vertex[0] += 128;
            }
            let after = materials.prepare(&current, &model.materials).unwrap();
            for (old, new) in before.batches.iter().zip(&after.batches) {
                assert_eq!(old.indices, new.indices);
                assert_eq!(old.vertices.len(), new.vertices.len());
                for (a, b) in old.vertices.iter().zip(&new.vertices) {
                    assert!((b.position[0] - a.position[0] - 1.).abs() < 1e-6);
                    assert_eq!(a.uv, b.uv);
                    assert_eq!(a.color, b.color);
                }
                triangles += old.indices.len() / 3;
            }
            for face in &mut current.triangles {
                face.flags |= 64;
            }
            assert!(
                materials
                    .prepare(&current, &model.materials)
                    .unwrap()
                    .batches
                    .iter()
                    .all(|b| b.indices.is_empty())
            );
        }
        assert!(triangles > 100);
        eprintln!(
            "model render preparation: {} models / {triangles} triangles",
            models.len()
        );
    }
}
