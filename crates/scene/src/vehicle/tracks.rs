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

pub use tankracer_game::vehicle_animation::Tracks;

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
                period_scale: if group < 2 { 32. } else { 2. },
            })
        })
        .collect()
}
