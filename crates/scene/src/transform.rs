//! Integer simulation coordinates cross into the renderer only here.
use crate::camera::WORLD_UNITS;
use glam::{Mat4, Vec3};

pub fn physics_position(position: [i32; 3]) -> Vec3 {
    Vec3::new(
        position[0] as f32,
        position[1] as f32,
        -(position[2] as f32),
    ) / (WORLD_UNITS * 8.)
}

/// Source matrix rows are local axes in world space (Q12). Reflect both local
/// and world Z, preserving the winding of right-handed renderer geometry.
pub fn physics_pose(position: [i32; 3], basis: [i32; 9]) -> Mat4 {
    let axis =
        |i: usize| Vec3::new(basis[i] as f32, basis[i + 1] as f32, -(basis[i + 2] as f32)) / 4096.;
    Mat4::from_cols(
        axis(0).extend(0.),
        axis(3).extend(0.),
        (-axis(6)).extend(0.),
        physics_position(position).extend(1.),
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn translated_quarter_turn_preserves_physics_axes_and_handedness() {
        let pose = physics_pose([1024, 2048, -3072], [0, 0, -4096, 0, 4096, 0, 4096, 0, 0]);
        assert_eq!(
            pose.transform_point3(Vec3::new(2., 3., -4.)),
            Vec3::new(5., 5., 5.)
        );
        assert_eq!(pose.determinant(), 1.);
    }
}
