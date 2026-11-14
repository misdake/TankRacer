//! Object presets and qualified class settings from OBPARAMS.DAT.
use crate::config::Gain;

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct ObjectType {
    pub name: String,
    pub friction: u8,
    pub elasticity: u16,
    pub height_factor: u8,
    pub threshold: u8,
    pub y_spin_factor: u16,
    pub solidity: i16,
    pub hit_spin_x: Gain,
    pub hit_spin_z: Gain,
    pub hit_slow: Gain,
    pub spin_slow: Gain,
    pub flatten: Gain,
    pub stop_spin: u16,
    pub max_deflection: u16,
    pub flags: u32,
    pub collision_margin: u16,
    /// Native fixed nine-byte token, which may have no NUL when exactly full.
    pub bash_sound: [u8; 9],
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    NonAscii,
    Truncated,
    MissingGroup,
    TokenTooLong,
}
#[derive(Debug, Default)]
pub struct Catalog {
    pub entries: Vec<ObjectType>,
    pub general: General,
    pub fall: Fall,
    pub moon: Moon,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Moon {
    pub player: Gain,
    pub other: Gain,
}
impl Default for Moon {
    fn default() -> Self {
        Self {
            player: Gain {
                multiplier: 1,
                shift: 2,
            },
            other: Gain {
                multiplier: 1,
                shift: 1,
            },
        }
    }
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct General {
    pub gravity: i32,
    pub circuit_power_mul: i32,
}
impl Default for General {
    fn default() -> Self {
        Self {
            gravity: 5,
            circuit_power_mul: 70,
        }
    }
}
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Fall {
    pub minimum_turn_rate: i32,
    pub maximum_turn_rate: i32,
    pub squash_angle: i32,
    pub gravity: i32,
    pub elasticity_multiplier: i32,
    pub elasticity_shift: u32,
}
impl Catalog {
    pub fn get(&self, name: &str) -> Option<&ObjectType> {
        self.entries
            .iter()
            .find(|o| o.name.eq_ignore_ascii_case(name))
    }
    pub fn decode(bytes: &[u8]) -> Result<Self, Error> {
        if !bytes.is_ascii() {
            return Err(Error::NonAscii);
        }
        let text = std::str::from_utf8(bytes).map_err(|_| Error::NonAscii)?;
        let mut tokens = Tokens { text, cursor: 0 };
        let mut result = Self::default();
        while let Some(kind) = tokens.next()? {
            if !kind.eq_ignore_ascii_case("object") && !kind.eq_ignore_ascii_case("class") {
                continue;
            }
            let name = tokens.required()?;
            if name.len() >= 25 {
                return Err(Error::TokenTooLong);
            }
            if tokens.required()? != "(" {
                return Err(Error::MissingGroup);
            }
            if kind.eq_ignore_ascii_case("class") {
                loop {
                    let key = tokens.required()?;
                    if key == ")" {
                        break;
                    }
                    if name.eq_ignore_ascii_case("general") {
                        match key.to_ascii_lowercase().as_str() {
                            ":gravity" => result.general.gravity = tokens.integer()?,
                            ":circuit_power_mul" => {
                                result.general.circuit_power_mul = tokens.integer()?
                            }
                            _ => (),
                        }
                    } else if name.eq_ignore_ascii_case("fall") {
                        match key.to_ascii_lowercase().as_str() {
                            ":min_rot_speed" => result.fall.minimum_turn_rate = tokens.integer()?,
                            ":max_rot_speed" => result.fall.maximum_turn_rate = tokens.integer()?,
                            ":squash_angle" => {
                                result.fall.squash_angle = tokens.integer()?.wrapping_mul(182)
                            }
                            ":fall_gravity" => result.fall.gravity = tokens.integer()?,
                            ":fall_elas_mul" => {
                                result.fall.elasticity_multiplier = tokens.integer()?
                            }
                            ":fall_elas_shf" => {
                                result.fall.elasticity_shift = tokens.integer()? as u32
                            }
                            _ => (),
                        }
                    } else if name.eq_ignore_ascii_case("moon") {
                        match key.to_ascii_lowercase().as_str() {
                            ":moongrav_mul" => {
                                result.moon.player.multiplier = tokens.integer()? as u16
                            }
                            ":moongrav_shf" => result.moon.player.shift = tokens.integer()? as u16,
                            ":aimoongrav_mul" => {
                                result.moon.other.multiplier = tokens.integer()? as u16
                            }
                            ":aimoongrav_shf" => result.moon.other.shift = tokens.integer()? as u16,
                            _ => (),
                        }
                    }
                }
                continue;
            }
            let mut object = ObjectType {
                name: name.into(),
                ..Default::default()
            };
            loop {
                let key = tokens.required()?;
                if key == ")" {
                    break;
                }
                macro_rules! number {
                    ($field:expr, $ty:ty) => {
                        $field = tokens.integer()? as $ty
                    };
                }
                match key.to_ascii_lowercase().as_str() {
                    ":friction" => number!(object.friction, u8),
                    ":elasticity" => number!(object.elasticity, u16),
                    ":height_factor" => number!(object.height_factor, u8),
                    ":threshold" => number!(object.threshold, u8),
                    ":y_spin_factor" => number!(object.y_spin_factor, u16),
                    ":solidity" => number!(object.solidity, i16),
                    ":hit_rtsp_xmul" => number!(object.hit_spin_x.multiplier, u16),
                    ":hit_rtsp_xshf" => number!(object.hit_spin_x.shift, u16),
                    ":hit_rtsp_zmul" => number!(object.hit_spin_z.multiplier, u16),
                    ":hit_rtsp_zshf" => number!(object.hit_spin_z.shift, u16),
                    ":hit_rtslw_mul" => number!(object.hit_slow.multiplier, u16),
                    ":hit_rtslw_shf" => number!(object.hit_slow.shift, u16),
                    ":slow_spin_mul" => number!(object.spin_slow.multiplier, u16),
                    ":slow_spin_shf" => number!(object.spin_slow.shift, u16),
                    ":rot_flat_mul" => number!(object.flatten.multiplier, u16),
                    ":rot_flat_shf" => number!(object.flatten.shift, u16),
                    ":stop_spin_spd" => number!(object.stop_spin, u16),
                    ":max_deflect" => {
                        object.max_deflection = tokens.integer()?.wrapping_mul(182) as u16
                    }
                    ":expand_coll_radius" => number!(object.collision_margin, u16),
                    ":bash_sound" => {
                        let value = tokens.required()?.as_bytes();
                        for (i, b) in object.bash_sound.iter_mut().enumerate() {
                            *b = value.get(i).copied().unwrap_or(0);
                        }
                    }
                    ":snap_to_track" | ":collidable" | ":oscillating" | ":flip_180" => {
                        let mask = match key.to_ascii_lowercase().as_str() {
                            ":snap_to_track" => 8,
                            ":collidable" => 4,
                            ":oscillating" => 16,
                            _ => 32,
                        };
                        let value = tokens.required()?;
                        if value.eq_ignore_ascii_case("true") {
                            object.flags |= mask;
                        } else if value.eq_ignore_ascii_case("false") {
                            object.flags &= !mask;
                        }
                    }
                    _ => (), // Native unknown tokens consume no associated value.
                }
            }
            result.entries.push(object);
        }
        Ok(result)
    }
}

/// General file whitespace/comments and parenthesis boundaries, unlike the
/// class parameter lexer. Original files use flat, nonnested preset groups.
struct Tokens<'a> {
    text: &'a str,
    cursor: usize,
}
impl<'a> Tokens<'a> {
    fn next(&mut self) -> Result<Option<&'a str>, Error> {
        let b = self.text.as_bytes();
        loop {
            while b
                .get(self.cursor)
                .is_some_and(|v| v.is_ascii_whitespace() || *v == 0)
            {
                self.cursor += 1;
            }
            if b.get(self.cursor) != Some(&b'#') {
                break;
            }
            while b.get(self.cursor).is_some_and(|v| *v != b'\n') {
                self.cursor += 1;
            }
        }
        let Some(&first) = b.get(self.cursor) else {
            return Ok(None);
        };
        let mut start = self.cursor;
        self.cursor += 1;
        if matches!(first, b'(' | b')') {
            return Ok(Some(&self.text[start..self.cursor]));
        }
        let quoted = first == b'"';
        if quoted {
            start += 1;
        }
        while let Some(&c) = b.get(self.cursor) {
            if (quoted && c == b'"')
                || (!quoted && (c.is_ascii_whitespace() || matches!(c, 0 | b'(' | b')')))
            {
                break;
            }
            self.cursor += 1;
        }
        let end = self.cursor;
        if quoted {
            if b.get(self.cursor) != Some(&b'"') {
                return Err(Error::Truncated);
            }
            self.cursor += 1;
        }
        Ok(Some(&self.text[start..end]))
    }
    fn required(&mut self) -> Result<&'a str, Error> {
        self.next()?.ok_or(Error::Truncated)
    }
    fn integer(&mut self) -> Result<i32, Error> {
        let token = self.required()?;
        let negative = token.starts_with('-');
        let digits = token.strip_prefix(['-', '+']).unwrap_or(token);
        let mut value = 0_u32;
        for b in digits.bytes().take_while(u8::is_ascii_digit) {
            value = value.wrapping_mul(10).wrapping_add(u32::from(b - b'0'));
        }
        Ok(if negative {
            value.wrapping_neg()
        } else {
            value
        } as i32)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::archive::Archive;
    #[test]
    fn shipped_catalogue_and_widths_casefold_duplicates_and_boolean_flags() {
        let bytes = std::fs::read(
            std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
                .join("../../assets/game/DATA/FASTFILE.DAT"),
        )
        .unwrap();
        let archive = Archive::parse(&bytes).unwrap();
        let catalog = Catalog::decode(archive.get("OBPARAMS.DAT").unwrap()).unwrap();
        assert_eq!(catalog.entries.len(), 19);
        assert_eq!(catalog.get("ROCK").unwrap().collision_margin, 500);
        assert_eq!(catalog.get("trough").unwrap().flags, 12);
        assert_eq!(
            catalog.moon.player,
            Gain {
                multiplier: 3,
                shift: 2
            }
        );
        assert_eq!(
            catalog.moon.other,
            Gain {
                multiplier: 3,
                shift: 2
            }
        );
        let catalog = Catalog::decode(b"# comment\nObject X (:solidity 65535 :friction 258 :max_deflect -1 :collidable TRUE :collidable FALSE :bash_sound abcdefghijk )\nobject x (:solidity 2)").unwrap();
        let first = catalog.get("x").unwrap();
        assert_eq!(
            (first.solidity, first.friction, first.max_deflection),
            (-1, 2, (-182_i32) as u16)
        );
        assert_eq!(first.flags, 0);
        assert_eq!(&first.bash_sound, b"abcdefghi");
        assert!(Catalog::decode(b"object unfinished (").is_err());
    }
}
