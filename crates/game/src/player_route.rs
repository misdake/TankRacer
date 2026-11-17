//! Player route binding at race startup (frame_player_routes_initialize).
//! Checkpoint crossing and lap events are not yet part of the driving tick.
use crate::{object_query::EntityId, waypoint};

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct State {
    pub checkpoint: Option<EntityId>,
    pub passed: u32,
}
impl State {
    pub(crate) fn at_start(
        routes: &waypoint::View<'_>,
        position: [i32; 3],
        work_limit: u32,
    ) -> Result<Self, i32> {
        Ok(Self {
            checkpoint: routes.nearest(position, work_limit)?,
            passed: 0,
        })
    }
}
