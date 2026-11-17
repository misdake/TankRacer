//! Read-only vehicle render extraction. Part poses are already world-space;
//! authored offsets, suspension, turret yaw and recoil must not be applied twice.
use crate::{
    model::{Materials, Prepared, UvRow},
    transform::{physics_pose, physics_position},
};
use glam::Mat4;
use std::error::Error;
use tankracer_assets::{config::Part, mesh::Geometry};
use tankracer_game::{pose, vehicle_model::VehicleModel};

pub mod aerial;
pub mod layer;
pub mod shadow;
pub mod tracks;
pub const PARTS: [Part; 8] = [
    Part::Body,
    Part::Turret,
    Part::Gun,
    Part::LeftTrack,
    Part::RightTrack,
    Part::Under,
    Part::End,
    Part::Bottom,
];

pub struct PartView {
    pub kind: Part,
    pub transform: Mat4,
    pub geometry: Prepared,
    pub revision: u64,
    pub uv_phase: [f32; 2],
    pub basis: tankracer_game::math::Matrix,
    pub layer_normals: layer::NormalBanks,
    source: Geometry,
    uv_rows: Vec<UvRow>,
}

pub struct VehicleScene {
    pub materials: Materials,
    pub parts: Vec<PartView>,
    pub revision: u64,
    pub tracks: tracks::Tracks,
    pub shadow: shadow::Shadow,
    pub shaded: bool,
    pub cockpit: bool,
    pub sight: tankracer_render::geometry::MeshData,
    pub sight_kind: crate::world::BatchKind,
    pub sight_position: glam::Vec3,
    pub layer_atlas: [f32; 4],
    pub aerial: Option<aerial::Aerial>,
    pub presentation: layer::Context,
}

impl VehicleScene {
    pub fn load(
        model: &VehicleModel,
        vehicle: &[u8],
        shared: &[u8],
    ) -> Result<Self, Box<dyn Error>> {
        let meshes = PARTS
            .into_iter()
            .chain([Part::Aerial])
            .filter_map(|p| model.parts[p.slot()].as_ref());
        let materials = Materials::load(
            meshes
                .flat_map(|m| m.source.materials.iter().map(|m| m.name.as_str()))
                .chain(["XHAIR", "BLOB"]),
            vehicle,
            shared,
        )?;
        let mut parts = Vec::new();
        for kind in PARTS {
            if let Some(mesh) = &model.parts[kind.slot()] {
                let uv_rows =
                    tracks::side(kind).map_or_else(Vec::new, |s| tracks::rows(&model.tracks[s]));
                let normals = if layer::eligible(kind) {
                    mesh.source.vertex_normals.as_deref()
                } else {
                    None
                };
                if layer::eligible(kind)
                    && normals.is_none()
                    && mesh.geometry().triangles.iter().any(|t| t.normal[1] > 1500)
                {
                    return Err("Vehicle layer is missing authored vertex normals".into());
                }
                let (geometry, layer_normals) = materials.prepare_vehicle(
                    mesh.geometry(),
                    &mesh.source.materials,
                    &uv_rows,
                    normals,
                )?;
                parts.push(PartView {
                    kind,
                    transform: Mat4::IDENTITY,
                    geometry,
                    basis: tankracer_game::math::Matrix::IDENTITY,
                    layer_normals,
                    revision: 1,
                    uv_phase: [0.; 2],
                    source: mesh.geometry().clone(),
                    uv_rows,
                });
            }
        }
        let (sight, sight_kind) = materials.sprite("XHAIR", [0, 0, 63, 63])?;
        let layer_atlas = materials.normalized_rect("BLOB")?;
        let aerial = model.parts[Part::Aerial.slot()]
            .as_ref()
            .map(|mesh| -> Result<_, Box<dyn Error>> {
                let geometry = materials.prepare(mesh.geometry(), &mesh.source.materials)?;
                if !geometry.transparent.is_empty() {
                    return Err("Unsupported blended aerial prototype".into());
                }
                Ok(aerial::Aerial {
                    geometry,
                    poses: [pose::Pose::default(); 4],
                })
            })
            .transpose()?;
        Ok(Self {
            materials,
            parts,
            revision: 1,
            tracks: tracks::Tracks::default(),
            shadow: shadow::Shadow::load(&model.shadow)?,
            shaded: false,
            cockpit: false,
            sight,
            sight_kind,
            sight_position: glam::Vec3::ZERO,
            layer_atlas,
            aerial,
            presentation: layer::Context::default(),
        })
    }

    pub fn refresh(
        &mut self,
        model: &VehicleModel,
        poses: &pose::State,
    ) -> Result<(), Box<dyn Error>> {
        for part in &mut self.parts {
            let mesh = model.parts[part.kind.slot()]
                .as_ref()
                .ok_or("Vehicle render part disappeared")?;
            if mesh.geometry() != &part.source {
                // Private collision damage and gun recoil modify authoritative
                // local geometry. Only that part needs new GPU vertex data.
                (part.geometry, part.layer_normals) = self.materials.prepare_vehicle(
                    mesh.geometry(),
                    &mesh.source.materials,
                    &part.uv_rows,
                    if layer::eligible(part.kind) {
                        mesh.source.vertex_normals.as_deref()
                    } else {
                        None
                    },
                )?;
                part.source.clone_from(mesh.geometry());
                part.revision = part.revision.wrapping_add(1);
                self.revision = self.revision.wrapping_add(1);
            }
            let pose = match part.kind {
                Part::Body => poses.body,
                Part::Turret => poses.turret,
                Part::Gun => poses.gun,
                Part::LeftTrack => poses.tracks[0],
                Part::RightTrack => poses.tracks[1],
                Part::Under => poses.attachments[0],
                Part::End => poses.attachments[1],
                Part::Bottom => pose::Pose {
                    position: poses.bottom_position,
                    matrix: poses.reference,
                },
                _ => unreachable!(),
            };
            part.transform = physics_pose(pose.position, pose.matrix.0);
            part.basis = pose.matrix;
            part.uv_phase = tracks::side(part.kind).map_or([0.; 2], |s| self.tracks.phase(s));
        }
        Ok(())
    }

    pub fn refresh_player(
        &mut self,
        player: &tankracer_game::vehicle_startup::Player,
    ) -> Result<(), Box<dyn Error>> {
        self.refresh(&player.model, &player.physics.motion.poses)?;
        self.shadow
            .refresh(&player.physics.motion.hull, player.physics.motion.ready);
        self.shaded = player.physics.contact.sampling.primary.special_count as i8 > 1;
        self.presentation.player = player.physics.player;
        self.presentation.shield_timer = player.prefix.spin.speed;
        self.presentation.invincible = player.prefix.spin.disabled != 0;
        if let Some(aerial) = &mut self.aerial {
            aerial.poses = aerial::poses(player);
        }
        let motion = &player.physics.motion;
        let z = motion
            .articulation
            .recoil
            .min(0)
            .wrapping_mul(4)
            .wrapping_add(5120);
        let offset = motion.poses.turret.matrix.local_to_world([0, 0, z]);
        self.sight_position = physics_position(std::array::from_fn(|i| {
            motion.poses.turret.position[i].wrapping_add(offset[i])
        }));
        Ok(())
    }

    pub fn visible(&self, part: &PartView) -> bool {
        (part.kind == Part::Bottom) == self.cockpit
    }

    pub fn visible_parts(&self) -> usize {
        self.parts.iter().filter(|p| self.visible(p)).count()
            + usize::from(self.aerial_visible()) * 4
    }
    pub fn aerial_visible(&self) -> bool {
        self.aerial.is_some()
            && self.presentation.player
            && !self.cockpit
            && !self.presentation.hide_aerial
    }
    pub fn layer(
        &self,
        part: &PartView,
        view: tankracer_game::math::Matrix,
        shaded: bool,
    ) -> Option<tankracer_render::material::NormalLayer> {
        let context = layer::Context {
            cockpit: self.cockpit,
            ..self.presentation
        };
        layer::material(
            part.basis,
            view,
            context.mode(part.kind),
            self.layer_atlas,
            shaded,
        )
    }

    /// Original 300 x 300 descriptor, in raw physics units. Screen aligned,
    /// including camera roll; no extra turret rotation is applied here.
    pub fn sight_transform(&self, camera: &crate::camera::Camera) -> Mat4 {
        let half = 300. / 2048.;
        Mat4::from_cols(
            (camera.right() * half).extend(0.),
            (camera.up() * half).extend(0.),
            (-camera.forward()).extend(0.),
            self.sight_position.extend(1.),
        )
    }

    /// Vehicle-specific render caches, separate from extra per-map data.
    pub fn resident_bytes(&self) -> usize {
        std::mem::size_of::<Self>()
            - std::mem::size_of::<Materials>()
            - std::mem::size_of::<shadow::Shadow>()
            + self.shadow.resident_bytes()
            + self.materials.resident_bytes()
            + self.aerial.as_ref().map_or(0, |a| {
                a.resident_bytes() - std::mem::size_of::<aerial::Aerial>()
            })
            + self.parts.capacity() * std::mem::size_of::<PartView>()
            + self.sight.vertices.capacity()
                * std::mem::size_of::<tankracer_render::geometry::Vertex>()
            + self.sight.indices.capacity() * 4
            + self
                .parts
                .iter()
                .map(|p| {
                    p.source.vertices.capacity() * std::mem::size_of::<[i16; 3]>()
                        + p.layer_normals
                            .iter()
                            .map(|v| v.capacity() * 16)
                            .sum::<usize>()
                        + p.uv_rows.capacity() * std::mem::size_of::<UvRow>()
                        + p.geometry
                            .uv_scroll
                            .iter()
                            .map(|v| v.capacity() * std::mem::size_of::<[f32; 4]>())
                            .sum::<usize>()
                        + p.source.triangles.capacity()
                            * std::mem::size_of::<tankracer_assets::mesh::Triangle>()
                        + p.geometry
                            .batches
                            .iter()
                            .map(|b| {
                                b.vertices.capacity()
                                    * std::mem::size_of::<tankracer_render::geometry::Vertex>()
                                    + b.indices.capacity() * 4
                            })
                            .sum::<usize>()
                        + p.geometry.transparent.capacity()
                            * std::mem::size_of::<crate::world::TransparentTriangle>()
                })
                .sum::<usize>()
    }
}
