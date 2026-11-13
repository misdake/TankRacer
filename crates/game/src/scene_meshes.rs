//! Mutable scene geometry is addressed by stable handles. Shared shape bindings
//! and explicit private copies preserve original ownership without RefCell or
//! frame-by-frame vertex copies. Renderers borrow this same store read-only.
use std::collections::HashMap;
use tankracer_assets::{mesh::Geometry, model::Model};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MeshId(usize);

struct Mesh {
    geometry: Option<Geometry>,
    hint: u16,
    shared: bool,
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
        self.entries[id.0].shared = true;
        self.names.insert(key, id);
        id
    }
    /// Clone the *current* geometry, including any earlier shared deformation.
    pub fn private_copy(&mut self, source: MeshId) -> Option<MeshId> {
        let copy = self.get(source)?.clone();
        let hint = self.entries[source.0].hint;
        let id = self.insert(copy);
        self.entries[id.0].hint = hint;
        Some(id)
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
        self.entries.get_mut(id.0)?.geometry.as_mut()
    }
    /// Retire owned geometry without renumbering handles or invalidating aliases.
    /// Named/shared geometry is protected from class-local destruction.
    pub fn release_private(&mut self, id: MeshId) -> Option<()> {
        let mesh = self.entries.get_mut(id.0)?;
        if mesh.shared {
            return None;
        }
        mesh.geometry.take()?;
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
