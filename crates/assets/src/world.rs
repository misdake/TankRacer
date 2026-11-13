//! BSM v4 static world, decoded from file offsets into owned typed geometry.
//! Reference: frame_shape_prepare_world/relocate_world_meshes in the C oracle.

use std::{
    collections::{HashMap, HashSet},
    fmt,
};

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Error {
    InvalidHeader,
    Truncated { offset: usize, bytes: usize },
    InvalidMesh { offset: usize, reason: &'static str },
    InvalidMaterial { offset: usize },
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "invalid BSM world: {self:?}")
    }
}
impl std::error::Error for Error {}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct MaterialId(u32);

impl MaterialId {
    pub fn index(self) -> usize {
        self.0 as usize
    }
}

#[derive(Debug)]
pub struct SourceMaterial {
    pub name: String,
    pub spellings: Vec<String>,
}

#[derive(Debug)]
pub struct Triangle {
    pub source_offset: u32,
    pub normal: [i16; 3],
    pub color: u16,
    pub indices: [u8; 3],
    pub sides: u8,
    pub uv: [[u8; 2]; 3],
    pub flags: u16,
    pub material: Option<MaterialId>,
    pub material_source_offset: u32,
}

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
    /// Offsets are preserved as provenance, never dereferenced after import.
    pub primary_collision: u32,
    pub secondary_collision: u32,
    pub terrain: u32,
}

struct Reader<'a>(&'a [u8]);
impl Reader<'_> {
    fn span(&self, offset: usize, bytes: usize) -> Result<&[u8], Error> {
        self.0
            .get(offset..)
            .and_then(|s| s.get(..bytes))
            .ok_or(Error::Truncated { offset, bytes })
    }
    fn u16(&self, at: usize) -> Result<u16, Error> {
        let s = self.span(at, 2)?;
        Ok(u16::from_le_bytes([s[0], s[1]]))
    }
    fn i16(&self, at: usize) -> Result<i16, Error> {
        Ok(self.u16(at)? as i16)
    }
    fn u32(&self, at: usize) -> Result<u32, Error> {
        let s = self.span(at, 4)?;
        Ok(u32::from_le_bytes([s[0], s[1], s[2], s[3]]))
    }
    fn material(&self, at: usize) -> Result<&str, Error> {
        let tail = self
            .0
            .get(at..)
            .ok_or(Error::InvalidMaterial { offset: at })?;
        let length = tail
            .iter()
            .take(16)
            .position(|&b| b == 0)
            .ok_or(Error::InvalidMaterial { offset: at })?;
        if length == 0 || !tail[..length].is_ascii() {
            return Err(Error::InvalidMaterial { offset: at });
        }
        std::str::from_utf8(&tail[..length]).map_err(|_| Error::InvalidMaterial { offset: at })
    }
}

impl World {
    pub fn decode(bytes: &[u8]) -> Result<Self, Error> {
        let header = Reader(bytes);
        if bytes.len() < 20 || header.u32(4)? != 4 {
            return Err(Error::InvalidHeader);
        }
        let length = header.u32(0)? as usize;
        // The file declares the data area only; all offsets are relative to +20.
        let data = Reader(header.span(20, length)?);
        let mut world = Self {
            meshes: Vec::new(),
            materials: Vec::new(),
            primary_collision: header.u32(8)?,
            secondary_collision: header.u32(12)?,
            terrain: header.u32(16)?,
        };
        let mut visited = HashSet::new();
        let mut materials = HashMap::<[u8; 16], MaterialId>::new();
        let mut node = 0;
        loop {
            if !visited.insert(node) {
                return Err(Error::InvalidMesh {
                    offset: node,
                    reason: "cyclic mesh chain",
                });
            }
            data.span(node, 48)?;
            let count = data.u16(node)? as usize;
            let faces = data.u16(node + 2)? as usize;
            if count > 256 {
                return Err(Error::InvalidMesh {
                    offset: node,
                    reason: "vertex indices exceed one byte",
                });
            }
            let vertices_at = data.u32(node + 4)? as usize;
            let faces_at = data.u32(node + 8)? as usize;
            data.span(vertices_at, count * 8)?;
            data.span(faces_at, faces * 24)?;
            let mut vertices = Vec::with_capacity(count);
            for i in 0..count {
                let at = vertices_at + i * 8;
                vertices.push([data.i16(at)?, data.i16(at + 2)?, data.i16(at + 4)?]);
            }
            let mut triangles = Vec::with_capacity(faces);
            for i in 0..faces {
                let at = faces_at + i * 24;
                let bytes = data.span(at, 24)?;
                let indices = [bytes[8], bytes[9], bytes[10]];
                if indices.iter().any(|&i| i as usize >= count) {
                    return Err(Error::InvalidMesh {
                        offset: node,
                        reason: "triangle index outside vertex table",
                    });
                }
                let material_offset = data.u32(at + 20)?;
                let material = if material_offset == 0 {
                    None
                } else {
                    let name = data.material(material_offset as usize)?;
                    let mut key = [0; 16];
                    for (out, byte) in key.iter_mut().zip(name.bytes()) {
                        *out = byte.to_ascii_lowercase();
                    }
                    let id = *materials.entry(key).or_insert_with(|| {
                        let id = MaterialId(world.materials.len() as u32);
                        world.materials.push(SourceMaterial {
                            name: name.to_ascii_lowercase(),
                            spellings: Vec::new(),
                        });
                        id
                    });
                    let spellings = &mut world.materials[id.index()].spellings;
                    if !spellings.iter().any(|s| s == name) {
                        spellings.push(name.into());
                    }
                    Some(id)
                };
                triangles.push(Triangle {
                    source_offset: at as u32,
                    normal: [data.i16(at)?, data.i16(at + 2)?, data.i16(at + 4)?],
                    color: data.u16(at + 6)?,
                    indices,
                    sides: bytes[11],
                    uv: [
                        [bytes[12], bytes[13]],
                        [bytes[14], bytes[15]],
                        [bytes[16], bytes[17]],
                    ],
                    flags: data.u16(at + 18)?,
                    material,
                    material_source_offset: material_offset,
                });
            }
            world.meshes.push(WorldMesh {
                source_offset: node as u32,
                position: [
                    data.u32(node + 36)? as i32,
                    data.u32(node + 40)? as i32,
                    data.u32(node + 44)? as i32,
                ],
                flags: data.u32(node + 28)?,
                vertices,
                triangles,
            });
            let next = data.u32(node + 32)? as usize;
            if next == 0 {
                break;
            }
            node = next;
        }
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
