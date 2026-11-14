//! Original checkpoint crossing, route score and nearest-point queries.
use super::{Point, Routes, Work, obstacle};
use crate::{
    entities::Entities,
    geometry,
    object_query::{Collider, EntityId, ObstacleId},
    obstacle_collision::World,
    scene_entity::Handle,
    symbols::Binding,
};
pub struct View<'a> {
    pub routes: &'a Routes,
    pub world: &'a World,
    pub entities: &'a Entities<Handle>,
}
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Checkpoint {
    pub next: Option<EntityId>,
    pub finishes_lap: bool,
}
fn next(point: &Point, side: usize) -> Result<Option<EntityId>, i32> {
    match point.next.get(side).ok_or(-4)? {
        Binding::Direct(id) => Ok(*id),
        Binding::Named(_) => Err(-5),
    }
}
impl View<'_> {
    fn point(&self, entity: EntityId) -> Result<(ObstacleId, &Point), i32> {
        let id = obstacle(self.entities, entity)?;
        Ok((id, self.routes.point(id)?))
    }
    fn collider(&self, id: ObstacleId) -> Result<&Collider, i32> {
        self.world.colliders.get(id.0).ok_or(-4)
    }
    pub fn checkpoint(
        &self,
        position: [i32; 3],
        current: EntityId,
        side: usize,
    ) -> Result<Checkpoint, i32> {
        let (_, p) = self.point(current)?;
        let Some(target) = next(p, side)? else {
            return Ok(Checkpoint::default());
        };
        let (id, p) = self.point(target)?;
        let c = self.collider(id)?;
        let delta =
            std::array::from_fn::<_, 3, _>(|i| position[i].wrapping_sub(c.pose.position[i]));
        if delta.iter().any(|d| d.wrapping_abs() >= 51200) {
            return Ok(Checkpoint::default());
        }
        let local = if let Some(matrix) = c.external_basis {
            std::array::from_fn(|i| {
                (0..3).fold(0_i32, |sum, j| {
                    sum.wrapping_add(delta[j].wrapping_mul(matrix.0[i * 3 + j]))
                }) >> 12
            })
        } else {
            delta
        };
        Ok(
            if local[2] > 0 && local[0].wrapping_abs() < (p.width >> 1) {
                Checkpoint {
                    next: Some(target),
                    finishes_lap: p.lap != 0,
                }
            } else {
                Checkpoint::default()
            },
        )
    }
    fn visit(
        &self,
        position: [i32; 2],
        limit: u32,
        mut visit: impl FnMut(ObstacleId) -> Result<(), i32>,
    ) -> Result<(), i32> {
        let mut work = Work::new(limit)?;
        let grid = std::array::from_fn::<_, 2, _>(|i| {
            position[i]
                .wrapping_sub(self.routes.origin[i])
                .wrapping_shr(self.routes.shifts[i])
        });
        let mut row = grid[1].wrapping_sub(self.routes.radius[1]);
        let row_end = grid[1].wrapping_add(self.routes.radius[1]);
        while row <= row_end {
            work.step()?;
            if (0..8).contains(&row) {
                let mut col = grid[0].wrapping_sub(self.routes.radius[0]);
                let col_end = grid[0].wrapping_add(self.routes.radius[0]);
                while col <= col_end {
                    work.step()?;
                    if (0..8).contains(&col) {
                        for &id in &self.routes.cells[(col + row * 8) as usize] {
                            work.step()?;
                            visit(id)?;
                        }
                    }
                    col = col.wrapping_add(1);
                }
            }
            row = row.wrapping_add(1);
        }
        Ok(())
    }
    pub fn progress(
        &self,
        position: [i32; 2],
        current: EntityId,
        lap: i32,
        limit: u32,
    ) -> Result<i32, i32> {
        let (_, current) = self.point(current)?;
        let mut best = None;
        let mut distance = 1024000;
        self.visit(position, limit, |id| {
            let p = self.routes.point(id)?;
            if p.progress == -1 {
                return Ok(());
            }
            let c = self.collider(id)?;
            let [x, z] = [
                position[0].wrapping_sub(c.pose.position[0]),
                position[1].wrapping_sub(c.pose.position[2]),
            ];
            if x.wrapping_abs() >= 153600 || z.wrapping_abs() >= 153600 {
                return Ok(());
            }
            let m = c.external_basis.ok_or(-4)?.0;
            if m[8].wrapping_mul(z).wrapping_add(m[6].wrapping_mul(x)) >= 0 {
                return Ok(());
            }
            let width = if p.hairpin != 0 && p.progress.wrapping_sub(current.progress) != 1 {
                x.wrapping_mul(m[0]).wrapping_add(m[2].wrapping_mul(z))
            } else {
                0
            };
            if width != 0 && width.wrapping_abs() >= (p.width >> 1) {
                return Ok(());
            }
            let [ax, az] = [x.wrapping_abs(), z.wrapping_abs()];
            let d = az.wrapping_sub(ax.min(az) >> 1).wrapping_add(ax);
            if d < distance {
                best = Some(id);
                distance = d;
            }
            Ok(())
        })?;
        let Some(id) = best else {
            return Ok(-1);
        };
        let p = self.routes.point(id)?;
        let entity = self.collider(id)?.entity;
        let score = if p.lap != 0 {
            let score = lap
                .wrapping_add(1)
                .wrapping_mul(10240000)
                .wrapping_sub(distance);
            if next(current, 0)? != Some(entity) {
                score.wrapping_sub(10240000)
            } else {
                score
            }
        } else {
            let score = p
                .progress
                .wrapping_add(lap.wrapping_mul(50))
                .wrapping_mul(204800)
                .wrapping_sub(distance);
            if current.progress < p.progress.wrapping_sub(2) {
                score.wrapping_sub(10240000)
            } else {
                score
            }
        };
        Ok(score)
    }
    pub fn nearest(&self, position: [i32; 3], limit: u32) -> Result<Option<EntityId>, i32> {
        let mut best = None;
        let mut distance = 0xfa000_u32;
        self.visit([position[0], position[2]], limit, |id| {
            let p = self.routes.point(id)?;
            if p.progress == -1 {
                return Ok(());
            }
            let c = self.collider(id)?;
            let delta =
                std::array::from_fn::<_, 3, _>(|i| position[i].wrapping_sub(c.pose.position[i]));
            if delta.iter().any(|d| d.wrapping_abs() >= 153600) {
                return Ok(());
            }
            let d = geometry::approximate_distance(delta) as u32;
            if d < distance {
                best = Some(c.entity);
                distance = d;
            }
            Ok(())
        })?;
        Ok(best)
    }
}
