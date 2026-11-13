//! Shared fixed line/quad records, with the source's bounded exhaustion aliases.
pub const FREE: i32 = 0xe0000000_u32 as i32;
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct LineHandle(pub u32);
impl LineHandle {
    pub const EXHAUSTED: Self = Self(u32::MAX);
    pub const OVERFLOW: Self = Self(100);
    pub const RELEASED: Self = Self(0xfe5cbe12);
}
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct QuadHandle(pub u32);
impl QuadHandle {
    pub const EXHAUSTED: Self = Self(u32::MAX);
    pub const OVERFLOW: Self = Self(160);
    pub const RELEASED: Self = Self(0xfeb4024d);
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
    fn scaled(mut self) -> Self {
        self.points = self.points.map(|p| p.map(|v| v >> 3));
        self
    }
    fn words(self) -> [u32; 16] {
        let mut out = [0; 16];
        for (word, value) in out.iter_mut().zip(self.points.into_iter().flatten()) {
            *word = value as u32;
        }
        out[12] = self.material;
        for (word, bytes) in out[13..].iter_mut().zip(self.style.chunks_exact(4)) {
            *word = u32::from_le_bytes(bytes.try_into().unwrap());
        }
        out
    }
    fn from_words(words: [u32; 16]) -> Self {
        Self {
            points: std::array::from_fn(|p| std::array::from_fn(|i| words[p * 3 + i] as i32)),
            material: words[12],
            style: std::array::from_fn(|i| words[13 + i / 4].to_le_bytes()[i % 4]),
        }
    }
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    InvalidHandle(LineHandle),
    InvalidQuadHandle(QuadHandle),
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
    /// Quad handle -1 addresses the record immediately before the quad bank.
    /// It is distinct from the line allocator's -1 alias into the last quad.
    pub exhausted_quad: Quad,
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
            exhausted_quad: Quad::ZERO,
        };
        pool.reset();
        pool
    }
}
impl Pool {
    /// Quad 160 overlaps the two words before line 0, all of line 0 and the
    /// position of line 1. Reconstruct only this rare alias, keeping normal
    /// draws/allocations on the directly owned typed records.
    fn quad_overflow(&self) -> Quad {
        let mut words = [0; 16];
        for (word, color) in words[..2].iter_mut().zip(self.exhausted_colors) {
            *word = u32::from_le_bytes(color);
        }
        for (word, value) in words[2..8]
            .iter_mut()
            .zip(self.lines[0].points.into_iter().flatten())
        {
            *word = value as u32;
        }
        for (word, color) in words[8..10].iter_mut().zip(self.lines[0].colors) {
            *word = u32::from_le_bytes(color);
        }
        for (word, value) in words[10..]
            .iter_mut()
            .zip(self.lines[1].points.into_iter().flatten())
        {
            *word = value as u32;
        }
        Quad::from_words(words)
    }
    fn write_quad_overflow(&mut self, quad: Quad) {
        let words = quad.words();
        self.exhausted_colors = [words[0].to_le_bytes(), words[1].to_le_bytes()];
        self.lines[0].points =
            std::array::from_fn(|p| std::array::from_fn(|i| words[2 + p * 3 + i] as i32));
        self.lines[0].colors = [words[8].to_le_bytes(), words[9].to_le_bytes()];
        self.lines[1].points =
            std::array::from_fn(|p| std::array::from_fn(|i| words[10 + p * 3 + i] as i32));
    }
    fn edit_quad<R>(
        &mut self,
        handle: QuadHandle,
        run: impl FnOnce(&mut Quad) -> R,
    ) -> Result<R, Error> {
        match handle {
            QuadHandle(0..=159) => Ok(run(&mut self.quads[handle.0 as usize])),
            QuadHandle::EXHAUSTED => Ok(run(&mut self.exhausted_quad)),
            QuadHandle::OVERFLOW => {
                let mut quad = self.quad_overflow();
                let result = run(&mut quad);
                self.write_quad_overflow(quad);
                Ok(result)
            }
            _ => Err(Error::InvalidQuadHandle(handle)),
        }
    }
    pub fn allocate_quad(&mut self, quad: Quad) -> QuadHandle {
        if self.quad_count == 160 {
            return QuadHandle::EXHAUSTED;
        }
        if let Some(index) = self.quads.iter().position(Quad::is_free) {
            self.quad_count = self.quad_count.wrapping_add(1);
            self.quads[index] = quad.scaled();
            QuadHandle(index as u32)
        } else {
            QuadHandle::OVERFLOW
        }
    }
    pub fn update_quad(&mut self, handle: QuadHandle, points: [[i32; 3]; 4]) -> Result<(), Error> {
        self.edit_quad(handle, |quad| {
            quad.points = points.map(|p| p.map(|v| v >> 3))
        })
    }
    pub fn replace_quad(&mut self, handle: QuadHandle, quad: Quad) -> Result<(), Error> {
        self.edit_quad(handle, |target| *target = quad.scaled())
    }
    pub fn release_quad(&mut self, handle: QuadHandle) -> Result<(), Error> {
        let released = self.edit_quad(handle, |quad| {
            if quad.is_free() {
                false
            } else {
                quad.points[0][1] = FREE;
                true
            }
        })?;
        if released {
            self.quad_count = self.quad_count.wrapping_sub(1);
        }
        Ok(())
    }
    pub fn visible_quads(&self) -> impl Iterator<Item = (QuadHandle, &Quad)> {
        self.quads
            .iter()
            .enumerate()
            .filter(|(_, quad)| self.quad_count != 0 && !quad.is_free())
            .map(|(i, quad)| (QuadHandle(i as u32), quad))
    }
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
