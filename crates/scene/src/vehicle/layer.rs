//! Original 4F7530 gates and 4FD120 vertex-normal projection. The additive
//! contribution is fused with its base face, so normal depth/cutout stays exact.
use tankracer_assets::config::Part;
use tankracer_game::math::Matrix;
use tankracer_render::material::NormalLayer;

pub type NormalBanks = [Vec<[f32; 4]>; 5];

#[derive(Debug, Clone, Copy)]
pub struct Context {
    pub additive_capability: bool,
    pub player: bool,
    pub game_mode: i32,
    pub starting: bool,
    pub cockpit: bool,
    pub shield_timer: i32,
    pub invincible: bool,
    pub hide_aerial: bool,
}
impl Default for Context {
    fn default() -> Self {
        Self {
            additive_capability: true,
            player: true,
            game_mode: 0,
            starting: false,
            cockpit: false,
            shield_timer: 0,
            invincible: false,
            hide_aerial: false,
        }
    }
}
impl Context {
    pub fn mode(self, part: Part) -> u32 {
        if !self.additive_capability
            || !self.player
            || self.cockpit
            || matches!(self.game_mode, 2 | 3)
            || !eligible(part)
            || (part != Part::End && (self.shield_timer > 0 || self.invincible))
        {
            0
        } else if self.starting {
            2
        } else {
            1
        }
    }
}
pub fn eligible(part: Part) -> bool {
    matches!(part, Part::Body | Part::Turret | Part::End)
}

/// Model rows dotted with view rows; source division truncates toward zero.
pub fn matrix(model: Matrix, view: Matrix, mode: u32) -> Matrix {
    if mode != 2 {
        return model;
    }
    Matrix(std::array::from_fn(|index| {
        let (row, column) = (index / 3, index % 3);
        let sum = (0..3).fold(0_i32, |sum, k| {
            sum.wrapping_add(model.0[row * 3 + k].wrapping_mul(view.0[column * 3 + k]))
        });
        sum / 4096
    }))
}
/// CPU oracle for the identical integer expression in the vertex shader.
pub fn uv(normal: [i16; 3], matrix: Matrix) -> [i32; 2] {
    std::array::from_fn(|i| {
        let sum = (0..3).fold(0_i32, |sum, j| {
            sum.wrapping_add(i32::from(normal[j]).wrapping_mul(matrix.0[i * 6 + j]))
        });
        (sum >> 9).wrapping_add(32768)
    })
}
pub fn material(
    model: Matrix,
    view: Matrix,
    mode: u32,
    atlas: [f32; 4],
    shaded: bool,
) -> Option<NormalLayer> {
    if mode == 0 {
        return None;
    }
    let m = matrix(model, view, mode).0;
    Some(NormalLayer {
        rows: [[m[0], m[1], m[2]], [m[6], m[7], m[8]]],
        atlas,
        gain: if shaded { 32. } else { 128. } / 255.,
    })
}
