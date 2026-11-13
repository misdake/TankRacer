//! Authored physics resources. Addresses and packed object layouts stay in test oracles.
mod tank;
mod text;
use std::fmt;
pub use tank::{Drive, Node, Part, ResolveContext, ResolvedTank, Suspension, Tank};
use text::Reader;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Error {
    InvalidText(usize),
    MissingValue(usize),
    UnknownKey(String),
    MissingBattleOverride,
    InvalidSuspensionBounds,
}
impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "invalid physics config: {self:?}")
    }
}
impl std::error::Error for Error {}

/// Resource words remain complete here; consumers select the source shift byte.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Gain {
    pub multiplier: u16,
    pub shift: u16,
}
impl Gain {
    pub fn packed(self) -> u32 {
        u32::from(self.multiplier) | (u32::from(self.shift) << 16)
    }
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Damping {
    pub step: i16,
    pub fraction: u8,
    pub shift: u8,
}
impl Damping {
    pub fn packed(self) -> u32 {
        u32::from(self.step as u16)
            | (u32::from(self.fraction) << 16)
            | (u32::from(self.shift) << 24)
    }
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Surface {
    pub drag: Gain,
    pub grip: Gain,
    /// Dry, rain light/medium/heavy, snow light/medium/heavy.
    pub slide: [Gain; 7],
    pub bump: Gain,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Surfaces {
    pub entries: [Surface; 10],
}

impl Surfaces {
    pub const NAMES: [&'static str; 10] = [
        "none",
        "track",
        "grass",
        "water",
        "mud",
        "gravel",
        "sand",
        "ice",
        "cattle_grid",
        "snow",
    ];
    pub fn decode(bytes: &[u8]) -> Result<Self, Error> {
        let mut reader = Reader::new(bytes)?;
        let mut result = Self::default();
        while reader.token()?.is_some() {
            // Original ignores the header token itself.
            let name = reader.required()?.to_ascii_lowercase();
            let index = Self::NAMES
                .iter()
                .position(|&n| n == name)
                .ok_or(Error::UnknownKey(name))?;
            let surface = &mut result.entries[index];
            for _ in 0..10 {
                let key = reader.required()?.to_ascii_lowercase();
                let value = reader.gain()?;
                match key.as_str() {
                    "drag_factor:" => surface.drag = value,
                    "grip_factor:" => surface.grip = value,
                    "slide_factor:" => surface.slide[0] = value,
                    "slide_factor_rain_light:" => surface.slide[1] = value,
                    "slide_factor_rain_medium:" => surface.slide[2] = value,
                    "slide_factor_rain_heavy:" => surface.slide[3] = value,
                    "slide_factor_snow_light:" => surface.slide[4] = value,
                    "slide_factor_snow_medium:" => surface.slide[5] = value,
                    "slide_factor_snow_heavy:" => surface.slide[6] = value,
                    "bump_factor:" => surface.bump = value,
                    _ => return Err(Error::UnknownKey(key)),
                }
            }
        }
        result.entries[0] = result.entries[1];
        Ok(result)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::archive::Archive;

    #[test]
    fn authored_surfaces_and_all_fifteen_tanks_decode_without_a_memory_layout() {
        let bytes = std::fs::read(
            std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
                .join("../../assets/game/DATA/FASTFILE.DAT"),
        )
        .unwrap();
        let archive = Archive::parse(&bytes).unwrap();
        let surfaces = Surfaces::decode(archive.get("SURFS.DAT").unwrap()).unwrap();
        assert_eq!(surfaces.entries[0], surfaces.entries[1]);
        assert_eq!(
            surfaces.entries[1].grip,
            Gain {
                multiplier: 1000,
                shift: 10
            }
        );
        assert_eq!(
            surfaces.entries[7].slide[0],
            Gain {
                multiplier: 256,
                shift: 10
            }
        );
        for index in 1..=15 {
            let tank = Tank::decode(archive.get(&format!("TANK{index}.DAT")).unwrap()).unwrap();
            assert!(tank.nodes.iter().all(|n| !n.name.is_empty()));
            assert_eq!(tank.suspension.travel_bounds().unwrap(), [-307200, 307200]);
            let normal = tank.resolve(ResolveContext::default()).unwrap();
            assert_eq!(normal.tank, tank);
        }
    }

    #[test]
    fn duplicate_casefold_wrapping_widths_and_explicit_override_dependencies() {
        let mut tank=Tank::decode(b"# comment\nMAX_TURN: 1\nmax_turn: 4294967297\nsidegrip_fraction: -1 65535\nforward_drag: 65535 257 258\nbody_node: \"tank b.sm\" -1 2 3\n").unwrap();
        assert_eq!(tank.drive.max_turn, 1);
        assert_eq!(
            tank.drive.sidegrip_fraction,
            Gain {
                multiplier: 65535,
                shift: 65535
            }
        );
        assert_eq!(
            tank.drive.forward_drag,
            Damping {
                step: -1,
                fraction: 1,
                shift: 2
            }
        );
        assert_eq!(tank.nodes[Part::Body.slot()].name, "tank b.sm");
        assert_eq!(
            tank.resolve(ResolveContext {
                mode: 2,
                ..ResolveContext::default()
            }),
            Err(Error::MissingBattleOverride)
        );
        tank.apply(b"max_turn_battle: 123\ntraction_limit_battle: 456\n")
            .unwrap();
        let battle = tank
            .resolve(ResolveContext {
                mode: 2,
                ambient_battle: Some([7, 8]),
                ..ResolveContext::default()
            })
            .unwrap();
        assert_eq!(battle.tank.drive.max_turn, 123);
        assert_eq!(battle.tank.drive.traction_limit, 456);
        assert!(matches!(
            Tank::decode(b"unknown: 1\n"),
            Err(Error::UnknownKey(_))
        ));
        assert!(Tank::decode(b"max_rpm:").is_err());
    }
}
