//! frame_turret_input: Player's Z/X rotation and both-button centering.
//! The angle belongs to the authoritative pose input, not a render transform.
use crate::fixed::damp;
use tankracer_assets::config::Damping;
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Keys {
    pub left: bool,
    pub right: bool,
}
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct State {
    pub velocity: i32,
    /// Original player-slot cooldown/latch are owned here for single-player.
    pub cooldown: i32,
    pub centering: bool,
    pub moving: bool,
    pub center_lock: i16,
    pub pressed: [bool; 2],
}
impl State {
    fn integrate(&mut self, angle: &mut i16, dt: i32) {
        self.velocity = damp(
            self.velocity,
            Damping {
                step: 1,
                fraction: 1,
                shift: 7,
            },
            dt,
        );
        *angle = angle.wrapping_add((dt.wrapping_mul(self.velocity) >> 6) as i16);
    }
    pub fn tick(&mut self, angle: &mut i16, keys: Keys, dt: i32) {
        self.pressed = [false; 2];
        self.cooldown = self.cooldown.wrapping_sub(dt);
        for (i, down) in [keys.left, keys.right].into_iter().enumerate() {
            if down && self.cooldown < 0 {
                let force = 22_i32.wrapping_mul(dt);
                self.velocity = if i == 0 {
                    self.velocity.wrapping_add(force)
                } else {
                    self.velocity.wrapping_sub(force)
                };
                self.pressed[i] = true;
                self.centering = false;
            }
        }
        if self.pressed == [true; 2] && !self.centering && *angle != 0 && self.center_lock < 1 {
            self.centering = true;
            self.cooldown = 600;
        }
        let before = *angle;
        if !self.centering {
            self.integrate(angle, dt);
        } else if before != 0 {
            let force = 44_i32.wrapping_mul(dt);
            self.velocity = if before < 0 {
                self.velocity.wrapping_add(force)
            } else {
                self.velocity.wrapping_sub(force)
            };
            self.integrate(angle, dt);
            if (before < 0 && *angle > 0) || (before > 0 && *angle < 0) {
                *angle = 0;
                self.velocity = 0;
                self.centering = false;
            }
        }
        self.moving = self.velocity != 0;
    }
}
