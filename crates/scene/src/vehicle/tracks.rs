//! Visual-only travel (434B70) and the authored UV pointer tables (434C60).
//! Advance once after a successful fixed tick; redraws only sample the phase.
use crate::model::UvRow;
use tankracer_assets::config::Part;
use tankracer_game::vehicle_model::TrackUvs;

pub fn side(part: Part) -> Option<usize> {
    match part {
        Part::LeftTrack => Some(0),
        Part::RightTrack => Some(1),
        _ => None,
    }
}

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
        // 434BC0's sign/magnitude shift, including 32-bit overflow/INT_MIN.
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

pub(super) fn rows(table: &TrackUvs) -> Vec<UvRow> {
    table
        .groups
        .iter()
        .enumerate()
        .flat_map(|(group, refs)| {
            refs.iter().map(move |r| UvRow {
                triangle: usize::from(r.triangle),
                corner: usize::from(r.corner),
                base: if group < 2 {
                    if group == 0 { 0 } else { 31 }
                } else {
                    r.snapped_v
                },
                channel: usize::from(group >= 2),
            })
        })
        .collect()
}
