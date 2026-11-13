//! Ordered tick membership, independent of render visibility and activity values.
use crate::{object_query::ObstacleId, spatial};

#[derive(Debug, Clone, Default)]
pub struct Ticks {
    pub flags: Vec<u32>,
    pub order: Vec<ObstacleId>,
    /// The next scheduled entity, not the one currently executing.
    pub next: Option<ObstacleId>,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    MissingEntity(ObstacleId),
    InvalidCursor,
    Spatial(spatial::Error),
}
impl Ticks {
    pub fn enable(&mut self, id: ObstacleId) -> Result<bool, Error> {
        let flags = self.flags.get_mut(id.0).ok_or(Error::MissingEntity(id))?;
        if *flags & 16 != 0 {
            return Ok(false);
        }
        self.order.push(id);
        *flags |= 16;
        Ok(true)
    }
    pub fn disable(&mut self, id: ObstacleId, limit: u32) -> Result<bool, Error> {
        if limit == 0 {
            return Err(Error::Spatial(spatial::Error::InvalidLimit));
        }
        let flags = self.flags.get_mut(id.0).ok_or(Error::MissingEntity(id))?;
        if *flags & 16 == 0 {
            return Ok(false);
        }
        if self.next == Some(id) {
            let i = self
                .order
                .iter()
                .position(|&v| v == id)
                .ok_or(Error::InvalidCursor)?;
            self.next = self.order.get(i + 1).copied();
        }
        spatial::remove_first(&mut self.order, &id, limit).map_err(Error::Spatial)?;
        *flags &= !48;
        Ok(true)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn enable_appends_once_and_disable_advances_next_before_a_possible_error() {
        let mut ticks = Ticks {
            flags: vec![32, 0],
            order: Vec::new(),
            next: None,
        };
        assert_eq!(ticks.enable(ObstacleId(1)), Ok(true));
        assert_eq!(ticks.enable(ObstacleId(0)), Ok(true));
        assert_eq!(ticks.enable(ObstacleId(0)), Ok(false));
        ticks.next = Some(ObstacleId(0));
        assert_eq!(
            ticks.disable(ObstacleId(0), 1),
            Err(Error::Spatial(spatial::Error::Limit))
        );
        assert_eq!(ticks.next, None);
        assert_eq!(ticks.flags[0], 48);
        assert_eq!(ticks.disable(ObstacleId(0), 2), Ok(true));
        assert_eq!(ticks.flags[0], 0);
        assert_eq!(ticks.order, [ObstacleId(1)]);
    }
}
