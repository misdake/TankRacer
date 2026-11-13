//! Geometry inputs consumed by suspension, without coupling queries to a vehicle arena.
use crate::terrain::{self, Error, Hit};
use tankracer_assets::terrain::TerrainData;

/// Original column-major Q12 orientation and raw physics position.
#[derive(Debug, Clone, Copy)]
pub struct Transform {
    pub basis: [i32; 9],
    pub origin: [i32; 3],
}

impl Transform {
    pub fn point(&self, local: [i32; 3]) -> [i32; 3] {
        std::array::from_fn(|i| {
            self.basis[i]
                .wrapping_mul(local[0])
                .wrapping_add(self.basis[3 + i].wrapping_mul(local[1]))
                .wrapping_add(self.basis[6 + i].wrapping_mul(local[2]))
                .wrapping_shr(12)
                .wrapping_add(self.origin[i])
        })
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct AuxiliaryContact {
    pub world: [i32; 3],
    pub kind: i32,
    pub surface: i32,
    pub cached: i32,
}

impl AuxiliaryContact {
    /// Original 42EE80: world is updated before a query error; kind/surface/cache
    /// are committed only after the query completes. ±75 boundaries are asymmetric.
    pub fn sample(
        &mut self,
        data: &TerrainData,
        local: [i32; 3],
        transform: &Transform,
        force_surface: bool,
    ) -> Result<bool, Error> {
        self.world = transform.point(local);
        let mut hit = Hit::default();
        let found = terrain::query(data, self.world[0], self.world[2], &mut hit)?;
        if found {
            let gap = self.world[1].wrapping_sub(hit.height);
            self.kind = if gap < -75 {
                0
            } else if gap < 75 {
                1
            } else {
                2
            };
            self.surface = hit.surface as i32;
        } else {
            self.kind = 3;
            self.surface = 0;
        }
        if force_surface {
            self.surface = 1;
        }
        self.cached = 1;
        Ok(found)
    }
}

pub fn contact_materials(primary: [i32; 2], auxiliary: [i32; 2]) -> [i32; 2] {
    if primary[0] == primary[1] {
        [primary[0]; 2]
    } else {
        auxiliary
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn both_material_outputs_use_the_same_primary_comparison() {
        assert_eq!(contact_materials([7, 7], [2, 3]), [7, 7]);
        assert_eq!(contact_materials([7, 8], [2, 3]), [2, 3]);
    }
}
