use bytemuck::{Pod, Zeroable};

#[derive(Debug, Clone, Copy, Pod, Zeroable)]
#[repr(C)]
pub struct Vertex {
    pub position: [f32; 3],
    pub color: [f32; 4],
    pub uv: [f32; 2],
    pub billboard_offset: f32,
    pub double_sided: f32,
}

#[derive(Debug, Default)]
pub struct MeshData {
    pub vertices: Vec<Vertex>,
    pub indices: Vec<u32>,
}
/// Streams already supported by the renderer; absent streams allocate nothing.
#[derive(Default, Clone, Copy)]
pub struct MeshStreams<'a> {
    pub uv_scroll: &'a [[f32; 4]],
    pub normals: &'a [[f32; 4]],
}

impl MeshData {
    /// Counter-clockwise XY quad; also used by pixel-based pipeline tests.
    pub fn quad(z: f32) -> Self {
        Self {
            vertices: [[-1., -1., z], [1., -1., z], [1., 1., z], [-1., 1., z]]
                .into_iter()
                .enumerate()
                .map(|(i, position)| Vertex {
                    position,
                    color: [1.; 4],
                    uv: [[0., 0.], [1., 0.], [1., 1.], [0., 1.]][i],
                    billboard_offset: 0.,
                    double_sided: 0.,
                })
                .collect(),
            indices: vec![0, 1, 2, 0, 2, 3],
        }
    }

    pub fn cube() -> Self {
        let mut vertices = Vec::with_capacity(24);
        let mut indices = Vec::with_capacity(36);
        for (points, shade) in [
            (
                [[-1., -1., 1.], [1., -1., 1.], [1., 1., 1.], [-1., 1., 1.]],
                0.85,
            ),
            (
                [
                    [1., -1., -1.],
                    [-1., -1., -1.],
                    [-1., 1., -1.],
                    [1., 1., -1.],
                ],
                0.6,
            ),
            (
                [[1., -1., 1.], [1., -1., -1.], [1., 1., -1.], [1., 1., 1.]],
                0.75,
            ),
            (
                [
                    [-1., -1., -1.],
                    [-1., -1., 1.],
                    [-1., 1., 1.],
                    [-1., 1., -1.],
                ],
                0.65,
            ),
            (
                [[-1., 1., 1.], [1., 1., 1.], [1., 1., -1.], [-1., 1., -1.]],
                1.,
            ),
            (
                [
                    [-1., -1., -1.],
                    [1., -1., -1.],
                    [1., -1., 1.],
                    [-1., -1., 1.],
                ],
                0.4,
            ),
        ] {
            let first = vertices.len() as u32;
            vertices.extend(points.into_iter().enumerate().map(|(i, position)| Vertex {
                position,
                color: [shade, shade, shade, 1.],
                uv: [[0., 0.], [1., 0.], [1., 1.], [0., 1.]][i],
                billboard_offset: 0.,
                double_sided: 0.,
            }));
            indices.extend([0, 1, 2, 0, 2, 3].map(|i| i + first));
        }
        Self { vertices, indices }
    }
}
