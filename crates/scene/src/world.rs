//! Static batches retain source face ownership. Sorting changes indices only.
use crate::{
    atlas::Atlas,
    camera::{Camera, WORLD_UNITS, from_world},
};
use glam::Vec3;
use std::{collections::HashMap, error::Error};
use tankracer_assets::{
    archive::Archive,
    budget::MapBudget,
    surface::{Image, Surface},
    world::{Triangle, World},
};
use tankracer_render::geometry::{MeshData, Vertex};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BatchKind {
    Opaque,
    OpaqueTwoSided,
    Masked,
    MaskedTwoSided,
    Transparent,
}

impl BatchKind {
    pub const ALL: [Self; 5] = [
        Self::Opaque,
        Self::OpaqueTwoSided,
        Self::Masked,
        Self::MaskedTwoSided,
        Self::Transparent,
    ];
    pub const fn slot(self) -> usize {
        self as usize
    }
    pub(crate) fn for_surface(class: u32, double_sided: bool) -> Result<Self, &'static str> {
        match (class, double_sided) {
            (0, false) => Ok(Self::Opaque),
            (0, true) => Ok(Self::OpaqueTwoSided),
            (1, false) => Ok(Self::Masked),
            (1, true) => Ok(Self::MaskedTwoSided),
            (2, _) => Ok(Self::Transparent),
            _ => Err("Unsupported material class"),
        }
    }
}

#[derive(Debug, Clone, Copy)]
pub struct FaceSource {
    pub mesh_offset: u32,
    pub triangle_offset: u32,
    pub first_index: u32,
}

#[derive(Debug)]
pub struct Batch {
    pub kind: BatchKind,
    pub geometry: MeshData,
    pub sources: Vec<FaceSource>,
}

#[derive(Debug)]
pub struct TransparentTriangle {
    pub indices: [u32; 3],
    pub center: Vec3,
}

#[derive(Debug, Default)]
pub struct TransparencyOrder {
    keys: Vec<(f32, usize)>,
    pub indices: Vec<u32>,
    last_camera: Option<[u32; 6]>,
}

impl TransparencyOrder {
    fn new(count: usize) -> Self {
        Self {
            keys: Vec::with_capacity(count),
            indices: Vec::with_capacity(count * 3),
            last_camera: None,
        }
    }

    pub fn update(&mut self, triangles: &[TransparentTriangle], camera: &Camera) -> bool {
        let forward = camera.forward();
        let state = [
            camera.position.x.to_bits(),
            camera.position.y.to_bits(),
            camera.position.z.to_bits(),
            forward.x.to_bits(),
            forward.y.to_bits(),
            forward.z.to_bits(),
        ];
        if self.last_camera == Some(state) {
            return false;
        }
        self.keys.clear();
        self.indices.clear();
        self.keys.extend(
            triangles
                .iter()
                .enumerate()
                .map(|(i, t)| ((t.center - camera.position).dot(forward), i)),
        );
        self.keys
            .sort_unstable_by(|a, b| b.0.total_cmp(&a.0).then(a.1.cmp(&b.1)));
        for &(_, i) in &self.keys {
            self.indices.extend(triangles[i].indices);
        }
        self.last_camera = Some(state);
        true
    }
    fn resident_bytes(&self) -> usize {
        self.keys.capacity() * std::mem::size_of::<(f32, usize)>() + self.indices.capacity() * 4
    }
}

#[derive(Debug, Default)]
pub struct SceneStats {
    pub source_meshes: usize,
    pub source_triangles: usize,
    pub rendered_triangles: usize,
    pub hidden_triangles: usize,
    pub disabled_triangles: usize,
    pub degenerate_triangles: usize,
    pub billboard_triangles: usize,
    pub unique_vertices: usize,
    pub vertex_bytes: usize,
    pub index_bytes: usize,
    pub acceleration_bytes: usize,
}

#[derive(Debug)]
pub struct WorldScene {
    pub atlas: Atlas,
    pub batches: Vec<Batch>,
    pub transparent: Vec<TransparentTriangle>,
    pub order: TransparencyOrder,
    pub minimum: Vec3,
    pub maximum: Vec3,
    pub materials: Vec<String>,
    pub stats: SceneStats,
}

#[derive(Default)]
struct Builder {
    geometry: MeshData,
    lookup: HashMap<[u8; std::mem::size_of::<Vertex>()], u32>,
    sources: Vec<FaceSource>,
}

impl Builder {
    fn triangle(&mut self, corners: [Vertex; 3], mesh: u32, triangle: u32) -> [u32; 3] {
        let first = self.geometry.indices.len() as u32;
        let indices = corners.map(|vertex| {
            let key = bytemuck::cast::<Vertex, [u8; std::mem::size_of::<Vertex>()]>(vertex);
            *self.lookup.entry(key).or_insert_with(|| {
                let id = self.geometry.vertices.len() as u32;
                self.geometry.vertices.push(vertex);
                id
            })
        });
        self.geometry.indices.extend(indices);
        self.sources.push(FaceSource {
            mesh_offset: mesh,
            triangle_offset: triangle,
            first_index: first,
        });
        indices
    }
}

impl WorldScene {
    pub fn load_archive(bytes: &[u8]) -> Result<Self, Box<dyn Error>> {
        let archive = Archive::parse(bytes)?;
        let file = archive
            .entries()
            .iter()
            .find(|e| e.name.ends_with(".BSM"))
            .ok_or("Level has no BSM world")?;
        let world = World::decode(archive.get(file.name).ok_or("World bytes missing")?)?;
        let mut images = Vec::<Image>::with_capacity(world.materials.len());
        for material in &world.materials {
            let name = format!("{}.SUR", material.name.to_ascii_uppercase());
            let bytes = archive
                .get(&name)
                .ok_or_else(|| format!("Missing level material resource: {name}"))?;
            images.push(Surface::open(bytes)?.decode(0)?);
        }
        let atlas = Atlas::pack(&images)?;
        let mut builders: [Builder; 5] = std::array::from_fn(|_| Builder::default());
        let mut transparent = Vec::new();
        let mut minimum = Vec3::splat(f32::INFINITY);
        let mut maximum = Vec3::splat(f32::NEG_INFINITY);
        let mut stats = SceneStats {
            source_meshes: world.meshes.len(),
            ..SceneStats::default()
        };
        for mesh in &world.meshes {
            stats.source_triangles += mesh.triangles.len();
            if mesh.flags & 1 != 0 {
                stats.hidden_triangles += mesh.triangles.len();
                continue;
            }
            for triangle in &mesh.triangles {
                if triangle.flags & 64 != 0 {
                    stats.disabled_triangles += 1;
                    continue;
                }
                let winding = winding(&mesh.vertices, triangle);
                if triangle.billboard.is_none() && winding.is_none() {
                    stats.degenerate_triangles += 1;
                    continue;
                }
                let double_sided = triangle.flags & 1 != 0 || triangle.billboard.is_some();
                let class = triangle
                    .material
                    .map_or(0, |id| images[id.index()].flags & 3);
                let group = BatchKind::for_surface(class, double_sided)?;
                let color = legacy_color(triangle.color, triangle.material.is_some());
                let region = triangle
                    .material
                    .map_or(atlas.white, |id| atlas.regions[id.index()]);
                let mut corners = std::array::from_fn(|i| {
                    let vertex = mesh.vertices[triangle.indices[i] as usize];
                    let local = if let Some(billboard) = &triangle.billboard {
                        [billboard.center[0], vertex[1], billboard.center[1]]
                    } else {
                        vertex
                    };
                    let point = from_world(std::array::from_fn(|axis| {
                        mesh.position[axis].wrapping_add(i32::from(local[axis]))
                    }));
                    let radius = triangle.billboard.as_ref().map_or(Vec3::ZERO, |b| {
                        Vec3::new(
                            f32::from(b.half_width) / WORLD_UNITS,
                            0.,
                            f32::from(b.half_width) / WORLD_UNITS,
                        )
                    });
                    minimum = minimum.min(point - radius);
                    maximum = maximum.max(point + radius);
                    let billboard_offset = triangle.billboard.as_ref().map_or(0., |b| {
                        f32::from(b.half_width) / WORLD_UNITS
                            * if b.positive_vertices & (1 << i) != 0 {
                                1.
                            } else {
                                -1.
                            }
                    });
                    Vertex {
                        position: point.to_array(),
                        color,
                        uv: atlas.uv(region, triangle.uv[i]),
                        billboard_offset,
                        double_sided: f32::from(u8::from(double_sided)),
                    }
                });
                // Z reflection changes handedness. Use the source normal to
                // establish winding once; software normal tests leave the frame.
                if winding == Some(true) {
                    corners.swap(1, 2);
                }
                let center = corners
                    .iter()
                    .map(|v| Vec3::from_array(v.position))
                    .sum::<Vec3>()
                    / 3.;
                let indices = builders[group.slot()].triangle(
                    corners,
                    mesh.source_offset,
                    triangle.source_offset,
                );
                if group == BatchKind::Transparent {
                    transparent.push(TransparentTriangle { indices, center });
                }
                stats.rendered_triangles += 1;
                stats.billboard_triangles += usize::from(triangle.billboard.is_some());
            }
        }
        if stats.rendered_triangles == 0 || !minimum.is_finite() || !maximum.is_finite() {
            return Err("World contains no visible finite geometry".into());
        }
        let batches: Vec<_> = builders
            .into_iter()
            .zip(BatchKind::ALL)
            .map(|(b, kind)| Batch {
                kind,
                geometry: b.geometry,
                sources: b.sources,
            })
            .collect();
        let order = TransparencyOrder::new(transparent.len());
        let mut budget = MapBudget::default();
        for batch in &batches {
            stats.unique_vertices += batch.geometry.vertices.len();
            stats.vertex_bytes += batch.geometry.vertices.len() * std::mem::size_of::<Vertex>();
            stats.index_bytes += batch.geometry.indices.len() * 4;
            // Retained CPU index copies and face ownership are explicitly counted.
            budget.account(
                batch.geometry.indices.capacity() * 4
                    + batch.sources.capacity() * std::mem::size_of::<FaceSource>(),
            )?;
        }
        budget.account(
            transparent.capacity() * std::mem::size_of::<TransparentTriangle>()
                + order.resident_bytes(),
        )?;
        let materials: Vec<_> = world.materials.into_iter().map(|m| m.name).collect();
        budget.account(
            std::mem::size_of::<Self>()
                + batches.capacity() * std::mem::size_of::<Batch>()
                + atlas.regions.capacity() * std::mem::size_of::<crate::atlas::Rect>()
                + materials.capacity() * std::mem::size_of::<String>()
                + materials.iter().map(String::capacity).sum::<usize>(),
        )?;
        stats.acceleration_bytes = budget.bytes();
        Ok(Self {
            atlas,
            batches,
            transparent,
            order,
            minimum,
            maximum,
            materials,
            stats,
        })
    }

    /// A live renderer with one joint world/actor sorter no longer needs this
    /// static-only cache. Release its heap and return the exact accounted bytes.
    /// Returning to static inspection must load a fresh scene (or reaccount a
    /// rebuilt cache); mesh/atlas/source ownership stays intact.
    pub fn release_static_order(&mut self) -> usize {
        let bytes = self.order.resident_bytes();
        self.order = TransparencyOrder::default();
        self.stats.acceleration_bytes -= bytes;
        bytes
    }

    pub fn inspection_camera(&self) -> Camera {
        let center = (self.minimum + self.maximum) * 0.5;
        let extent = (self.maximum - self.minimum).max_element();
        Camera::looking_at(center + Vec3::new(0.25, 0.65, 0.7) * extent, center)
    }
}

pub(crate) fn legacy_color(packed: u16, textured: bool) -> [f32; 4] {
    if textured && packed == 0x7bde {
        return [1.; 4];
    }
    let p = u32::from(packed);
    let expand = |shift: u32| -> u32 {
        let c = ((p >> shift) & 31u32) << 3;
        if textured { (c << 1) & 255 } else { c }
    };
    [
        expand(1) as f32 / 255.,
        expand(6) as f32 / 255.,
        expand(11) as f32 / 255.,
        1.,
    ]
}

pub(crate) fn winding(vertices: &[[i16; 3]], triangle: &Triangle) -> Option<bool> {
    let points = triangle
        .indices
        .map(|i| vertices[i as usize].map(i64::from));
    let a: [i64; 3] = std::array::from_fn(|i| points[1][i] - points[0][i]);
    let b: [i64; 3] = std::array::from_fn(|i| points[2][i] - points[0][i]);
    let cross = [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ];
    if cross == [0; 3] {
        return None;
    }
    Some(
        cross
            .iter()
            .zip(triangle.normal)
            .map(|(&c, n)| c * i64::from(n))
            .sum::<i64>()
            > 0,
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn england_build_is_complete_atlas_batched_and_under_budget() {
        let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../../assets/game/LEVELS/ENGLAND/FASTFILE.DAT");
        let mut scene = WorldScene::load_archive(&std::fs::read(path).unwrap()).unwrap();
        assert_eq!(scene.stats.source_meshes, 612);
        assert_eq!(scene.stats.source_triangles, 8881);
        assert_eq!(
            scene.stats.source_triangles,
            scene.stats.rendered_triangles
                + scene.stats.hidden_triangles
                + scene.stats.disabled_triangles
                + scene.stats.degenerate_triangles
        );
        assert!(scene.stats.acceleration_bytes < 1_000_000);
        assert!(scene.stats.billboard_triangles > 0);
        assert!(
            scene
                .batches
                .iter()
                .filter(|b| !b.geometry.indices.is_empty())
                .count()
                <= 5
        );
        assert!(scene.stats.unique_vertices < scene.stats.rendered_triangles * 3);
        let camera = scene.inspection_camera();
        assert!(scene.order.update(&scene.transparent, &camera));
        assert!(!scene.order.update(&scene.transparent, &camera));
        assert_eq!(scene.order.indices.len(), scene.transparent.len() * 3);
        // Switching to the joint world/actor sorter preserves the exact world
        // order while releasing the redundant static cache, including capacity.
        let expected = scene.order.indices.clone();
        let before = scene.stats.acceleration_bytes;
        let released = scene.release_static_order();
        assert!(released > expected.len() * 4);
        assert_eq!(scene.stats.acceleration_bytes, before - released);
        assert_eq!(scene.release_static_order(), 0);
        assert_eq!(scene.order.resident_bytes(), 0);
        let mut joint = crate::transparency::Order::default();
        assert!(joint.update(
            &[crate::transparency::Object {
                geometry: 0,
                model: glam::Mat4::IDENTITY
            }],
            |_| &scene.transparent,
            &camera,
            0
        ));
        assert_eq!(joint.banks[0], expected);
        eprintln!(
            "ENGLAND: {:?}, atlas {}x{}",
            scene.stats, scene.atlas.width, scene.atlas.height
        );
    }
}
