//! Class-owned component handles in the game's one identity/scheduling table.
use crate::object_query::{ObstacleId, TankId};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Handle {
    Obstacle(ObstacleId),
    Vehicle(TankId),
}
impl Handle {
    pub fn obstacle(self) -> Option<ObstacleId> {
        match self {
            Self::Obstacle(id) => Some(id),
            _ => None,
        }
    }
    pub fn vehicle(self) -> Option<TankId> {
        match self {
            Self::Vehicle(id) => Some(id),
            _ => None,
        }
    }
}
