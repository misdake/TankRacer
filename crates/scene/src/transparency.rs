//! One back-to-front order across world faces and transformed model instances.
//! Each prototype keeps resident vertices; only its index bank is reordered.
use crate::{camera::Camera, world::TransparentTriangle};
use glam::Mat4;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Object {
    pub geometry: usize,
    pub model: Mat4,
}

#[derive(Clone, Copy, Debug)]
pub struct Run {
    pub object: usize,
    pub first: u32,
    pub count: u32,
}
#[derive(Clone, Copy)]
struct Key {
    depth: f32,
    object: usize,
    triangle: usize,
}
#[derive(Default)]
pub struct Order {
    keys: Vec<Key>,
    pub banks: Vec<Vec<u32>>,
    pub runs: Vec<Run>,
    pub objects: Vec<Object>,
    camera: Option<[u32; 6]>,
    revision: u64,
}
impl Order {
    pub fn update<'a>(
        &mut self,
        objects: &[Object],
        triangles: impl Fn(usize) -> &'a [TransparentTriangle],
        camera: &Camera,
        revision: u64,
    ) -> bool {
        let forward = camera.forward();
        let state = [
            camera.position.x.to_bits(),
            camera.position.y.to_bits(),
            camera.position.z.to_bits(),
            forward.x.to_bits(),
            forward.y.to_bits(),
            forward.z.to_bits(),
        ];
        if self.camera == Some(state) && self.objects == objects && self.revision == revision {
            return false;
        }
        self.objects.clear();
        self.objects.extend_from_slice(objects);
        self.keys.clear();
        self.runs.clear();
        let banks = objects.iter().map(|o| o.geometry + 1).max().unwrap_or(0);
        self.banks.resize_with(banks, Vec::new);
        for bank in &mut self.banks {
            bank.clear();
        }
        for (object, instance) in objects.iter().enumerate() {
            self.keys
                .extend(
                    triangles(instance.geometry)
                        .iter()
                        .enumerate()
                        .map(|(triangle, face)| Key {
                            depth: (instance.model.transform_point3(face.center) - camera.position)
                                .dot(forward),
                            object,
                            triangle,
                        }),
                );
        }
        self.keys.sort_unstable_by(|a, b| {
            b.depth
                .total_cmp(&a.depth)
                .then(a.object.cmp(&b.object))
                .then(a.triangle.cmp(&b.triangle))
        });
        for key in &self.keys {
            let geometry = objects[key.object].geometry;
            let bank = &mut self.banks[geometry];
            let first = bank.len() as u32;
            bank.extend(triangles(geometry)[key.triangle].indices);
            if let Some(run) = self.runs.last_mut()
                && run.object == key.object
            {
                run.count += 3;
            } else {
                self.runs.push(Run {
                    object: key.object,
                    first,
                    count: 3,
                });
            }
        }
        self.camera = Some(state);
        self.revision = revision;
        true
    }
    pub fn resident_bytes(&self) -> usize {
        std::mem::size_of::<Self>()
            + self.keys.capacity() * std::mem::size_of::<Key>()
            + self.banks.capacity() * std::mem::size_of::<Vec<u32>>()
            + self.banks.iter().map(|b| b.capacity() * 4).sum::<usize>()
            + self.runs.capacity() * std::mem::size_of::<Run>()
            + self.objects.capacity() * std::mem::size_of::<Object>()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use glam::Vec3;
    #[test]
    fn interleaves_world_and_shared_instances_and_invalidates_on_geometry_change() {
        let faces = [
            vec![
                TransparentTriangle {
                    indices: [0, 1, 2],
                    center: Vec3::new(0., 0., -8.),
                },
                TransparentTriangle {
                    indices: [3, 4, 5],
                    center: Vec3::new(0., 0., -2.),
                },
            ],
            vec![TransparentTriangle {
                indices: [6, 7, 8],
                center: Vec3::ZERO,
            }],
        ];
        let mut objects = vec![
            Object {
                geometry: 0,
                model: Mat4::IDENTITY,
            },
            Object {
                geometry: 1,
                model: Mat4::from_translation(Vec3::new(0., 0., -5.)),
            },
            Object {
                geometry: 1,
                model: Mat4::from_translation(Vec3::new(0., 0., -10.)),
            },
        ];
        let camera = Camera::looking_at(Vec3::ZERO, Vec3::NEG_Z);
        let mut order = Order::default();
        assert!(order.update(&objects, |i| &faces[i], &camera, 0));
        assert_eq!(
            order.runs.iter().map(|r| r.object).collect::<Vec<_>>(),
            [2, 0, 1, 0]
        );
        assert_eq!(
            order.banks,
            [vec![0, 1, 2, 3, 4, 5], vec![6, 7, 8, 6, 7, 8]]
        );
        assert_eq!(
            order
                .runs
                .iter()
                .map(|r| (r.first, r.count))
                .collect::<Vec<_>>(),
            [(0, 3), (0, 3), (3, 3), (3, 3)]
        );
        assert!(!order.update(&objects, |i| &faces[i], &camera, 0));
        assert!(order.update(&objects, |i| &faces[i], &camera, 1));
        objects[1].model = Mat4::from_translation(Vec3::new(0., 0., -12.));
        assert!(order.update(&objects, |i| &faces[i], &camera, 1));
        assert_eq!(order.runs[0].object, 1);
        objects.truncate(1);
        assert!(order.update(&objects, |i| &faces[i], &camera, 1));
        assert_eq!(order.runs.len(), 1);
        assert_eq!(order.runs[0].count, 6);
    }
}
