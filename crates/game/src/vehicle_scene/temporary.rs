//! Four shared temporary line effects: activation, expiry and update.
use crate::{
    effect_pool::{LineHandle, Pool},
    math, random,
};

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Row {
    pub first: [i32; 3],
    pub last: [i32; 3],
    /// First luminance/blue, last luminance/blue; alpha stays in line styles.
    pub colors: [u8; 4],
    pub phases: [u16; 2],
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct State {
    pub available: u32,
    pub timestamps: [u32; 4],
    pub lines: [[LineHandle; 9]; 4],
    pub rows: [[Row; 3]; 4],
}
impl Default for State {
    fn default() -> Self {
        Self {
            available: 0,
            timestamps: [0; 4],
            lines: [[LineHandle(0); 9]; 4],
            rows: [[Row::default(); 3]; 4],
        }
    }
}
pub struct Context<'a> {
    pub pool: &'a mut Pool,
    pub random: &'a mut u32,
}
fn endpoint(position: [i32; 3], yaw: u32, pitch: u32) -> [i32; 3] {
    let radius = math::cosine(pitch & 0x3ffc).wrapping_mul(2700) >> 12;
    [
        position[0].wrapping_add(math::cosine(yaw).wrapping_mul(radius) >> 12),
        position[1]
            .wrapping_add(math::sine(pitch & 0x3ffc).wrapping_mul(2700) >> 12)
            .wrapping_sub(1000),
        position[2].wrapping_add(math::sine(yaw).wrapping_mul(radius) >> 12),
    ]
}
fn endpoints(position: [i32; 3], seed: &mut u32, tick: bool) -> ([i32; 3], [i32; 3], [i32; 3]) {
    let yaw = (random::next(seed) >> 4) & 65535;
    let pitch = (random::next(seed) >> 4) & 16383;
    let first = endpoint(position, yaw, pitch);
    let (yaw, pitch) = if tick {
        let pitch = (random::next(seed) >> 4) & 16383;
        let yaw = (random::next(seed) >> 4) & 65535;
        (yaw, pitch)
    } else {
        let yaw = (random::next(seed) >> 4) & 65535;
        let pitch = (random::next(seed) >> 4) & 16383;
        (yaw, pitch)
    };
    let last = endpoint(position, yaw, pitch);
    let middle = std::array::from_fn(|i| first[i].wrapping_add(last[i]) >> 1);
    (first, last, middle)
}
fn jitter(point: [i32; 3], seed: &mut u32) -> [i32; 3] {
    point.map(|v| {
        v.wrapping_add(((random::next(seed) >> 4) & 1023) as i32)
            .wrapping_sub(512)
    })
}
fn pair_colors(row: &mut Row, colors: &mut [[u8; 4]; 2], seed: &mut u32) {
    let first = (((random::next(seed) >> 13) & 127) + 20) as u8;
    colors[0][0] = first;
    colors[0][1] = first;
    row.colors[0] = first;
    let last = (((random::next(seed) >> 4) & 127) + 20) as u8;
    colors[1][0] = last;
    colors[1][1] = last;
    colors[0][2] = (((random::next(seed) >> 13) & 127) + 70) as u8;
    row.colors[1] = colors[0][2];
    colors[1][2] = (((random::next(seed) >> 4) & 127) + 70) as u8;
}
fn last_colors(row: &mut Row, colors: &mut [[u8; 4]; 2], seed: &mut u32) {
    let last = (((random::next(seed) >> 4) & 127) + 20) as u8;
    colors[1][0] = last;
    colors[1][1] = last;
    row.colors[2] = last;
    colors[1][2] = (((random::next(seed) >> 4) & 127) + 70) as u8;
    row.colors[3] = colors[1][2];
}
impl State {
    /// Environment startup clears only slot timestamps, retaining old payload.
    pub fn reset_timestamps(&mut self) {
        self.timestamps = [0; 4];
    }
    /// Releases in row/column order, including repeated/exhausted handles.
    pub fn expire(&mut self, slot: i16, pool: &mut Pool) -> Result<(), i32> {
        for handle in self.lines.get(slot as usize).ok_or(-1)? {
            pool.release(*handle).map_err(|_| -1)?;
        }
        self.available = slot as u32;
        self.timestamps[slot as usize] = 0;
        Ok(())
    }
    /// The caller owns timer application. An already-active timer only refreshes
    /// the timestamp; it does not regenerate geometry or consume random values.
    pub fn start(
        &mut self,
        timer: i16,
        slot: &mut i16,
        position: [i32; 3],
        time: u32,
        context: Context<'_>,
    ) -> Result<(), i32> {
        if timer != 0 {
            *self.timestamps.get_mut(*slot as usize).ok_or(-1)? = time;
            return Ok(());
        }
        let selected = if self.available == u32::MAX {
            let mut selected = 0;
            for i in 1..4 {
                if self.timestamps[i] < self.timestamps[selected] {
                    selected = i;
                }
            }
            self.expire(selected as i16, context.pool)?;
            self.available = u32::MAX;
            self.timestamps[selected] = time;
            selected
        } else {
            let selected = self.available as usize;
            self.available = u32::MAX;
            *self.timestamps.get_mut(selected).ok_or(-1)? = time;
            if let Some(free) = self.timestamps.iter().position(|v| *v == 0) {
                self.available = free as u32;
            }
            selected
        };
        *slot = selected as i16;
        let mut colors = [[0, 0, 0, 1], [0; 4]];
        for row in 0..3 {
            let state = &mut self.rows[selected][row];
            let (first, last, middle) = endpoints(position, context.random, false);
            state.first = first;
            state.last = last;
            for line in 0..2 {
                let points = [first, jitter(middle, context.random)];
                pair_colors(state, &mut colors, context.random);
                self.lines[selected][row * 3 + line] = context.pool.allocate(points, colors);
            }
            last_colors(state, &mut colors, context.random);
            self.lines[selected][row * 3 + 2] = context.pool.allocate([first, last], colors);
        }
        Ok(())
    }
    pub fn tick(
        &mut self,
        timer: i16,
        slot: i16,
        position: [i32; 3],
        context: Context<'_>,
    ) -> Result<(), i32> {
        if timer == 0 {
            return Ok(());
        }
        let selected = slot as usize;
        let rows = self.rows.get_mut(selected).ok_or(-1)?;
        let handles = &self.lines[selected];
        let mut colors = [[0, 0, 0, 1], [0; 4]];
        for (row, state) in rows.iter_mut().enumerate() {
            let (first, last, middle) = endpoints(position, context.random, true);
            state.first = first;
            state.last = last;
            let mut points = [first, jitter(middle, context.random)];
            pair_colors(state, &mut colors, context.random);
            context
                .pool
                .update(handles[row * 3], points, colors)
                .map_err(|_| -1)?;
            points[0] = jitter(middle, context.random);
            let first = (((random::next(context.random) >> 4) & 127) + 20) as u8;
            colors[0][0] = first;
            colors[0][1] = first;
            colors[0][2] = (((random::next(context.random) >> 4) & 127) + 70) as u8;
            context
                .pool
                .update(handles[row * 3 + 1], points, colors)
                .map_err(|_| -1)?;
            points[1] = last;
            last_colors(state, &mut colors, context.random);
            context
                .pool
                .update(handles[row * 3 + 2], points, colors)
                .map_err(|_| -1)?;
            state.phases =
                std::array::from_fn(|_| ((random::next(context.random) >> 4) & 511) as u16);
        }
        Ok(())
    }
}
