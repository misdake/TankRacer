//! One suspension contact (42E400). Values retain the original fixed-point units.
pub use crate::fixed::Coefficient;

#[derive(Debug, Clone, Copy, Default)]
pub struct Config {
    /// Resource validation must enforce lower <= upper.
    pub lower: i32,
    pub upper: i32,
    pub force_positive: Coefficient,
    pub force_negative: Coefficient,
    pub output_gain: Coefficient,
    pub tilt_gain: Coefficient,
    pub shape_shift: u32,
    pub restore_positive: Coefficient,
    pub restore_negative: Coefficient,
    pub damping_shift: u8,
    pub travel_dead_step: i16,
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct State {
    pub travel: i32,
    pub rate: i32,
    pub output: i32,
    pub tilt: [i32; 2],
}

impl State {
    pub fn step(&mut self, config: &Config, mut force: i32, dt: i32, corner: u32, auxiliary: i32) {
        debug_assert!(config.lower <= config.upper);
        if force > 0 {
            self.rate = self.rate.wrapping_add(config.force_positive.apply(force));
        } else if force < 0 {
            self.rate = self.rate.wrapping_add(config.force_negative.apply(force));
        } else {
            self.output = 0;
        }

        let shaped = self.travel.wrapping_shr(config.shape_shift);
        let restoring_gain = if self.travel <= 0 {
            config.restore_negative
        } else {
            config.restore_positive
        };
        let restoring = restoring_gain
            .apply(shaped.wrapping_mul(shaped))
            .wrapping_mul(dt);
        self.rate = self.rate.wrapping_add(if self.travel <= 0 {
            restoring
        } else {
            restoring.wrapping_neg()
        });
        let previous = self.rate;
        self.rate = previous.wrapping_sub(
            previous
                .wrapping_mul(dt)
                .wrapping_shr(u32::from(config.damping_shift)),
        );
        if (previous < 0 && self.rate > 0) || (previous > 0 && self.rate < 0) {
            self.rate = 0;
        }

        self.travel = self.travel.wrapping_add(self.rate);
        let step = i32::from(config.travel_dead_step).wrapping_mul(dt);
        self.travel = if step < self.travel {
            self.travel.wrapping_sub(step)
        } else if self.travel < step.wrapping_neg() {
            self.travel.wrapping_add(step)
        } else {
            0
        };
        self.travel = self.travel.clamp(config.lower, config.upper);

        if force > 0 {
            force = config.output_gain.apply(self.travel);
        }
        self.output = force >> 14;
        let tilt = config.tilt_gain.apply(force);
        if corner >= 4 || (tilt <= 0 && auxiliary != 2) {
            return;
        }
        let tilt = tilt >> 9;
        self.tilt[0] = self.tilt[0].wrapping_add(if corner == 0 || corner == 2 {
            tilt.wrapping_neg()
        } else {
            tilt
        });
        self.tilt[1] = self.tilt[1].wrapping_add(if corner < 2 {
            tilt.wrapping_neg()
        } else {
            tilt
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn damping_cannot_reverse_the_rate_and_travel_stays_in_bounds() {
        let config = Config {
            lower: -20,
            upper: 20,
            ..Config::default()
        };
        let mut state = State {
            rate: 100,
            travel: 30,
            ..State::default()
        };
        state.step(&config, 0, 2, 4, 0);
        assert_eq!(state.rate, 0); // 100 - 100*2 would reverse sign.
        assert_eq!(state.travel, 20);
    }

    #[test]
    fn negative_tilt_requires_airborne_auxiliary_and_valid_corner() {
        let config = Config {
            lower: -100,
            upper: 100,
            tilt_gain: Coefficient {
                multiplier: 1,
                shift: 0,
            },
            ..Config::default()
        };
        let mut state = State::default();
        state.step(&config, -16384, 1, 0, 1);
        assert_eq!(state.tilt, [0, 0]);
        assert_eq!(state.output, -1);
        state.step(&config, -16384, 1, 0, 2);
        assert_eq!(state.tilt, [32, 32]);
        state.step(&config, -16384, 1, 4, 2);
        assert_eq!(state.tilt, [32, 32]);
    }
}
