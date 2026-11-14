//! Authored WayPoint ownership, ordered grid and lap-circuit linkage.
//! Positions/matrices stay in the common scene; references use its symbols/IDs.
mod queries;
use crate::{
    entities::Entities,
    object_query::{Collider, EntityId, ObstacleId},
    scene_entity::Handle,
    symbols::{Binding, Symbols},
};
pub use queries::{Checkpoint, View};
use tankracer_assets::{
    parameters::{Parameters, fixed1024},
    terrain::TerrainData,
};

#[derive(Debug, Clone)]
pub struct Point {
    pub next: [Binding; 2],
    pub lap: u8,
    pub hairpin: u8,
    pub braking_range: u16,
    pub gate_size: i32,
    pub width: i32,
    pub max_speed: i32,
    pub progress: i32,
    pub weather: [u8; 3],
    pub strength: [u8; 3],
    pub activate_lap: u8,
    pub check: u8,
}
impl Default for Point {
    fn default() -> Self {
        Self {
            next: [Binding::Direct(None); 2],
            lap: 0,
            hairpin: 0,
            braking_range: 0,
            gate_size: 0,
            width: 0,
            max_speed: 0,
            progress: 0,
            weather: [0; 3],
            strength: [0; 3],
            activate_lap: 0,
            check: 0,
        }
    }
}
impl Point {
    pub fn configure(
        &mut self,
        p: Parameters<'_>,
        symbols: &mut Symbols,
        limit: u32,
    ) -> Result<(), i32> {
        if limit == 0 {
            return Err(-1);
        }
        if let Some(mut names) = p.find(":next") {
            let mut i = 0;
            while let Some(name) = names.word() {
                if name.starts_with([')', ':']) {
                    break;
                }
                if i >= limit as usize {
                    return Err(-3);
                }
                // More links overwrite unrelated packed fields in the original.
                // This malformed record has no supported typed interpretation.
                let slot = self.next.get_mut(i).ok_or(-4)?;
                *slot = Binding::Named(symbols.intern(name).map_err(|_| -1)?);
                i += 1;
            }
        }
        self.weather = [0; 3];
        self.strength = [0; 3];
        for (i, key) in [":weather_1", ":weather_2", ":weather_3"]
            .into_iter()
            .enumerate()
        {
            if let Some(mut v) = p.find(key) {
                let kind = v.token_word().unwrap_or("");
                let strength = v.token_word().unwrap_or("");
                self.weather[i] = match kind
                    .get(..kind.len().min(63))
                    .unwrap()
                    .to_ascii_lowercase()
                    .as_str()
                {
                    "rain" => 2,
                    "snow" => 3,
                    "lightning" => 4,
                    "rain_and_lightning" => 5,
                    "off" => 1,
                    _ => 0,
                };
                self.strength[i] = match strength
                    .get(..strength.len().min(63))
                    .unwrap()
                    .to_ascii_lowercase()
                    .as_str()
                {
                    "light" => 8,
                    "medium" => 16,
                    "heavy" => 32,
                    _ => 0,
                };
            }
        }
        if p.find(":lap").is_some() {
            self.lap = 1;
        }
        if p.find(":hairpin").is_some() {
            self.hairpin = 1;
        }
        self.width = if let Some(mut v) = p.find(":width") {
            v.integer().unwrap_or(self.width).wrapping_shl(10)
        } else {
            20480
        };
        let mut gate_bits = None;
        if let Some(mut v) = p.find(":gate_size") {
            let gate = v.real32().ok_or(-4)?;
            gate_bits = Some(gate.to_bits());
            // Truncate whole units BEFORE converting to Q10, unlike positions.
            self.gate_size = fixed1024(gate / 1024.).wrapping_shl(10);
        }
        self.max_speed = if let Some(mut v) = p.find(":max_speed") {
            v.integer().unwrap_or(self.max_speed)
        } else {
            1000000
        };
        if let Some(mut v) = p.find(":braking_range") {
            self.braking_range = (v.integer().ok_or(-4)? as u16).wrapping_shl(10);
        }
        self.activate_lap = if let Some(mut v) = p.find(":lap_to_activate") {
            (v.integer().ok_or(-4)? as u8).wrapping_sub(1)
        } else {
            255
        };
        if let Some(mut v) = p.find(":check") {
            self.check = v.integer().map(|v| v as u32).or(gate_bits).ok_or(-4)? as u8;
        }
        Ok(())
    }
}
pub struct Routes {
    initialized: bool,
    pub points: Vec<Option<Point>>,
    pub order: Vec<ObstacleId>,
    pub cells: [Vec<ObstacleId>; 64],
    pub origin: [i32; 2],
    pub shifts: [u32; 2],
    pub radius: [i32; 2],
    pub backwards_check: Option<EntityId>,
    pub fallback: Option<EntityId>,
    pub backwards_index: i32,
    pub lap_start: Option<EntityId>,
    pub weather: [[u8; 64]; 3],
}
impl Default for Routes {
    fn default() -> Self {
        Self {
            initialized: false,
            points: vec![],
            order: vec![],
            cells: std::array::from_fn(|_| vec![]),
            origin: [0; 2],
            shifts: [0; 2],
            radius: [1; 2],
            backwards_check: None,
            fallback: None,
            backwards_index: 0,
            lap_start: None,
            weather: [[0; 64]; 3],
        }
    }
}
impl Routes {
    pub fn initialize_grid(&mut self, terrain: &TerrainData, limit: u32) -> Result<(), i32> {
        let mut work = Work::new(limit)?;
        self.origin = terrain.origin.map(|v| v.wrapping_mul(8));
        self.shifts = terrain.shifts.map(|v| u32::from(v) + 3);
        for i in 0..2 {
            self.radius[i] = 1;
            let width = 1_i32.wrapping_shl(self.shifts[i]);
            let mut n = 1_i32;
            while width.wrapping_mul(n) < 153600 {
                work.step()?;
                n = n.wrapping_add(1);
            }
            if n > 1 {
                self.radius[i] = n;
            }
        }
        self.initialized = true;
        Ok(())
    }
    pub fn point(&self, id: ObstacleId) -> Result<&Point, i32> {
        self.points.get(id.0).and_then(Option::as_ref).ok_or(-4)
    }
    pub fn point_mut(&mut self, id: ObstacleId) -> Result<&mut Point, i32> {
        self.points.get_mut(id.0).and_then(Option::as_mut).ok_or(-4)
    }
    pub fn insert(&mut self, id: ObstacleId, position: [i32; 3]) -> Result<(), i32> {
        if !self.initialized {
            return Err(-5);
        }
        let x = position[0]
            .wrapping_sub(self.origin[0])
            .wrapping_shr(self.shifts[0]);
        let z = position[2]
            .wrapping_sub(self.origin[1])
            .wrapping_shr(self.shifts[1]);
        if (0..8).contains(&x) && (0..8).contains(&z) {
            self.cells[(x + z * 8) as usize].insert(0, id);
        }
        self.order.push(id);
        Ok(())
    }
    /// Conservatively count all typed route records and query metadata as extra
    /// map data, including sparse capacity, inline tables and Vec metadata.
    pub fn resident_bytes(&self) -> usize {
        std::mem::size_of::<Self>()
            + self.points.capacity() * std::mem::size_of::<Option<Point>>()
            + self.order.capacity() * std::mem::size_of::<ObstacleId>()
            + self
                .cells
                .iter()
                .map(|c| c.capacity() * std::mem::size_of::<ObstacleId>())
                .sum::<usize>()
    }
    pub fn link(
        &mut self,
        symbols: &Symbols,
        entities: &Entities<Handle>,
        colliders: &[Collider],
        limit: u32,
    ) -> Result<(), i32> {
        let mut work = Work::new(limit)?;
        let mut seen = vec![];
        let mut branches = vec![];
        let mut joins = vec![];
        for i in 0..self.order.len() {
            work.step()?;
            let id = self.order[i];
            for side in 0..2 {
                let next = self.point(id)?.next[side].resolve(symbols);
                self.point_mut(id)?.next[side] = Binding::Direct(next);
                if let Some(next) = next {
                    if contains(&seen, next, limit)? {
                        joins.push(next);
                    }
                    seen.push(next);
                    if side == 1 {
                        branches.push(id);
                    }
                }
            }
        }
        for i in 0..self.order.len() {
            work.step()?;
            let id = self.order[i];
            if contains(&branches, id, limit)? {
                let mut next = self.point(id)?.next[1].resolve(symbols).ok_or(-4)?;
                while !contains(&joins, next, limit)? {
                    work.step()?;
                    let id = obstacle(entities, next)?;
                    let point = self.point_mut(id)?;
                    next = point.next[0].resolve(symbols).ok_or(-4)?;
                    point.progress = -1;
                }
            }
        }
        let mut start = None;
        for &id in &self.order {
            work.step()?;
            let p = self.point(id)?;
            if p.lap != 0 && p.progress != -1 {
                start = Some(id);
            }
        }
        let start = start.ok_or(-4)?;
        let start_entity = entities_at(colliders, start)?;
        self.lap_start = Some(start_entity);
        let mut index = 0_i32;
        let mut id = start;
        loop {
            work.step()?;
            let p = self.point_mut(id)?;
            if p.progress != -1 {
                p.progress = index;
                index = index.wrapping_add(1);
            }
            id = obstacle(entities, p.next[0].resolve(symbols).ok_or(-4)?)?;
            if id == start {
                break;
            }
        }
        if let Some(id) = self.backwards_check {
            self.fallback = Some(id);
            self.backwards_index = self.point(obstacle(entities, id)?)?.progress;
        } else {
            self.backwards_index = index >> 1;
            id = start;
            loop {
                work.step()?;
                let p = self.point(id)?;
                let next = p.next[0].resolve(symbols).ok_or(-4)?;
                if p.progress == self.backwards_index {
                    self.fallback = Some(entities_at(colliders, id)?);
                }
                id = obstacle(entities, next)?;
                if id == start {
                    break;
                }
            }
        }
        let mut value = 0_u8;
        let mut work = Work::new(limit)?;
        for lap in 0..3 {
            id = start;
            let last = loop {
                work.step()?;
                let p = self.point(id)?;
                value = match p.weather[lap] {
                    1 => 0,
                    2 => p.strength[lap] | 1,
                    3 => p.strength[lap] | 2,
                    4 => p.strength[lap] | 4,
                    5 => p.strength[lap] | 5,
                    _ => value,
                };
                let progress = usize::try_from(p.progress).map_err(|_| -4)?;
                let next = p.next[0].resolve(symbols).ok_or(-4)?;
                *self.weather[lap].get_mut(progress).ok_or(-4)? = value;
                id = obstacle(entities, next)?;
                if id == start {
                    break progress;
                }
            };
            self.weather[lap][last..].fill(value);
        }
        Ok(())
    }
}
pub(super) fn obstacle(entities: &Entities<Handle>, entity: EntityId) -> Result<ObstacleId, i32> {
    entities
        .record(entity)
        .and_then(|r| r.object)
        .and_then(Handle::obstacle)
        .ok_or(-4)
}
fn entities_at(colliders: &[Collider], id: ObstacleId) -> Result<EntityId, i32> {
    colliders.get(id.0).map(|c| c.entity).ok_or(-4)
}
fn contains<T: Copy + PartialEq>(values: &[T], value: T, limit: u32) -> Result<bool, i32> {
    crate::spatial::contains(values, &value, limit).map_err(|_| -3)
}
struct Work {
    used: u32,
    limit: u32,
}
impl Work {
    fn new(limit: u32) -> Result<Self, i32> {
        if limit == 0 {
            Err(-1)
        } else {
            Ok(Self { used: 0, limit })
        }
    }
    fn step(&mut self) -> Result<(), i32> {
        if self.used >= self.limit {
            Err(-3)
        } else {
            self.used += 1;
            Ok(())
        }
    }
}
