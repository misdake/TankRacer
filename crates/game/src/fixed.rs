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
}

/// Original sign/magnitude shift. Preserve its wrapping INT_MIN behavior too.
pub fn trunc_shift(value: i32, shift: u32) -> i32 {
    if value < 0 {
        value.wrapping_neg().wrapping_shr(shift).wrapping_neg()
    } else {
        value.wrapping_shr(shift)
    }
}
