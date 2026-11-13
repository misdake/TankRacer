//! Entity identity and record ownership, without a guest memory layout.
//! IDs deliberately have no generation: original references observe ID reuse.
use crate::{dispatch::ClassId, object_query::EntityId};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Record<T> {
    pub class: Option<ClassId>,
    /// A handle into a class-owned component store, never a WebGL/resource owner.
    pub object: Option<T>,
}
#[derive(Debug, Clone, Copy)]
struct Slot<T> {
    record: Option<Record<T>>,
    next_free: EntityId,
}
impl<T> Default for Slot<T> {
    fn default() -> Self {
        Self {
            record: None,
            next_free: EntityId(0),
        }
    }
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    Capacity,
    MissingEntity(EntityId),
    InvalidLimit,
    Limit,
}
#[derive(Debug, Clone)]
pub struct Entities<T> {
    slots: Vec<Slot<T>>,
    free_head: EntityId,
    high_water: EntityId,
    records: u32,
    /// Forced IDs advance before allocation/initialization, including failures.
    pub sequential: bool,
    pub next_requested: EntityId,
}
impl<T: Copy> Entities<T> {
    pub fn new(capacity: u32) -> Result<Self, Error> {
        let mut result = Self {
            slots: Vec::new(),
            free_head: EntityId(0),
            high_water: EntityId(0),
            records: 0,
            sequential: false,
            next_requested: EntityId(0),
        };
        result.reset(capacity)?;
        Ok(result)
    }
    pub fn capacity(&self) -> u32 {
        self.slots.len() as u32
    }
    pub fn record_count(&self) -> u32 {
        self.records
    }
    pub fn free_head(&self) -> EntityId {
        self.free_head
    }
    pub fn high_water(&self) -> EntityId {
        self.high_water
    }
    pub fn record(&self, id: EntityId) -> Option<&Record<T>> {
        self.slots.get(id.0 as usize)?.record.as_ref()
    }
    pub fn record_mut(&mut self, id: EntityId) -> Option<&mut Record<T>> {
        self.slots.get_mut(id.0 as usize)?.record.as_mut()
    }
    pub fn next_free(&self, id: EntityId) -> Option<EntityId> {
        self.slots.get(id.0 as usize).map(|v| v.next_free)
    }
    /// Startup resets free links/counts. Reinitializing at the same capacity
    /// retains existing records; session teardown must release them beforehand.
    pub fn reset(&mut self, capacity: u32) -> Result<(), Error> {
        if !(2..=i32::MAX as u32).contains(&capacity) {
            return Err(Error::Capacity);
        }
        if self.capacity() != capacity {
            self.slots = vec![Slot::default(); capacity as usize];
        }
        for id in 1..capacity {
            self.slots[id as usize].next_free =
                EntityId(if id + 1 < capacity { id + 1 } else { 0 });
        }
        self.records = 0;
        self.free_head = EntityId(1);
        self.high_water = EntityId(1);
        Ok(())
    }
    /// Preserve the source's 128-slot growth policy, including unreachable new
    /// free slots when growth was explicitly requested with an existing chain.
    pub fn grow(&mut self) -> Result<(), Error> {
        let old = self.capacity();
        let new = old
            .checked_add(128)
            .filter(|&v| v <= i32::MAX as u32)
            .ok_or(Error::Capacity)?;
        self.slots.resize(new as usize, Slot::default());
        for id in old..new {
            self.slots[id as usize].next_free = EntityId(if id + 1 < new { id + 1 } else { 0 });
        }
        if self.free_head.0 == 0 {
            self.free_head = EntityId(old);
            self.high_water = EntityId(old);
        }
        Ok(())
    }
    pub fn unlink_free(&mut self, id: EntityId, limit: u32) -> Result<EntityId, Error> {
        if limit == 0 {
            return Err(Error::InvalidLimit);
        }
        if id.0 >= self.capacity() {
            return Err(Error::MissingEntity(id));
        }
        let mut current = self.free_head;
        if current == id {
            self.free_head = self.slots[current.0 as usize].next_free;
            return Ok(id);
        }
        let mut next = self.slots[current.0 as usize].next_free;
        let mut work = 0;
        while next.0 != 0 && next != id {
            if work >= limit {
                return Err(Error::Limit);
            }
            work += 1;
            current = next;
            next = self.slots[next.0 as usize].next_free;
        }
        if next.0 != 0 || id.0 == 0 {
            self.slots[current.0 as usize].next_free = self.slots[next.0 as usize].next_free;
        }
        Ok(if next.0 == 0 { EntityId(0) } else { id })
    }
    /// Allocate a blank classless record. A requested occupied ID overwrites its
    /// binding and still increases record_count, exactly as the original. Handles
    /// are Copy so this never implicitly destroys class-owned components.
    pub fn new_record(&mut self, requested: EntityId, limit: u32) -> Result<EntityId, Error> {
        if limit == 0 {
            return Err(Error::InvalidLimit);
        }
        if requested.0 > i32::MAX as u32 {
            return Err(Error::Capacity);
        }
        let id = if requested.0 == 0 {
            if self.free_head.0 == 0 {
                self.grow()?;
            }
            let id = self.free_head;
            self.free_head = self.slots[id.0 as usize].next_free;
            id
        } else {
            let mut work = 0;
            while requested.0 >= self.capacity() {
                if work >= limit {
                    return Err(Error::Limit);
                }
                work += 1;
                self.grow()?;
            }
            if self.record(requested).is_none() {
                self.unlink_free(requested, limit)?;
            }
            requested
        };
        self.slots[id.0 as usize].record = Some(Record {
            class: None,
            object: None,
        });
        if self.high_water.0 < id.0 {
            self.high_water = id;
        }
        self.records = self.records.wrapping_add(1);
        Ok(id)
    }
    pub fn create_record(&mut self, limit: u32) -> Result<EntityId, Error> {
        let requested = if self.sequential {
            self.next_requested
        } else {
            EntityId(0)
        };
        if self.sequential {
            self.next_requested.0 = self.next_requested.0.wrapping_add(1);
        }
        self.new_record(requested, limit)
    }
    /// Base ID-release method. Record accounting belongs to the enclosing
    /// destructor, after both class methods succeed, rather than this free link.
    pub fn release_id(&mut self, id: EntityId) -> Result<(), Error> {
        self.detach(id)?;
        self.slots[id.0 as usize].next_free = self.free_head;
        self.free_head = id;
        Ok(())
    }
    pub fn detach(&mut self, id: EntityId) -> Result<(), Error> {
        self.slots
            .get_mut(id.0 as usize)
            .ok_or(Error::MissingEntity(id))?
            .record = None;
        Ok(())
    }
    pub(crate) fn finish_destroy(&mut self) {
        self.records = self.records.wrapping_sub(1);
    }
}
