use glam::{Mat4, Vec3};

pub const WORLD_UNITS: f32 = 128.;

/// BSM uses the simulation's published /8 units. Reflect Z once at the boundary:
/// (X,Y,Z)/128 -> (X,Y,-Z)/128, with unchanged physics coordinates.
pub fn from_world(position: [i32; 3]) -> Vec3 {
    Vec3::new(
        position[0] as f32,
        position[1] as f32,
        -(position[2] as f32),
    ) / WORLD_UNITS
}

#[derive(Debug)]
pub struct Camera {
    pub position: Vec3,
    yaw: f32,
    pitch: f32,
}

impl Camera {
    pub fn looking_at(position: Vec3, target: Vec3) -> Self {
        let forward = (target - position).normalize_or_zero();
        Self {
            position,
            yaw: forward.x.atan2(-forward.z),
            pitch: forward.y.asin(),
        }
    }
    pub fn forward(&self) -> Vec3 {
        Vec3::new(
            self.yaw.sin() * self.pitch.cos(),
            self.pitch.sin(),
            -self.yaw.cos() * self.pitch.cos(),
        )
    }
    pub fn right(&self) -> Vec3 {
        Vec3::new(self.yaw.cos(), 0., self.yaw.sin())
    }
    pub fn view_projection(&self, aspect: f32) -> Mat4 {
        // Original focal length 512 at 480 high; far distance here is expanded
        // for map inspection, not a change to any gameplay camera or visibility.
        let fov = 2. * (480_f32 / 1024.).atan();
        Mat4::perspective_rh_gl(fov, aspect, 0.1, 4000.)
            * Mat4::look_to_rh(self.position, self.forward(), Vec3::Y)
    }
    pub fn look(&mut self, dx: f32, dy: f32) {
        self.yaw += dx * 0.003;
        self.pitch = (self.pitch - dy * 0.003).clamp(-1.5, 1.5);
    }
    pub fn move_local(&mut self, direction: Vec3, seconds: f32, fast: bool) {
        let speed = if fast { 180. } else { 45. };
        let movement =
            self.right() * direction.x + Vec3::Y * direction.y + self.forward() * direction.z;
        self.position += movement.normalize_or_zero() * seconds.clamp(0., 0.1) * speed;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn world_point_in_front_of_camera_projects_to_center() {
        let camera = Camera::looking_at(from_world([128, 256, 384]), from_world([128, 256, 512]));
        let projected = camera.view_projection(4. / 3.) * from_world([128, 256, 512]).extend(1.);
        assert!(projected.x.abs() < 1e-5 && projected.y.abs() < 1e-5 && projected.w > 0.);
        assert!((camera.right() - Vec3::X).length() < 1e-5);
    }
}
