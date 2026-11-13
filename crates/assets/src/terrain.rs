//! Ordered collision graph, decoded once. Rendering visibility never removes
//! collision triangles. Source offsets are provenance, not runtime pointers.
use std::{collections::HashMap, fmt};

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Error {
    InvalidHeader,
    Truncated { offset: usize, bytes: usize },
    InvalidGraph { offset: usize, reason: &'static str },
}
impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "invalid terrain graph: {self:?}")
    }
}
impl std::error::Error for Error {}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NodeId(pub u32);

#[derive(Debug, Default, Clone, Copy)]
pub struct Span {
    pub start: u32,
    pub length: u32,
}

#[derive(Debug)]
pub struct CollisionTriangle {
    pub source_offset: u32,
    pub normal: [i16; 3],
    pub indices: [u8; 3],
    pub flags: u16,
}

#[derive(Debug)]
pub struct CollisionMesh {
    pub source_offset: u32,
    pub position: [i32; 3],
    pub flags: u32,
    pub radius: u16,
    pub vertices: Vec<[i16; 3]>,
    pub triangles: Vec<CollisionTriangle>,
    /// Stored triangle bounding-box centers used by the fallback metric.
    pub face_centers: Vec<[i16; 3]>,
}

#[derive(Debug)]
pub struct TerrainNode {
    pub source_offset: u32,
    pub mesh: u32,
    pub shifts: [u8; 2],
    /// The original table starts at +10; +48 addresses its center (entry 28).
    pub bins: [i16; 64],
    pub candidates: Span,
    pub half_bounds: [i16; 2],
    pub surface: u16,
    pub extra: [i16; 4],
    pub has_owner: bool,
}

#[derive(Debug)]
pub struct FallbackNode {
    pub source_offset: u32,
    pub mesh: u32,
    pub basis: [i16; 4],
    pub bounds: [i16; 4],
    pub candidates: Span,
}

#[derive(Debug)]
pub struct FallbackData {
    pub cells: [Span; 64],
    pub node_refs: Vec<u32>,
    pub nodes: Vec<FallbackNode>,
}

impl Default for FallbackData {
    fn default() -> Self {
        Self {
            cells: [Span::default(); 64],
            node_refs: Vec::new(),
            nodes: Vec::new(),
        }
    }
}

#[derive(Debug)]
pub struct TerrainData {
    pub source_root: u32,
    pub origin: [i32; 2],
    pub shifts: [u8; 2],
    pub cells: [Span; 64],
    pub node_refs: Vec<NodeId>,
    pub nodes: Vec<TerrainNode>,
    pub meshes: Vec<CollisionMesh>,
    pub triangle_refs: Vec<u8>,
    pub fallback: FallbackData,
}

struct Reader<'a>(&'a [u8]);
impl Reader<'_> {
    fn span(&self, at: usize, bytes: usize) -> Result<&[u8], Error> {
        self.0
            .get(at..)
            .and_then(|s| s.get(..bytes))
            .ok_or(Error::Truncated { offset: at, bytes })
    }
    fn u8(&self, at: usize) -> Result<u8, Error> {
        Ok(self.span(at, 1)?[0])
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
    fn i32(&self, at: usize) -> Result<i32, Error> {
        Ok(self.u32(at)? as i32)
    }
}

impl TerrainData {
    pub fn decode_bsm(bytes: &[u8]) -> Result<Self, Error> {
        let header = Reader(bytes);
        if bytes.len() < 20 || header.u32(4)? != 4 {
            return Err(Error::InvalidHeader);
        }
        let data = Reader(header.span(20, header.u32(0)? as usize)?);
        let root = header.u32(16)? as usize;
        if root == 0 {
            return Err(Error::InvalidGraph {
                offset: 0,
                reason: "missing terrain root",
            });
        }
        data.span(root, 0x118)?;
        let mut result = Self {
            source_root: root as u32,
            origin: [data.i32(root + 0x108)?, data.i32(root + 0x10c)?],
            shifts: [data.u8(root + 0x110)?, data.u8(root + 0x112)?],
            cells: [Span::default(); 64],
            node_refs: Vec::new(),
            nodes: Vec::new(),
            meshes: Vec::new(),
            triangle_refs: Vec::new(),
            fallback: FallbackData::default(),
        };
        let mut node_ids = HashMap::new();
        let mut mesh_ids = HashMap::new();
        let mut node = header.u32(8)? as usize;
        while node != 0 {
            if node_ids.contains_key(&node) {
                return Err(Error::InvalidGraph {
                    offset: node,
                    reason: "cyclic primary node chain",
                });
            }
            data.span(node, 0xa8)?;
            let mesh_at = data.u32(node + 4)? as usize;
            let mesh = if let Some(&id) = mesh_ids.get(&mesh_at) {
                id
            } else {
                let id = result.meshes.len() as u32;
                result.meshes.push(decode_mesh(&data, mesh_at)?);
                mesh_ids.insert(mesh_at, id);
                id
            };
            let mut bins = [0; 64];
            for (i, bin) in bins.iter_mut().enumerate() {
                *bin = data.i16(node + 0x10 + i * 2)?;
            }
            let list = data.u32(node + 0xc)? as usize;
            let mut end = 0;
            for &bin in &bins {
                if bin == -1 {
                    continue;
                }
                if bin < 0 {
                    return Err(Error::InvalidGraph {
                        offset: node,
                        reason: "negative triangle list offset",
                    });
                }
                let start = bin as usize;
                let length = triangle_list_length(
                    &data,
                    list + start,
                    result.meshes[mesh as usize].triangles.len(),
                )?;
                end = end.max(start + length);
            }
            let candidates = Span {
                start: result.triangle_refs.len() as u32,
                length: end as u32,
            };
            result
                .triangle_refs
                .extend_from_slice(data.span(list, end)?);
            let id = NodeId(result.nodes.len() as u32);
            node_ids.insert(node, id.0);
            result.nodes.push(TerrainNode {
                source_offset: node as u32,
                mesh,
                shifts: [data.u8(node + 8)?, data.u8(node + 10)?],
                bins,
                candidates,
                half_bounds: [data.i16(node + 0x92)?, data.i16(node + 0x96)?],
                surface: data.u16(node + 0x98)?,
                extra: [
                    data.i16(node + 0x9a)?,
                    data.i16(node + 0x9c)?,
                    data.i16(node + 0x9e)?,
                    data.i16(node + 0xa0)?,
                ],
                has_owner: data.u32(node + 0xa4)? != 0,
            });
            node = data.u32(node)? as usize;
        }
        let (cells, refs) = decode_grid(&data, root, 0, &node_ids)?;
        result.cells = cells;
        result.node_refs = refs.into_iter().map(NodeId).collect();
        node_ids.clear();
        node = header.u32(12)? as usize;
        while node != 0 {
            if node_ids.contains_key(&node) {
                return Err(Error::InvalidGraph {
                    offset: node,
                    reason: "cyclic secondary node chain",
                });
            }
            data.span(node, 0x1c)?;
            let mesh_at = data.u32(node + 4)? as usize;
            let mesh = if let Some(&id) = mesh_ids.get(&mesh_at) {
                id
            } else {
                let id = result.meshes.len() as u32;
                result.meshes.push(decode_mesh(&data, mesh_at)?);
                mesh_ids.insert(mesh_at, id);
                id
            };
            let collision_mesh = &result.meshes[mesh as usize];
            if collision_mesh.face_centers.len() != collision_mesh.triangles.len() {
                return Err(Error::InvalidGraph {
                    offset: mesh_at,
                    reason: "fallback mesh has no face centers",
                });
            }
            let list = data.u32(node + 8)? as usize;
            let length = triangle_list_length(&data, list, collision_mesh.triangles.len())?;
            let candidates = Span {
                start: result.triangle_refs.len() as u32,
                length: length as u32,
            };
            result
                .triangle_refs
                .extend_from_slice(data.span(list, length)?);
            node_ids.insert(node, result.fallback.nodes.len() as u32);
            result.fallback.nodes.push(FallbackNode {
                source_offset: node as u32,
                mesh,
                basis: [
                    data.i16(node + 0xc)?,
                    data.i16(node + 0xe)?,
                    data.i16(node + 0x10)?,
                    data.i16(node + 0x12)?,
                ],
                bounds: [
                    data.i16(node + 0x14)?,
                    data.i16(node + 0x16)?,
                    data.i16(node + 0x18)?,
                    data.i16(node + 0x1a)?,
                ],
                candidates,
            });
            node = data.u32(node)? as usize;
        }
        (result.fallback.cells, result.fallback.node_refs) =
            decode_grid(&data, root, 1, &node_ids)?;
        Ok(result)
    }

    pub fn node(&self, id: NodeId) -> &TerrainNode {
        &self.nodes[id.0 as usize]
    }

    pub fn resident_bytes(&self) -> usize {
        std::mem::size_of::<Self>()
            + self.node_refs.capacity() * std::mem::size_of::<NodeId>()
            + self.nodes.capacity() * std::mem::size_of::<TerrainNode>()
            + self.meshes.capacity() * std::mem::size_of::<CollisionMesh>()
            + self.triangle_refs.capacity()
            + self.fallback.node_refs.capacity() * std::mem::size_of::<u32>()
            + self.fallback.nodes.capacity() * std::mem::size_of::<FallbackNode>()
            + self
                .meshes
                .iter()
                .map(|m| {
                    m.vertices.capacity() * std::mem::size_of::<[i16; 3]>()
                        + m.triangles.capacity() * std::mem::size_of::<CollisionTriangle>()
                        + m.face_centers.capacity() * std::mem::size_of::<[i16; 3]>()
                })
                .sum::<usize>()
    }
}

fn triangle_list_length(r: &Reader<'_>, at: usize, faces: usize) -> Result<usize, Error> {
    for i in 0..65536 {
        let which = r.u8(at + i)?;
        if which == 255 {
            return Ok(i + 1);
        }
        if usize::from(which) >= faces {
            return Err(Error::InvalidGraph {
                offset: at + i,
                reason: "triangle list index outside mesh",
            });
        }
    }
    Err(Error::InvalidGraph {
        offset: at,
        reason: "unterminated triangle list",
    })
}

fn decode_grid(
    r: &Reader<'_>,
    root: usize,
    grid: usize,
    ids: &HashMap<usize, u32>,
) -> Result<([Span; 64], Vec<u32>), Error> {
    let table = r.u32(root + grid * 4)? as usize;
    let entries = usize::from(r.u16(root + 0x114 + grid * 2)?);
    r.span(table, entries * 4)?;
    let mut cells = [Span::default(); 64];
    let mut refs = Vec::new();
    let mut lists = HashMap::<usize, Span>::new();
    for (cell, span) in cells.iter_mut().enumerate() {
        let at = root + 8 + grid * 0x80 + cell * 2;
        let offset = r.i16(at)?;
        if offset == -1 {
            continue;
        }
        if offset < 0 || offset as usize >= entries {
            return Err(Error::InvalidGraph {
                offset: at,
                reason: "invalid root list offset",
            });
        }
        let offset = offset as usize;
        if let Some(&cached) = lists.get(&offset) {
            *span = cached;
            continue;
        }
        let start = refs.len();
        let mut i = offset;
        loop {
            if i >= entries {
                return Err(Error::InvalidGraph {
                    offset: table,
                    reason: "unterminated root list",
                });
            }
            let which = r.u32(table + i * 4)? as usize;
            i += 1;
            if which == 0 {
                break;
            }
            refs.push(*ids.get(&which).ok_or(Error::InvalidGraph {
                offset: which,
                reason: "root references an unbound node",
            })?);
        }
        *span = Span {
            start: start as u32,
            length: (refs.len() - start) as u32,
        };
        lists.insert(offset, *span);
    }
    Ok((cells, refs))
}

fn decode_mesh(r: &Reader<'_>, at: usize) -> Result<CollisionMesh, Error> {
    r.span(at, 48)?;
    let n = usize::from(r.u16(at)?);
    let faces = usize::from(r.u16(at + 2)?);
    if n > 256 {
        return Err(Error::InvalidGraph {
            offset: at,
            reason: "collision vertex indices exceed byte range",
        });
    }
    let points = r.u32(at + 4)? as usize;
    let polys = r.u32(at + 8)? as usize;
    r.span(points, n * 8)?;
    r.span(polys, faces * 24)?;
    let mut vertices = Vec::with_capacity(n);
    for i in 0..n {
        let p = points + i * 8;
        vertices.push([r.i16(p)?, r.i16(p + 2)?, r.i16(p + 4)?]);
    }
    let mut triangles = Vec::with_capacity(faces);
    for i in 0..faces {
        let p = polys + i * 24;
        let indices = [r.u8(p + 8)?, r.u8(p + 9)?, r.u8(p + 10)?];
        if indices.iter().any(|&v| v as usize >= n) {
            return Err(Error::InvalidGraph {
                offset: p,
                reason: "collision triangle vertex outside mesh",
            });
        }
        triangles.push(CollisionTriangle {
            source_offset: p as u32,
            normal: [r.i16(p)?, r.i16(p + 2)?, r.i16(p + 4)?],
            indices,
            flags: r.u16(p + 18)?,
        });
    }
    let centers = [
        r.u32(at + 0x10)? as usize,
        r.u32(at + 0x14)? as usize,
        r.u32(at + 0x18)? as usize,
    ];
    let mut face_centers = Vec::new();
    if centers.iter().all(|&offset| offset != 0) {
        face_centers.reserve_exact(faces);
        for i in 0..faces {
            face_centers.push([
                r.i16(centers[0] + i * 2)?,
                r.i16(centers[1] + i * 2)?,
                r.i16(centers[2] + i * 2)?,
            ]);
        }
    }
    Ok(CollisionMesh {
        source_offset: at as u32,
        position: [r.i32(at + 36)?, r.i32(at + 40)?, r.i32(at + 44)?],
        flags: r.u32(at + 28)?,
        radius: r.u16(at + 12)?,
        vertices,
        triangles,
        face_centers,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::archive::Archive;
    #[test]
    fn corrupt_secondary_cycles_grids_and_missing_centers_are_rejected() {
        let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../../assets/game/LEVELS/ENGLAND/FASTFILE.DAT");
        let bytes = std::fs::read(path).unwrap();
        let archive = Archive::parse(&bytes).unwrap();
        let file = archive
            .entries()
            .iter()
            .find(|e| e.name.ends_with(".BSM"))
            .unwrap();
        let source = archive.get(file.name).unwrap();
        let node = u32::from_le_bytes(source[12..16].try_into().unwrap()) as usize;
        let root = u32::from_le_bytes(source[16..20].try_into().unwrap()) as usize;
        let mesh =
            u32::from_le_bytes(source[20 + node + 4..20 + node + 8].try_into().unwrap()) as usize;
        for (at, value, reason) in [
            (
                20 + node,
                (node as u32).to_le_bytes().to_vec(),
                "cyclic secondary node chain",
            ),
            (
                20 + root + 0x88,
                (-2_i16).to_le_bytes().to_vec(),
                "invalid root list offset",
            ),
            (
                20 + mesh + 0x10,
                0_u32.to_le_bytes().to_vec(),
                "fallback mesh has no face centers",
            ),
        ] {
            let mut data = source.to_vec();
            data[at..at + value.len()].copy_from_slice(&value);
            assert!(
                matches!(TerrainData::decode_bsm(&data), Err(Error::InvalidGraph {reason: actual,..}) if actual==reason),
                "{reason}"
            );
        }
    }
    #[test]
    fn every_cup_collision_graph_decodes_with_bounded_resident_data() {
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
            let terrain = TerrainData::decode_bsm(archive.get(file.name).unwrap())
                .unwrap_or_else(|e| panic!("{name}: {e}"));
            assert!(!terrain.nodes.is_empty());
            assert!(!terrain.meshes.is_empty());
            assert!(terrain.resident_bytes() < 2_000_000);
            eprintln!(
                "{name}: {} primary / {} fallback nodes, {} collision meshes, {} bytes",
                terrain.nodes.len(),
                terrain.fallback.nodes.len(),
                terrain.meshes.len(),
                terrain.resident_bytes()
            );
        }
    }
}
