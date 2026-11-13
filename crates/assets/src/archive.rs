//! FASTF101 directory parsing. Entries borrow their complete, validated source.

use std::{collections::HashSet, fmt, ops::Range};

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Error {
    InvalidHeader,
    TruncatedDirectory,
    InvalidName,
    InvalidSpan(String),
    DuplicateName(String),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "invalid FASTFILE archive: {self:?}")
    }
}

impl std::error::Error for Error {}

#[derive(Debug)]
pub struct Entry<'a> {
    pub name: &'a str,
    range: Range<usize>,
}

pub struct Archive<'a> {
    bytes: &'a [u8],
    entries: Vec<Entry<'a>>,
}

impl<'a> Archive<'a> {
    pub fn parse(bytes: &'a [u8]) -> Result<Self, Error> {
        if bytes.len() < 28 || &bytes[..8] != b"FASTF101" {
            return Err(Error::InvalidHeader);
        }
        let count = word(&bytes[24..28]) as usize;
        if count > (bytes.len() - 28) / 24 {
            return Err(Error::TruncatedDirectory);
        }
        let directory_end = 28 + count * 24;
        let mut entries = Vec::with_capacity(count);
        let mut names = HashSet::with_capacity(count);
        for record in bytes[28..directory_end].chunks_exact(24) {
            let length = record[..12].iter().position(|&b| b == 0).unwrap_or(12);
            let name = std::str::from_utf8(&record[..length]).map_err(|_| Error::InvalidName)?;
            if name.is_empty() || !name.is_ascii() {
                return Err(Error::InvalidName);
            }
            if !names.insert(name) {
                return Err(Error::DuplicateName(name.into()));
            }
            let offset = word(&record[12..16]) as usize;
            let size = word(&record[16..20]) as usize;
            if offset < directory_end || offset > bytes.len() || size > bytes.len() - offset {
                return Err(Error::InvalidSpan(name.into()));
            }
            entries.push(Entry {
                name,
                range: offset..offset + size,
            });
        }
        Ok(Self { bytes, entries })
    }

    pub fn entries(&self) -> &[Entry<'a>] {
        &self.entries
    }

    pub fn get(&self, name: &str) -> Option<&'a [u8]> {
        self.entries
            .iter()
            .find(|e| e.name == name)
            .map(|e| &self.bytes[e.range.clone()])
    }
}

fn word(bytes: &[u8]) -> u32 {
    u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]])
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn all_shipped_archives_validate_and_resolve_every_entry() {
        let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
        for name in [
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
            let bytes = std::fs::read(root.join(name).join("FASTFILE.DAT")).unwrap();
            let archive = Archive::parse(&bytes).unwrap_or_else(|e| panic!("{name}: {e}"));
            assert!(!archive.entries().is_empty(), "{name}");
            for entry in archive.entries() {
                assert!(archive.get(entry.name).is_some());
            }
            assert!(archive.get("MISSING.FILE").is_none());
        }
    }

    #[test]
    fn rejects_directory_overflow_and_resource_spans_into_the_directory() {
        let mut bytes = vec![0; 52];
        bytes[..8].copy_from_slice(b"FASTF101");
        bytes[24..28].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(matches!(
            Archive::parse(&bytes),
            Err(Error::TruncatedDirectory)
        ));
        bytes[24..28].copy_from_slice(&1_u32.to_le_bytes());
        bytes[28] = b'A';
        bytes[40..44].copy_from_slice(&28_u32.to_le_bytes());
        assert!(matches!(Archive::parse(&bytes), Err(Error::InvalidSpan(_))));
    }
}
