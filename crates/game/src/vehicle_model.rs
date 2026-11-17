//! Authored model assembly and geometry-derived vehicle metadata (4350A0).
pub use crate::geometry::Bounds;
use crate::math;
use std::{collections::HashMap, sync::Arc};
use tankracer_assets::{
    config::{Gain, Part, ResolveContext, ResolvedTank, Surfaces, Tank},
    mesh::{Geometry, MaterialId},
    model::{Model, ShapeArchive},
};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Space {
    Vehicle,
    Shared,
    Level,
}
pub trait ShapeSource {
    fn shape(&self, space: Space, name: &str) -> Option<&[u8]>;
}
pub struct Bundles<'a> {
    pub vehicle: Vec<ShapeArchive<'a>>,
    pub shared: Vec<ShapeArchive<'a>>,
    pub level: Vec<ShapeArchive<'a>>,
}
impl ShapeSource for Bundles<'_> {
    fn shape(&self, space: Space, name: &str) -> Option<&[u8]> {
        let bundles = match space {
            Space::Vehicle => &self.vehicle,
            Space::Shared => &self.shared,
            Space::Level => &self.level,
        };
        bundles.iter().find_map(|bundle| bundle.get(name))
    }
}
#[derive(Debug)]
pub enum Error {
    Config(tankracer_assets::config::Error),
    Mesh(tankracer_assets::mesh::Error),
    Missing(String),
    RequiredPart(Part),
    InvalidShadow,
}
#[derive(Default)]
pub struct ModelBank {
    // The original name cache is global within a world: first namespace binding
    // wins even if a later request uses a different namespace. Geometry handles
    // remain shared independently of the caller's lookup category.
    models: HashMap<String, Arc<Model>>,
}
impl ModelBank {
    pub fn models(&self) -> impl Iterator<Item = &Arc<Model>> {
        self.models.values()
    }
    pub fn get(&self, name: &str) -> Option<&Arc<Model>> {
        self.models.get(&name.to_ascii_lowercase())
    }
    /// Scene file loading (500850) permits an absent/empty resource to yield a
    /// null shape. Callers requiring geometry must validate that separately.
    pub fn load_optional(
        &mut self,
        source: &impl ShapeSource,
        space: Space,
        name: &str,
    ) -> Result<Option<Arc<Model>>, Error> {
        match self.load(source, space, name) {
            Err(Error::Missing(_)) => Ok(None),
            result => result,
        }
    }
    pub fn load(
        &mut self,
        source: &impl ShapeSource,
        space: Space,
        name: &str,
    ) -> Result<Option<Arc<Model>>, Error> {
        if name.eq_ignore_ascii_case("null") {
            return Ok(None);
        }
        let key = name.to_ascii_lowercase();
        if let Some(model) = self.models.get(&key) {
            return Ok(Some(Arc::clone(model)));
        }
        let bytes = source
            .shape(space, name)
            .ok_or_else(|| Error::Missing(name.into()))?;
        if bytes.is_empty() {
            return Ok(None);
        }
        let model = Arc::new(Model::decode(bytes).map_err(Error::Mesh)?);
        self.models.insert(key, Arc::clone(&model));
        Ok(Some(model))
    }
}
#[derive(Debug)]
pub struct Mesh {
    pub source: Arc<Model>,
    pub(crate) owned: Option<Geometry>,
    revision: u64,
    pub radius: u16,
}
impl Mesh {
    fn new(source: Arc<Model>, owned: bool) -> Self {
        Self {
            radius: source.radius,
            owned: owned.then(|| source.geometry.clone()),
            revision: 1,
            source,
        }
    }
    pub fn geometry(&self) -> &Geometry {
        self.owned.as_ref().unwrap_or(&self.source.geometry)
    }
    pub fn is_private(&self) -> bool {
        self.owned.is_some()
    }
    pub fn revision(&self) -> u64 {
        self.revision
    }
    /// Explicit geometry edits invalidate presentation even if the edit fails.
    pub fn edit<R>(&mut self, edit: impl FnOnce(&mut Geometry) -> R) -> Option<R> {
        let geometry = self.owned.as_mut()?;
        self.revision = self.revision.wrapping_add(1);
        Some(edit(geometry))
    }
    pub(crate) fn changed(&mut self) {
        self.revision = self.revision.wrapping_add(1);
    }
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct UvRef {
    pub triangle: u16,
    pub corner: u8,
    pub snapped_v: u8,
}
#[derive(Debug, Default)]
pub struct TrackUvs {
    /// Flat and side material identities are local to this model's material table.
    pub materials: [Option<MaterialId>; 2],
    /// Side high/low (6 each), flat high/low (24 each), in original selection order.
    pub groups: [Vec<UvRef>; 4],
}
impl TrackUvs {
    fn build(model: &Model) -> Self {
        let mut out = Self::default();
        for (index, tri) in model.geometry.triangles.iter().enumerate() {
            let nx = i32::from(tri.normal[0]);
            let flat = nx < 1001 && nx > -1001;
            if flat && (nx.abs() >= 64 || tri.normal[1] <= -3500) {
                continue;
            }
            let material = usize::from(!flat);
            if out.materials[material].is_none() {
                out.materials[material] = tri.material;
            }
            let low = tri.uv.map(|uv| uv[1]).into_iter().min().unwrap_or(0);
            let high = tri.uv.map(|uv| uv[1]).into_iter().max().unwrap_or(0);
            for corner in 0..3 {
                let lower = tri.uv[corner][1] == low;
                let group = usize::from(flat) * 2 + usize::from(lower);
                let cap = if flat { 24 } else { 6 };
                if out.groups[group].len() < cap {
                    let v = if lower { low } else { high };
                    out.groups[group].push(UvRef {
                        triangle: index as u16,
                        corner: corner as u8,
                        snapped_v: if flat { (v / 10) * 10 } else { 0 },
                    });
                }
            }
        }
        out
    }
}

#[derive(Debug, Clone, Copy)]
pub struct BuildContext {
    pub resolve: ResolveContext,
    pub ambient_wobble: bool,
    pub session_active: bool,
}
#[derive(Debug)]
pub struct VehicleModel {
    pub config: ResolvedTank,
    pub moon: tankracer_assets::object_catalog::Moon,
    pub parts: [Option<Mesh>; 10],
    pub offsets: [[i32; 3]; 10],
    pub primary_local: [[i32; 3]; 4],
    pub auxiliary_local: [[i32; 3]; 2],
    pub track_dimensions: [i32; 3],
    pub geometry_bounds: Bounds,
    pub collision_bounds: Bounds,
    pub query_bounds: [Bounds; 2],
    pub aerial_height: i32,
    pub gun_vertices: [u8; 4],
    pub gun_original_z: [i16; 4],
    pub damage_vertices: [[u8; 4]; 2],
    pub shadow: Mesh,
    pub tracks: [TrackUvs; 2],
    pub alternate_control: [i16; 2],
    pub reset_sidegrip: Gain,
}
const MODEL_PARTS: [Part; 9] = [
    Part::Body,
    Part::Turret,
    Part::Gun,
    Part::LeftTrack,
    Part::RightTrack,
    Part::Under,
    Part::End,
    Part::Aerial,
    Part::Bottom,
];
impl VehicleModel {
    pub fn pose_config(&self, gun_pivot: [i32; 3]) -> crate::pose::Config {
        crate::pose::Config {
            track_offsets: [
                self.offsets[Part::LeftTrack.slot()],
                self.offsets[Part::RightTrack.slot()],
            ],
            turret_offset: self.offsets[Part::Turret.slot()],
            gun_offset: self.offsets[Part::Gun.slot()],
            attachment_offsets: [
                self.offsets[Part::Under.slot()],
                self.offsets[Part::End.slot()],
            ],
            bottom_offset: self.offsets[Part::Bottom.slot()],
            track_length: self.track_dimensions[2],
            travel_gain: self.config.tank.suspension.susp_to_shape,
            gun_pivot,
        }
    }
    pub fn build(
        tank: &Tank,
        source: &impl ShapeSource,
        bank: &mut ModelBank,
        surfaces: &Surfaces,
        context: BuildContext,
    ) -> Result<Self, Error> {
        let mut config = tank.resolve(context.resolve).map_err(Error::Config)?;
        let mut parts: [Option<Mesh>; 10] = std::array::from_fn(|_| None);
        for part in MODEL_PARTS.into_iter().chain([Part::Distance]) {
            let space = if part == Part::Aerial {
                Space::Shared
            } else {
                Space::Vehicle
            };
            let owned = part == Part::Gun
                || (context.resolve.player && matches!(part, Part::Body | Part::Turret));
            parts[part.slot()] = bank
                .load(source, space, &tank.nodes[part.slot()].name)?
                .map(|model| Mesh::new(model, owned));
        }
        let required = |part: Part| parts[part.slot()].as_ref().ok_or(Error::RequiredPart(part));
        let left = required(Part::LeftTrack)?;
        let right = required(Part::RightTrack)?;
        let tracks = [
            TrackUvs::build(&left.source),
            TrackUvs::build(&right.source),
        ];
        let track_dimensions = std::array::from_fn(|i| {
            (i32::from(left.source.bounds[1][i]) - i32::from(left.source.bounds[0][i])) * 8
        });
        let mut offsets = tank.nodes.each_ref().map(|node| node.offset);
        let body = offsets[Part::Body.slot()];
        for part in MODEL_PARTS {
            if !matches!(part, Part::Body | Part::Aerial | Part::Bottom) {
                for (value, base) in offsets[part.slot()].iter_mut().zip(body) {
                    *value = value.wrapping_sub(base);
                }
            }
        }
        offsets[Part::Body.slot()] = [0; 3];
        let turret = offsets[Part::Turret.slot()];
        for (value, base) in offsets[Part::Gun.slot()].iter_mut().zip(turret) {
            *value = value.wrapping_sub(base);
        }
        let mut geometry_bounds = Bounds {
            min: [16000000; 3],
            max: [-16000000; 3],
        };
        for part in MODEL_PARTS {
            if part == Part::Bottom {
                continue;
            }
            if let Some(model) = &parts[part.slot()] {
                for (i, &offset) in offsets[part.slot()].iter().enumerate() {
                    geometry_bounds.min[i] = geometry_bounds.min[i]
                        .min(offset.wrapping_add(i32::from(model.source.bounds[0][i]) * 8));
                    geometry_bounds.max[i] = geometry_bounds.max[i]
                        .max(offset.wrapping_add(i32::from(model.source.bounds[1][i]) * 8));
                }
            }
        }
        let mut primary_local = [[0; 3]; 4];
        let mut auxiliary_local = [[0; 3]; 2];
        for (side, track) in [left, right].into_iter().enumerate() {
            let [min, max] = track.source.bounds;
            let x = if side == 0 { min[0] } else { max[0] };
            primary_local[side * 2] = [
                i32::from(x) * 8,
                i32::from(min[1]) * 8,
                i32::from(max[2]) * 8,
            ];
            primary_local[side * 2 + 1] = [
                i32::from(x) * 8,
                i32::from(min[1]) * 8,
                i32::from(min[2]) * 8,
            ];
            auxiliary_local[side] = [
                i32::from(x) * 8,
                i32::from(min[1]) * 8,
                i32::from(min[2])
                    .wrapping_add(tank.suspension.cog_offset)
                    .wrapping_mul(8),
            ];
        }
        let aerial_height = parts[Part::Aerial.slot()].as_ref().map_or(0, |m| {
            (i32::from(m.source.bounds[1][1]) - i32::from(m.source.bounds[0][1])) * 8
        });
        let mut gun_vertices = [0; 4];
        let mut gun_original_z = [0; 4];
        if let Some(gun) = &mut parts[Part::Gun.slot()] {
            let minimum = gun
                .geometry()
                .vertices
                .iter()
                .map(|v| v[2])
                .fold(0, i16::min);
            let mut selected = 0;
            for (index, v) in gun.geometry().vertices.iter().enumerate() {
                if i32::from(v[2]) < i32::from(minimum) + 40
                    && i32::from(v[2]) > i32::from(minimum) - 40
                {
                    if selected < 4 {
                        gun_vertices[selected] = index as u8;
                        gun_original_z[selected] = v[2];
                    }
                    selected += 1;
                }
            }
            gun.radius = gun.radius.wrapping_mul(2);
        }
        let mut shadow = Mesh::new(
            bank.load(source, Space::Shared, "shadow.sm")?
                .ok_or(Error::InvalidShadow)?,
            true,
        );
        let geometry = shadow.owned.as_mut().ok_or(Error::InvalidShadow)?;
        if geometry.vertices.len() < 4 || geometry.triangles.len() < 2 {
            return Err(Error::InvalidShadow);
        }
        let zextent = i32::from(
            parts[Part::LeftTrack.slot()]
                .as_ref()
                .ok_or(Error::RequiredPart(Part::LeftTrack))?
                .source
                .bounds[1][2],
        ) * 8;
        for (i, v) in geometry.vertices[..4].iter_mut().enumerate() {
            *v = [
                ((if i == 0 || i == 3 {
                    geometry_bounds.min[0]
                } else {
                    geometry_bounds.max[0]
                }) >> 3) as i16,
                0,
                ((if i < 2 {
                    zextent.wrapping_neg()
                } else {
                    zextent
                }) >> 3) as i16,
            ];
        }
        for tri in &mut geometry.triangles[..2] {
            tri.color = 0x2108;
        }
        let mut damage_vertices = [[0; 4]; 2];
        for (output, part) in damage_vertices.iter_mut().zip([Part::Body, Part::Turret]) {
            let mesh = parts[part.slot()]
                .as_ref()
                .ok_or(Error::RequiredPart(part))?;
            for (quadrant, vertex) in output.iter_mut().enumerate() {
                let mut best = 0;
                for (index, &[x, y, z]) in mesh.geometry().vertices.iter().enumerate() {
                    if y > 0
                        && (if quadrant & 1 != 0 { x > 0 } else { x < 0 })
                        && (if quadrant & 2 != 0 { z < 0 } else { z > 0 })
                    {
                        let distance = math::length2(i32::from(x), i32::from(z));
                        if best < distance {
                            best = distance;
                            *vertex = index as u8;
                        }
                    }
                }
            }
        }
        let d = &mut config.tank.drive;
        let reset_sidegrip = d.sidegrip_fraction;
        let alternate_control = [
            surfaces.entries[1].slide[0].multiplier as i16,
            d.rotational_drag.step,
        ];
        if context.resolve.mode == 4 || context.resolve.mode == 5 {
            d.max_turn = d.max_turn.wrapping_mul(3) >> 1;
            d.rotational_drag.step = (d.rotational_drag.step as u16).wrapping_mul(12) as i16;
        }
        if context.ambient_wobble && !context.session_active {
            d.sidegrip_fraction.shift = d.sidegrip_fraction.shift.wrapping_add(1);
        }
        Ok(Self {
            moon: tankracer_assets::object_catalog::Moon::default(),
            config,
            parts,
            offsets,
            primary_local,
            auxiliary_local,
            track_dimensions,
            geometry_bounds,
            collision_bounds: geometry_bounds.expanded([1536; 3]),
            query_bounds: [
                geometry_bounds.expanded([600, 1536, 600]),
                geometry_bounds.expanded([2100, 3036, 2100]),
            ],
            aerial_height,
            gun_vertices,
            gun_original_z,
            damage_vertices,
            shadow,
            tracks,
            alternate_control,
            reset_sidegrip,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tankracer_assets::archive::Archive;

    fn with_assets(test: impl FnOnce(&Archive<'_>, &Bundles<'_>, &Surfaces)) {
        let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
        let vehicles: Vec<_> = ["BRONZE", "SILVER", "GOLD"]
            .map(|name| std::fs::read(root.join("TANKS").join(name).join("FASTFILE.DAT")).unwrap())
            .into_iter()
            .collect();
        let shared = std::fs::read(root.join("INGAME/FASTFILE.DAT")).unwrap();
        let data = std::fs::read(root.join("DATA/FASTFILE.DAT")).unwrap();
        let shapes = |bytes| {
            let archive = Archive::parse(bytes).unwrap();
            let file = archive
                .entries()
                .iter()
                .find(|e| e.name.ends_with(".SS"))
                .unwrap();
            ShapeArchive::parse(archive.get(file.name).unwrap()).unwrap()
        };
        let source = Bundles {
            vehicle: vehicles.iter().map(|b| shapes(b)).collect(),
            shared: vec![shapes(&shared)],
            level: Vec::new(),
        };
        let archive = Archive::parse(&data).unwrap();
        let surfaces = Surfaces::decode(archive.get("SURFS.DAT").unwrap()).unwrap();
        test(&archive, &source, &surfaces);
    }
    #[test]
    fn real_model_contacts_and_instance_copies_preserve_shared_templates() {
        with_assets(|archive, source, surfaces| {
            let tank = Tank::decode(archive.get("TANK1.DAT").unwrap()).unwrap();
            let mut bank = ModelBank::default();
            let context = BuildContext {
                resolve: ResolveContext {
                    player: true,
                    ..ResolveContext::default()
                },
                ambient_wobble: false,
                session_active: true,
            };
            let mut player =
                VehicleModel::build(&tank, source, &mut bank, surfaces, context).unwrap();
            assert_eq!(player.primary_local[0], [-512, -464, 1704]);
            assert_eq!(player.auxiliary_local[0], [-512, -464, 368]);
            assert_eq!(player.pose_config([0; 3]).track_length, 3416);
            let original = player.parts[Part::Body.slot()]
                .as_ref()
                .unwrap()
                .source
                .geometry
                .vertices[0];
            player.parts[Part::Body.slot()]
                .as_mut()
                .unwrap()
                .owned
                .as_mut()
                .unwrap()
                .vertices[0] = [i16::MIN; 3];
            let other = VehicleModel::build(
                &tank,
                source,
                &mut bank,
                surfaces,
                BuildContext {
                    resolve: ResolveContext::default(),
                    ..context
                },
            )
            .unwrap();
            let a = player.parts[Part::Body.slot()].as_ref().unwrap();
            let b = other.parts[Part::Body.slot()].as_ref().unwrap();
            assert!(Arc::ptr_eq(&a.source, &b.source));
            assert!(b.owned.is_none());
            assert_eq!(b.geometry().vertices[0], original);
            let gun = player.parts[Part::Gun.slot()].as_ref().unwrap();
            assert_eq!(gun.radius, 82);
            assert_eq!(gun.source.radius, 41);
            for index in 2..=15 {
                let tank = Tank::decode(archive.get(&format!("TANK{index}.DAT")).unwrap()).unwrap();
                let model =
                    VehicleModel::build(&tank, source, &mut bank, surfaces, context).unwrap();
                assert!(model.track_dimensions.iter().all(|&v| v > 0));
            }
        });
    }
    #[test]
    fn cache_casefolds_names_and_preserves_first_namespace_binding() {
        struct Source {
            vehicle: Vec<u8>,
            shared: Vec<u8>,
        }
        impl ShapeSource for Source {
            fn shape(&self, space: Space, _: &str) -> Option<&[u8]> {
                Some(if space == Space::Vehicle {
                    &self.vehicle
                } else {
                    &self.shared
                })
            }
        }
        let mut source = Source {
            vehicle: vec![0; 84],
            shared: vec![0; 84],
        };
        source.vehicle[12] = 1;
        source.shared[12] = 2;
        let mut bank = ModelBank::default();
        let first = bank
            .load(&source, Space::Vehicle, "alias.sm")
            .unwrap()
            .unwrap();
        let second = bank
            .load(&source, Space::Shared, "ALIAS.SM")
            .unwrap()
            .unwrap();
        assert!(Arc::ptr_eq(&first, &second));
        assert_eq!(second.radius, 1);
        let fresh = ModelBank::default()
            .load(&source, Space::Shared, "alias.sm")
            .unwrap()
            .unwrap();
        assert_eq!(fresh.radius, 2);
    }
}
