//! Real impact ring generation and animation (42A5C0 / 42AAC0).
use crate::{
    effect_pool::{self, LineHandle, Pool},
    fixed::trunc_shift,
    math::{self, Matrix},
    object_query::EntityId,
    random,
};

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Particle {
    pub distance: [i16; 2],
    pub speed: [i32; 2],
    pub pitch: [i32; 2],
    pub yaw: [i32; 2],
    pub line: LineHandle,
    pub colors: [[u8; 3]; 2],
}
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Batch {
    pub particles: [Particle; 10],
    pub kind: i32,
    pub corner: u32,
    pub owner: Option<EntityId>,
    pub active: bool,
    pub age: i32,
    pub frame: u32,
}
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct State {
    pub batches: [Batch; 4],
    pub next: u32,
}
#[derive(Debug, Clone, Copy)]
pub struct Tuning {
    pub forward_offset: [i32; 3],
    pub reverse_offset: [i32; 3],
}
impl Default for Tuning {
    fn default() -> Self {
        // Original PE .data at 531868 / 531878, SHA256
        // e8c4f12dd3caac55c4b1b29536475b58222dc44de82e008cae0e4a9c7fd9b9bb.
        // Explicit immutable inputs; no captured world memory is required.
        Self {
            forward_offset: [0, 0, 2000],
            reverse_offset: [0, 0, -2000],
        }
    }
}
#[derive(Debug, Clone, Copy)]
pub struct Vehicle {
    pub entity: EntityId,
    pub contacts: [[i32; 3]; 4],
    pub forward_velocity: i32,
    pub external_basis: Option<Matrix>,
    pub cooldown: u32,
}
pub struct Context<'a> {
    pub frame: u32,
    pub random: &'a mut u32,
    pub pool: &'a mut Pool,
    pub tuning: Tuning,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    InvalidRingOrKind,
    InvalidCorner(u32),
    MissingBasis,
    Arithmetic(math::Error),
    Pool(effect_pool::Error),
}
impl Error {
    /// Synchronous collision-service status, retaining the reference's distinct
    /// angle, missing-matrix and invalid-resource failure paths.
    pub fn status(self) -> i32 {
        match self {
            Self::Arithmetic(_) => -2,
            Self::Pool(_) => -1,
            Self::InvalidRingOrKind | Self::InvalidCorner(_) | Self::MissingBasis => -4,
        }
    }
}
struct Angles {
    sy: i32,
    cy: i32,
    sp: i32,
    cp: i32,
}
impl Angles {
    fn new(yaw: i32, pitch: i32) -> Self {
        Self {
            sy: math::sine(yaw as u32),
            cy: math::cosine(yaw as u32),
            sp: math::sine(pitch as u32),
            cp: math::cosine(pitch as u32),
        }
    }
    fn endpoint(&self, radius: i32, origin: [i32; 3], offset: [i32; 3]) -> [i32; 3] {
        // The input is always (0,0,radius): eliminate zero products while keeping
        // the original per-stage wrapping/truncation. Share lookups for both ends.
        let x = trunc_shift(radius.wrapping_mul(self.sy).wrapping_neg(), 12);
        let z = trunc_shift(radius.wrapping_mul(self.cy), 12);
        let local = [
            x,
            trunc_shift(z.wrapping_mul(self.sp), 12),
            trunc_shift(z.wrapping_mul(self.cp), 12),
        ];
        std::array::from_fn(|i| local[i].wrapping_add(origin[i]).wrapping_add(offset[i]))
    }
}
fn offset(vehicle: &Vehicle, tuning: Tuning) -> [i32; 3] {
    let offset = if vehicle.forward_velocity < 0 {
        tuning.reverse_offset
    } else {
        tuning.forward_offset
    };
    vehicle
        .external_basis
        .map_or([0; 3], |m| m.local_to_world(offset))
}
impl State {
    pub fn emit(
        &mut self,
        vehicle: &mut Vehicle,
        corner: u32,
        kind: i32,
        context: &mut Context<'_>,
    ) -> Result<bool, Error> {
        if kind != 3 && kind != 4 && vehicle.cooldown < 500 {
            return Ok(false);
        }
        if self.next >= 4 || !(0..=6).contains(&kind) {
            return Err(Error::InvalidRingOrKind);
        }
        let batch = &mut self.batches[self.next as usize];
        if batch.active {
            for p in &batch.particles {
                if p.line != LineHandle::RELEASED {
                    context.pool.release(p.line).map_err(Error::Pool)?;
                }
            }
        }
        let (angle, spread) = if kind == 0 {
            (0, 65520)
        } else {
            let first = if kind == 1 || kind == 3 || kind == 5 {
                0
            } else {
                2
            };
            let mut x = vehicle.contacts[first + 1][0].wrapping_sub(vehicle.contacts[first][0]);
            let mut z = vehicle.contacts[first + 1][2].wrapping_sub(vehicle.contacts[first][2]);
            if vehicle.forward_velocity < 0 {
                x = x.wrapping_neg();
                z = z.wrapping_neg();
            }
            let angle = math::angle(x, z).map_err(Error::Arithmetic)?.wrapping_neg();
            if kind == 3 || kind == 4 {
                vehicle.cooldown = 0;
            }
            (angle, if kind == 5 || kind == 6 { 9100 } else { 5460 })
        };
        let offset = if matches!(kind, 1 | 2 | 5 | 6) {
            offset(vehicle, context.tuning)
        } else {
            [0; 3]
        };
        let origin = *vehicle
            .contacts
            .get(corner as usize)
            .ok_or(Error::InvalidCorner(corner))?;
        for p in &mut batch.particles {
            let r0 = (random::next(context.random) >> 4) & 65535;
            let r1 = (random::next(context.random) >> 4) & 65535;
            let magnitude = ((r0 % 55 + 10) * 256) as i32;
            let pitch = (r0 % spread) as i32 - (spread as i32 >> 1);
            let yaw = ((r1 % spread) as i32)
                .wrapping_add(angle)
                .wrapping_sub(spread as i32 >> 1);
            p.distance = [1000, 1800];
            p.speed = [magnitude; 2];
            p.pitch = [pitch; 2];
            p.yaw = [yaw; 2];
            p.colors = [[0; 3], [255, (r0 % 126).wrapping_sub(126) as u8, 0]];
            let angles = Angles::new(yaw, pitch);
            let points = [
                angles.endpoint(1000, origin, offset),
                angles.endpoint(1800, origin, offset),
            ];
            p.line = context
                .pool
                .allocate(points, [[0, 0, 0, 1], [255, p.colors[1][1], 0, 0]]);
        }
        batch.owner = Some(vehicle.entity);
        batch.active = true;
        batch.corner = corner;
        batch.frame = context.frame;
        batch.age = 0;
        batch.kind = kind;
        self.next = if self.next == 3 { 0 } else { self.next + 1 };
        Ok(true)
    }
    pub fn tick(
        &mut self,
        vehicle: &mut Vehicle,
        dt: i32,
        context: &mut Context<'_>,
    ) -> Result<(), Error> {
        vehicle.cooldown = vehicle.cooldown.wrapping_add(dt as u32);
        for batch in &mut self.batches {
            if batch.owner != Some(vehicle.entity) || !batch.active {
                continue;
            }
            if batch.frame == context.frame {
                return Ok(());
            }
            let origin = *vehicle
                .contacts
                .get(batch.corner as usize)
                .ok_or(Error::InvalidCorner(batch.corner))?;
            let mut transformed_offset = None;
            for p in &mut batch.particles {
                p.distance[1] =
                    p.distance[1].wrapping_add((p.speed[1].wrapping_mul(dt) >> 8) as i16);
                p.distance[0] =
                    p.distance[0].wrapping_add((p.speed[0].wrapping_mul(dt) >> 8) as i16);
                let near = Angles::new(p.yaw[0], p.pitch[0]);
                let far = if p.yaw[0] == p.yaw[1] && p.pitch[0] == p.pitch[1] {
                    &near
                } else {
                    &Angles::new(p.yaw[1], p.pitch[1])
                };
                let mut points = [
                    near.endpoint(i32::from(p.distance[0]), origin, [0; 3]),
                    far.endpoint(i32::from(p.distance[1]), origin, [0; 3]),
                ];
                if matches!(batch.kind, 1 | 2 | 5 | 6) {
                    if vehicle.external_basis.is_none() {
                        return Err(Error::MissingBasis);
                    }
                    // Resolve once per batch, after the first distance writes:
                    // moving validation earlier would change the error prefix.
                    let offset =
                        *transformed_offset.get_or_insert_with(|| offset(vehicle, context.tuning));
                    for point in &mut points {
                        for i in 0..3 {
                            point[i] = point[i].wrapping_add(offset[i]);
                        }
                    }
                }
                let colors = [
                    [p.colors[0][0], p.colors[0][1], p.colors[0][2], 1],
                    [p.colors[1][0], p.colors[1][1], p.colors[1][2], 0],
                ];
                context
                    .pool
                    .update(p.line, points, colors)
                    .map_err(Error::Pool)?;
            }
            batch.age = batch.age.wrapping_add(dt);
            if batch.age > 60 {
                for p in &mut batch.particles {
                    if p.line != LineHandle::RELEASED {
                        context.pool.release(p.line).map_err(Error::Pool)?;
                        p.line = LineHandle::RELEASED;
                    }
                }
                batch.active = false;
                batch.age = 0;
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn vehicle() -> Vehicle {
        Vehicle {
            entity: EntityId(1),
            contacts: [[0, 0, 100], [0, 0, -100], [100, 0, 100], [100, 0, -100]],
            forward_velocity: 1,
            external_basis: Some(Matrix::IDENTITY),
            cooldown: 500,
        }
    }
    fn context<'a>(pool: &'a mut Pool, random: &'a mut u32) -> Context<'a> {
        Context {
            pool,
            random,
            frame: 0,
            tuning: Tuning {
                forward_offset: [0; 3],
                reverse_offset: [0; 3],
            },
        }
    }
    #[test]
    fn exhaustion_still_consumes_twenty_draws_and_commits_the_batch() {
        let mut pool = Pool::default();
        for _ in 0..100 {
            pool.allocate([[0; 3]; 2], [[0; 4]; 2]);
        }
        let mut seed = 7;
        let mut expected = seed;
        for _ in 0..20 {
            random::next(&mut expected);
        }
        let mut state = State::default();
        let mut vehicle = vehicle();
        assert!(
            state
                .emit(&mut vehicle, 2, 3, &mut context(&mut pool, &mut seed))
                .unwrap()
        );
        assert_eq!(seed, expected);
        assert_eq!(vehicle.cooldown, 0);
        assert!(state.batches[0].active);
        assert!(
            state.batches[0]
                .particles
                .iter()
                .all(|p| p.line == LineHandle::EXHAUSTED)
        );
        assert_eq!(pool.line_count, 100);
    }
    #[test]
    fn same_frame_returns_after_earlier_batches_but_before_later_batches() {
        let mut pool = Pool::default();
        let mut seed = 7;
        let mut context = context(&mut pool, &mut seed);
        let mut state = State::default();
        let mut vehicle = vehicle();
        for frame in 10..14 {
            context.frame = frame;
            state.emit(&mut vehicle, 0, 3, &mut context).unwrap();
        }
        let later = state.batches[2..].to_vec();
        context.frame = 11;
        state.tick(&mut vehicle, 16, &mut context).unwrap();
        assert_eq!(vehicle.cooldown, 16);
        assert_eq!(state.batches[0].age, 16);
        assert_ne!(state.batches[0].particles[0].distance, [1000, 1800]);
        assert_eq!(state.batches[1].age, 0);
        assert_eq!(&state.batches[2..], later);
    }
    #[test]
    fn missing_basis_preserves_first_distance_writes_and_the_old_line() {
        let mut pool = Pool::default();
        let mut seed = 7;
        let mut context = context(&mut pool, &mut seed);
        let mut state = State::default();
        let mut vehicle = vehicle();
        state.emit(&mut vehicle, 0, 1, &mut context).unwrap();
        let line = context.pool.lines[0];
        let particles = state.batches[0].particles;
        context.frame = 1;
        vehicle.external_basis = None;
        assert_eq!(
            state.tick(&mut vehicle, 16, &mut context),
            Err(Error::MissingBasis)
        );
        assert_eq!(vehicle.cooldown, 516);
        assert_ne!(
            state.batches[0].particles[0].distance,
            particles[0].distance
        );
        assert_eq!(state.batches[0].particles[1..], particles[1..]);
        assert_eq!(context.pool.lines[0], line);
        assert_eq!(state.batches[0].age, 0);
    }
    #[test]
    fn failed_reuse_releases_old_lines_before_angle_validation() {
        let mut pool = Pool::default();
        let mut seed = 7;
        let mut context = context(&mut pool, &mut seed);
        let mut state = State::default();
        let mut vehicle = vehicle();
        state.emit(&mut vehicle, 0, 0, &mut context).unwrap();
        let batch = state.batches[0];
        let seed = *context.random;
        state.next = 0;
        vehicle.contacts[1][0] = i32::MIN;
        assert!(matches!(
            state.emit(&mut vehicle, 0, 3, &mut context),
            Err(Error::Arithmetic(_))
        ));
        assert_eq!(context.pool.line_count, 0);
        assert_eq!(state.batches[0], batch);
        assert_eq!(*context.random, seed);
        assert_eq!(vehicle.cooldown, 500);
    }
    #[test]
    fn retirement_is_strictly_after_sixty_and_marks_every_handle() {
        let mut pool = Pool::default();
        let mut seed = 7;
        let mut context = context(&mut pool, &mut seed);
        let mut state = State::default();
        let mut vehicle = vehicle();
        state.emit(&mut vehicle, 0, 3, &mut context).unwrap();
        context.frame = 1;
        state.tick(&mut vehicle, 60, &mut context).unwrap();
        assert!(state.batches[0].active);
        assert_eq!(context.pool.line_count, 10);
        state.tick(&mut vehicle, 1, &mut context).unwrap();
        assert!(!state.batches[0].active);
        assert_eq!(state.batches[0].age, 0);
        assert_eq!(context.pool.line_count, 0);
        assert!(
            state.batches[0]
                .particles
                .iter()
                .all(|p| p.line == LineHandle::RELEASED)
        );
    }
}
