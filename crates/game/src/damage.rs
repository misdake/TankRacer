//! One-shot body/turret dents, track wear and exact integer face-normal repair.
use crate::{pose::Pose, vector};
use tankracer_assets::mesh::Geometry;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Part {
    Body,
    Turret,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    Vertex(usize),
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct State {
    pub body: [u8; 4],
    pub turret: [u8; 4],
    pub tracks: [u8; 2],
}

/// One vehicle's writable damage binding. Player geometry is instance-owned;
/// non-player track wear does not require a writable copy of the body mesh.
pub struct Target<'a> {
    pub state: &'a mut State,
    pub player: bool,
    pub body_pose: Pose,
    pub body_vertices: [u8; 4],
    pub body_mesh: Option<&'a mut Geometry>,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TargetError {
    MissingBody,
    Mesh(Error),
}
impl Target<'_> {
    pub fn track(&mut self, kind: i32, world: [i32; 3]) -> Result<(), TargetError> {
        if self.state.hit_track(kind) && self.player {
            let q = quadrant(self.body_pose, world);
            if self.state.body[q] != 0 {
                return Ok(());
            }
            let Some(mesh) = self.body_mesh.as_deref_mut() else {
                // Wear and the one-shot flag precede the original mesh read.
                self.state.body[q] = 1;
                return Err(TargetError::MissingBody);
            };
            self.state
                .hit(Part::Body, self.body_pose, world, self.body_vertices, mesh)
                .map_err(TargetError::Mesh)?;
        }
        Ok(())
    }
}

pub fn quadrant(pose: Pose, world: [i32; 3]) -> usize {
    let local = pose.matrix.world_to_local(std::array::from_fn(|i| {
        world[i].wrapping_sub(pose.position[i])
    }));
    usize::from(local[2] <= 0) * 2 + usize::from(local[0] > 0)
}

/// Repair only faces incident to this vertex. Material, UV and other faces stay
/// unchanged; optional imported vertex normals are not updated by this stage.
pub fn repair_normals(mesh: &mut Geometry, vertex: u8) -> Result<(), Error> {
    for face in &mut mesh.triangles {
        if !face.indices.contains(&vertex) {
            continue;
        }
        let mut p = [[0_i32; 3]; 3];
        for (point, index) in p.iter_mut().zip(face.indices) {
            *point = mesh
                .vertices
                .get(usize::from(index))
                .ok_or(Error::Vertex(usize::from(index)))?
                .map(i32::from);
        }
        let mut a = std::array::from_fn(|i| p[2][i].wrapping_sub(p[0][i]));
        let mut b = std::array::from_fn(|i| p[1][i].wrapping_sub(p[0][i]));
        vector::normalize(&mut a);
        vector::normalize(&mut b);
        let mut normal = vector::cross(a, b);
        vector::normalize(&mut normal);
        face.normal = normal.map(|v| v as i16);
    }
    Ok(())
}

impl State {
    /// A selected quadrant can dent once, even when several quadrants select
    /// the same vertex. Its flag commits before the mesh is accessed.
    pub fn hit(
        &mut self,
        part: Part,
        pose: Pose,
        world: [i32; 3],
        selected: [u8; 4],
        mesh: &mut Geometry,
    ) -> Result<bool, Error> {
        let q = quadrant(pose, world);
        let flags = match part {
            Part::Body => &mut self.body,
            Part::Turret => &mut self.turret,
        };
        if flags[q] != 0 {
            return Ok(false);
        }
        flags[q] = 1;
        let vertex = selected[q];
        let position = mesh
            .vertices
            .get_mut(usize::from(vertex))
            .ok_or(Error::Vertex(usize::from(vertex)))?;
        let gains = match part {
            Part::Body => [3500, 4096, 2500],
            Part::Turret => [3000, if q & 2 != 0 { 1200 } else { 2200 }, 3900],
        };
        for (i, gain) in gains.into_iter().enumerate() {
            if part == Part::Body && i == 1 {
                continue;
            }
            position[i] = (i32::from(position[i]).wrapping_mul(gain) >> 12) as i16;
        }
        repair_normals(mesh, vertex)?;
        Ok(true)
    }

    /// Return whether wear increased. The caller then dents the body for a
    /// player-controlled tank; saturation skips that body hit too. Kind 2 is
    /// the first track; every other kind selects the second, as in the source.
    pub fn hit_track(&mut self, kind: i32) -> bool {
        let wear = &mut self.tracks[usize::from(kind != 2)];
        if *wear >= 10 {
            return false;
        }
        *wear += 1;
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn missing_player_body_commits_wear_and_flag_but_nonplayer_needs_no_mesh() {
        let mut state = State::default();
        let mut target = Target {
            state: &mut state,
            player: true,
            body_pose: Pose::default(),
            body_vertices: [0; 4],
            body_mesh: None,
        };
        assert_eq!(target.track(2, [0; 3]), Err(TargetError::MissingBody));
        assert_eq!(target.state.body, [0, 0, 1, 0]);
        assert_eq!(target.state.tracks, [1, 0]);
        assert!(target.track(2, [0; 3]).is_ok()); // Already dented: no mesh read.
        target.player = false;
        assert!(target.track(3, [1; 3]).is_ok());
        assert_eq!(target.state.tracks, [2, 1]);
        assert_eq!(target.state.body, [0, 0, 1, 0]);
    }

    #[test]
    fn quadrant_boundaries_duplicate_vertices_and_one_shot_flags_are_distinct() {
        let pose = Pose::default();
        for (world, expected) in [
            ([0, 0, 0], 2),
            ([0, 0, 1], 0),
            ([1, 0, 1], 1),
            ([1, 0, 0], 3),
        ] {
            assert_eq!(quadrant(pose, world), expected);
        }
        let mut mesh = Geometry {
            vertices: vec![[4096, -17, -4096]],
            triangles: Vec::new(),
        };
        let mut state = State::default();
        assert!(
            state
                .hit(Part::Body, pose, [1, 0, 1], [0; 4], &mut mesh)
                .unwrap()
        );
        let first = mesh.vertices[0];
        assert!(
            !state
                .hit(Part::Body, pose, [1, 0, 1], [0; 4], &mut mesh)
                .unwrap()
        );
        assert_eq!(mesh.vertices[0], first);
        assert!(
            state
                .hit(Part::Body, pose, [-1, 0, 1], [0; 4], &mut mesh)
                .unwrap()
        );
        assert_ne!(mesh.vertices[0], first);
        assert_eq!(mesh.vertices[0][1], -17);
        assert_eq!(state.body, [1, 1, 0, 0]);
    }

    #[test]
    fn wear_saturates_and_failed_geometry_keeps_the_committed_quadrant() {
        let mut state = State::default();
        for _ in 0..10 {
            assert!(state.hit_track(2));
        }
        assert!(!state.hit_track(2));
        assert!(state.hit_track(-1));
        assert_eq!(state.tracks, [10, 1]);
        let mut mesh = Geometry {
            vertices: Vec::new(),
            triangles: Vec::new(),
        };
        assert_eq!(
            state.hit(Part::Turret, Pose::default(), [0; 3], [7; 4], &mut mesh),
            Err(Error::Vertex(7))
        );
        assert_eq!(state.turret, [0, 0, 1, 0]);
        assert_eq!(
            state.hit(Part::Turret, Pose::default(), [0; 3], [7; 4], &mut mesh),
            Ok(false)
        );
    }
}
