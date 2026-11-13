//! Original ordered object queries, using typed resources instead of guest lists.
use crate::{fixed::trunc_shift, geometry::approximate_distance, math::Matrix, pose::Pose, vector};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct EntityId(pub u32);
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ObstacleId(pub usize);
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TankId(pub usize);

pub use crate::geometry::Bounds;
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    Arithmetic,
    MissingObstacle(ObstacleId),
    MissingTank(TankId),
    Limit,
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct SegmentHit {
    pub point: [i32; 3],
    pub normal: [i32; 3],
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Code {
    Miss = 0,
    Crossing = 1,
    Inside = 2,
}

/// A miss may write point; only Crossing writes normal. Starting inside the
/// inclusive bounds returns Inside even for a stationary segment.
pub fn segment_box(
    a: [i32; 3],
    b: [i32; 3],
    bounds: Bounds,
    out: &mut SegmentHit,
) -> Result<Code, Error> {
    let raw = std::array::from_fn(|i| b[i].wrapping_sub(a[i]));
    let mut direction = raw;
    vector::normalize(&mut direction);
    let mut side = [2; 3];
    let mut candidate = [0_i32; 3];
    let mut inside = true;
    for i in 0..3 {
        if a[i] < bounds.min[i] {
            side[i] = 1;
            candidate[i] = bounds.min[i];
            inside = false;
        } else if a[i] > bounds.max[i] {
            side[i] = 0;
            candidate[i] = bounds.max[i];
            inside = false;
        }
    }
    if inside {
        out.point = a;
        return Ok(Code::Inside);
    }
    let mut distance = [-4096; 3];
    for i in 0..3 {
        if side[i] != 2 && direction[i] != 0 {
            distance[i] = candidate[i]
                .wrapping_sub(a[i])
                .wrapping_mul(4096)
                .checked_div(direction[i])
                .ok_or(Error::Arithmetic)?;
        }
    }
    let mut axis = usize::from(distance[0] < distance[1]);
    if distance[axis] < distance[2] {
        axis = 2;
    }
    if distance[axis] < 0 {
        return Ok(Code::Miss);
    }
    out.point =
        std::array::from_fn(|i| a[i].wrapping_add(direction[i].wrapping_mul(distance[axis]) >> 12));
    for i in 0..3 {
        if out.point[i] < bounds.min[i].wrapping_sub(2)
            || out.point[i] > bounds.max[i].wrapping_add(2)
        {
            return Ok(Code::Miss);
        }
    }
    if distance[axis] > approximate_distance(raw) {
        return Ok(Code::Miss);
    }
    out.normal = [0; 3];
    out.normal[axis] = if side[axis] == 1 { -4096 } else { 4096 };
    Ok(Code::Crossing)
}

#[derive(Debug, Clone, Copy)]
pub struct Collider {
    pub entity: EntityId,
    pub pose: Pose,
    pub external_basis: Option<Matrix>,
    pub radius: u16,
    pub bounds: Bounds,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Hit<Id> {
    pub entity: Option<EntityId>,
    pub object: Option<Id>,
    pub code: i32,
    pub point: [i32; 3],
    pub normal: [i32; 3],
}
impl<Id> Default for Hit<Id> {
    fn default() -> Self {
        Self {
            entity: None,
            object: None,
            code: 0,
            point: [0; 3],
            normal: [0; 3],
        }
    }
}
impl<Id> Hit<Id> {
    fn set(&mut self, id: Id, entity: EntityId, code: Code, point: Option<SegmentHit>) {
        self.object = Some(id);
        self.entity = Some(entity);
        self.code = code as i32;
        if let Some(p) = point {
            self.point = p.point;
            self.normal = p.normal;
        }
    }
}

/// Borrowed candidate lists in original order. Ownership and grid maintenance
/// belong to the world; querying introduces no per-map cache or allocations.
pub struct Grid<'a> {
    pub origin: [i32; 2],
    pub shifts: [u32; 2],
    pub cells: [&'a [ObstacleId]; 64],
}
impl Grid<'_> {
    pub fn candidates(&self, start: [i32; 3]) -> &[ObstacleId] {
        let x = start[0]
            .wrapping_sub(self.origin[0])
            .wrapping_shr(self.shifts[0])
            .clamp(0, 7);
        let z = start[2]
            .wrapping_sub(self.origin[1])
            .wrapping_shr(self.shifts[1])
            .clamp(0, 7);
        self.cells[(x + 8 * z) as usize]
    }
}
fn broadphase(origin: [i32; 3], start: [i32; 3], length: i32, radius: i32) -> bool {
    (0..3).all(|i| {
        origin[i]
            .wrapping_sub(start[i])
            .wrapping_abs()
            .wrapping_sub(length)
            < radius
    })
}
fn relative(point: [i32; 3], origin: [i32; 3], basis: Option<Matrix>) -> [i32; 3] {
    let v = std::array::from_fn(|i| point[i].wrapping_sub(origin[i]));
    basis.map_or(v, |m| m.world_to_local(v))
}
fn world_hit(
    hit: SegmentHit,
    basis: Matrix,
    origin: [i32; 3],
    delta: [i32; 3],
) -> Option<SegmentHit> {
    let normal = basis.local_to_world(hit.normal);
    let mut direction = delta;
    vector::normalize(&mut direction);
    let projection = (0..3).fold(0_i32, |sum, i| {
        sum.wrapping_add(trunc_shift(direction[i].wrapping_mul(normal[i]), 12))
    });
    if projection >= 0 {
        return None;
    }
    let point = basis.local_to_world(hit.point);
    Some(SegmentHit {
        point: std::array::from_fn(|i| point[i].wrapping_add(origin[i])),
        normal,
    })
}
pub fn obstacles(
    grid: &Grid<'_>,
    objects: &[Collider],
    self_entity: EntityId,
    a: [i32; 3],
    b: [i32; 3],
    out: &mut Hit<ObstacleId>,
) -> Result<bool, Error> {
    query_list(
        grid.candidates(a),
        objects,
        Some(self_entity),
        a,
        b,
        false,
        out,
    )
}
pub fn deformables(
    candidates: &[ObstacleId],
    objects: &[Collider],
    a: [i32; 3],
    b: [i32; 3],
    out: &mut Hit<ObstacleId>,
) -> Result<bool, Error> {
    query_list(candidates, objects, None, a, b, true, out)
}
fn query_list(
    candidates: &[ObstacleId],
    objects: &[Collider],
    self_entity: Option<EntityId>,
    a: [i32; 3],
    b: [i32; 3],
    deformable: bool,
    out: &mut Hit<ObstacleId>,
) -> Result<bool, Error> {
    let delta = std::array::from_fn(|i| b[i].wrapping_sub(a[i]));
    let length = approximate_distance(delta);
    for &id in candidates.iter().take(65536) {
        let object = objects.get(id.0).ok_or(Error::MissingObstacle(id))?;
        let radius = (i32::from(object.radius) + 96) * 16;
        if !broadphase(object.pose.position, a, length, radius)
            || (!deformable && Some(object.entity) == self_entity)
        {
            continue;
        }
        let basis = if deformable {
            object.external_basis
        } else {
            Some(object.pose.matrix)
        };
        let local_a = relative(a, object.pose.position, basis);
        let local_b = relative(b, object.pose.position, basis);
        let mut hit = SegmentHit::default();
        let mut code = segment_box(local_a, local_b, object.bounds, &mut hit)?;
        if !deformable && code == Code::Inside {
            let mut direction = std::array::from_fn(|i| local_b[i].wrapping_sub(local_a[i]));
            if approximate_distance(direction) > 0 {
                vector::normalize(&mut direction);
                let behind = std::array::from_fn(|i| {
                    local_a[i].wrapping_sub(direction[i].wrapping_mul(radius >> 11))
                });
                code = segment_box(behind, local_a, object.bounds, &mut hit)?;
            }
        }
        if deformable && code != Code::Miss {
            if code == Code::Inside {
                hit.normal = local_a;
                vector::normalize(&mut hit.normal);
            }
            out.set(id, object.entity, code, Some(hit));
            return Ok(true);
        }
        if !deformable
            && code == Code::Crossing
            && let Some(hit) = world_hit(
                hit,
                basis.unwrap_or(Matrix::IDENTITY),
                object.pose.position,
                delta,
            )
        {
            out.set(id, object.entity, code, Some(hit));
            return Ok(true);
        }
    }
    if candidates.len() > 65536 {
        Err(Error::Limit)
    } else {
        Ok(false)
    }
}

#[derive(Debug, Clone, Copy)]
pub struct Tank {
    pub collider: Collider,
    pub slot: u8,
    pub active: bool,
    pub disabled: bool,
    pub ready: bool,
    pub overlap_bounds: Bounds,
    pub contacts: [[i32; 3]; 4],
}
fn corners_inside(a: &Tank, b: &Tank) -> bool {
    b.contacts.iter().any(|&point| contains(a, point))
}
pub(crate) fn contains(tank: &Tank, point: [i32; 3]) -> bool {
    let local = relative(
        point,
        tank.collider.pose.position,
        Some(tank.collider.pose.matrix),
    );
    local[0] > tank.overlap_bounds.min[0]
        && local[0] < tank.overlap_bounds.max[0]
        && local[2] > tank.overlap_bounds.min[2]
        && local[2] < tank.overlap_bounds.max[2]
}
pub fn overlap(a: &Tank, b: &Tank) -> bool {
    corners_inside(a, b) || corners_inside(b, a)
}

/// Slot order wins, not nearest distance. The self active flag gates querying;
/// peer active flags do not. Inside/overlap updates metadata only. A miss clears
/// metadata while preserving point/normal, unlike obstacle/deformable misses.
pub fn tanks(
    self_tank: &Tank,
    objects: &[Tank],
    slots: &[Option<TankId>; 9],
    a: [i32; 3],
    b: [i32; 3],
    out: &mut Hit<TankId>,
) -> Result<Code, Error> {
    let radius = i32::from(self_tank.collider.radius) * 64;
    let delta = std::array::from_fn(|i| b[i].wrapping_sub(a[i]));
    for (slot, id) in slots.iter().enumerate() {
        let Some(id) = *id else {
            continue;
        };
        if slot == usize::from(self_tank.slot) || !self_tank.active {
            continue;
        }
        let object = objects.get(id.0).ok_or(Error::MissingTank(id))?;
        if object.disabled || !object.ready {
            continue;
        }
        let collider = &object.collider;
        let origin = collider.pose.position;
        if !(origin[0] < a[0].wrapping_add(radius)
            && origin[0] > a[0].wrapping_sub(radius)
            && origin[2] < a[2].wrapping_add(radius)
            && origin[2] > a[2].wrapping_sub(radius))
        {
            continue;
        }
        if overlap(self_tank, object) {
            out.set(id, collider.entity, Code::Inside, None);
            return Ok(Code::Inside);
        }
        let local_a = relative(a, origin, collider.external_basis);
        let local_b = relative(b, origin, collider.external_basis);
        let mut hit = SegmentHit::default();
        let code = segment_box(local_a, local_b, collider.bounds, &mut hit)?;
        if code == Code::Inside {
            out.set(id, collider.entity, code, None);
            return Ok(code);
        }
        if code == Code::Crossing
            && let Some(hit) = world_hit(
                hit,
                collider.external_basis.unwrap_or(Matrix::IDENTITY),
                origin,
                delta,
            )
        {
            out.set(id, collider.entity, code, Some(hit));
            return Ok(code);
        }
    }
    out.entity = None;
    out.object = None;
    out.code = 0;
    Ok(Code::Miss)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn segment_inside_preserves_normal_and_short_miss_still_writes_point() {
        let bounds = Bounds {
            min: [-10; 3],
            max: [10; 3],
        };
        let mut out = SegmentHit {
            point: [99; 3],
            normal: [123; 3],
        };
        assert_eq!(
            segment_box([0; 3], [0; 3], bounds, &mut out),
            Ok(Code::Inside)
        );
        assert_eq!(out.point, [0; 3]);
        assert_eq!(out.normal, [123; 3]);
        assert_eq!(
            segment_box([-20, 0, 0], [-15, 0, 0], bounds, &mut out),
            Ok(Code::Miss)
        );
        assert_eq!(out.point, [-10, 0, 0]);
        assert_eq!(out.normal, [123; 3]);
    }
    #[test]
    fn source_order_and_deformable_local_output_are_intentional() {
        let object = Collider {
            entity: EntityId(1),
            pose: Pose {
                position: [100, 0, 100],
                matrix: Matrix::IDENTITY,
            },
            external_basis: None,
            radius: 100,
            bounds: Bounds {
                min: [-10; 3],
                max: [10; 3],
            },
        };
        let objects = [
            object,
            Collider {
                entity: EntityId(2),
                ..object
            },
        ];
        let mut out = Hit::default();
        assert_eq!(
            deformables(
                &[ObstacleId(1), ObstacleId(0)],
                &objects,
                [80, 0, 100],
                [120, 0, 100],
                &mut out
            ),
            Ok(true)
        );
        assert_eq!(out.entity, Some(EntityId(2)));
        assert_eq!(out.point, [-10, 0, 0]);
        assert_eq!(out.normal, [-4096, 0, 0]);
        assert_eq!(
            deformables(&[], &objects, [0; 3], [0; 3], &mut out),
            Ok(false)
        );
        assert_eq!(out.entity, Some(EntityId(2)));
    }

    #[test]
    fn candidate_limit_and_missing_handles_preserve_output() {
        let object = Collider {
            entity: EntityId(1),
            pose: Pose {
                position: [1000000; 3],
                matrix: Matrix::IDENTITY,
            },
            external_basis: None,
            radius: 0,
            bounds: Bounds {
                min: [-1; 3],
                max: [1; 3],
            },
        };
        let ids = vec![ObstacleId(0); 65537];
        let initial = Hit {
            entity: Some(EntityId(2)),
            object: Some(ObstacleId(9)),
            code: 7,
            point: [3; 3],
            normal: [4; 3],
        };
        let mut out = initial;
        assert_eq!(
            deformables(&ids[..65536], &[object], [0; 3], [0; 3], &mut out),
            Ok(false)
        );
        assert_eq!(
            deformables(&ids, &[object], [0; 3], [0; 3], &mut out),
            Err(Error::Limit)
        );
        assert_eq!(out, initial);
        assert_eq!(
            deformables(&[ObstacleId(1)], &[object], [0; 3], [0; 3], &mut out),
            Err(Error::MissingObstacle(ObstacleId(1)))
        );
        assert_eq!(out, initial);
    }

    #[test]
    fn inactive_peer_is_queryable_and_tank_miss_clears_only_metadata() {
        let collider = Collider {
            entity: EntityId(1),
            pose: Pose::default(),
            external_basis: None,
            radius: 1,
            bounds: Bounds {
                min: [-10; 3],
                max: [10; 3],
            },
        };
        let own = Tank {
            collider,
            slot: 0,
            active: true,
            disabled: false,
            ready: true,
            overlap_bounds: collider.bounds,
            contacts: [[1000; 3]; 4],
        };
        let mut peer = Tank {
            collider: Collider {
                entity: EntityId(2),
                ..collider
            },
            slot: 1,
            active: false,
            ..own
        };
        let mut slots = [None; 9];
        slots[1] = Some(TankId(0));
        let mut out = Hit {
            point: [17; 3],
            normal: [19; 3],
            ..Hit::default()
        };
        assert_eq!(
            tanks(&own, &[peer], &slots, [0; 3], [0; 3], &mut out),
            Ok(Code::Inside)
        );
        assert_eq!(out.entity, Some(EntityId(2)));
        assert_eq!(out.point, [17; 3]);
        assert_eq!(out.normal, [19; 3]);
        peer.disabled = true;
        assert_eq!(
            tanks(&own, &[peer], &slots, [0; 3], [0; 3], &mut out),
            Ok(Code::Miss)
        );
        assert_eq!(out.entity, None);
        assert_eq!(out.object, None);
        assert_eq!(out.code, 0);
        assert_eq!(out.point, [17; 3]);
        assert_eq!(out.normal, [19; 3]);
    }
}
