//! Live tick membership and deferred stop/destruction, in original update order.
use crate::{
    object_query::{EntityId, ObstacleId},
    spatial,
};
use std::collections::VecDeque;

/// Entity IDs and dense component IDs use the same scheduler implementation.
/// The scene owner chooses the index domain; flags have only one owner.
pub trait Index: Copy + Eq {
    fn index(self) -> usize;
}
impl Index for EntityId {
    fn index(self) -> usize {
        self.0 as usize
    }
}
impl Index for ObstacleId {
    fn index(self) -> usize {
        self.0
    }
}

#[derive(Debug, Clone)]
pub struct Ticks<I = ObstacleId> {
    pub flags: Vec<u32>,
    pub order: Vec<I>,
    /// The next scheduled entity, not the one currently executing.
    pub next: Option<I>,
}
impl<I> Default for Ticks<I> {
    fn default() -> Self {
        Self {
            flags: Vec::new(),
            order: Vec::new(),
            next: None,
        }
    }
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error<I = ObstacleId> {
    MissingEntity(I),
    InvalidCursor,
    Spatial(spatial::Error),
    Method(i32),
}
impl<I: Index> Ticks<I> {
    pub fn enable(&mut self, id: I) -> Result<bool, Error<I>> {
        let flags = self
            .flags
            .get_mut(id.index())
            .ok_or(Error::MissingEntity(id))?;
        if *flags & 16 != 0 {
            return Ok(false);
        }
        self.order.push(id);
        *flags |= 16;
        Ok(true)
    }
    pub fn disable(&mut self, id: I, limit: u32) -> Result<bool, Error<I>> {
        if limit == 0 {
            return Err(Error::Spatial(spatial::Error::InvalidLimit));
        }
        let flags = self
            .flags
            .get_mut(id.index())
            .ok_or(Error::MissingEntity(id))?;
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

    /// Save the successor before dispatch, then follow the live cursor. Removing
    /// the next member advances it; appending behind the end sentinel waits for
    /// the next pass. Errors retain the cursor and all completed callback writes.
    pub fn run(
        &mut self,
        limit: u32,
        mut tick: impl FnMut(&mut Self, I) -> Result<(), i32>,
    ) -> Result<(), Error<I>> {
        valid_limit(limit)?;
        let mut current = self.order.first().copied();
        let mut work = 0;
        let mut position = 0;
        while let Some(id) = current {
            step(&mut work, limit)?;
            // Linear pass without callbacks changing membership. Only repair the
            // position when a callback removed/reordered members before it.
            let index = if self.order.get(position) == Some(&id) {
                position
            } else {
                self.order
                    .iter()
                    .position(|&v| v == id)
                    .ok_or(Error::InvalidCursor)?
            };
            position = index + 1;
            self.next = self.order.get(index + 1).copied();
            tick(self, id).map_err(Error::Method)?;
            current = self.next;
        }
        self.next = None;
        Ok(())
    }
}

/// Deferred lists share bit 32 in `Ticks::flags`, as in the source. A stop
/// request can therefore suppress a later destruction request. Lists are LIFO,
/// unique independently, and are not cleared until their own callbacks do so.
#[derive(Debug, Clone)]
pub struct Queues<I = ObstacleId> {
    pub notifications: VecDeque<I>,
    pub destruction: VecDeque<I>,
}
impl<I> Default for Queues<I> {
    fn default() -> Self {
        Self {
            notifications: VecDeque::new(),
            destruction: VecDeque::new(),
        }
    }
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Event<I = ObstacleId> {
    Stop(I),
    /// Run the class destructor and ID-release methods, in that order.
    Destroy(I),
}
impl<I: Index> Queues<I> {
    pub fn queue(
        &mut self,
        ticks: &mut Ticks<I>,
        id: I,
        destroy: bool,
        limit: u32,
    ) -> Result<(), Error<I>> {
        valid_limit(limit)?;
        let flags = ticks
            .flags
            .get_mut(id.index())
            .ok_or(Error::MissingEntity(id))?;
        if destroy && *flags & 32 != 0 {
            return Ok(());
        }
        *flags |= 32;
        let list = if destroy {
            &mut self.destruction
        } else {
            &mut self.notifications
        };
        // Contains visits the end sentinel too, including when the list is empty.
        let position = list.iter().position(|&v| v == id);
        if position.unwrap_or(list.len()) >= limit as usize {
            return Err(Error::Spatial(spatial::Error::Limit));
        }
        if position.is_none() {
            list.push_front(id);
        }
        Ok(())
    }

    pub fn cancel_notification(
        &mut self,
        ticks: &mut Ticks<I>,
        id: I,
        limit: u32,
    ) -> Result<(), Error<I>> {
        valid_limit(limit)?;
        *ticks
            .flags
            .get_mut(id.index())
            .ok_or(Error::MissingEntity(id))? &= !32;
        let position = self.notifications.iter().position(|&v| v == id);
        if position.unwrap_or(self.notifications.len()) >= limit as usize {
            return Err(Error::Spatial(spatial::Error::Limit));
        }
        if let Some(position) = position {
            self.notifications.remove(position);
        }
        Ok(())
    }

    /// Notifications finish before destruction starts. Notifications added by
    /// destruction callbacks wait for the next drain. Pop happens before dispatch;
    /// a failed method consumes its item. Empty terminal pops also spend budget.
    pub fn drain(
        &mut self,
        ticks: &mut Ticks<I>,
        limit: u32,
        mut invoke: impl FnMut(&mut Self, &mut Ticks<I>, Event<I>) -> Result<(), i32>,
    ) -> Result<(), Error<I>> {
        valid_limit(limit)?;
        let mut work = 0;
        for destroy in [false, true] {
            loop {
                step(&mut work, limit)?;
                let list = if destroy {
                    &mut self.destruction
                } else {
                    &mut self.notifications
                };
                let Some(id) = list.pop_front() else {
                    break;
                };
                invoke(self, ticks, Event::Stop(id)).map_err(Error::Method)?;
                if destroy {
                    invoke(self, ticks, Event::Destroy(id)).map_err(Error::Method)?;
                }
            }
        }
        Ok(())
    }
}
fn valid_limit<I>(limit: u32) -> Result<(), Error<I>> {
    if limit == 0 {
        Err(Error::Spatial(spatial::Error::InvalidLimit))
    } else {
        Ok(())
    }
}
fn step<I>(work: &mut u32, limit: u32) -> Result<(), Error<I>> {
    if *work >= limit {
        return Err(Error::Spatial(spatial::Error::Limit));
    }
    *work += 1;
    Ok(())
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

    #[test]
    fn callback_membership_changes_follow_the_saved_successor() {
        let mut ticks = Ticks::<EntityId> {
            flags: vec![0; 5],
            ..Ticks::default()
        };
        for id in [1, 2, 3] {
            ticks.enable(EntityId(id)).unwrap();
        }
        let mut visited = Vec::new();
        ticks
            .run(10, |ticks, id| {
                visited.push(id.0);
                if id == EntityId(1) {
                    ticks.disable(EntityId(2), 10).unwrap();
                }
                if id == EntityId(3) {
                    ticks.enable(EntityId(4)).unwrap();
                }
                Ok(())
            })
            .unwrap();
        assert_eq!(visited, [1, 3]);
        visited.clear();
        ticks
            .run(10, |_, id| {
                visited.push(id.0);
                Ok(())
            })
            .unwrap();
        assert_eq!(visited, [1, 3, 4]);
    }

    #[test]
    fn pending_stop_suppresses_destroy_and_destroy_cannot_reopen_the_stop_phase() {
        let mut ticks = Ticks::<EntityId> {
            flags: vec![0; 4],
            ..Ticks::default()
        };
        let mut queues = Queues::default();
        queues.queue(&mut ticks, EntityId(1), false, 10).unwrap();
        queues.queue(&mut ticks, EntityId(1), true, 10).unwrap();
        assert!(queues.destruction.is_empty());
        queues.queue(&mut ticks, EntityId(2), true, 10).unwrap();
        let mut trace = Vec::new();
        queues
            .drain(&mut ticks, 10, |queues, ticks, event| {
                trace.push(event);
                if event == Event::Destroy(EntityId(2)) {
                    queues.queue(ticks, EntityId(3), false, 10).unwrap();
                }
                Ok(())
            })
            .unwrap();
        assert_eq!(
            trace,
            [
                Event::Stop(EntityId(1)),
                Event::Stop(EntityId(2)),
                Event::Destroy(EntityId(2))
            ]
        );
        assert_eq!(queues.notifications, [EntityId(3)]);
        assert_eq!(ticks.flags[1], 32);
        assert_eq!(
            Queues::default().drain(&mut ticks, 1, |_, _, _| unreachable!()),
            Err(Error::Spatial(spatial::Error::Limit))
        );
    }
}
