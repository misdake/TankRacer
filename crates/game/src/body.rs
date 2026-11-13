//! Articulation, player lean and the four-point contact hull.
use crate::{
    fixed::{damp, decay, rate_step, trunc_shift},
    math::Matrix,
};
use tankracer_assets::config::Damping;

#[derive(Debug, Clone, Copy)]
pub struct Tuning {
    pub recoil_decay: u8,
    pub recoil_step: i16,
    pub pitch_damping: Damping,
    pub lean_decay: u8,
}
impl Default for Tuning {
    fn default() -> Self {
        Self {
            recoil_decay: 15,
            recoil_step: 1,
            pitch_damping: Damping {
                step: 3,
                fraction: 1,
                shift: 14,
            },
            lean_decay: 6,
        }
    }
}
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Articulation {
    pub recoil: i32,
    pub recoil_rate: i32,
    pub pitch: i32,
    pub pitch_rate: i32,
}
pub struct GunMesh<'a> {
    pub vertices: &'a mut [[i16; 3]],
    pub indices: [u8; 4],
    pub original_z: [i16; 4],
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    InvalidVertex(usize),
}
impl Articulation {
    pub fn step(
        &mut self,
        dt: i32,
        tuning: &Tuning,
        mesh: Option<GunMesh<'_>>,
    ) -> Result<(), Error> {
        let Some(mesh) = mesh else {
            return Ok(());
        };
        let force = trunc_shift(
            self.recoil.wrapping_mul(self.recoil),
            if self.recoil < 1 { 16 } else { 14 },
        );
        self.recoil_rate = if self.recoil < 1 {
            self.recoil_rate.wrapping_add(dt.wrapping_mul(force))
        } else {
            self.recoil_rate.wrapping_sub(dt.wrapping_mul(force))
        };
        self.recoil_rate = decay(self.recoil_rate, tuning.recoil_decay, dt);
        let mut recoil = self.recoil.wrapping_add(self.recoil_rate);
        if recoil < -500 {
            recoil = -500;
            self.recoil_rate = 0;
        } else if recoil > 200 {
            recoil = 200;
            self.recoil_rate = 0;
        }
        self.recoil = rate_step(recoil, i32::from(tuning.recoil_step), dt);
        let force = trunc_shift(
            self.pitch.wrapping_mul(self.pitch),
            if self.pitch < 1 { 15 } else { 14 },
        );
        self.pitch_rate = if self.pitch < 1 {
            self.pitch_rate.wrapping_add(dt.wrapping_mul(force))
        } else {
            self.pitch_rate.wrapping_sub(dt.wrapping_mul(force))
        };
        self.pitch_rate = damp(self.pitch_rate, tuning.pitch_damping, dt);
        self.pitch = self.pitch.wrapping_add(self.pitch_rate);
        if self.pitch < -1456 {
            self.pitch = -1456;
            self.pitch_rate = 0;
        } else if self.pitch > 364 {
            self.pitch = 364;
            self.pitch_rate = 0;
        }
        for (index, z) in mesh.indices.into_iter().zip(mesh.original_z) {
            let vertex = mesh
                .vertices
                .get_mut(index as usize)
                .ok_or(Error::InvalidVertex(index as usize))?;
            vertex[2] = i32::from(z).wrapping_sub(self.recoil >> 3) as i16;
        }
        Ok(())
    }
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Spring {
    pub position: i32,
    pub rate: i32,
}
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Lean {
    pub lateral: Spring,
    pub longitudinal: Spring,
}
impl Lean {
    pub fn step(&mut self, acceleration: [i32; 2], dt: i32, tuning: &Tuning) {
        let delta = [
            trunc_shift(acceleration[0].wrapping_mul(-60), 10),
            trunc_shift(acceleration[1].wrapping_mul(60), 10),
        ];
        for (spring, delta) in [&mut self.lateral, &mut self.longitudinal]
            .into_iter()
            .zip(delta)
        {
            let mut rate = spring.rate.wrapping_add(delta);
            let position = spring.position.clamp(-2730, 2730);
            let force = trunc_shift(position.wrapping_mul(position), 15);
            rate = if position < 1 {
                rate.wrapping_add(dt.wrapping_mul(force))
            } else {
                rate.wrapping_sub(dt.wrapping_mul(force))
            };
            spring.rate = decay(rate, tuning.lean_decay, dt);
            spring.position = position.wrapping_add(spring.rate);
        }
    }
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Contact {
    pub world: [i32; 3],
    pub gap: i32,
}
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Hull {
    pub position: [i32; 3],
    pub matrix: Matrix,
    pub vertices: [[i16; 3]; 4],
    pub radius: u16,
}
impl Hull {
    pub fn refresh(&mut self, current: [i32; 3], previous: [i32; 3], contacts: [Contact; 4]) {
        self.position = current;
        self.matrix = Matrix::IDENTITY;
        let mut maximum = 0;
        for (vertex, index) in self.vertices.iter_mut().zip([1, 3, 2, 0]) {
            let contact = contacts[index];
            let mut distance = 0_i32;
            for j in 0..3 {
                // (sample + (current-previous)) - current is exactly sample-previous
                // in the original wrapping ring; avoid recomputing the canceled term.
                let mut value = contact.world[j].wrapping_sub(previous[j]);
                if j == 1 {
                    value = value.wrapping_sub(contact.gap);
                }
                distance = distance.wrapping_add(value.wrapping_abs());
                vertex[j] = trunc_shift(value, 3) as i16;
            }
            maximum = maximum.max(distance);
        }
        let radius = (maximum >> 4) as u16;
        self.radius = radius.wrapping_add(radius >> 5);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn absent_gun_skips_articulation_and_duplicate_vertices_keep_last_write() {
        let initial = Articulation {
            recoil: 70,
            recoil_rate: 13,
            pitch: -99,
            pitch_rate: 123,
        };
        let mut state = initial.clone();
        state.step(17, &Tuning::default(), None).unwrap();
        assert_eq!(state, initial);
        let mut state = Articulation::default();
        let mut vertices = [[1, 2, 3]; 2];
        state
            .step(
                0,
                &Tuning::default(),
                Some(GunMesh {
                    vertices: &mut vertices,
                    indices: [0, 1, 0, 1],
                    original_z: [80, 160, 240, 320],
                }),
            )
            .unwrap();
        assert_eq!(vertices, [[1, 2, 240], [1, 2, 320]]);
    }
    #[test]
    fn lean_clamps_before_integration_and_can_overshoot_afterwards() {
        let mut state = Lean {
            lateral: Spring {
                position: 10000,
                rate: 10000,
            },
            longitudinal: Spring::default(),
        };
        state.step([0; 2], 0, &Tuning::default());
        assert_eq!(state.lateral.position, 12730);
    }
    #[test]
    fn hull_cancellation_preserves_wrap_gap_and_vertex_quantization() {
        let contacts = [Contact {
            world: [80, 40, -160],
            gap: 24,
        }; 4];
        let mut first = Hull::default();
        first.refresh([i32::MIN; 3], [0; 3], contacts);
        let mut second = Hull::default();
        second.refresh([i32::MAX; 3], [0; 3], contacts);
        assert_eq!(first.vertices, [[10, 2, -20]; 4]);
        assert_eq!(first.radius, 16);
        assert_eq!(first.vertices, second.vertices);
        assert_eq!(first.radius, second.radius);
    }
}
