//! Read-only vehicle render extraction. Part poses are already world-space;
//! authored offsets, suspension, turret yaw and recoil must not be applied twice.
use crate::{
    model::{Materials, Prepared, UvRow},
    transform::physics_pose,
};
use glam::Mat4;
use std::error::Error;
use tankracer_assets::{config::Part, mesh::Geometry};
use tankracer_game::{pose, vehicle_model::VehicleModel};

pub mod tracks;
pub const PARTS: [Part; 5] = [
    Part::Body,
    Part::Turret,
    Part::Gun,
    Part::LeftTrack,
    Part::RightTrack,
];

pub struct PartView {
    pub kind: Part,
    pub transform: Mat4,
    pub geometry: Prepared,
    pub revision: u64,
    pub uv_phase: [f32; 2],
    source: Geometry,
    uv_rows: Vec<UvRow>,
}

pub struct VehicleScene {
    pub materials: Materials,
    pub parts: Vec<PartView>,
    pub revision: u64,
    pub tracks: tracks::Tracks,
}

impl VehicleScene {
    pub fn load(
        model: &VehicleModel,
        vehicle: &[u8],
        shared: &[u8],
    ) -> Result<Self, Box<dyn Error>> {
        let meshes = PARTS
            .into_iter()
            .filter_map(|p| model.parts[p.slot()].as_ref());
        let materials = Materials::load(
            meshes.flat_map(|m| m.source.materials.iter().map(|m| m.name.as_str())),
            vehicle,
            shared,
        )?;
        let mut parts = Vec::new();
        for kind in PARTS {
            if let Some(mesh) = &model.parts[kind.slot()] {
                let uv_rows =
                    tracks::side(kind).map_or_else(Vec::new, |s| tracks::rows(&model.tracks[s]));
                parts.push(PartView {
                    kind,
                    transform: Mat4::IDENTITY,
                    geometry: materials.prepare_animated(
                        mesh.geometry(),
                        &mesh.source.materials,
                        &uv_rows,
                    )?,
                    revision: 1,
                    uv_phase: [0.; 2],
                    source: mesh.geometry().clone(),
                    uv_rows,
                });
            }
        }
        Ok(Self {
            materials,
            parts,
            revision: 1,
            tracks: tracks::Tracks::default(),
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
                part.geometry = self.materials.prepare_animated(
                    mesh.geometry(),
                    &mesh.source.materials,
                    &part.uv_rows,
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
                _ => unreachable!(),
            };
            part.transform = physics_pose(pose.position, pose.matrix.0);
            part.uv_phase = tracks::side(part.kind).map_or([0.; 2], |s| self.tracks.phase(s));
        }
        Ok(())
    }

    /// Vehicle-specific render caches, separate from extra per-map data.
    pub fn resident_bytes(&self) -> usize {
        std::mem::size_of::<Self>() - std::mem::size_of::<Materials>()
            + self.materials.resident_bytes()
            + self.parts.capacity() * std::mem::size_of::<PartView>()
            + self
                .parts
                .iter()
                .map(|p| {
                    p.source.vertices.capacity() * std::mem::size_of::<[i16; 3]>()
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
