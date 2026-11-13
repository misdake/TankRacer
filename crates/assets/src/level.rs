//! FSTPC 5 authored objects. Strings/groups borrow the archive; class factories
//! resolve names and construct game components in a separate step.
use std::fmt;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    NonAscii(usize),
    Header,
    Truncated(usize),
    TokenTooLong(usize),
    GroupTooLong(usize),
}
impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "invalid FSTPC level: {self:?}")
    }
}
impl std::error::Error for Error {}

#[derive(Debug, Clone, Copy)]
pub struct Object<'a> {
    pub class: &'a str,
    pub name: &'a str,
    /// World coordinates, binary64 * 1024 truncated to signed64 then low32.
    pub position: [i32; 3],
    /// Authored integer degrees * 182, wrapping, before Q12 matrix construction.
    pub angles: [u32; 3],
    pub shape: &'a str,
    /// Outer parentheses removed; nested groups and source bytes retained.
    pub parameters: &'a str,
}
pub struct Level<'a> {
    pub objects: Vec<Object<'a>>,
    /// Uninterpreted Paths payload. The object driver stops at this exact token.
    pub paths: &'a str,
}
impl<'a> Level<'a> {
    pub fn decode(bytes: &'a [u8]) -> Result<Self, Error> {
        if let Some(at) = bytes.iter().position(|b| !b.is_ascii()) {
            return Err(Error::NonAscii(at));
        }
        let text = std::str::from_utf8(bytes).map_err(|_| Error::Header)?;
        let mut reader = Reader { text, cursor: 0 };
        if reader.token(80)? != "FSTPC" || integer(reader.token(32)?) != 5 {
            return Err(Error::Header);
        }
        let mut objects = Vec::new();
        loop {
            let class = reader.token(32)?;
            if class == "Paths" {
                return Ok(Self {
                    objects,
                    paths: &text[reader.cursor..],
                });
            }
            let name = reader.token(64)?;
            let position = [
                coordinate(reader.token(32)?),
                coordinate(reader.token(32)?),
                coordinate(reader.token(32)?),
            ];
            let angles = [
                integer(reader.token(32)?).wrapping_mul(182),
                integer(reader.token(32)?).wrapping_mul(182),
                integer(reader.token(32)?).wrapping_mul(182),
            ];
            let shape = reader.token(16)?;
            let parameters = reader.group()?;
            objects.push(Object {
                class,
                name,
                position,
                angles,
                shape,
                parameters,
            });
        }
    }
}

struct Reader<'a> {
    text: &'a str,
    cursor: usize,
}
impl<'a> Reader<'a> {
    fn token(&mut self, capacity: usize) -> Result<&'a str, Error> {
        let bytes = self.text.as_bytes();
        // The world body lexer differs from the general config/file lexer:
        // only space, CR and LF delimit tokens; quotes have no special meaning.
        while bytes
            .get(self.cursor)
            .is_some_and(|b| matches!(b, b' ' | b'\r' | b'\n'))
        {
            self.cursor += 1;
        }
        let start = self.cursor;
        while bytes
            .get(self.cursor)
            .is_some_and(|b| !matches!(b, b' ' | b'\r' | b'\n'))
        {
            self.cursor += 1;
        }
        if self.cursor == start {
            return Err(Error::Truncated(start));
        }
        if self.cursor - start > 255 {
            return Err(Error::TokenTooLong(start));
        }
        let end = self.cursor.min(start + capacity - 1);
        // Native token copies terminate at an embedded NUL while still consuming
        // the full source token. Preserve that bounded behavior without an arena.
        let value = &self.text[start..end];
        Ok(value.split('\0').next().unwrap_or_default())
    }
    fn group(&mut self) -> Result<&'a str, Error> {
        let bytes = self.text.as_bytes();
        while bytes.get(self.cursor) != Some(&b'(') {
            if self.cursor == bytes.len() {
                return Err(Error::Truncated(self.cursor));
            }
            self.cursor += 1;
        }
        self.cursor += 1;
        let start = self.cursor;
        let mut depth = 1;
        loop {
            let b = *bytes
                .get(self.cursor)
                .ok_or(Error::Truncated(self.cursor))?;
            if self.cursor - start >= 1023 {
                return Err(Error::GroupTooLong(start));
            }
            self.cursor += 1;
            match b {
                b'(' => depth += 1,
                b')' => depth -= 1,
                _ => (),
            }
            if depth == 0 {
                return Ok(&self.text[start..self.cursor - 1]);
            }
        }
    }
}

fn integer(text: &str) -> u32 {
    let (negative, digits) = signed(text.trim_start_matches(|c: char| c.is_ascii_whitespace()));
    let mut value = 0_u32;
    for b in digits.bytes().take_while(u8::is_ascii_digit) {
        value = value.wrapping_mul(10).wrapping_add(u32::from(b - b'0'));
    }
    if negative {
        value.wrapping_neg()
    } else {
        value
    }
}
fn signed(text: &str) -> (bool, &str) {
    if let Some(rest) = text.strip_prefix('-') {
        (true, rest)
    } else {
        (false, text.strip_prefix('+').unwrap_or(text))
    }
}
fn coordinate(text: &str) -> i32 {
    let value = crate::number::decimal(text);
    let bits = value.to_bits();
    let exponent = ((bits >> 52) & 2047) as u32;
    if !(1013..1076).contains(&exponent) {
        return 0;
    }
    let fraction = (bits & 0x000f_ffff_ffff_ffff) | 0x0010_0000_0000_0000;
    let whole = if exponent >= 1065 {
        fraction << (exponent - 1065)
    } else {
        fraction >> (1065 - exponent)
    } as u32;
    if bits >> 63 != 0 {
        whole.wrapping_neg() as i32
    } else {
        whole as i32
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn all_shipped_cup_object_files_decode_with_borrowed_names_and_parameters() {
        use crate::archive::Archive;
        let root =
            std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../assets/game/LEVELS");
        let (mut files, mut records) = (0, 0);
        for map in [
            "ENGLAND", "MEXICO", "SWAMP", "EAST", "THEME", "BIO", "MONACO", "CIRCUIT",
        ] {
            let bytes = std::fs::read(root.join(map).join("FASTFILE.DAT")).unwrap();
            let archive = Archive::parse(&bytes).unwrap();
            for entry in archive.entries() {
                let source = archive.get(entry.name).unwrap();
                if !source.starts_with(b"FSTPC 5") {
                    continue;
                }
                let level =
                    Level::decode(source).unwrap_or_else(|e| panic!("{map}/{}: {e}", entry.name));
                let start = source.as_ptr() as usize;
                let end = start + source.len();
                for object in &level.objects {
                    for text in [object.class, object.name, object.shape, object.parameters] {
                        let at = text.as_ptr() as usize;
                        assert!(at >= start && at + text.len() <= end);
                    }
                }
                files += 1;
                records += level.objects.len();
            }
        }
        assert_eq!((files, records), (50, 4392));
    }
    #[test]
    fn nested_groups_numeric_prefixes_and_truncated_tokens_preserve_source_rules() {
        let bytes = b"FSTPC 5\r\nObject named\n-0.23 1D2 +.5garbage -8 4294967296 360 LongShapeNameTooLong.sm ((:next other)(:empty))\nPaths 0 1";
        let level = Level::decode(bytes).unwrap();
        let object = level.objects[0];
        assert_eq!(object.position, [-235, 102400, 512]);
        assert_eq!(object.angles, [(-1456_i32) as u32, 0, 65520]);
        assert_eq!(object.shape, "LongShapeNameTo");
        assert_eq!(object.parameters, "(:next other)(:empty)");
        assert_eq!(level.paths, " 0 1");
        assert_eq!(coordinate("4294967296"), 0);
        assert_eq!(coordinate("1e100"), 0);
        assert_eq!(coordinate("-0.0009"), 0);
    }
    #[test]
    fn missing_paths_bad_header_and_oversized_groups_are_rejected() {
        assert!(matches!(
            Level::decode(b"FSTPC 4 Paths"),
            Err(Error::Header)
        ));
        assert!(matches!(
            Level::decode(b"FSTPC 5 Thing x 0 0 0 0 0 0 nil ()"),
            Err(Error::Truncated(_))
        ));
        let text = format!(
            "FSTPC 5 Thing x 0 0 0 0 0 0 nil ({}) Paths",
            "x".repeat(1023)
        );
        assert!(matches!(
            Level::decode(text.as_bytes()),
            Err(Error::GroupTooLong(_))
        ));
    }
}
