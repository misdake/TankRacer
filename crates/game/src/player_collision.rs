//! Player collision indication/impact decay, before parent Tank dispatch.
use crate::vehicle::CollisionState;

/// Original dominant-component estimate, including unsigned abs comparisons.
/// X requires strict dominance; an X/Y tie selects Y when Z is lower. Remaining
/// dominant ties select Z, preserving the source comparison order.
pub use crate::geometry::approximate_distance as change_magnitude;
pub fn step(state: &mut CollisionState, dt: i32, race_active: bool) {
    if !race_active {
        return;
    }
    let mut marker = state.collision_size as i32;
    if marker != 0 {
        marker = marker.wrapping_sub(dt).max(0);
    }
    for change in [
        change_magnitude(state.ground_delta),
        change_magnitude(state.tank_delta),
        state.obstacle_impulse,
    ] {
        if change > 30000 {
            marker = marker.wrapping_add(change.wrapping_sub(30000) >> 11);
        }
    }
    state.collision_size = marker.min(1250) as u32;
    let impact = state.impact_accumulator;
    if impact != 0 {
        state.impact_accumulator = if impact < dt {
            0
        } else {
            impact.wrapping_sub(dt)
        };
    }
}
