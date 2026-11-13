//! Original 8x8 obstacle membership, including prepend and first-match removal.
use crate::{
    object_query::{Collider, ObstacleId},
    obstacle_collision::Grid,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Cell {
    pub x: i16,
    pub z: u16,
}
impl Cell {
    pub fn unpack(value: u32) -> Option<Self> {
        (value != u32::MAX).then_some(Self {
            x: (value >> 16) as i16,
            z: value as u16,
        })
    }
    pub fn packed(self) -> u32 {
        ((self.x as u16 as u32) << 16) | u32::from(self.z)
    }
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    InvalidLimit,
    Limit,
}

pub(crate) fn remove_first<T: PartialEq>(
    list: &mut Vec<T>,
    value: &T,
    limit: u32,
) -> Result<(), Error> {
    if limit == 0 {
        return Err(Error::InvalidLimit);
    }
    match list.iter().position(|v| v == value) {
        Some(i) if i < (limit as usize) => {
            list.remove(i);
            Ok(())
        }
        None if list.len() < (limit as usize) => Ok(()),
        _ => Err(Error::Limit),
    }
}
pub(crate) fn contains<T: PartialEq>(list: &[T], value: &T, limit: u32) -> Result<bool, Error> {
    if limit == 0 {
        return Err(Error::InvalidLimit);
    }
    for (i, item) in list.iter().enumerate() {
        if i >= limit as usize {
            return Err(Error::Limit);
        }
        if item == value {
            return Ok(true);
        }
    }
    if list.len() < limit as usize {
        Ok(false)
    } else {
        Err(Error::Limit)
    }
}
pub fn insert(
    grid: &mut Grid,
    id: ObstacleId,
    collider: &Collider,
    shape_radius: Option<u16>,
) -> Cell {
    let x = collider.pose.position[0]
        .wrapping_sub(grid.origin[0])
        .wrapping_shr(grid.shifts[0])
        .clamp(0, 7);
    let z = collider.pose.position[2]
        .wrapping_sub(grid.origin[1])
        .wrapping_shr(grid.shifts[1])
        .clamp(0, 7);
    let radius = shape_radius.map_or(5632, |r| (i32::from(r) + 352) * 16);
    for row in z - 1..=z + 1 {
        if !(0..8).contains(&row) {
            continue;
        }
        let lower = row
            .wrapping_shl(grid.shifts[1])
            .wrapping_add(grid.origin[1]);
        let top = lower.wrapping_add(1_i32.wrapping_shl(grid.shifts[1]));
        let pz = collider.pose.position[2];
        if !(pz.wrapping_sub(radius) < top && lower < pz.wrapping_add(radius)) {
            continue;
        }
        for col in x - 1..=x + 1 {
            if !(0..8).contains(&col) {
                continue;
            }
            let lower = col
                .wrapping_shl(grid.shifts[0])
                .wrapping_add(grid.origin[0]);
            let top = lower.wrapping_add(1_i32.wrapping_shl(grid.shifts[0]));
            let px = collider.pose.position[0];
            if px.wrapping_sub(radius) >= top || px.wrapping_add(radius) <= lower {
                continue;
            }
            grid.cells[(col + 8 * row) as usize].insert(0, id);
        }
    }
    Cell {
        x: x as i16,
        z: z as u16,
    }
}
pub fn remove(
    grid: &mut Grid,
    id: ObstacleId,
    cell: Option<Cell>,
    limit: u32,
) -> Result<(), Error> {
    if limit == 0 {
        return Err(Error::InvalidLimit);
    }
    if let Some(cell) = cell {
        let (x, z) = (i32::from(cell.x), i32::from(cell.z));
        for row in z - 1..=z + 1 {
            if !(0..8).contains(&row) {
                continue;
            }
            for col in x - 1..=x + 1 {
                if (0..8).contains(&col) {
                    remove_first(&mut grid.cells[(col + 8 * row) as usize], &id, limit)?;
                }
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn removal_is_local_first_match_and_keeps_completed_cells_on_limit() {
        let mut grid = Grid {
            origin: [0; 2],
            shifts: [12; 2],
            cells: std::array::from_fn(|_| Vec::new()),
        };
        grid.cells[0] = vec![ObstacleId(0), ObstacleId(0)];
        grid.cells[1] = vec![ObstacleId(1), ObstacleId(0)];
        grid.cells[63] = vec![ObstacleId(0)];
        assert_eq!(
            remove(&mut grid, ObstacleId(0), Some(Cell { x: 0, z: 0 }), 1),
            Err(Error::Limit)
        );
        assert_eq!(grid.cells[0], [ObstacleId(0)]);
        assert_eq!(grid.cells[1], [ObstacleId(1), ObstacleId(0)]);
        assert_eq!(grid.cells[63], [ObstacleId(0)]);
        assert_eq!(Cell::unpack(u32::MAX), None);
        assert_eq!(Cell::unpack(0xffff0000), Some(Cell { x: -1, z: 0 }));
    }
}
