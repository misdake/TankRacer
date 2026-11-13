//! The simulation's original 32-bit fixed-point arithmetic, including x86 shifts.

/// A nonnegative 16-bit gain followed by an arithmetic right shift.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Coefficient {
    pub multiplier: u16,
    pub shift: u8,
}

impl Coefficient {
    pub const fn from_packed(packed: u32) -> Self {
        Self {
            multiplier: packed as u16,
            shift: (packed >> 16) as u8,
        }
    }

    pub fn apply(self, value: i32) -> i32 {
        value
            .wrapping_mul(i32::from(self.multiplier))
            .wrapping_shr(u32::from(self.shift))
    }

    pub fn apply_truncated(self, value: i32) -> i32 {
        trunc_shift(
            value.wrapping_mul(i32::from(self.multiplier)),
            u32::from(self.shift),
        )
    }
}

impl From<tankracer_assets::config::Gain> for Coefficient {
    fn from(gain: tankracer_assets::config::Gain) -> Self {
        Self {
            multiplier: gain.multiplier,
            shift: gain.shift as u8,
        }
    }
}

/// Original sign/magnitude shift. Preserve its wrapping INT_MIN behavior too.
pub fn trunc_shift(value: i32, shift: u32) -> i32 {
    if value < 0 {
        value.wrapping_neg().wrapping_shr(shift).wrapping_neg()
    } else {
        value.wrapping_shr(shift)
    }
}

pub fn rate_step(value: i32, force: i32, dt: i32) -> i32 {
    let amount = force.wrapping_mul(dt);
    if value > amount {
        value.wrapping_sub(amount)
    } else if value < amount.wrapping_neg() {
        value.wrapping_add(amount)
    } else {
        0
    }
}
pub fn decay(value: i32, shift: u8, dt: i32) -> i32 {
    let result = value.wrapping_sub(value.wrapping_mul(dt).wrapping_shr(u32::from(shift)));
    if (value < 0 && result > 0) || (value > 0 && result < 0) {
        0
    } else {
        result
    }
}
pub fn damp(value: i32, descriptor: tankracer_assets::config::Damping, dt: i32) -> i32 {
    decay(
        rate_step(value, i32::from(descriptor.step), dt),
        descriptor.shift,
        dt,
    )
}
