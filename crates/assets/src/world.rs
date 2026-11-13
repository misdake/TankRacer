//! BSM v4 world geometry, sharing the model mesh decoder.
use crate::mesh::{self, Materials, Reader};
pub use crate::mesh::{Billboard, Error, MaterialId, SourceMaterial, Triangle};
use std::collections::HashSet;

#[derive(Debug)]
pub struct WorldMesh {
    pub source_offset: u32,
    pub position: [i32; 3],
    pub flags: u32,
    pub vertices: Vec<[i16; 3]>,
    pub triangles: Vec<Triangle>,
}
#[derive(Debug)]
pub struct World {
    pub meshes: Vec<WorldMesh>,
    pub materials: Vec<SourceMaterial>,
    pub primary_collision: u32,
    pub secondary_collision: u32,
    pub terrain: u32,
}
impl World {
    pub fn decode(bytes: &[u8]) -> Result<Self, Error> {
        let header = Reader(bytes);
        if bytes.len() < 20 || header.u32(4)? != 4 {
            return Err(Error::InvalidHeader);
        }
        let data = Reader(header.span(20, header.u32(0)? as usize)?);
        let mut world = Self {
            meshes: Vec::new(),
            materials: Vec::new(),
            primary_collision: header.u32(8)?,
            secondary_collision: header.u32(12)?,
            terrain: header.u32(16)?,
        };
        let mut visited = HashSet::new();
        let mut materials = Materials::default();
        let mut node = 0;
        loop {
            if !visited.insert(node) {
                return Err(Error::InvalidMesh {
                    offset: node,
                    reason: "cyclic mesh chain",
                });
            }
            data.span(node, 48)?;
            let geometry = mesh::decode(&data, node, &mut materials)?;
            world.meshes.push(WorldMesh {
                source_offset: node as u32,
                position: [
                    data.u32(node + 36)? as i32,
                    data.u32(node + 40)? as i32,
                    data.u32(node + 44)? as i32,
                ],
                flags: data.u32(node + 28)?,
                vertices: geometry.vertices,
                triangles: geometry.triangles,
            });
            let next = data.u32(node + 32)? as usize;
            if next == 0 {
                break;
            }
            node = next;
        }
        world.materials = materials.entries;
        Ok(world)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::archive::Archive;

    #[test]
    fn decode_all_cup_worlds() {
        let root =
            std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../assets/game/LEVELS");
        for name in [
            "ENGLAND", "MEXICO", "SWAMP", "EAST", "THEME", "BIO", "MONACO", "CIRCUIT",
        ] {
            let bytes = std::fs::read(root.join(name).join("FASTFILE.DAT")).unwrap();
            let archive = Archive::parse(&bytes).unwrap();
            let file = archive
                .entries()
                .iter()
                .find(|e| e.name.ends_with(".BSM"))
                .unwrap();
            let world = World::decode(archive.get(file.name).unwrap())
                .unwrap_or_else(|e| panic!("{name}: {e}"));
            assert!(!world.meshes.is_empty());
            assert!(world.terrain != 0);
            assert!(world.meshes.iter().any(|m| !m.triangles.is_empty()));
        }
    }

    #[test]
    fn rejects_cycles_and_indices_outside_the_vertex_table() {
        let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../../assets/game/LEVELS/ENGLAND/FASTFILE.DAT");
        let bytes = std::fs::read(path).unwrap();
        let archive = Archive::parse(&bytes).unwrap();
        let name = archive
            .entries()
            .iter()
            .find(|e| e.name.ends_with(".BSM"))
            .unwrap()
            .name;
        let original = archive.get(name).unwrap();
        let mut cyclic = original.to_vec();
        let next = u32::from_le_bytes(cyclic[52..56].try_into().unwrap());
        assert_ne!(next, 0);
        let at = 20 + next as usize + 32;
        cyclic[at..at + 4].copy_from_slice(&next.to_le_bytes());
        assert!(matches!(
            World::decode(&cyclic),
            Err(Error::InvalidMesh {
                reason: "cyclic mesh chain",
                ..
            })
        ));
        let mut invalid = original.to_vec();
        assert_ne!(u16::from_le_bytes(invalid[22..24].try_into().unwrap()), 0);
        invalid[20..22].copy_from_slice(&0_u16.to_le_bytes());
        assert!(matches!(
            World::decode(&invalid),
            Err(Error::InvalidMesh {
                reason: "triangle index outside vertex table",
                ..
            })
        ));
    }
}
