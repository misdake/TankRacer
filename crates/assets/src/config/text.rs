use super::{Damping, Error, Gain, Node};

pub(super) struct Reader<'a> {
    bytes: &'a [u8],
    cursor: usize,
}
impl<'a> Reader<'a> {
    pub fn new(bytes: &'a [u8]) -> Result<Self, Error> {
        if let Some(at) = bytes.iter().position(|&b| b >= 128) {
            return Err(Error::InvalidText(at));
        }
        Ok(Self { bytes, cursor: 0 })
    }
    fn skip(&mut self) {
        loop {
            while self
                .bytes
                .get(self.cursor)
                .is_some_and(|b| b.is_ascii_whitespace() || *b == 0)
            {
                self.cursor += 1;
            }
            if self.bytes.get(self.cursor) != Some(&b'#') {
                break;
            }
            while self.bytes.get(self.cursor).is_some_and(|&b| b != b'\n') {
                self.cursor += 1;
            }
        }
    }
    pub fn token(&mut self) -> Result<Option<String>, Error> {
        self.skip();
        if self.cursor == self.bytes.len() {
            return Ok(None);
        }
        let start = self.cursor;
        let quoted = self.bytes[self.cursor] == b'"';
        if quoted {
            self.cursor += 1;
        }
        let mut value = String::new();
        let mut previous = 0;
        while let Some(&b) = self.bytes.get(self.cursor) {
            self.cursor += 1;
            if quoted && b == b'"' && previous != b'\\' {
                return Ok(Some(value));
            }
            if b < 32 || (!quoted && b == 32) {
                if quoted {
                    return Err(Error::InvalidText(start));
                } else {
                    return Ok(Some(value));
                }
            }
            if quoted && (b == b'"' || b == b'\\') && previous == b'\\' {
                value.pop();
            }
            value.push(char::from(b));
            previous = b;
            if value.len() > 31 {
                return Err(Error::InvalidText(start));
            }
        }
        if quoted {
            Err(Error::InvalidText(start))
        } else {
            Ok(Some(value))
        }
    }
    pub fn required(&mut self) -> Result<String, Error> {
        self.token()?.ok_or(Error::MissingValue(self.cursor))
    }
    pub fn integer(&mut self) -> Result<i32, Error> {
        self.skip();
        let start = self.cursor;
        let mut negative = false;
        if let Some(&b) = self.bytes.get(self.cursor)
            && (b == b'-' || b == b'+')
        {
            negative = b == b'-';
            self.cursor += 1;
        }
        let digits = self.cursor;
        let mut value = 0_u32;
        while let Some(&b) = self.bytes.get(self.cursor) {
            if !b.is_ascii_digit() {
                break;
            }
            value = value.wrapping_mul(10).wrapping_add(u32::from(b - b'0'));
            self.cursor += 1;
        }
        if digits == self.cursor {
            return Err(Error::MissingValue(start));
        }
        Ok(if negative {
            value.wrapping_neg()
        } else {
            value
        } as i32)
    }
    pub fn vector(&mut self) -> Result<[i32; 3], Error> {
        Ok([self.integer()?, self.integer()?, self.integer()?])
    }
    pub fn gain(&mut self) -> Result<Gain, Error> {
        Ok(Gain {
            multiplier: self.integer()? as u16,
            shift: self.integer()? as u16,
        })
    }
    pub fn damping(&mut self) -> Result<Damping, Error> {
        Ok(Damping {
            step: self.integer()? as i16,
            fraction: self.integer()? as u8,
            shift: self.integer()? as u8,
        })
    }
    pub fn node(&mut self) -> Result<Node, Error> {
        let name = self.required()?;
        if name.len() > 15 {
            return Err(Error::InvalidText(self.cursor));
        }
        Ok(Node {
            name,
            offset: self.vector()?,
        })
    }
}
