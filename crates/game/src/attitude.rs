use crate::{
    contact::Coefficient,
    fixed::trunc_shift,
    math::{self, Axis, Error, Matrix},
};

#[derive(Debug, Clone, Copy, Default)]
pub struct Config {
    pub longitudinal: Coefficient,
    pub lateral: Coefficient,
}
impl From<&tankracer_assets::config::Suspension> for Config {
    fn from(config: &tankracer_assets::config::Suspension) -> Self {
        Self {
            longitudinal: config.accel_to_rot.into(),
            lateral: config.steer_to_rot.into(),
        }
    }
}
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct State {
    pub reference: Matrix,
    pub matrix: Matrix,
    pub position: [i32; 3],
    pub angular: [i32; 3],
    pub longitudinal_delta: i32,
    pub lateral_delta: i32,
    pub pivot: [i32; 3],
}
impl State {
    pub fn step(&mut self, config: &Config, dt: i32, frame: u32) -> Result<(), Error> {
        self.angular[0] = self.angular[0]
            .wrapping_add(config.longitudinal.apply_truncated(self.longitudinal_delta));
        self.angular[2] =
            self.angular[2].wrapping_sub(config.lateral.apply_truncated(self.lateral_delta));
        self.matrix.rotate(
            Axis::Y,
            trunc_shift(self.angular[1].wrapping_mul(dt), 14) as u32,
        );
        // Guards intentionally read the OLD reference and retain cross-axis coupling.
        let r = self.reference.0;
        let horizontal = math::length2(r[0].wrapping_abs(), r[2].wrapping_abs());
        if math::angle(r[1].wrapping_abs(), horizontal)? > 12740 {
            if r[1] >= 0 {
                if self.angular[2] < 0 {
                    self.angular[2] = self.angular[2].wrapping_neg() >> 2;
                }
                self.angular[2] = self.angular[2].wrapping_add(5000);
            } else {
                if self.angular[2] > 0 {
                    self.angular[2] = (self.angular[2] >> 2).wrapping_neg();
                }
                self.angular[2] = self.angular[2].wrapping_sub(5000);
            }
        }
        let horizontal = math::length2(r[6].wrapping_abs(), r[8].wrapping_abs());
        if math::angle(r[7].wrapping_abs(), horizontal)? > 12740 {
            if r[7] <= 0 {
                if self.angular[0] < 0 {
                    self.angular[0] = trunc_shift(self.angular[2], 2).wrapping_neg();
                }
                self.angular[0] = self.angular[0].wrapping_add(5000);
            } else {
                if self.angular[0] > 0 {
                    self.angular[0] = trunc_shift(self.angular[2], 2).wrapping_neg();
                }
                self.angular[0] = self.angular[0].wrapping_sub(5000);
            }
        }
        let angles = [
            trunc_shift(self.angular[0].wrapping_mul(dt), 14),
            0,
            trunc_shift(self.angular[2].wrapping_mul(dt), 14),
        ];
        self.matrix
            .rotate_pivot(&mut self.position, self.pivot, angles);
        if frame.is_multiple_of(10) {
            self.matrix.normalize()?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn flip_guards_use_old_reference_and_original_cross_axis_rate() {
        let mut state = State {
            reference: Matrix([0, 4096, 0, 4096, 0, 0, 0, -4096, 0]),
            angular: [-100, 0, -400],
            ..State::default()
        };
        state.step(&Config::default(), 0, 1).unwrap();
        assert_eq!(state.angular, [3725, 0, 5100]);
        assert_eq!(state.matrix, Matrix::IDENTITY);
    }
}
