//! Fixed-update visual travel (434B70). No GPU, frame rate or draw dependency.
#[derive(Default, Debug, Clone, Copy, PartialEq, Eq)]
pub struct Tracks {
    pub travel: [u32; 2],
}
impl Tracks {
    pub fn advance(&mut self, speed: i32, yaw: i32, locked: bool) {
        if locked {
            return;
        }
        let speed = speed.clamp(-300000, 300000);
        let product = yaw.wrapping_mul(400);
        let turn = if product < 0 {
            product.wrapping_neg().wrapping_shr(10).wrapping_neg()
        } else {
            product >> 10
        };
        self.travel[0] = self.travel[0].wrapping_add(speed.wrapping_sub(turn) as u32);
        self.travel[1] = self.travel[1].wrapping_add(speed.wrapping_add(turn) as u32);
    }
    pub fn phase(&self, side: usize) -> [f32; 2] {
        let phase = self.travel[side] >> 17;
        [(phase & 7) as f32, (phase % 5) as f32]
    }
}
