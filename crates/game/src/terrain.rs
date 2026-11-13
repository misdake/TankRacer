//! Original 501530 selection and 501910/501770/501830 height contracts.
use tankracer_assets::terrain::{CollisionMesh, CollisionTriangle, NodeId, TerrainData};

pub const MISS_HEIGHT: i32 = i32::MIN + 1;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    Arithmetic,
    InvalidBucket,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Hit {
    pub height: i32,
    pub node: Option<NodeId>,
    pub normal: [i32; 3],
    pub surface: u32,
    pub special: i32,
    pub extra: [i32; 4],
    pub flags: u32,
}

impl Default for Hit {
    fn default() -> Self {
        Self {
            height: MISS_HEIGHT,
            node: None,
            normal: [0; 3],
            surface: 0,
            special: 0,
            extra: [0; 4],
            flags: 0,
        }
    }
}

pub struct Selection<'a> {
    pub node: NodeId,
    pub mesh: &'a CollisionMesh,
    pub triangle: &'a CollisionTriangle,
}

pub fn select(data: &TerrainData, x: i32, z: i32) -> Result<Option<Selection<'_>>, Error> {
    let (sx, sz) = (x >> 3, z >> 3);
    let gx = sx
        .wrapping_sub(data.origin[0])
        .wrapping_shr(u32::from(data.shifts[0]));
    let gz = sz
        .wrapping_sub(data.origin[1])
        .wrapping_shr(u32::from(data.shifts[1]));
    if !(0..8).contains(&gx) || !(0..8).contains(&gz) {
        return Ok(None);
    }
    let cell = data.cells[(gx + 8 * gz) as usize];
    for &id in &data.node_refs[cell.start as usize..(cell.start + cell.length) as usize] {
        let node = data.node(id);
        let mesh = &data.meshes[node.mesh as usize];
        let dx = sx.wrapping_sub(mesh.position[0]);
        let dz = sz.wrapping_sub(mesh.position[2]);
        if dx.wrapping_abs() > i32::from(node.half_bounds[0])
            || dz.wrapping_abs() > i32::from(node.half_bounds[1])
        {
            continue;
        }
        let bucket = dx
            .wrapping_shr(13_u32.wrapping_sub(u32::from(node.shifts[0])))
            .wrapping_add(
                dz.wrapping_shr(13_u32.wrapping_sub(u32::from(node.shifts[1])))
                    .wrapping_mul(-8),
            );
        let Some(&list) = node.bins.get(bucket.wrapping_add(28) as usize) else {
            return Err(Error::InvalidBucket);
        };
        if list == -1 {
            continue;
        }
        let start = node.candidates.start + list as u32;
        let end = node.candidates.start + node.candidates.length;
        for &face in &data.triangle_refs[start as usize..end as usize] {
            if face == 255 {
                break;
            }
            let triangle = &mesh.triangles[face as usize];
            let [a, b, c] = triangle.indices.map(|i| mesh.vertices[i as usize]);
            if edge(dx, dz, a, b) && edge(dx, dz, b, c) && edge(dx, dz, c, a) {
                return Ok(Some(Selection {
                    node: id,
                    mesh,
                    triangle,
                }));
            }
        }
    }
    Ok(None)
}

fn edge(x: i32, z: i32, a: [i16; 3], b: [i16; 3]) -> bool {
    x.wrapping_sub(i32::from(a[0]))
        .wrapping_mul(i32::from(b[2]).wrapping_sub(i32::from(a[2])))
        <= z.wrapping_sub(i32::from(a[2]))
            .wrapping_mul(i32::from(b[0]).wrapping_sub(i32::from(a[0])))
}

fn plane(
    selection: &Selection<'_>,
    x: i32,
    z: i32,
    vertical_special_case: bool,
) -> Result<i32, Error> {
    let [nx, ny, nz] = selection.triangle.normal.map(i32::from);
    let [vx, vy, vz] =
        selection.mesh.vertices[selection.triangle.indices[0] as usize].map(i32::from);
    let [ox, oy, oz] = selection.mesh.position;
    // 501770/501830 deliberately use vertex X in their ny==0 branch.
    if ny == 0 && vertical_special_case {
        return Ok(oy.wrapping_add(vx).wrapping_mul(8));
    }
    let numerator = oz
        .wrapping_add(vz)
        .wrapping_sub(z >> 3)
        .wrapping_mul(nz)
        .wrapping_add(ox.wrapping_add(vx).wrapping_sub(x >> 3).wrapping_mul(nx))
        .wrapping_mul(8);
    let quotient = numerator.checked_div(ny).ok_or(Error::Arithmetic)?;
    Ok(quotient.wrapping_add(oy.wrapping_add(vy).wrapping_mul(8)))
}

pub fn query(data: &TerrainData, x: i32, z: i32, out: &mut Hit) -> Result<bool, Error> {
    let Some(selection) = select(data, x, z)? else {
        out.height = MISS_HEIGHT;
        return Ok(false);
    };
    let height = plane(&selection, x, z, false)?;
    let node = data.node(selection.node);
    out.height = height;
    out.node = Some(selection.node);
    out.normal = selection.triangle.normal.map(i32::from);
    out.surface = u32::from(node.surface);
    out.special = i32::from(selection.triangle.flags & 0x100 != 0);
    let flags = (u32::from(selection.triangle.flags) & 0x2000)
        | (selection.mesh.flags & 65535)
        | if node.has_owner { 0x20 } else { 0 };
    out.flags = (out.flags & 0xffff0000) | (flags & 65535);
    if selection.mesh.flags & 0x10 != 0 {
        out.extra = node.extra.map(i32::from);
    }
    Ok(true)
}

pub fn height(data: &TerrainData, x: i32, z: i32, out: &mut i32) -> Result<bool, Error> {
    let Some(selection) = select(data, x, z)? else {
        *out = MISS_HEIGHT;
        return Ok(false);
    };
    *out = plane(&selection, x, z, true)?;
    Ok(true)
}

pub fn normal_height(
    data: &TerrainData,
    x: i32,
    z: i32,
    out: &mut i32,
    normal: &mut [i32; 3],
) -> Result<bool, Error> {
    let Some(selection) = select(data, x, z)? else {
        *out = MISS_HEIGHT;
        return Ok(false);
    };
    *normal = selection.triangle.normal.map(i32::from);
    *out = plane(&selection, x, z, true)?;
    Ok(true)
}

#[cfg(test)]
mod tests {
    use super::*;
    use tankracer_assets::terrain::{Span, TerrainNode};

    fn flat() -> TerrainData {
        TerrainData {
            source_root: 1,
            origin: [-32768; 2],
            shifts: [13; 2],
            cells: [Span {
                start: 0,
                length: 1,
            }; 64],
            node_refs: vec![NodeId(0)],
            nodes: vec![TerrainNode {
                source_offset: 96,
                mesh: 0,
                shifts: [0; 2],
                bins: [0; 64],
                candidates: Span {
                    start: 0,
                    length: 2,
                },
                half_bounds: [32767; 2],
                surface: 77,
                extra: [11, -12, 13, -14],
                has_owner: true,
            }],
            meshes: vec![CollisionMesh {
                source_offset: 0,
                position: [0, 500, 0],
                flags: 0x10,
                radius: 1000,
                vertices: vec![[0, 3, 0], [100, 3, 0], [0, 3, 100]],
                triangles: vec![CollisionTriangle {
                    source_offset: 72,
                    normal: [0, 4096, 0],
                    indices: [0, 1, 2],
                    flags: 0x2100,
                }],
                face_centers: Vec::new(),
            }],
            triangle_refs: vec![0, 255],
            fallback: Default::default(),
        }
    }

    #[test]
    fn hit_preserves_upper_flags_and_miss_preserves_everything_but_height() {
        let data = flat();
        let mut out = Hit {
            flags: 0xabcd1234,
            ..Hit::default()
        };
        assert_eq!(query(&data, 8, 8, &mut out), Ok(true));
        assert_eq!(out.height, 4024);
        assert_eq!(out.flags, 0xabcd2030);
        assert_eq!(out.special, 1);
        assert_eq!(out.extra, [11, -12, 13, -14]);
        let old = out.clone();
        assert_eq!(query(&data, -1, 0, &mut out), Ok(false));
        assert_eq!(
            out,
            Hit {
                height: MISS_HEIGHT,
                ..old
            }
        );
    }

    #[test]
    fn candidate_priority_is_not_changed_to_nearest_or_highest_surface() {
        let mut data = flat();
        data.meshes.push(CollisionMesh {
            source_offset: 1000,
            position: [0, 999, 0],
            flags: 0,
            radius: 1000,
            vertices: data.meshes[0].vertices.clone(),
            triangles: vec![CollisionTriangle {
                source_offset: 1072,
                normal: [0, 4096, 0],
                indices: [0, 1, 2],
                flags: 0,
            }],
            face_centers: Vec::new(),
        });
        data.nodes.push(TerrainNode {
            source_offset: 2000,
            mesh: 1,
            shifts: [0; 2],
            bins: [0; 64],
            candidates: Span {
                start: 0,
                length: 2,
            },
            half_bounds: [32767; 2],
            surface: 88,
            extra: [0; 4],
            has_owner: false,
        });
        data.node_refs = vec![NodeId(0), NodeId(1)];
        data.cells = [Span {
            start: 0,
            length: 2,
        }; 64];
        let mut out = Hit::default();
        query(&data, 8, 8, &mut out).unwrap();
        assert_eq!(out.node, Some(NodeId(0)));
        assert_eq!(out.height, 4024);
        data.node_refs.reverse();
        query(&data, 8, 8, &mut out).unwrap();
        assert_eq!(out.node, Some(NodeId(1)));
        assert_eq!(out.height, 8016);
    }

    #[test]
    fn vertical_plane_contracts_and_conditional_extra_outputs_are_distinct() {
        let mut data = flat();
        data.meshes[0].triangles[0].normal = [4096, 0, 0];
        let mut out = Hit {
            flags: 0x12345678,
            ..Hit::default()
        };
        let old = out.clone();
        assert_eq!(query(&data, 8, 8, &mut out), Err(Error::Arithmetic));
        assert_eq!(out, old);
        let mut h = 123;
        let mut normal = [99; 3];
        assert_eq!(normal_height(&data, 8, 8, &mut h, &mut normal), Ok(true));
        assert_eq!(h, 4000);
        assert_eq!(normal, [4096, 0, 0]);
        data.meshes[0].flags = 0;
        data.meshes[0].triangles[0].normal = [0, 4096, 0];
        out.extra = [5, 6, 7, 8];
        query(&data, 8, 8, &mut out).unwrap();
        assert_eq!(out.extra, [5, 6, 7, 8]);
    }
}
