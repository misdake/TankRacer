//! Weapon spin, packed color chase and its shared update pulse (429A00).
use crate::{fixed::trunc_shift, random};

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct State {
    pub flags: u8,
    pub visible: u8,
    pub hold: i32,
    pub color_timer: i32,
    pub color: u32,
    pub target_color: u32,
    pub angle: i32,
    pub speed: i32,
    pub disabled: u8,
}
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Shared {
    pub count: u8,
    pub pulse: u8,
}
#[derive(Debug, Clone, Copy)]
pub struct Tuning {
    pub fast: i32,
    pub slow: i32,
    pub stop: i32,
    pub accelerate: u32,
    pub settle: u32,
    pub fade: u32,
}
impl Default for Tuning {
    fn default() -> Self {
        Self {
            fast: 27955,
            slow: 16307,
            stop: 931,
            accelerate: 0x08010001,
            settle: 0x09010001,
            fade: 0x0e010005,
        }
    }
}
/// Scalar approach (441400). Both products deliberately narrow before decay.
pub(super) fn approach(value: i32, goal: i32, packed: u32, dt: i32) -> i32 {
    let amount = packed.wrapping_mul(dt as u32 & 65535) as i16 as i32;
    let factor = ((packed >> 16 & 255).wrapping_mul(dt as u32 & 255) & 255) as i32;
    let delta = crate::fixed::rate_step(value.wrapping_sub(goal), amount, 1);
    let result = delta.wrapping_sub(trunc_shift(delta.wrapping_mul(factor), packed >> 24));
    let result = if (delta < 0 && result > 0) || (delta > 0 && result < 0) {
        0
    } else {
        result
    };
    result.wrapping_add(goal)
}
fn chase(value: &mut u32, target: u32, mask: u32, base: u32, up: u32, down: u32) {
    let goal = target & mask;
    if (*value ^ target) & mask == 0 {
        return;
    }
    if *value & mask < goal {
        *value ^= (((*value & base).wrapping_add(up)) ^ *value) & mask;
        if *value & mask > goal {
            *value ^= (*value ^ target) & mask;
        }
    }
    if *value & mask > goal {
        *value ^= (((*value & base).wrapping_sub(down)) ^ *value) & mask;
        if *value & mask < goal {
            *value ^= (*value ^ target) & mask;
        }
    }
}
impl State {
    fn choose_color(&mut self, seed: &mut u32) -> u32 {
        let mut roll = (random::next(seed) >> 4) & 65535;
        self.target_color ^= ((roll % 12 * 2 + 6) ^ self.target_color) & 0x3e;
        roll = (random::next(seed) >> 4) & 65535;
        self.target_color ^= (((roll % 12 + 3) * 64) ^ self.target_color) & 0x7c0;
        roll = (random::next(seed) >> 4) & 65535;
        self.target_color ^= (((roll % 12 + 3) * 2048) ^ self.target_color) & 0xf800;
        roll / 12
    }
    pub fn start(&mut self, player: bool, mode: i32, shared: &mut Shared, seed: &mut u32) -> u32 {
        if self.speed == 0 {
            shared.count = shared.count.wrapping_add(1);
        }
        self.hold = if !player || mode == 4 || mode == 5 {
            0
        } else {
            4000
        };
        self.color &= 0xffff0001;
        self.speed = 1;
        self.visible = 1;
        let result = self.choose_color(seed);
        self.flags = 1;
        shared.pulse = 0;
        result
    }
    pub fn step(
        &mut self,
        dt: i32,
        tuning: Tuning,
        limit: u32,
        shared: &mut Shared,
        seed: &mut u32,
    ) -> Result<(), i32> {
        if limit == 0 {
            return Err(-1);
        }
        if self.disabled == 0 {
            self.disabled = 1;
        }
        let mut goal = 0;
        let mut packed = 0;
        let flags = self.flags;
        if flags & 1 != 0 {
            goal = tuning.fast;
            packed = tuning.accelerate;
        } else if flags & 2 != 0 {
            goal = tuning.slow;
            if self.speed == goal {
                self.hold = self.hold.wrapping_sub(dt);
                if self.hold < 0 {
                    self.flags = 4;
                }
            } else {
                packed = tuning.settle;
            }
        } else if flags & 4 != 0 && self.speed > tuning.stop {
            goal = tuning.stop;
            packed = tuning.accelerate;
        }
        if flags & 1 != 0
            || (flags & 2 != 0 && self.speed != goal)
            || (flags & 3 == 0 && flags & 4 != 0 && self.speed > tuning.stop)
        {
            self.speed = approach(self.speed, goal, packed, dt);
            if flags & 1 != 0 && self.speed == tuning.fast {
                self.flags = 2;
            }
        }
        self.angle = self.angle.wrapping_add(dt.wrapping_mul(self.speed) >> 8);
        let mut work = 0;
        while self.angle > 32760 || self.angle < -32759 {
            if work >= limit {
                return Err(-3);
            }
            work += 1;
            self.angle = if self.angle > 32760 {
                self.angle.wrapping_sub(65520)
            } else {
                self.angle.wrapping_add(65520)
            };
        }
        self.color_timer = self.color_timer.wrapping_add(dt);
        if self.color_timer <= 60 {
            shared.pulse = 0;
            return Ok(());
        }
        self.color_timer = 0;
        shared.pulse = 1;
        if self.flags & 4 != 0 && self.speed <= tuning.stop {
            self.speed = approach(self.speed, 0, tuning.fade, dt);
            if self.speed == 0 {
                self.speed = 1;
            }
            for (mask, base, down) in [
                (0x3e, 0xfffffffe, 5),
                (0x7c0, 0xffffffc0, 0x81),
                (0xf800, 0xfffff800, 0x1001),
            ] {
                if self.color & mask != 0 {
                    self.color ^= (((self.color & base).wrapping_sub(down)) ^ self.color) & mask;
                }
            }
            if self.color & 0xfffe == 0 && self.speed == 1 {
                if self.flags & 8 != 0 {
                    self.color = (self.color & 0xffffffe1) | 0x20;
                } else {
                    self.speed = 0;
                    self.disabled = 0;
                    shared.count = shared.count.wrapping_sub(1);
                }
            }
        } else {
            chase(&mut self.color, self.target_color, 0x3e, 0xfffffffe, 6, 5);
            chase(
                &mut self.color,
                self.target_color,
                0x7c0,
                0xffffffc0,
                0xc0,
                0x81,
            );
            chase(
                &mut self.color,
                self.target_color,
                0xf800,
                0xfffff800,
                0x1800,
                0x1001,
            );
            if (self.color ^ self.target_color) & 0xfffe == 0 {
                self.choose_color(seed);
            }
        }
        Ok(())
    }
}
