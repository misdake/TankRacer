//! Alternate collision grid (501A50), preserving its non-Euclidean center metric.
use tankracer_assets::terrain::TerrainData;

/// Axis-aligned bounds in the simulation's fixed-point coordinate space.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Bounds {
    pub min: [i32; 3],
    pub max: [i32; 3],
}
impl Bounds {
    pub fn expanded(self, padding: [i32; 3]) -> Self {
        Self {
            min: std::array::from_fn(|i| self.min[i].wrapping_sub(padding[i])),
            max: std::array::from_fn(|i| self.max[i].wrapping_add(padding[i])),
        }
    }
}

/// Unsigned magnitudes: largest + one quarter of each remaining component.
/// The signed result can wrap; replacing this with a length changes selection.
pub fn approximate_distance(delta: [i32; 3]) -> i32 {
    let [x, y, z] = delta.map(|v| v.wrapping_abs() as u32);
    let value = if x > y && x > z {
        x.wrapping_add(y >> 2).wrapping_add(z >> 2)
    } else if y >= x && y > z {
        y.wrapping_add(x >> 2).wrapping_add(z >> 2)
    } else {
        z.wrapping_add(x >> 2).wrapping_add(y >> 2)
    };
    value as i32
}

/// Mode 1 excludes mesh flag 4, mode 2 requires it; other modes accept both.
/// Strictly smaller metric wins, so source order resolves ties. Miss preserves normal.
pub fn fallback(data: &TerrainData, world: [i32; 3], mode: i32, normal: &mut [i32; 3]) -> bool {
    let [x, y, z] = world.map(|v| v >> 3);
    let gx = x
        .wrapping_sub(data.origin[0])
        .wrapping_shr(u32::from(data.shifts[0]));
    let gz = z
        .wrapping_sub(data.origin[1])
        .wrapping_shr(u32::from(data.shifts[1]));
    if !(0..8).contains(&gx) || !(0..8).contains(&gz) {
        return false;
    }
    let cell = data.fallback.cells[(gx + 8 * gz) as usize];
    let refs = &data.fallback.node_refs[cell.start as usize..(cell.start + cell.length) as usize];
    let mut best = 300000;
    let mut selected = None;
    for &id in refs {
        let node = &data.fallback.nodes[id as usize];
        let mesh = &data.meshes[node.mesh as usize];
        if (mode == 1 && mesh.flags & 4 != 0) || (mode == 2 && mesh.flags & 4 == 0) {
            continue;
        }
        let dx = x.wrapping_sub(mesh.position[0]);
        let dz = z.wrapping_sub(mesh.position[2]);
        let radius = i32::from(mesh.radius) * 2;
        if dx.wrapping_abs() > radius || dz.wrapping_abs() > radius {
            continue;
        }
        let [a, b, c, d] = node.basis.map(i32::from);
        let lx = b.wrapping_mul(dz).wrapping_add(a.wrapping_mul(dx)) >> 12;
        let lz = d.wrapping_mul(dz).wrapping_add(c.wrapping_mul(dx)) >> 12;
        let [x0, x1, z0, z1] = node.bounds.map(i32::from);
        if lx > x1 || lx < x0 || lz > z1 || lz < z0 {
            continue;
        }
        let candidates = node.candidates;
        for &index in &data.triangle_refs
            [candidates.start as usize..(candidates.start + candidates.length) as usize]
        {
            if index == 255 {
                break;
            }
            let index = index as usize;
            let center = mesh.face_centers[index].map(i32::from);
            let distance = approximate_distance([
                dx.wrapping_sub(center[0]),
                y.wrapping_sub(mesh.position[1]).wrapping_sub(center[1]),
                dz.wrapping_sub(center[2]),
            ]);
            if distance < best {
                best = distance;
                selected = Some(mesh.triangles[index].normal);
            }
        }
    }
    if let Some(value) = selected {
        *normal = value.map(i32::from);
        true
    } else {
        false
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn distance_uses_unsigned_magnitudes_and_wraps_its_signed_result() {
        for delta in [
            [100, 40, 20],
            [40, 100, 20],
            [40, 20, 100],
            [-100, -40, -20],
        ] {
            assert_eq!(approximate_distance(delta), 115);
        }
        assert_eq!(approximate_distance([i32::MIN, 0, 0]), i32::MIN);
        assert_eq!(approximate_distance([i32::MAX; 3]), -1_073_741_827);
    }
}
