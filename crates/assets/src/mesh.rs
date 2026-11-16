//! Shared SM/BSM vertex, face, material and billboard decoding.
use std::{collections::HashMap, fmt};

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Error {
    InvalidHeader,
    Truncated { offset: usize, bytes: usize },
    InvalidMesh { offset: usize, reason: &'static str },
    InvalidMaterial { offset: usize },
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "invalid mesh data: {self:?}")
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

#[derive(Debug, Clone)]
pub struct SourceMaterial {
    pub name: String,
    pub spellings: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Billboard {
    pub center: [i16; 2],
    pub half_width: u16,
    pub positive_vertices: u8,
}

#[derive(Debug, Clone, PartialEq, Eq)]
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
    pub billboard: Option<Billboard>,
}

pub(crate) struct Reader<'a>(pub(crate) &'a [u8]);
impl<'a> Reader<'a> {
    pub(crate) fn span(&self, offset: usize, bytes: usize) -> Result<&'a [u8], Error> {
        self.0
            .get(offset..)
            .and_then(|s| s.get(..bytes))
            .ok_or(Error::Truncated { offset, bytes })
    }
    pub(crate) fn u16(&self, at: usize) -> Result<u16, Error> {
        let s = self.span(at, 2)?;
        Ok(u16::from_le_bytes([s[0], s[1]]))
    }
    pub(crate) fn i16(&self, at: usize) -> Result<i16, Error> {
        Ok(self.u16(at)? as i16)
    }
    pub(crate) fn u32(&self, at: usize) -> Result<u32, Error> {
        let s = self.span(at, 4)?;
        Ok(u32::from_le_bytes([s[0], s[1], s[2], s[3]]))
    }
    pub(crate) fn material(&self, at: usize) -> Result<&'a str, Error> {
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

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Geometry {
    pub vertices: Vec<[i16; 3]>,
    pub triangles: Vec<Triangle>,
}
#[derive(Default)]
pub(crate) struct Materials {
    indices: HashMap<[u8; 16], MaterialId>,
    pub(crate) entries: Vec<SourceMaterial>,
}
pub(crate) fn decode(
    data: &Reader<'_>,
    node: usize,
    materials: &mut Materials,
) -> Result<Geometry, Error> {
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
            let id = *materials.indices.entry(key).or_insert_with(|| {
                let id = MaterialId(materials.entries.len() as u32);
                materials.entries.push(SourceMaterial {
                    name: name.to_ascii_lowercase(),
                    spellings: Vec::new(),
                });
                id
            });
            let spellings = &mut materials.entries[id.index()].spellings;
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
            billboard: if data.u16(at + 18)? & 30 == 0 {
                None
            } else {
                let x = data.u32(node + 16)? as usize;
                let z = data.u32(node + 24)? as usize;
                if x == 0 || z == 0 {
                    return Err(Error::InvalidMesh {
                        offset: node,
                        reason: "missing billboard centers",
                    });
                }
                let flags = data.u16(at + 18)?;
                Some(Billboard {
                    center: [data.i16(x + i * 2)?, data.i16(z + i * 2)?],
                    half_width: ((flags & 30) >> 1) * 64,
                    positive_vertices: if flags & 32 == 0 { 6 } else { 2 },
                })
            },
        });
    }
    Ok(Geometry {
        vertices,
        triangles,
    })
}
