//! Moving/deformable surface sampling (4073D0), with explicit resource and state.
use crate::fixed::trunc_shift;

#[derive(Debug, Clone)]
pub struct Geometry {
    pub sample_bounds: [[i32; 2]; 2],
    /// Local raw X lower/upper, Z lower/upper; deliberately separate from bounds.
    pub edges: [i32; 4],
    pub edge_cap: i32,
    pub vertices: Vec<[i16; 3]>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Pose {
    pub origin: [i32; 3],
    /// Original row-major world-to-local Q12 basis, not the track's local pose.
    pub inverse_basis: [i32; 9],
}

impl Default for Pose {
    fn default() -> Self {
        Self {
            origin: [0; 3],
            inverse_basis: [4096, 0, 0, 0, 4096, 0, 0, 0, 4096],
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Record {
    pub position: [i32; 3],
    pub state: u8,
    pub color: [u8; 3],
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct State {
    pub pose: Pose,
    /// Resource loading must validate at most 65536 records, the source limit.
    pub records: Vec<Record>,
    pub dirty: u8,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Sample {
    pub local_raw: [i32; 3],
    pub local_clamped: [i32; 3],
    pub vertex_height: i32,
    pub edge_weight: i32,
    pub offset: i32,
    pub activated_records: u32,
}

pub fn edge_weight(local: [i32; 3], bounds: [i32; 4], cap: i32) -> i32 {
    let distance = [
        local[0].wrapping_sub(bounds[0]).wrapping_abs(),
        local[0].wrapping_sub(bounds[1]).wrapping_abs(),
        local[2].wrapping_sub(bounds[2]).wrapping_abs(),
        local[2].wrapping_sub(bounds[3]).wrapping_abs(),
    ]
    .into_iter()
    .min()
    .unwrap_or(0);
    distance.wrapping_mul(10).min(cap)
}

impl State {
    pub fn sample(&mut self, geometry: &Geometry, world: [i32; 3]) -> Sample {
        debug_assert!(self.records.len() <= 65536);
        let relative: [i32; 3] =
            std::array::from_fn(|i| world[i].wrapping_sub(self.pose.origin[i]));
        let local_raw: [i32; 3] = std::array::from_fn(|i| {
            self.pose.inverse_basis[i * 3]
                .wrapping_mul(relative[0])
                .wrapping_add(self.pose.inverse_basis[i * 3 + 1].wrapping_mul(relative[1]))
                .wrapping_add(self.pose.inverse_basis[i * 3 + 2].wrapping_mul(relative[2]))
                >> 12
        });
        let mut local_clamped = local_raw.map(|v| trunc_shift(v, 3));
        for (component, [lower, upper]) in [0, 2].into_iter().zip(geometry.sample_bounds) {
            // Original sequential bounds checks also define reversed-bound behavior.
            local_clamped[component] = local_clamped[component].max(lower).min(upper);
        }
        let mut maximum = 0;
        for &[x, y, z] in &geometry.vertices {
            let dx = local_clamped[0].wrapping_sub(i32::from(x));
            let dz = local_clamped[2].wrapping_sub(i32::from(z));
            if dx > -128 && dx < 128 && dz > -128 && dz < 128 {
                maximum = maximum.max(i32::from(y));
            }
        }
        let mut activated_records = 0;
        for record in &mut self.records {
            let activate = match record.state {
                0 => true,
                1 => {
                    let dx = local_raw[0].wrapping_sub(record.position[0]);
                    let dz = local_raw[2].wrapping_sub(record.position[2]);
                    dx > -750 && dx < 750 && dz > -750 && dz < 750
                }
                _ => false,
            };
            if activate {
                record.state = 2;
                record.color = [127; 3];
                self.dirty = 1;
                activated_records += 1;
            }
        }
        let edge_weight = edge_weight(local_raw, geometry.edges, geometry.edge_cap);
        Sample {
            local_raw,
            local_clamped,
            vertex_height: maximum,
            edge_weight,
            offset: trunc_shift(edge_weight.wrapping_mul(maximum.wrapping_mul(8)), 16),
            activated_records,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn vertex_and_record_windows_are_strict_and_activation_uses_raw_coordinates() {
        let geometry = Geometry {
            sample_bounds: [[-10, 10]; 2],
            edges: [-10000, 10000, -10000, 10000],
            edge_cap: 65536,
            vertices: vec![[138, 300, 0], [137, 40, 0], [0, -100, 0]],
        };
        let mut state = State {
            pose: Pose::default(),
            records: vec![
                Record {
                    position: [800, 0, 0],
                    state: 1,
                    color: [9; 3],
                },
                Record {
                    position: [50, 0, 0],
                    state: 1,
                    color: [9; 3],
                },
                Record {
                    position: [i32::MAX; 3],
                    state: 0,
                    color: [9; 3],
                },
            ],
            dirty: 0,
        };
        let out = state.sample(&geometry, [800, 0, 0]);
        assert_eq!(out.local_clamped, [10, 0, 0]);
        assert_eq!(out.vertex_height, 40); // Distance 128 rejected; 127 accepted.
        assert_eq!(out.activated_records, 2);
        assert_eq!(state.records[0].color, [127; 3]);
        assert_eq!(state.records[1].state, 1); // Raw distance 750, not clamped distance.
        assert_eq!(state.records[2].state, 2); // State 0 activates unconditionally.
        assert_eq!(state.dirty, 1);
        assert_eq!(state.sample(&geometry, [800, 0, 0]).activated_records, 0);
    }
}
