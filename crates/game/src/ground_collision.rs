//! Ordered four-contact ground response (432190). Effects execute synchronously.
use crate::{
    collision, damage,
    fixed::trunc_shift,
    geometry::approximate_distance,
    impact_particles,
    math::{self, Matrix},
};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Contact {
    pub kind: i16,
    pub corner: i32,
    pub normal: [i32; 3],
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct State {
    pub velocity: [i32; 3],
    pub yaw: i32,
}
#[derive(Debug, Clone, Copy)]
pub struct Environment {
    pub mode: i32,
    pub player: bool,
    pub forward_velocity: i32,
    pub position: [i32; 3],
    pub external_basis: Option<Matrix>,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    Arithmetic(math::Error),
    Effect(i32),
}
/// Required game operations, not deferred rendering notices. Implementations
/// commit shared RNG, particles and damage before returning; failure stops the
/// stage with earlier writes intact. No silent default implementation exists.
pub trait Effects {
    fn impact(&mut self, contact: usize, kind: i32) -> Result<(), i32>;
    fn track_damage(&mut self, kind: i32, world: [i32; 3]) -> Result<(), i32>;
}

/// Real synchronous game operations for the four-contact response. Contact
/// record index and its authored corner are distinct: emission reads the latter.
pub struct GameEffects<'a> {
    particles: &'a mut impact_particles::State,
    vehicle: &'a mut impact_particles::Vehicle,
    context: impact_particles::Context<'a>,
    damage: damage::Target<'a>,
    corners: [u32; 4],
}
impl<'a> GameEffects<'a> {
    pub fn new(
        contacts: &[Contact; 4],
        particles: &'a mut impact_particles::State,
        vehicle: &'a mut impact_particles::Vehicle,
        context: impact_particles::Context<'a>,
        damage: damage::Target<'a>,
    ) -> Self {
        Self {
            particles,
            vehicle,
            context,
            damage,
            corners: contacts.map(|c| c.corner as u32),
        }
    }
}
impl Effects for GameEffects<'_> {
    fn impact(&mut self, contact: usize, kind: i32) -> Result<(), i32> {
        let corner = *self.corners.get(contact).ok_or(-4)?;
        self.particles
            .emit(self.vehicle, corner, kind, &mut self.context)
            .map(|_| ())
            .map_err(impact_particles::Error::status)
    }
    fn track_damage(&mut self, kind: i32, world: [i32; 3]) -> Result<(), i32> {
        self.damage.track(kind, world).map_err(|_| -1)
    }
}
fn effect(result: Result<(), i32>) -> Result<(), Error> {
    result.map_err(|code| Error::Effect(if code < 0 { code } else { -5 }))
}
impl State {
    pub fn step(
        &mut self,
        contacts: &mut [Contact; 4],
        env: Environment,
        effects: &mut impl Effects,
    ) -> Result<(), Error> {
        let initial = self.velocity;
        let mut last = -1;
        let mut flipped = false;
        for (index, contact) in contacts.iter_mut().enumerate() {
            match contact.kind {
                4 | 5 => {
                    if contact.kind == 4 {
                        if env.player
                            && env.forward_velocity.wrapping_abs() > 150000
                            && env.mode != 2
                            && env.mode != 3
                        {
                            effect(effects.impact(index, if index < 2 { 3 } else { 4 }))?;
                        }
                        last = contact.corner;
                    }
                    let r = collision::respond(
                        contact.normal,
                        self.velocity,
                        None,
                        None,
                        env.mode,
                        env.player,
                    )
                    .map_err(Error::Arithmetic)?;
                    contact.normal = r.normal;
                    self.velocity = r.velocity;
                    if r.approach > 0 && (contact.corner as u32) <= 3 {
                        let negative = contact.corner == 0 || contact.corner == 3;
                        if (negative && self.yaw > 0) || (!negative && self.yaw < 0) {
                            self.yaw = trunc_shift(self.yaw.wrapping_mul(300), 10).wrapping_neg();
                        } else {
                            let amount =
                                trunc_shift(approximate_distance(initial).wrapping_mul(300), 10);
                            let amount = trunc_shift(amount.wrapping_mul(r.approach), 12);
                            self.yaw = if negative {
                                self.yaw.wrapping_sub(amount)
                            } else {
                                self.yaw.wrapping_add(amount)
                            };
                        }
                    }
                }
                3 if !flipped => {
                    self.velocity[0] = self.velocity[0].wrapping_neg();
                    self.velocity[2] = self.velocity[2].wrapping_neg();
                    flipped = true;
                }
                _ => {}
            }
        }
        if last != -1 && env.forward_velocity.wrapping_abs() > 450000 {
            let local = [
                if last == 0 || last == 1 { -1024 } else { 1024 },
                0,
                if last == 0 || last == 2 { 1024 } else { -1024 },
            ];
            let point = env
                .external_basis
                .map_or(local, |m| m.local_to_world(local));
            let world = std::array::from_fn(|i| point[i].wrapping_add(env.position[i]));
            effect(effects.track_damage(if env.forward_velocity > 0 { 2 } else { 3 }, world))?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    struct Fail;
    impl Effects for Fail {
        fn impact(&mut self, _: usize, _: i32) -> Result<(), i32> {
            Err(-9)
        }
        fn track_damage(&mut self, _: i32, _: [i32; 3]) -> Result<(), i32> {
            panic!("unexpected damage")
        }
    }
    #[test]
    fn callback_failure_keeps_prior_flip_but_skips_current_reflection() {
        let mut state = State {
            velocity: [100, 200, 300],
            yaw: 7,
        };
        let mut contacts = [Contact {
            kind: 3,
            corner: 0,
            normal: [4096, 0, 0],
        }; 4];
        contacts[1].kind = 4;
        let env = Environment {
            mode: 0,
            player: true,
            forward_velocity: 150001,
            position: [0; 3],
            external_basis: None,
        };
        assert_eq!(
            state.step(&mut contacts, env, &mut Fail),
            Err(Error::Effect(-9))
        );
        assert_eq!(state.velocity, [-100, 200, -300]);
        assert_eq!(state.yaw, 7);
        contacts[1].kind = 3;
        assert!(state.step(&mut contacts, env, &mut Fail).is_ok());
        assert_eq!(state.velocity, [100, 200, 300]); // Four missing contacts still flip only once.
    }
}
