use super::{Damping, Error, Gain, text::Reader};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Part {
    Body,
    Turret,
    Gun,
    LeftTrack,
    RightTrack,
    Under,
    End,
    Distance,
    Aerial,
    Bottom,
}
impl Part {
    pub const KEYS: [&'static str; 10] = [
        "body_node:",
        "turret_node:",
        "gun_node:",
        "ltrack_node:",
        "rtrack_node:",
        "under_node:",
        "end_node:",
        "distance_node:",
        "aerial_node:",
        "fbottom_node:",
    ];
    pub fn slot(self) -> usize {
        self as usize
    }
}
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Node {
    pub name: String,
    pub offset: [i32; 3],
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Drive {
    pub max_rpm: i32,
    pub max_power: i32,
    pub max_turn: i32,
    pub max_turn_battle: Option<i32>,
    pub initial_power: i32,
    pub power_mul: i32,
    pub max_brakes: i32,
    pub turn_fraction: Gain,
    pub hill_roll: Gain,
    pub understeer_fraction: Gain,
    pub rearweight_fraction: Gain,
    pub rearweight_accel_fraction: Gain,
    pub traction_limit: i32,
    pub traction_limit_battle: Option<i32>,
    pub catchup_traction_limit: i32,
    pub sidegrip_fraction: Gain,
    pub forward_drag: Damping,
    pub track_drag: Damping,
    pub track_air_drag: Damping,
    pub track_spin_drag: Damping,
    pub gravity_drag: Damping,
    pub rotational_drag: Damping,
    pub air_rotational_drag: Damping,
    pub sideways_drag: Damping,
    pub force_drag: Damping,
    pub turbo_amount: i32,
    pub turbo_power_mul: i32,
}
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Suspension {
    pub initial_weight: i32,
    pub initial_pressure: i32,
    pub pressure_fraction: Gain,
    pub cog_offset: i32,
    pub force_to_susp: Gain,
    pub down_force_to_susp: Gain,
    pub susp_to_force: Gain,
    pub susp_to_rot: Gain,
    pub spring_pre_shift: i32,
    pub spring: Gain,
    pub easy_spring: Gain,
    pub susp_speed_drag: Damping,
    pub susp_drag: Damping,
    pub accel_to_rot: Gain,
    pub steer_to_rot: Gain,
    pub susp_to_shape: Gain,
    pub shape_min: i32,
    pub shape_max: i32,
}
impl Suspension {
    pub fn travel_bounds(&self) -> Result<[i32; 2], Error> {
        let bounds = [self.shape_min, self.shape_max].map(|v| {
            v.wrapping_mul(i32::from(self.susp_to_shape.multiplier))
                .wrapping_shl(u32::from(self.susp_to_shape.shift as u8))
        });
        if bounds[0] > bounds[1] {
            Err(Error::InvalidSuspensionBounds)
        } else {
            Ok(bounds)
        }
    }
}
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Tank {
    pub nodes: [Node; 10],
    pub glow_offset: [i32; 3],
    pub turbo_offset: [i32; 3],
    pub drive: Drive,
    pub suspension: Suspension,
}
#[derive(Debug, Clone, Copy, Default)]
pub struct ResolveContext {
    pub special: bool,
    pub player: bool,
    pub mode: u32,
    pub map: u32,
    pub circuit_power_mul: i32,
    pub ambient_battle: Option<[i32; 2]>,
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ResolvedTank {
    pub tank: Tank,
    pub mode5_backup: Option<[i32; 2]>,
}

impl Tank {
    pub fn decode(bytes: &[u8]) -> Result<Self, Error> {
        let mut value = Self::default();
        value.apply(bytes)?;
        Ok(value)
    }
    pub fn apply(&mut self, bytes: &[u8]) -> Result<(), Error> {
        let mut reader = Reader::new(bytes)?;
        while let Some(key) = reader.token()? {
            let key = key.to_ascii_lowercase();
            if let Some(slot) = Part::KEYS.iter().position(|&k| k == key) {
                self.nodes[slot] = reader.node()?;
                continue;
            }
            let d = &mut self.drive;
            let s = &mut self.suspension;
            match key.as_str() {
                "glow_offset:" => self.glow_offset = reader.vector()?,
                "turbo_offset:" => self.turbo_offset = reader.vector()?,
                "max_rpm:" => d.max_rpm = reader.integer()?,
                "max_power:" => d.max_power = reader.integer()?,
                "max_turn:" => d.max_turn = reader.integer()?,
                "max_turn_battle:" => d.max_turn_battle = Some(reader.integer()?),
                "initial_power:" => d.initial_power = reader.integer()?,
                "power_mul:" => d.power_mul = reader.integer()?,
                "max_brakes:" => d.max_brakes = reader.integer()?,
                "turn_fraction:" => d.turn_fraction = reader.gain()?,
                "hill_roll:" => d.hill_roll = reader.gain()?,
                "understeer_fraction:" => d.understeer_fraction = reader.gain()?,
                "rearweight_fraction:" => d.rearweight_fraction = reader.gain()?,
                "rearweight_accel_fraction:" => d.rearweight_accel_fraction = reader.gain()?,
                "traction_limit:" => d.traction_limit = reader.integer()?,
                "traction_limit_battle:" => d.traction_limit_battle = Some(reader.integer()?),
                "catchup_traction_limit:" => d.catchup_traction_limit = reader.integer()?,
                "initial_weight:" => s.initial_weight = reader.integer()?,
                "initial_pressure:" => s.initial_pressure = reader.integer()?,
                "pressure_fraction:" => s.pressure_fraction = reader.gain()?,
                "sidegrip_fraction:" => d.sidegrip_fraction = reader.gain()?,
                "cog_offset:" => s.cog_offset = reader.integer()?,
                "forward_drag:" => d.forward_drag = reader.damping()?,
                "track_drag:" => d.track_drag = reader.damping()?,
                "track_air_drag:" => d.track_air_drag = reader.damping()?,
                "track_spin_drag:" => d.track_spin_drag = reader.damping()?,
                "gravity_drag:" => d.gravity_drag = reader.damping()?,
                "rotational_drag:" => d.rotational_drag = reader.damping()?,
                "air_rotational_drag:" => d.air_rotational_drag = reader.damping()?,
                "sideways_drag:" => d.sideways_drag = reader.damping()?,
                "force_drag:" => d.force_drag = reader.damping()?,
                "force_to_susp:" => s.force_to_susp = reader.gain()?,
                "down_force_to_susp:" => s.down_force_to_susp = reader.gain()?,
                "susp_to_force:" => s.susp_to_force = reader.gain()?,
                "susp_to_rot:" => s.susp_to_rot = reader.gain()?,
                "spring_pre_shift:" => s.spring_pre_shift = reader.integer()?,
                "spring:" => s.spring = reader.gain()?,
                "easy_spring:" => s.easy_spring = reader.gain()?,
                "susp_speed_drag:" => s.susp_speed_drag = reader.damping()?,
                "susp_drag:" => s.susp_drag = reader.damping()?,
                "accel_to_rot:" => s.accel_to_rot = reader.gain()?,
                "steer_to_rot:" => s.steer_to_rot = reader.gain()?,
                "susp_to_shape:" => s.susp_to_shape = reader.gain()?,
                "shape_min_susp:" => s.shape_min = reader.integer()?,
                "shape_max_susp:" => s.shape_max = reader.integer()?,
                "turbo_amount:" => d.turbo_amount = reader.integer()?,
                "turbo_power_mul:" => d.turbo_power_mul = reader.integer()?,
                _ => return Err(Error::UnknownKey(key)),
            }
        }
        Ok(())
    }
    pub fn resolve(&self, context: ResolveContext) -> Result<ResolvedTank, Error> {
        let mut result = ResolvedTank {
            tank: self.clone(),
            mode5_backup: None,
        };
        let d = &mut result.tank.drive;
        if context.special && context.player {
            d.sidegrip_fraction.shift = d.sidegrip_fraction.shift.wrapping_add(1);
            d.max_power = d.max_power.wrapping_add(3000000);
            d.initial_power = d.initial_power.wrapping_add(2000000);
            d.power_mul = d.power_mul.wrapping_sub(80);
            d.traction_limit = d.traction_limit.wrapping_add(150);
        }
        if context.mode == 2 {
            d.max_turn = d
                .max_turn_battle
                .or(context.ambient_battle.map(|a| a[0]))
                .ok_or(Error::MissingBattleOverride)?;
            d.traction_limit = d
                .traction_limit_battle
                .or(context.ambient_battle.map(|a| a[1]))
                .ok_or(Error::MissingBattleOverride)?;
        } else if context.mode == 5 && context.player {
            result.mode5_backup = Some([d.traction_limit, d.initial_power]);
        }
        if context.map == 7 {
            d.power_mul = context.circuit_power_mul;
        }
        result.tank.suspension.travel_bounds()?;
        Ok(result)
    }
}
