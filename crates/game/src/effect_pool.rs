//! Shared fixed line/quad records, with the source's bounded exhaustion aliases.
pub const FREE: i32 = 0xe0000000_u32 as i32;
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct LineHandle(pub u32);
impl LineHandle {
    pub const EXHAUSTED: Self = Self(u32::MAX);
    pub const OVERFLOW: Self = Self(100);
    pub const RELEASED: Self = Self(0xfe5cbe12);
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Line {
    pub points: [[i32; 3]; 2],
    pub colors: [[u8; 4]; 2],
}
impl Line {
    pub const ZERO: Self = Self {
        points: [[0; 3]; 2],
        colors: [[0; 4]; 2],
    };
    fn scaled(points: [[i32; 3]; 2], colors: [[u8; 4]; 2]) -> Self {
        Self {
            points: points.map(|p| p.map(|v| v >> 3)),
            colors,
        }
    }
    pub fn is_free(&self) -> bool {
        self.points[0][1] == FREE
    }
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Quad {
    pub points: [[i32; 3]; 4],
    pub material: u32,
    pub style: [u8; 12],
}
impl Quad {
    pub const ZERO: Self = Self {
        points: [[0; 3]; 4],
        material: 0,
        style: [0; 12],
    };
    pub fn is_free(&self) -> bool {
        self.points[0][1] == FREE
    }
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    InvalidHandle(LineHandle),
}
pub struct Pool {
    pub lines: [Line; 100],
    pub line_count: u32,
    pub quads: [Quad; 160],
    pub quad_count: u32,
    /// The allocator can return 100 after count/marker desynchronization.
    /// This record is outside the renderer's 100-line scan, but remains state.
    pub overflow: Line,
    /// A full-pool handle aliases the last quad's tail and these trailing bytes.
    /// Keep that exact bounded quirk here, without a guest-memory arena.
    pub exhausted_colors: [[u8; 4]; 2],
}
impl Default for Pool {
    fn default() -> Self {
        let mut pool = Self {
            lines: [Line::ZERO; 100],
            line_count: 0,
            quads: [Quad::ZERO; 160],
            quad_count: 0,
            overflow: Line::ZERO,
            exhausted_colors: [[0; 4]; 2],
        };
        pool.reset();
        pool
    }
}
impl Pool {
    pub fn reset(&mut self) {
        for line in &mut self.lines {
            line.points[0][1] = FREE;
        }
        self.line_count = 0;
        for quad in &mut self.quads {
            quad.points[0][1] = FREE;
        }
        self.quad_count = 0;
    }
    pub fn allocate(&mut self, points: [[i32; 3]; 2], colors: [[u8; 4]; 2]) -> LineHandle {
        if self.line_count == 100 {
            return LineHandle::EXHAUSTED;
        }
        if let Some(index) = self.lines.iter().position(Line::is_free) {
            self.line_count = self.line_count.wrapping_add(1);
            self.lines[index] = Line::scaled(points, colors);
            LineHandle(index as u32)
        } else {
            LineHandle::OVERFLOW
        }
    }
    pub fn update(
        &mut self,
        handle: LineHandle,
        points: [[i32; 3]; 2],
        colors: [[u8; 4]; 2],
    ) -> Result<(), Error> {
        let line = Line::scaled(points, colors);
        match handle {
            LineHandle(0..=99) => self.lines[handle.0 as usize] = line,
            LineHandle::OVERFLOW => self.overflow = line,
            LineHandle::EXHAUSTED => {
                let [[a, b, c], [d, e, f]] = line.points;
                let quad = &mut self.quads[159];
                quad.points[3][1] = a;
                quad.points[3][2] = b;
                quad.material = c as u32;
                for (i, value) in [d, e, f].iter().enumerate() {
                    quad.style[i * 4..i * 4 + 4].copy_from_slice(&value.to_le_bytes());
                }
                self.exhausted_colors = colors;
            }
            _ => return Err(Error::InvalidHandle(handle)),
        }
        Ok(())
    }
    pub fn release(&mut self, handle: LineHandle) -> Result<(), Error> {
        let marker = match handle {
            LineHandle(0..=99) => &mut self.lines[handle.0 as usize].points[0][1],
            LineHandle::OVERFLOW => &mut self.overflow.points[0][1],
            LineHandle::EXHAUSTED => &mut self.quads[159].points[3][2],
            _ => return Err(Error::InvalidHandle(handle)),
        };
        if *marker != FREE {
            *marker = FREE;
            self.line_count = self.line_count.wrapping_sub(1);
        }
        Ok(())
    }
    pub fn visible_lines(&self) -> impl Iterator<Item = (LineHandle, &Line)> {
        self.lines
            .iter()
            .enumerate()
            .filter(|(_, line)| self.line_count != 0 && !line.is_free())
            .map(|(i, line)| (LineHandle(i as u32), line))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn exhaustion_retains_rng_independent_allocation_and_cross_record_updates() {
        let mut pool = Pool::default();
        for _ in 0..100 {
            pool.allocate([[0; 3]; 2], [[0; 4]; 2]);
        }
        assert_eq!(
            pool.allocate([[123; 3]; 2], [[9; 4]; 2]),
            LineHandle::EXHAUSTED
        );
        pool.update(
            LineHandle::EXHAUSTED,
            [[8, 16, 24], [32, 40, 48]],
            [[7; 4]; 2],
        )
        .unwrap();
        assert_eq!(pool.quads[159].points[3], [0, 1, 2]);
        assert_eq!(pool.quads[159].material, 3);
        assert_eq!(pool.exhausted_colors, [[7; 4]; 2]);
        pool.release(LineHandle::EXHAUSTED).unwrap();
        assert_eq!(pool.line_count, 99);
        assert_eq!(
            pool.allocate([[0; 3]; 2], [[0; 4]; 2]),
            LineHandle::OVERFLOW
        );
        assert_eq!(pool.visible_lines().count(), 100);
    }
}
