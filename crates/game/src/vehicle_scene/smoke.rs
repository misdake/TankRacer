//! Damage/traction smoke and the real severe-slip impact emission (439550).
use crate::{
    fixed::{damp, trunc_shift},
    geometry, impact_particles,
    pose::Pose,
    random, vehicle,
};
use tankracer_assets::config::Damping;

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Particle {
    pub position: [i32; 3],
    pub velocity: [i32; 3],
    pub life: i16,
    pub size: u16,
    pub rotation: u16,
    pub direction: u8,
    pub expanding: u8,
}
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct State {
    pub particles: [Particle; 15],
    pub timer: i32,
    pub side: u32,
    pub force: i32,
    pub count: u32,
    /// Turret/body damage severity and the selected authored smoke socket.
    pub damage: [u8; 2],
    pub sockets: [u8; 2],
    pub damage_timers: [i32; 2],
}
#[derive(Debug, Clone)]
pub struct Tuning {
    pub sockets: [[[i32; 3]; 6]; 2],
}
impl Default for Tuning {
    fn default() -> Self {
        Self {
            sockets: [
                [
                    [512, 0, 0],
                    [-512, 0, 0],
                    [0, 1024, -1024],
                    [0, 0, -1024],
                    [0, 0, 1024],
                    [0, 0, -1024],
                ],
                [
                    [1024, 512, 0],
                    [-1024, 512, 0],
                    [0, 512, 0],
                    [0, -512, 0],
                    [0, 512, 1024],
                    [0, 512, -1024],
                ],
            ],
        }
    }
}
#[derive(Debug, Clone, Copy)]
pub struct Input {
    pub dt: i32,
    pub player: bool,
    pub special: bool,
    pub velocity: [i32; 3],
    pub lateral_velocity: i32,
    pub track_slip: i32,
    pub penetrating_force: i32,
    pub materials: [i32; 2],
    pub contacts: [[i32; 3]; 4],
    pub corners: [u32; 4],
    pub poses: [Pose; 2],
}
impl Input {
    pub fn after_pose(v: &vehicle::AfterPose<'_>, special: bool) -> Self {
        Self {
            dt: v.frame.dt,
            player: v.player,
            special,
            velocity: v.motion.velocity,
            lateral_velocity: v.motion.rates.lateral_velocity,
            track_slip: v.motion.rates.track_slip,
            penetrating_force: v.motion.rates.penetrating_force,
            materials: v.contact.sampling.materials,
            contacts: v.motion.contacts.map(|p| p.world),
            corners: v.contact.response.points.map(|p| p.corner),
            poses: [v.motion.poses.turret, v.motion.poses.body],
        }
    }
}
fn velocity(input: &Input, rising: bool) -> [i32; 3] {
    std::array::from_fn(|i| {
        trunc_shift(input.velocity[i], 6).wrapping_add(if rising && i == 1 { 800 } else { 0 })
    })
}
fn ordinary(p: &mut Particle, input: &Input, side: usize, life: i32, seed: &mut u32) {
    p.velocity = velocity(input, false);
    p.life = life as i16;
    p.rotation = (random::next(seed) >> 4) as u16;
    if p.life > 2048 {
        p.life = 2048;
    }
    p.size = 8000;
    p.position = input.contacts[side * 2];
    p.direction = u8::from(side == 0);
    p.expanding = 1;
}
impl State {
    /// Collision state/pool/RNG are the same owners used earlier in the tick.
    /// Severe smoke emission calls the qualified impact generator synchronously.
    pub fn step(
        &mut self,
        input: &Input,
        tuning: &Tuning,
        impacts: &mut impact_particles::State,
        impact_vehicle: &mut impact_particles::Vehicle,
        context: &mut impact_particles::Context<'_>,
    ) -> Result<(), i32> {
        self.force = self.force.max(input.penetrating_force);
        if self.force != 0 {
            self.force = damp(
                self.force,
                Damping {
                    step: 800,
                    fraction: 1,
                    shift: 10,
                },
                input.dt,
            );
        }
        self.count = 0;
        let mut free = None;
        let mut damaged = [None; 2];
        for (i, p) in self.particles.iter_mut().enumerate() {
            if p.life < 1 {
                if self.damage[0] != 0 && damaged[0].is_none() {
                    damaged[0] = Some(i);
                } else if self.damage[1] != 0 && damaged[1].is_none() {
                    damaged[1] = Some(i);
                } else if free.is_none() {
                    free = Some(i);
                }
                continue;
            }
            self.count = self.count.wrapping_add(1);
            p.velocity[0] = trunc_shift(p.velocity[0].wrapping_mul(940), 10);
            p.velocity[2] = trunc_shift(p.velocity[2].wrapping_mul(940), 10);
            p.velocity[1] = p.velocity[1].wrapping_add(20);
            for j in 0..3 {
                p.position[j] = p.position[j]
                    .wrapping_add(trunc_shift(p.velocity[j].wrapping_mul(input.dt), 8));
            }
            p.life = p.life.wrapping_sub(input.dt.wrapping_mul(2) as i16);
            if p.expanding != 0 {
                p.size = p.size.wrapping_add(input.dt.wrapping_mul(15) as u16);
            }
            let angle = input.dt.wrapping_mul(40);
            p.rotation = p.rotation.wrapping_add(if p.direction != 0 {
                angle.wrapping_neg()
            } else {
                angle
            } as u16);
        }
        self.timer = self.timer.wrapping_add(input.dt);
        self.damage_timers = self.damage_timers.map(|t| t.wrapping_add(input.dt));
        for (body, slot) in damaged.into_iter().enumerate() {
            let Some(slot) = slot else {
                continue;
            };
            if self.damage_timers[body] >= if self.damage[body] > 8 { 150 } else { 300 } {
                self.damage_timers[body] = 0;
                let p = &mut self.particles[slot];
                p.velocity = velocity(input, true);
                p.rotation = (random::next(context.random) >> 4) as u16;
                p.life = 2048;
                p.size = 5600;
                let local = *tuning.sockets[body]
                    .get(self.sockets[body] as usize)
                    .ok_or(-1)?;
                let pose = input.poses[body];
                let point = pose.matrix.local_to_world(local);
                p.position = std::array::from_fn(|i| point[i].wrapping_add(pose.position[i]));
                p.expanding = 0;
            } else if free.is_none() {
                free = Some(slot);
            }
        }
        let Some(free) = free.filter(|_| self.timer >= 70 && !(input.special && input.player))
        else {
            return Ok(());
        };
        let value = input.lateral_velocity;
        if (value < -35000 && input.materials[0] == 1) || (value > 35000 && input.materials[1] == 1)
        {
            let side = usize::from(value > 35000);
            ordinary(
                &mut self.particles[free],
                input,
                side,
                trunc_shift(value.wrapping_abs().wrapping_sub(35000), 5),
                context.random,
            );
            self.timer = 0;
            if !input.player || (side == 1 && value < 130001) || (side == 0 && value > -130001) {
                return Ok(());
            }
            if geometry::approximate_distance(input.velocity) < 250001 {
                return Ok(());
            }
            let contact = side * 2 + usize::from(impact_vehicle.forward_velocity >= 0);
            impacts
                .emit(
                    impact_vehicle,
                    input.corners[contact],
                    if side == 0 { 1 } else { 2 },
                    context,
                )
                .map(|_| ())
                .map_err(impact_particles::Error::status)
        } else {
            let magnitude = input.track_slip.wrapping_abs();
            if self.force < 4000001 && magnitude < 1 {
                return Ok(());
            }
            self.side = 1_u32.wrapping_sub(self.side);
            let side = self.side as usize;
            if *input.materials.get(side).ok_or(-1)? != 1 {
                return Ok(());
            }
            let life = if magnitude < 1 {
                trunc_shift(self.force.wrapping_sub(4000000), 11)
            } else {
                magnitude >> 4
            };
            ordinary(&mut self.particles[free], input, side, life, context.random);
            self.timer = 0;
            Ok(())
        }
    }
}
