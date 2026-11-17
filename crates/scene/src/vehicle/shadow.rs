//! The original four-point suspension projection, not a new shadow simulation.
use crate::{camera::from_world, transform::physics_pose, world::legacy_color};
use glam::Mat4;
use tankracer_game::{body::Hull, vehicle_model::Mesh};
use tankracer_render::geometry::{MeshData, Vertex};

pub struct Shadow {
    pub geometry: MeshData,
    pub transform: Mat4,
    pub visible: bool,
    pub revision: u64,
    vertices: [[i16; 3]; 4],
}
impl Shadow {
    pub fn load(mesh: &Mesh) -> Result<Self, &'static str> {
        let source = mesh.geometry();
        if source.vertices.len() != 4
            || source.triangles.len() != 2
            || !mesh.source.materials.is_empty()
        {
            return Err("Unsupported vehicle shadow resource");
        }
        let vertices: [[i16; 3]; 4] = source.vertices.as_slice().try_into().unwrap();
        let color = legacy_color(source.triangles[0].color, false);
        Ok(Self {
            geometry: MeshData {
                vertices: vertices
                    .into_iter()
                    .map(|p| Vertex {
                        position: from_world(p.map(i32::from)).to_array(),
                        color,
                        uv: [0.; 2],
                        billboard_offset: 0.,
                        double_sided: 1.,
                    })
                    .collect(),
                indices: source
                    .triangles
                    .iter()
                    .flat_map(|t| t.indices.map(u32::from))
                    .collect(),
            },
            transform: Mat4::IDENTITY,
            visible: false,
            revision: 1,
            vertices,
        })
    }
    pub fn refresh(&mut self, hull: &Hull, ready: bool) {
        self.visible = ready;
        if !ready {
            return;
        }
        self.transform = physics_pose(hull.position, hull.matrix.0);
        if self.vertices != hull.vertices {
            self.vertices = hull.vertices;
            for (vertex, position) in self.geometry.vertices.iter_mut().zip(hull.vertices) {
                vertex.position = from_world(position.map(i32::from)).to_array();
            }
            self.revision = self.revision.wrapping_add(1);
        }
    }
    pub fn resident_bytes(&self) -> usize {
        std::mem::size_of::<Self>()
            + self.geometry.vertices.capacity() * std::mem::size_of::<Vertex>()
            + self.geometry.indices.capacity() * 4
    }
}
