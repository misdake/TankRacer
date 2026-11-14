//! Exact Q12 basis/trigonometry. No host transcendentals or floating-point physics.
use crate::fixed::trunc_shift;

const fn unpack<const N: usize>(bytes: &[u8]) -> [i16; N] {
    assert!(bytes.len() == N * 2);
    let mut out = [0; N];
    let mut i = 0;
    while i < N {
        out[i] = i16::from_le_bytes([bytes[i * 2], bytes[i * 2 + 1]]);
        i += 1;
    }
    out
}
static SINE: [i16; 20480] = unpack(include_bytes!("../data/sine.bin"));
static ANGLE: [i16; 257] = unpack(include_bytes!("../data/angle.bin"));
static COSINE_ANGLE: [i16; 4096] = unpack(include_bytes!("../data/cosine-angle.bin"));

fn cosine_angle(value: i32) -> Result<i32, Error> {
    let magnitude = value.checked_abs().ok_or(Error::Arithmetic)? as usize;
    if magnitude == 4096 {
        return Ok(0);
    } // Source table's explicit terminal zero.
    COSINE_ANGLE
        .get(magnitude)
        .copied()
        .map(i32::from)
        .ok_or(Error::Arithmetic)
}

pub fn sine(angle: u32) -> i32 {
    i32::from(SINE[((angle & 65532) >> 2) as usize])
}
pub fn cosine(angle: u32) -> i32 {
    i32::from(SINE[4096 + ((angle & 65532) >> 2) as usize])
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    Arithmetic,
}

pub fn angle(n: i32, d: i32) -> Result<i32, Error> {
    if n == i32::MIN || d == i32::MIN {
        return Err(Error::Arithmetic);
    }
    let (mut a, mut b) = (n.abs(), d.abs());
    let mut v = if a == 0 {
        0
    } else if b == 0 {
        16384
    } else if a == b {
        8192
    } else {
        let complement = a > b;
        if complement {
            std::mem::swap(&mut a, &mut b);
        }
        let numerator = if b < 65536 { a.wrapping_mul(32768) } else { a };
        let denominator = if b < 65536 {
            b.wrapping_mul(128)
        } else {
            b >> 8
        };
        let index = numerator
            .checked_div(denominator)
            .ok_or(Error::Arithmetic)?;
        let value = ANGLE.get(index as usize).ok_or(Error::Arithmetic)?;
        if complement {
            16384 - i32::from(*value)
        } else {
            i32::from(*value)
        }
    };
    if d >= 0 {
        if n < 0 {
            v = v.wrapping_neg();
        }
    } else {
        v = if n < 0 {
            v.wrapping_sub(32768)
        } else {
            32768_i32.wrapping_sub(v)
        };
    }
    Ok(v)
}

pub fn length2(x: i32, y: i32) -> i32 {
    let (a, b) = (x.wrapping_abs(), y.wrapping_abs());
    if b <= a {
        a.wrapping_add(b.wrapping_sub(b >> 1))
    } else {
        a.wrapping_add(b.wrapping_sub(a >> 1))
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Axis {
    X,
    Y,
    Z,
}

/// Rows are the object's axes in world space. Local->world uses the transpose.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Matrix(pub [i32; 9]);
impl Default for Matrix {
    fn default() -> Self {
        Self::IDENTITY
    }
}
impl Matrix {
    /// Original inverse basis lookup: quantized integer tables, not
    /// host atan/acos. Input pitch axis must be within the normalized Q12 range.
    pub fn angles(self) -> Result<([u32; 3], bool), Error> {
        let v = self.0[7];
        let a = cosine_angle(v)?;
        let pitch = if v < 0 {
            a.wrapping_sub(16384)
        } else {
            16384_i32.wrapping_sub(a)
        } as u32;
        let cosine = cosine(pitch);
        let mut out = [pitch, 0, 0];
        if cosine != 0 {
            for (component, sign, slot) in [(8, 6, 1), (4, 1, 2)] {
                let q = self.0[component]
                    .wrapping_mul(4096)
                    .checked_div(cosine)
                    .ok_or(Error::Arithmetic)?
                    .clamp(-4096, 4096);
                let mut a = cosine_angle(q)?;
                if q < 0 {
                    a = 32768_i32.wrapping_sub(a);
                }
                let q = self.0[sign]
                    .wrapping_mul(4096)
                    .checked_div(cosine)
                    .ok_or(Error::Arithmetic)?;
                if q.wrapping_neg() < 0 {
                    a = 65536_i32.wrapping_sub(a);
                }
                out[slot] = a as u32;
            }
        }
        Ok((out, cosine != 0))
    }
    pub const IDENTITY: Self = Self([4096, 0, 0, 0, 4096, 0, 0, 0, 4096]);
    pub fn local_to_world(self, local: [i32; 3]) -> [i32; 3] {
        std::array::from_fn(|i| {
            self.0[i]
                .wrapping_mul(local[0])
                .wrapping_add(self.0[3 + i].wrapping_mul(local[1]))
                .wrapping_add(self.0[6 + i].wrapping_mul(local[2]))
                >> 12
        })
    }
    pub fn world_to_local(self, world: [i32; 3]) -> [i32; 3] {
        std::array::from_fn(|i| {
            self.0[i * 3]
                .wrapping_mul(world[0])
                .wrapping_add(self.0[i * 3 + 1].wrapping_mul(world[1]))
                .wrapping_add(self.0[i * 3 + 2].wrapping_mul(world[2]))
                >> 12
        })
    }
    pub fn from_angles(angles: [u32; 3]) -> Self {
        let [x, y, z] = angles;
        let (cx, sx, cy, sy) = (cosine(x), sine(x), cosine(y), sine(y));
        let (u, v) = (sy.wrapping_mul(sx) >> 12, cy.wrapping_mul(sx) >> 12);
        let mut m = [0; 9];
        if z == 0 {
            m[0] = cy;
            m[2] = sy;
            m[4] = cx;
            m[1] = 0;
            m[3] = u;
            m[5] = v.wrapping_neg();
        } else {
            let (cz, sz) = (cosine(z), sine(z));
            m[0] = cy.wrapping_mul(cz).wrapping_sub(u.wrapping_mul(sz)) >> 12;
            m[1] = cx.wrapping_mul(sz).wrapping_neg() >> 12;
            m[2] = sy.wrapping_mul(cz).wrapping_add(v.wrapping_mul(sz)) >> 12;
            m[3] = cy.wrapping_mul(sz).wrapping_add(u.wrapping_mul(cz)) >> 12;
            m[5] = sz.wrapping_mul(sy).wrapping_sub(v.wrapping_mul(cz)) >> 12;
            m[4] = cx.wrapping_mul(cz) >> 12;
        }
        m[7] = sx;
        m[8] = cy.wrapping_mul(cx) >> 12;
        m[6] = cx.wrapping_mul(sy).wrapping_neg() >> 12;
        Self(m)
    }
    pub fn rotate(&mut self, axis: Axis, angle: u32) {
        if angle == 0 {
            return;
        }
        let (first, second) = match axis {
            Axis::X => (3, 6),
            Axis::Y => (0, 6),
            Axis::Z => (0, 3),
        };
        let (cosine, negative_sine) = (cosine(angle), sine(angle.wrapping_neg()));
        for i in 0..3 {
            let (a, b) = (self.0[first + i], self.0[second + i]);
            if axis == Axis::Y {
                self.0[first + i] = trunc_shift(
                    cosine
                        .wrapping_mul(a)
                        .wrapping_sub(negative_sine.wrapping_mul(b)),
                    12,
                );
                self.0[second + i] = trunc_shift(
                    negative_sine
                        .wrapping_mul(a)
                        .wrapping_add(cosine.wrapping_mul(b)),
                    12,
                );
            } else {
                self.0[first + i] = trunc_shift(
                    cosine
                        .wrapping_mul(a)
                        .wrapping_add(negative_sine.wrapping_mul(b)),
                    12,
                );
                self.0[second + i] = trunc_shift(
                    cosine
                        .wrapping_mul(b)
                        .wrapping_sub(negative_sine.wrapping_mul(a)),
                    12,
                );
            }
        }
    }
    pub fn normalize(&mut self) -> Result<(), Error> {
        for transpose in [false, true] {
            for row in 0..3 {
                let indices = std::array::from_fn::<_, 3, _>(|j| {
                    if transpose { row + j * 3 } else { row * 3 + j }
                });
                let values = indices.map(|i| self.0[i]);
                let sum = values
                    .into_iter()
                    .fold(0_i32, |sum, v| sum.wrapping_add(v.wrapping_mul(v)));
                let denominator = (sum >> 13).wrapping_add(2048);
                for (index, value) in indices.into_iter().zip(values) {
                    self.0[index] = value
                        .wrapping_mul(4096)
                        .checked_div(denominator)
                        .ok_or(Error::Arithmetic)?;
                }
            }
        }
        Ok(())
    }
    pub fn rotate_pivot(&mut self, position: &mut [i32; 3], pivot: [i32; 3], angles: [i32; 3]) {
        let before = self.local_to_world(pivot);
        self.rotate(Axis::Y, angles[1] as u32);
        self.rotate(Axis::X, angles[0] as u32);
        self.rotate(Axis::Z, angles[2] as u32);
        let after = self.local_to_world(pivot);
        for i in 0..3 {
            position[i] = position[i].wrapping_add(before[i].wrapping_sub(after[i]));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lookup_quantization_and_terminal_angle_alias_are_intentional() {
        assert_eq!(sine(0), 0);
        assert_eq!(sine(3), 0);
        assert_eq!(sine(4), 2);
        assert_eq!(cosine(0), 4096);
        assert_eq!(angle(1, 1), Ok(8192));
        assert_eq!(angle(65536, 65537), Ok(0)); // Ratio index256 aliases the sine table's first zero.
        assert_eq!(angle(i32::MIN, 1), Err(Error::Arithmetic));
    }
    #[test]
    fn inverse_basis_keeps_lookup_endpoints_and_singular_pitch() {
        assert_eq!(Matrix::IDENTITY.angles(), Ok(([0; 3], true)));
        assert_eq!(
            Matrix::from_angles([0, 0, 16384]).angles(),
            Ok(([0, 0, 16384], true))
        );
        assert_eq!(
            Matrix::from_angles([16384, 0, 0]).angles(),
            Ok(([16384, 0, 0], false))
        );
        assert_eq!(
            Matrix::from_angles([49152, 0, 0]).angles(),
            Ok(([(-16384_i32) as u32, 0, 0], false))
        );
    }
    #[test]
    fn zero_rotation_skips_arithmetic_but_nonzero_full_turn_does_not() {
        let mut matrix = Matrix([1000000; 9]);
        let initial = matrix;
        matrix.rotate(Axis::Y, 0);
        assert_eq!(matrix, initial);
        matrix.rotate(Axis::Y, 65536);
        assert_eq!(
            matrix.0,
            [
                -48576, -48576, -48576, 1000000, 1000000, 1000000, -48576, -48576, -48576
            ]
        );
    }
    #[test]
    fn normalization_error_preserves_rows_already_written() {
        let mut matrix = Matrix([4000, 1, 0, 65407, 339, 0, 0, 0, 4096]);
        assert_eq!(matrix.normalize(), Err(Error::Arithmetic));
        assert_eq!(&matrix.0[..3], &[4094, 1, 0]);
        assert_eq!(&matrix.0[3..], &[65407, 339, 0, 0, 0, 4096]);
    }
}
