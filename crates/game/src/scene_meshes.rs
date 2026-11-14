//! Mutable scene geometry is addressed by stable handles. Shared shape bindings
//! and explicit private copies preserve original ownership without RefCell or
//! frame-by-frame vertex copies. Renderers borrow this same store read-only.
use std::collections::HashMap;
use std::sync::Arc;
use tankracer_assets::{
    mesh::{Geometry, SourceMaterial},
    model::Model,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct MeshId(usize);
impl MeshId {
    pub fn index(self) -> usize {
        self.0
    }
}
pub struct MeshInfo<'a> {
    pub geometry: &'a Geometry,
    pub materials: &'a [SourceMaterial],
    pub version: u64,
}

struct Mesh {
    geometry: Option<Geometry>,
    hint: u16,
    shared: bool,
    centers: [Option<Vec<i16>>; 3],
    radius: u16,
    materials: Arc<[SourceMaterial]>,
    version: u64,
}
pub struct FragmentMesh<'a> {
    pub geometry: &'a mut Geometry,
    pub centers: &'a mut [Option<Vec<i16>>; 3],
    pub radius: &'a mut u16,
}
#[derive(Default)]
pub struct Meshes {
    entries: Vec<Mesh>,
    names: HashMap<String, MeshId>,
}
impl Meshes {
    pub fn insert(&mut self, geometry: Geometry) -> MeshId {
        let id = MeshId(self.entries.len());
        self.entries.push(Mesh {
            geometry: Some(geometry),
            hint: 0,
            shared: false,
            centers: [None, None, None],
            radius: 0,
            materials: Arc::from([]),
            version: 1,
        });
        id
    }
    /// The first case-insensitive name binding wins, even across namespaces.
    pub fn bind(&mut self, name: &str, model: &Model) -> MeshId {
        let key = name.to_ascii_lowercase();
        if let Some(&id) = self.names.get(&key) {
            return id;
        }
        let id = self.insert(model.geometry.clone());
        self.entries[id.0].hint = model.render_hint;
        self.entries[id.0].centers = model.face_centers.clone();
        self.entries[id.0].radius = model.radius;
        self.entries[id.0].shared = true;
        self.entries[id.0].materials = model.materials.clone().into();
        self.names.insert(key, id);
        id
    }
    /// Clone the *current* geometry, including any earlier shared deformation.
    pub fn private_copy(&mut self, source: MeshId) -> Option<MeshId> {
        let copy = self.get(source)?.clone();
        let hint = self.entries[source.0].hint;
        let centers = self.entries[source.0].centers.clone();
        let radius = self.entries[source.0].radius;
        let materials = Arc::clone(&self.entries[source.0].materials);
        let id = self.insert(copy);
        self.entries[id.0].hint = hint;
        self.entries[id.0].centers = centers;
        self.entries[id.0].radius = radius;
        self.entries[id.0].materials = materials;
        Some(id)
    }
    pub fn fragments(&mut self, id: MeshId) -> Option<FragmentMesh<'_>> {
        let mesh = self.entries.get_mut(id.0)?;
        mesh.version = mesh.version.wrapping_add(1);
        Some(FragmentMesh {
            geometry: mesh.geometry.as_mut()?,
            centers: &mut mesh.centers,
            radius: &mut mesh.radius,
        })
    }
    pub fn radius(&self, id: MeshId) -> Option<u16> {
        let mesh = self.entries.get(id.0)?;
        mesh.geometry.as_ref()?;
        Some(mesh.radius)
    }
    pub fn hint(&self, id: MeshId) -> Option<u16> {
        let mesh = self.entries.get(id.0)?;
        mesh.geometry.as_ref().map(|_| mesh.hint)
    }
    pub fn set_hint(&mut self, id: MeshId, hint: u16) -> Option<()> {
        let mesh = self.entries.get_mut(id.0)?;
        mesh.geometry.as_ref()?;
        mesh.hint = hint;
        Some(())
    }
    pub fn get(&self, id: MeshId) -> Option<&Geometry> {
        self.entries.get(id.0)?.geometry.as_ref()
    }
    pub fn get_mut(&mut self, id: MeshId) -> Option<&mut Geometry> {
        let mesh = self.entries.get_mut(id.0)?;
        mesh.version = mesh.version.wrapping_add(1);
        mesh.geometry.as_mut()
    }
    pub fn len(&self) -> usize {
        self.entries.len()
    }
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }
    pub fn info(&self, id: MeshId) -> Option<MeshInfo<'_>> {
        self.info_at(id.0)
    }
    pub fn info_at(&self, index: usize) -> Option<MeshInfo<'_>> {
        let mesh = self.entries.get(index)?;
        Some(MeshInfo {
            geometry: mesh.geometry.as_ref()?,
            materials: &mesh.materials,
            version: mesh.version,
        })
    }
    pub fn iter(&self) -> impl Iterator<Item = (MeshId, MeshInfo<'_>)> {
        self.entries
            .iter()
            .enumerate()
            .filter_map(|(i, _)| self.info(MeshId(i)).map(|m| (MeshId(i), m)))
    }
    /// Conservative render bookkeeping includes complete handle slots, Arc
    /// headers and shared material capacities. Geometry is counted separately.
    pub fn render_metadata_bytes(&self) -> usize {
        self.entries.capacity() * std::mem::size_of::<Mesh>()
            + self
                .entries
                .iter()
                .map(|mesh| {
                    if !mesh.shared {
                        return 2 * std::mem::size_of::<usize>();
                    }
                    2 * std::mem::size_of::<usize>()
                        + std::mem::size_of_val(&*mesh.materials)
                        + mesh
                            .materials
                            .iter()
                            .map(|m| {
                                m.name.capacity()
                                    + m.spellings.capacity() * std::mem::size_of::<String>()
                                    + m.spellings.iter().map(String::capacity).sum::<usize>()
                            })
                            .sum::<usize>()
                })
                .sum::<usize>()
    }
    /// Retire owned geometry without renumbering handles or invalidating aliases.
    /// Named/shared geometry is protected from class-local destruction.
    pub fn release_private(&mut self, id: MeshId) -> Option<()> {
        let mesh = self.entries.get_mut(id.0)?;
        if mesh.shared {
            return None;
        }
        mesh.geometry.take()?;
        mesh.centers = [None, None, None];
        mesh.materials = Arc::from([]);
        Some(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn shared_names_alias_but_private_copies_detach_at_the_current_geometry() {
        let model = Model {
            geometry: Geometry {
                vertices: vec![[0, 200, 0]],
                triangles: vec![],
            },
            materials: vec![],
            radius: 0,
            render_hint: 0,
            bounds: [[0; 3]; 2],
            vertex_normals: None,
            face_centers: [None, None, None],
        };
        let mut meshes = Meshes::default();
        let first = meshes.bind("car.sm", &model);
        meshes.get_mut(first).unwrap().vertices[0][1] = 193;
        let second = meshes.bind("CAR.SM", &model);
        assert_eq!(first, second);
        let private = meshes.private_copy(first).unwrap();
        assert_eq!(meshes.get(private).unwrap().vertices[0][1], 193);
        meshes.get_mut(first).unwrap().vertices[0][1] = 100;
        assert_eq!(meshes.get(second).unwrap().vertices[0][1], 100);
        assert_eq!(meshes.get(private).unwrap().vertices[0][1], 193);
        assert_eq!(model.geometry.vertices[0][1], 200);
    }
}
