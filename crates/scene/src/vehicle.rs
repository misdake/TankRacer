//! Read-only vehicle render extraction. Part poses are already world-space;
//! authored offsets, suspension, turret yaw and recoil must not be applied twice.
use crate::{
    model::{Materials, Prepared},
    transform::physics_pose,
};
use glam::Mat4;
use std::error::Error;
use tankracer_assets::{config::Part, mesh::Geometry};
use tankracer_game::{pose, vehicle_model::VehicleModel};

pub const PARTS: [Part; 3] = [Part::Body, Part::Turret, Part::Gun];

pub struct PartView {
    pub kind: Part,
    pub transform: Mat4,
    pub geometry: Prepared,
    pub revision: u64,
    source: Geometry,
}

pub struct VehicleScene {
    pub materials: Materials,
    pub parts: Vec<PartView>,
    pub revision: u64,
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
                parts.push(PartView {
                    kind,
                    transform: Mat4::IDENTITY,
                    geometry: materials.prepare(mesh.geometry(), &mesh.source.materials)?,
                    revision: 1,
                    source: mesh.geometry().clone(),
                });
            }
        }
        Ok(Self {
            materials,
            parts,
            revision: 1,
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
                part.geometry = self
                    .materials
                    .prepare(mesh.geometry(), &mesh.source.materials)?;
                part.source.clone_from(mesh.geometry());
                part.revision = part.revision.wrapping_add(1);
                self.revision = self.revision.wrapping_add(1);
            }
            let pose = match part.kind {
                Part::Body => poses.body,
                Part::Turret => poses.turret,
                Part::Gun => poses.gun,
                _ => unreachable!(),
            };
            part.transform = physics_pose(pose.position, pose.matrix.0);
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
