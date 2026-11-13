//! SS shape bundles and renderer-independent SM geometry/resources.
use crate::mesh::{self, Error, Geometry, Materials, Reader, SourceMaterial};
use std::{collections::HashSet, ops::Range};

#[derive(Debug)]
pub struct Entry<'a> {
    pub name: &'a str,
    range: Range<usize>,
}
pub struct ShapeArchive<'a> {
    bytes: &'a [u8],
    entries: Vec<Entry<'a>>,
}
impl<'a> ShapeArchive<'a> {
    pub fn parse(bytes: &'a [u8]) -> Result<Self, Error> {
        let r = Reader(bytes);
        if bytes.len() < 8 || r.u32(0)? as usize != bytes.len() {
            return Err(Error::InvalidHeader);
        }
        let count = r.u32(4)? as usize;
        if count > (bytes.len() - 8) / 12 {
            return Err(Error::InvalidHeader);
        }
        let mut entries = Vec::with_capacity(count);
        let mut names = HashSet::new();
        for i in 0..count {
            let record = r.span(8 + i * 12, 12)?;
            let n = record[..8].iter().position(|&v| v == 0).unwrap_or(8);
            if n == 0 || !record[..n].is_ascii() {
                return Err(Error::InvalidHeader);
            }
            let name = std::str::from_utf8(&record[..n]).map_err(|_| Error::InvalidHeader)?;
            if !names.insert(name.to_ascii_lowercase()) {
                return Err(Error::InvalidMesh {
                    offset: 8 + i * 12,
                    reason: "duplicate shape name",
                });
            }
            let offset = r.u32(8 + i * 12 + 8)? as usize;
            if offset < 8 + count * 12 {
                return Err(Error::InvalidMesh {
                    offset,
                    reason: "shape points into directory",
                });
            }
            let length = r.u32(offset)? as usize;
            r.span(offset + 4, length)?;
            entries.push(Entry {
                name,
                range: offset + 4..offset + 4 + length,
            });
        }
        Ok(Self { bytes, entries })
    }
    pub fn entries(&self) -> &[Entry<'a>] {
        &self.entries
    }
    pub fn get(&self, name: &str) -> Option<&'a [u8]> {
        if !name.is_ascii() || name.is_empty() || name.len() > 11 {
            return None;
        }
        let name = if name.len() > 3 && name[name.len() - 3..].eq_ignore_ascii_case(".sm") {
            &name[..name.len() - 3]
        } else {
            name
        };
        if name.len() > 8 {
            return None;
        }
        self.entries
            .iter()
            .find(|entry| entry.name.eq_ignore_ascii_case(name))
            .map(|entry| &self.bytes[entry.range.clone()])
    }
}

#[derive(Debug, Clone)]
pub struct Model {
    pub geometry: Geometry,
    pub materials: Vec<SourceMaterial>,
    pub radius: u16,
    /// Source word +0E, retained for later visibility/style migration.
    pub render_hint: u16,
    pub bounds: [[i16; 3]; 2],
    pub vertex_normals: Option<Vec<[i16; 3]>>,
}
impl Model {
    pub fn decode(bytes: &[u8]) -> Result<Self, Error> {
        let r = Reader(bytes);
        r.span(0, 84)?;
        let mut materials = Materials::default();
        let geometry = mesh::decode(&r, 0, &mut materials)?;
        let normal_offsets = [
            r.u32(72)? as usize,
            r.u32(76)? as usize,
            r.u32(80)? as usize,
        ];
        let vertex_normals = if normal_offsets == [0; 3] {
            None
        } else {
            if normal_offsets.contains(&0) {
                return Err(Error::InvalidMesh {
                    offset: 72,
                    reason: "incomplete vertex normals",
                });
            }
            let count = geometry.vertices.len();
            for offset in normal_offsets {
                r.span(offset, count * 2)?;
            }
            let mut normals = Vec::with_capacity(count);
            for i in 0..count {
                normals.push([
                    r.i16(normal_offsets[0] + i * 2)?,
                    r.i16(normal_offsets[1] + i * 2)?,
                    r.i16(normal_offsets[2] + i * 2)?,
                ]);
            }
            Some(normals)
        };
        Ok(Self {
            geometry,
            materials: materials.entries,
            radius: r.u16(12)?,
            render_hint: r.u16(14)?,
            bounds: [
                [r.i16(28)?, r.i16(30)?, r.i16(32)?],
                [r.i16(34)?, r.i16(36)?, r.i16(38)?],
            ],
            vertex_normals,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::archive::Archive;
    #[test]
    fn shipped_shape_bundles_and_every_nonempty_model_decode() {
        let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
        let mut total = 0;
        for pack in [
            "DATA",
            "COMMON",
            "GUI",
            "INGAME",
            "TANKS/BRONZE",
            "TANKS/SILVER",
            "TANKS/GOLD",
            "LEVELS/ENGLAND",
            "LEVELS/MEXICO",
            "LEVELS/SWAMP",
            "LEVELS/EAST",
            "LEVELS/THEME",
            "LEVELS/BIO",
            "LEVELS/MONACO",
            "LEVELS/CIRCUIT",
        ] {
            let bytes = std::fs::read(root.join(pack).join("FASTFILE.DAT")).unwrap();
            let archive = Archive::parse(&bytes).unwrap();
            for file in archive.entries().iter().filter(|e| e.name.ends_with(".SS")) {
                let shapes = ShapeArchive::parse(archive.get(file.name).unwrap()).unwrap();
                for entry in shapes.entries() {
                    let bytes = shapes.get(entry.name).unwrap();
                    if bytes.is_empty() {
                        continue;
                    }
                    let model = Model::decode(bytes)
                        .unwrap_or_else(|e| panic!("{pack}/{} {}: {e}", file.name, entry.name));
                    assert_eq!(
                        model.geometry.vertices.len(),
                        usize::from(u16::from_le_bytes(bytes[..2].try_into().unwrap()))
                    );
                    assert_eq!(
                        shapes.get(&format!("{}.SM", entry.name.to_ascii_uppercase())),
                        Some(bytes)
                    );
                    total += 1;
                }
            }
        }
        assert!(total > 100);
        eprintln!("{total} nonempty shipped models decoded");
    }
    #[test]
    fn shape_ranges_and_casefold_duplicates_are_rejected() {
        let mut bytes = vec![0; 40];
        bytes[..4].copy_from_slice(&40_u32.to_le_bytes());
        bytes[4..8].copy_from_slice(&2_u32.to_le_bytes());
        bytes[8..9].copy_from_slice(b"a");
        bytes[20..21].copy_from_slice(b"A");
        bytes[16..20].copy_from_slice(&32_u32.to_le_bytes());
        bytes[28..32].copy_from_slice(&36_u32.to_le_bytes());
        assert!(matches!(
            ShapeArchive::parse(&bytes),
            Err(Error::InvalidMesh {
                reason: "duplicate shape name",
                ..
            })
        ));
        bytes[20] = b'b';
        bytes[16..20].copy_from_slice(&8_u32.to_le_bytes());
        assert!(matches!(
            ShapeArchive::parse(&bytes),
            Err(Error::InvalidMesh {
                reason: "shape points into directory",
                ..
            })
        ));
    }
}
