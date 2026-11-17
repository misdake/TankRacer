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
    basis: Option<Mat4>,
}

impl Camera {
    pub fn looking_at(position: Vec3, target: Vec3) -> Self {
        let forward = (target - position).normalize_or_zero();
        Self {
            position,
            yaw: forward.x.atan2(-forward.z),
            pitch: forward.y.asin(),
            basis: None,
        }
    }
    pub fn forward(&self) -> Vec3 {
        if let Some(basis) = self.basis {
            return -basis.z_axis.truncate().normalize_or_zero();
        }
        Vec3::new(
            self.yaw.sin() * self.pitch.cos(),
            self.pitch.sin(),
            -self.yaw.cos() * self.pitch.cos(),
        )
    }
    pub fn right(&self) -> Vec3 {
        if let Some(basis) = self.basis {
            return basis.x_axis.truncate().normalize_or_zero();
        }
        Vec3::new(self.yaw.cos(), 0., self.yaw.sin())
    }
    pub fn view_projection(&self, aspect: f32) -> Mat4 {
        // Original focal length 512 at 480 high; far distance here is expanded
        // for map inspection, not a change to any gameplay camera or visibility.
        let fov = 2. * (480_f32 / 1024.).atan();
        let view = if let Some(basis) = self.basis {
            basis.transpose() * Mat4::from_translation(-self.position)
        } else {
            Mat4::look_to_rh(self.position, self.forward(), Vec3::Y)
        };
        Mat4::perspective_rh_gl(fov, aspect, 0.1, 4000.) * view
    }
    pub fn from_chase(state: &tankracer_game::chase_camera::State) -> Self {
        let basis = crate::transform::physics_pose([0; 3], state.matrix.0);
        let position = from_world(state.published_position());
        let mut camera = Self::looking_at(position, position - basis.z_axis.truncate());
        camera.basis = Some(basis);
        camera
    }
    pub fn up(&self) -> Vec3 {
        self.right().cross(self.forward()).normalize_or_zero()
    }
    pub fn look(&mut self, dx: f32, dy: f32) {
        self.basis = None;
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

    #[test]
    fn chase_basis_keeps_roll_and_published_integer_position() {
        use tankracer_game::{
            chase_camera::{Config, State, Subject},
            math::Matrix,
        };
        let mut state = State::new(
            &Config::default(),
            Subject {
                base: [0; 3],
                physical: [0; 3],
                basis: Matrix::IDENTITY,
            },
        )
        .unwrap();
        state.position = [12345, -23457, 34567];
        state.matrix = Matrix::from_angles([713, 3901, 8190]);
        let camera = Camera::from_chase(&state);
        assert_eq!(camera.position, from_world([1543, -2933, 4320]));
        let right = Vec3::new(
            state.matrix.0[0] as f32,
            state.matrix.0[1] as f32,
            -(state.matrix.0[2] as f32),
        )
        .normalize();
        assert!((camera.right() - right).length() < 1e-6);
        assert!(
            camera.right().y.abs() > 0.5,
            "roll must survive the render adapter"
        );
        let point = camera.position + camera.forward() * 10.;
        let clip = camera.view_projection(4. / 3.) * point.extend(1.);
        assert!((clip.x / clip.w).abs() < 0.001 && (clip.y / clip.w).abs() < 0.001 && clip.w > 0.);
    }
}
