//! Shared pickup light banks: simulation fade, follow motion and pool retirement.
//! Activation and render projection are separate operations, not part of fade.
use crate::{
    effect_pool::{Pool, Quad, QuadHandle},
    object_query::EntityId,
    vector,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Ray {
    pub angles: [u16; 2],
    pub angular_velocity: [i32; 2],
    pub palette_index: u32,
    pub handle: QuadHandle,
    pub quad: Quad,
}
impl Default for Ray {
    fn default() -> Self {
        Self {
            angles: [0; 2],
            angular_velocity: [0; 2],
            palette_index: 0,
            handle: QuadHandle::RELEASED,
            quad: Quad::ZERO,
        }
    }
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Ring {
    pub quad: Quad,
    pub handle: QuadHandle,
}
impl Default for Ring {
    fn default() -> Self {
        Self {
            quad: Quad::ZERO,
            handle: QuadHandle::RELEASED,
        }
    }
}
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Bank {
    pub active: u8,
    pub projected: u8,
    pub follow: [u8; 2],
    pub target: Option<EntityId>,
    pub life: i32,
    pub frame: u32,
    pub origin: [i32; 3],
    pub position: [i32; 3],
    pub speed: i32,
    pub inner_radius: i32,
    pub outer_radius: i32,
    pub flare_radius: i32,
    pub rays: [Ray; 10],
    pub rings: [Ring; 10],
}
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct State {
    pub banks: [Bank; 2],
}
#[derive(Debug, Clone, Copy)]
pub struct Tuning {
    pub outer_growth: i32,
    pub inner_growth: i32,
    pub flare_growth: i32,
    pub palette: [[u8; 3]; 10],
}
impl Tuning {
    /// Immutable PE palette/rates plus the externally configured flare rate.
    pub fn new(flare_growth: i32) -> Self {
        Self {
            outer_growth: 13631,
            inner_growth: 11796,
            flare_growth,
            palette: [
                [128, 0, 0],
                [128, 128, 44],
                [0, 0, 0],
                [128, 20, 0],
                [128, 58, 0],
                [0, 0, 0],
                [128, 128, 20],
                [128, 58, 0],
                [0, 0, 0],
                [128, 0, 0],
            ],
        }
    }
}
// Store each released handle immediately: later errors must retain that prefix.
fn retire<'a>(
    handles: impl Iterator<Item = &'a mut QuadHandle>,
    pool: &mut Pool,
) -> Result<(), i32> {
    for handle in handles {
        if *handle != QuadHandle::RELEASED {
            pool.release_quad(*handle).map_err(|_| -1)?;
            *handle = QuadHandle::RELEASED;
        }
    }
    Ok(())
}
impl State {
    pub fn fade(
        &mut self,
        target: EntityId,
        position: [i32; 3],
        frame: u32,
        dt: i32,
        tuning: &Tuning,
        pool: &mut Pool,
    ) -> Result<(), i32> {
        for bank in &mut self.banks {
            if bank.target != Some(target) || bank.active == 0 {
                continue;
            }
            // This returns from the entire call, rather than skipping one bank.
            if bank.frame == frame {
                return Ok(());
            }
            if bank.follow.iter().any(|v| *v != 0) {
                let mut v = std::array::from_fn(|i| position[i].wrapping_sub(bank.position[i]));
                vector::normalize(&mut v);
                bank.position = std::array::from_fn(|i| {
                    bank.position[i]
                        .wrapping_add(((v[i].wrapping_mul(bank.speed) >> 12).wrapping_mul(dt)) >> 8)
                });
                bank.speed = bank.speed.wrapping_sub(dt.wrapping_mul(128)).max(0);
            }
            bank.outer_radius = bank
                .outer_radius
                .wrapping_add(tuning.outer_growth.wrapping_mul(dt) >> 8);
            bank.inner_radius = bank
                .inner_radius
                .wrapping_add(tuning.inner_growth.wrapping_mul(dt) >> 8);
            bank.flare_radius = bank
                .flare_radius
                .wrapping_add(tuning.flare_growth.wrapping_mul(dt) >> 8);
            for ray in &mut bank.rays {
                for i in 0..2 {
                    ray.angles[i] = ray.angles[i]
                        .wrapping_add((ray.angular_velocity[i].wrapping_mul(dt) >> 8) as u16);
                }
                let color = tuning.palette.get(ray.palette_index as usize).ok_or(-1)?;
                for (i, c) in color.iter().enumerate() {
                    ray.quad.style[i + 1] = (i32::from(*c).wrapping_mul(bank.life) >> 17) as u8;
                }
                ray.palette_index = ray.palette_index.wrapping_add(1);
                if ray.palette_index == 10 {
                    ray.palette_index = 0;
                }
            }
            if bank.outer_radius > 25600 && bank.inner_radius > 25600 {
                retire(bank.rings.iter_mut().map(|r| &mut r.handle), pool)?;
            }
            bank.life = bank.life.wrapping_sub(dt.wrapping_mul(192));
            if bank.life < 1 {
                retire(bank.rays.iter_mut().map(|r| &mut r.handle), pool)?;
                retire(bank.rings.iter_mut().map(|r| &mut r.handle), pool)?;
                bank.active = 0;
            }
        }
        Ok(())
    }
}
