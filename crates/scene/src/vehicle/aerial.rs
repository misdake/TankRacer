//! 433D10 case7: four copies of one authored mesh, a turret socket and decaying
//! hull lean. Extract poses only; never advance the physical springs here.
use tankracer_game::{math::Axis, pose::Pose, vehicle_startup::Player};

pub fn poses(player: &Player) -> [Pose; 4] {
    let motion = &player.physics.motion;
    let turret = motion.poses.turret;
    let offset = turret
        .matrix
        .local_to_world(player.model.offsets[tankracer_assets::config::Part::Aerial.slot()]);
    let position = std::array::from_fn(|i| turret.position[i].wrapping_add(offset[i]));
    chain(
        Pose {
            position,
            matrix: motion.poses.body.matrix,
        },
        player.model.aerial_height,
        motion.lean.longitudinal.position,
        motion.lean.lateral.position,
    )
}
pub fn chain(mut pose: Pose, height: i32, mut pitch: i32, mut roll: i32) -> [Pose; 4] {
    std::array::from_fn(|_| {
        pose.matrix.rotate(Axis::X, pitch as u32);
        pose.matrix.rotate(Axis::Z, roll as u32);
        let current = pose;
        pitch = pitch.wrapping_mul(900) / 1024;
        roll = roll.wrapping_mul(900) / 1024;
        for i in 0..3 {
            pose.position[i] =
                pose.position[i].wrapping_add(height.wrapping_mul(pose.matrix.0[3 + i]) >> 12);
        }
        current
    })
}

pub struct Aerial {
    pub geometry: crate::model::Prepared,
    pub poses: [Pose; 4],
}
impl Aerial {
    pub fn resident_bytes(&self) -> usize {
        std::mem::size_of::<Self>()
            + self
                .geometry
                .batches
                .iter()
                .map(|b| {
                    b.vertices.capacity()
                        * std::mem::size_of::<tankracer_render::geometry::Vertex>()
                        + b.indices.capacity() * 4
                })
                .sum::<usize>()
            + self.geometry.transparent.capacity()
                * std::mem::size_of::<crate::world::TransparentTriangle>()
            + self
                .geometry
                .uv_scroll
                .iter()
                .map(|v| v.capacity() * 16)
                .sum::<usize>()
    }
}
