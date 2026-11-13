//! Eight track-spray particles and resolved surface styles (439EF0).
use crate::{contact::Coefficient, fixed::trunc_shift, math::Matrix, vehicle};

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Particle {
    pub position: [i32; 3],
    pub velocity: [i32; 3],
    pub life: i16,
    pub size: u16,
    pub floor: i16,
    pub rotation: u16,
    pub kind: i8,
    pub side: u8,
}
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct State {
    pub particles: [Particle; 8],
    pub timer: i32,
    pub side: u32,
    pub count: u32,
}
#[derive(Debug, Clone, Copy)]
pub struct Style {
    pub color: [u8; 3],
    pub source_flags: [u8; 2],
    pub blend: u16,
    pub radius: i16,
    pub initial_size: u16,
    pub gain: Coefficient,
    pub spin: u32,
    pub material: u32,
}
#[derive(Debug, Clone, Copy)]
pub struct Materials {
    pub filth: u32,
    pub crap: u32,
    pub splash: u32,
}
#[derive(Debug, Clone)]
pub struct Tuning {
    pub kinds: [i8; 16],
    pub styles: [Style; 6],
}
impl Tuning {
    /// Immutable style data plus the actual environment/material bindings.
    pub fn environment(materials: Materials, swamp: bool, hardware: bool) -> Self {
        let colors = [
            [30, 80, 6],
            [85, 60, 32],
            [110, 115, 75],
            if hardware { [30; 3] } else { [64; 3] },
            [128; 3],
            [60, 90, 128],
        ];
        Self {
            kinds: [-1, 5, 0, 3, 1, -1, 2, 4, -1, 4, 0, 0, 0, 0, 0, 0],
            styles: std::array::from_fn(|i| Style {
                color: colors[i],
                source_flags: [[2, 0], [2, 0], [8, 0], [4, 0], [2, 0], [2, 0]][i],
                blend: if i < 3 {
                    u16::MAX
                } else if i == 3 && !hardware {
                    3
                } else {
                    1
                },
                radius: if i == 3 { 700 } else { 5 },
                initial_size: if i == 3 || (i == 1 && swamp) { 650 } else { 0 },
                gain: Coefficient {
                    multiplier: 1,
                    shift: 9,
                },
                spin: match i {
                    1 if swamp => 80,
                    3 => 40,
                    5 => 4550,
                    _ => 0,
                },
                material: match i {
                    1 => materials.crap,
                    3 => materials.splash,
                    _ => materials.filth,
                },
            }),
        }
    }
}
#[derive(Debug, Clone, Copy)]
pub struct Input {
    pub dt: i32,
    pub track_speed: i32,
    pub velocity: [i32; 3],
    pub basis: Matrix,
    pub materials: [usize; 2],
    pub kinds: [i32; 4],
    pub contacts: [[i32; 3]; 4],
    /// Resolved original view slot, used only for particle type five.
    pub visible: i32,
}
impl Input {
    pub fn after_pose(v: &vehicle::AfterPose<'_>, visible: i32) -> Self {
        Self {
            dt: v.frame.dt,
            track_speed: v.powertrain.track_speed,
            velocity: v.motion.velocity,
            basis: v.motion.poses.reference,
            materials: v.contact.sampling.materials.map(|v| v as usize),
            kinds: v.contact.sampling.primary.points.each_ref().map(|p| p.kind),
            contacts: v.motion.contacts.map(|p| p.world),
            visible,
        }
    }
}
impl State {
    pub fn step(&mut self, input: &Input, tuning: &Tuning) -> Result<(), i32> {
        self.count = 0;
        let mut free = None;
        for (i, p) in self.particles.iter_mut().enumerate() {
            if p.life < 1 {
                free = Some(i);
                continue;
            }
            self.count = self.count.wrapping_add(1);
            let style = tuning.styles.get(p.kind as usize).ok_or(-1)?;
            p.velocity[0] = trunc_shift(p.velocity[0].wrapping_mul(940), 10);
            p.velocity[1] = p.velocity[1].wrapping_sub(100);
            p.velocity[2] = trunc_shift(p.velocity[2].wrapping_mul(940), 10);
            for i in 0..3 {
                p.position[i] = p.position[i]
                    .wrapping_add(trunc_shift(input.dt.wrapping_mul(p.velocity[i]), 8));
            }
            p.life = p.life.wrapping_sub(input.dt.wrapping_mul(6) as i16);
            p.size = p.size.wrapping_add(input.dt.wrapping_mul(2) as u16);
            p.position[1] = p.position[1].max(i32::from(p.floor));
            if style.spin != 0 {
                let angle = input.dt.wrapping_mul(style.spin as i16 as i32);
                p.rotation = p.rotation.wrapping_add(if p.side != 0 {
                    angle.wrapping_neg()
                } else {
                    angle
                } as u16);
            }
        }
        self.timer = self.timer.wrapping_add(input.dt);
        let Some(free) = free.filter(|_| self.timer >= 40) else {
            return Ok(());
        };
        self.side = 1_u32.wrapping_sub(self.side);
        let side = self.side as usize;
        let material = *input.materials.get(side).ok_or(-1)?;
        let kind = *tuning.kinds.get(material).ok_or(-1)?;
        let p = &mut self.particles[free];
        p.kind = kind;
        if kind == -1 || (kind == 5 && input.visible != 1) {
            return Ok(());
        }
        let magnitude = input.track_speed.wrapping_abs().wrapping_sub(50000);
        if magnitude < 1 || !(0..2).any(|i| matches!(input.kinds[side * 2 + i] as i16, 0 | 1)) {
            return Ok(());
        }
        let style = tuning.styles.get(kind as usize).ok_or(-1)?;
        // The source offsets the stale position by radius, then overwrites all
        // three coordinates before any observable query/callback. Omit that work.
        p.position = input.contacts[side * 2];
        p.side = side as u8;
        p.velocity =
            input
                .basis
                .local_to_world([0, 0, trunc_shift(input.track_speed, 6).wrapping_neg()]);
        p.velocity[0] = p.velocity[0].wrapping_add(trunc_shift(input.velocity[0], 6));
        let gain = style.gain.apply_truncated(magnitude);
        p.velocity[1] = p.velocity[1].wrapping_add(gain);
        p.life = 2048;
        p.velocity[2] = p.velocity[2].wrapping_add(trunc_shift(input.velocity[2], 6));
        p.floor = p.position[1] as i16;
        p.size = style.initial_size.wrapping_add(gain as u16);
        self.timer = 0;
        Ok(())
    }
}
