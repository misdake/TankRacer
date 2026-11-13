//! Class-parameter lexer (504F60/5050D0/504EE0), distinct from file/config syntax.
//! No tree or string copies: repeated searches reset a borrowed token cursor.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NonAscii;

#[derive(Debug, Clone, Copy)]
pub struct Parameters<'a>(&'a str);
#[derive(Debug, Clone, Copy)]
pub struct Values<'a> {
    text: &'a str,
    cursor: usize,
}
impl<'a> Parameters<'a> {
    pub fn new(text: &'a str) -> Result<Self, NonAscii> {
        if text.is_ascii() {
            Ok(Self(text))
        } else {
            Err(NonAscii)
        }
    }
    pub fn find(self, name: &str) -> Option<Values<'a>> {
        let mut values = Values {
            text: self.0,
            cursor: 0,
        };
        while let Some((word, more)) = values.token() {
            if word == name && more {
                return Some(values);
            }
            if !more {
                break;
            }
        }
        None
    }
    pub fn integer(self, name: &str) -> Option<i32> {
        self.find(name)?.integer()
    }
}
impl<'a> Values<'a> {
    pub fn position(&self) -> usize {
        self.cursor
    }
    fn token(&mut self) -> Option<(&'a str, bool)> {
        let bytes = self.text.as_bytes();
        while bytes
            .get(self.cursor)
            .is_some_and(|b| matches!(b, b' ' | b'\t'))
        {
            self.cursor += 1;
        }
        let first = *bytes.get(self.cursor)?;
        let mut start = self.cursor;
        self.cursor += 1;
        if matches!(first, b'(' | b')') {
            return Some((&self.text[start..self.cursor], self.cursor < bytes.len()));
        }
        let quoted = first == b'"';
        if quoted {
            start += 1;
        }
        while let Some(&b) = bytes.get(self.cursor) {
            if b == (if quoted { b'"' } else { b' ' }) || matches!(b, b'\t' | b'(' | b')') {
                break;
            }
            self.cursor += 1;
        }
        let end = self.cursor;
        if quoted && bytes.get(self.cursor) == Some(&b'"') {
            self.cursor += 1;
        }
        Some((&self.text[start..end], self.cursor < bytes.len()))
    }
    /// Native reads publish a value only if another token remains. A final
    /// token may exist while more==false; it deliberately produces None here.
    pub fn word(&mut self) -> Option<&'a str> {
        let (word, more) = self.token()?;
        more.then_some(word)
    }
    pub fn integer(&mut self) -> Option<i32> {
        let word = self.word()?;
        match word.as_bytes().first() {
            Some(b't' | b'T') => return Some(1),
            Some(b'f' | b'F') => return Some(0),
            _ => (),
        }
        let word = word.trim_start_matches(|c: char| c.is_ascii_whitespace());
        let negative = word.starts_with('-');
        let digits = word.strip_prefix(['-', '+']).unwrap_or(word);
        let mut value = 0_u32;
        for b in digits.bytes().take_while(u8::is_ascii_digit) {
            value = value.wrapping_mul(10).wrapping_add(u32::from(b - b'0'));
        }
        Some(if negative {
            value.wrapping_neg()
        } else {
            value
        } as i32)
    }
    pub fn real32(&mut self) -> Option<f32> {
        Some(crate::number::decimal(self.word()?) as f32)
    }
}

/// Original binary32 * 1024, signed64 truncation and low32 result.
pub fn fixed1024(value: f32) -> i32 {
    let bits = value.to_bits();
    let exponent = (bits >> 23) & 255;
    if !(117..180).contains(&exponent) {
        return 0;
    }
    let mantissa = u64::from((bits & 0x7f_ffff) | 0x80_0000);
    let magnitude = if exponent >= 140 {
        mantissa << (exponent - 140)
    } else {
        mantissa >> (140 - exponent)
    } as u32;
    if bits >> 31 != 0 {
        magnitude.wrapping_neg() as i32
    } else {
        magnitude as i32
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn exact_case_first_match_crlf_and_last_token_rules_are_preserved() {
        let p = Parameters::new("(:a 4294967295)(:a 2) :A -9 (:bool Things) :lost 7").unwrap();
        assert_eq!(p.integer(":a"), Some(-1));
        assert_eq!(p.integer(":A"), Some(-9));
        assert_eq!(p.integer(":bool"), Some(1));
        assert_eq!(p.integer(":lost"), None);
        let p = Parameters::new("\r\n:glued 1 \r\n :spaced 849.92 )").unwrap();
        assert!(p.find(":glued").is_none());
        assert_eq!(p.integer(":spaced"), Some(849));
        let p = Parameters::new(":word \"two words\" (:word later)").unwrap();
        assert_eq!(p.find(":word").unwrap().word(), Some("two words"));
        assert!(Parameters::new("中文").is_err());
    }
}
