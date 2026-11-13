//! SSURFACE v2/v3 selection, planar/PCX decoding and original device colors.
//! Import stays separate from filtering, atlas padding and GPU material policy.

use std::fmt;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Error {
    Truncated,
    InvalidHeader,
    UnsupportedVersion(u16),
    MissingFormat,
    UnsupportedPixels,
    InvalidDimensions,
    InvalidMip,
    InvalidRun,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "invalid SUR image: {self:?}")
    }
}
impl std::error::Error for Error {}

#[derive(Debug, Clone, Copy)]
pub struct Info {
    pub version: u16,
    pub width: u32,
    pub height: u32,
    pub mip_levels: u16,
    pub scale: u16,
    pub format: u16,
    pub kind: u16,
    pub bits: u16,
    pub compression: u16,
    pub pixel_offset: usize,
    pub flags: u32,
}

pub struct Surface<'a> {
    bytes: &'a [u8],
    pub info: Info,
}

#[derive(Debug)]
pub struct Image {
    pub width: u32,
    pub height: u32,
    pub flags: u32,
    pub rgba: Vec<u8>,
    pub consumed: usize,
}

impl<'a> Surface<'a> {
    /// The original browser/native device profile uses flags=4, preference=3.
    pub fn open(bytes: &'a [u8]) -> Result<Self, Error> {
        if bytes.len() < 256 {
            return Err(Error::Truncated);
        }
        if &bytes[..8] != b"SSURFACE" {
            return Err(Error::InvalidHeader);
        }
        let version = word(bytes, 8)?;
        if !(2..=3).contains(&version) {
            return Err(Error::UnsupportedVersion(version));
        }
        let count = word(bytes, 34)? as usize;
        if count > 4 {
            return Err(Error::InvalidHeader);
        }
        span(bytes, 256, count * 6)?;
        let mut selected = None;
        for wanted in [0, 1, 2, 3] {
            for i in 0..count {
                if word(bytes, 256 + i * 6)? == wanted {
                    selected = Some((wanted, dword(bytes, 258 + i * 6)? as usize));
                    break;
                }
            }
            if selected.is_some() {
                break;
            }
        }
        let (format, offset) = selected.ok_or(Error::MissingFormat)?;
        span(bytes, offset, 22)?;
        Ok(Self {
            bytes,
            info: Info {
                version,
                width: u32::from(word(bytes, 26)?),
                height: u32::from(word(bytes, 28)?),
                mip_levels: word(bytes, 30)?,
                scale: word(bytes, 32)?,
                format,
                kind: word(bytes, offset)?,
                bits: word(bytes, offset + 2)?,
                compression: word(bytes, offset + 20)?,
                pixel_offset: offset + 22,
                flags: dword(bytes, 36)?,
            },
        })
    }

    /// Exact source/device conversion, before any renderer-specific edge repair.
    pub fn decode(&self, mip: u32) -> Result<Image, Error> {
        let info = self.info;
        let channels = match (info.kind, info.bits) {
            (0, 8) => 1,
            (2, 32) => 4,
            _ => return Err(Error::UnsupportedPixels),
        };
        let flags = (info.flags | 4) & 65535;
        if info.format == 2 || info.compression > 2 || flags & 3 == 3 {
            return Err(Error::UnsupportedPixels);
        }
        if mip > u32::from(info.mip_levels) || mip > 31 {
            return Err(Error::InvalidMip);
        }
        let count = u64::from(info.width) * u64::from(info.height);
        // A checked allocation boundary; shipped images are far below 16M pixels.
        if count == 0 || count > 16_777_216 {
            return Err(Error::InvalidDimensions);
        }
        let mut scratch = vec![0; count as usize * 4];
        let mut input = Input {
            bytes: self.bytes,
            cursor: info.pixel_offset,
        };
        let mut palette = [[0; 4]; 256];
        if channels == 1 {
            let bytes = input.take(768)?;
            for (i, color) in palette.iter_mut().enumerate() {
                *color = [
                    bytes[512 + i],
                    bytes[256 + i],
                    bytes[i],
                    if i == 0 { 0 } else { 255 },
                ];
            }
        }
        let (mut width, mut height) = (info.width as usize, info.height as usize);
        for level in 0..=mip {
            if width == 0 || height == 0 {
                return Err(Error::InvalidDimensions);
            }
            if info.compression == 2 {
                for row in 0..height {
                    for channel in 0..channels {
                        input.channel(
                            &mut scratch,
                            row * width * channels + channel,
                            channels,
                            width,
                            true,
                        )?;
                    }
                }
            } else {
                for channel in 0..channels {
                    input.channel(
                        &mut scratch,
                        channel,
                        channels,
                        width * height,
                        info.compression != 0,
                    )?;
                }
            }
            if level < mip {
                width >>= 1;
                height >>= 1;
            }
        }
        let mut rgba = Vec::with_capacity(width * height * 4);
        for i in 0..width * height {
            let bgra = if channels == 1 {
                palette[scratch[i] as usize]
            } else {
                scratch[i * 4..i * 4 + 4]
                    .try_into()
                    .map_err(|_| Error::Truncated)?
            };
            rgba.extend(device_rgba(bgra, flags));
        }
        Ok(Image {
            width: width as u32,
            height: height as u32,
            flags,
            rgba,
            consumed: input.cursor,
        })
    }
}

fn span(bytes: &[u8], at: usize, length: usize) -> Result<&[u8], Error> {
    bytes
        .get(at..)
        .and_then(|s| s.get(..length))
        .ok_or(Error::Truncated)
}
fn word(bytes: &[u8], at: usize) -> Result<u16, Error> {
    let s = span(bytes, at, 2)?;
    Ok(u16::from_le_bytes([s[0], s[1]]))
}
fn dword(bytes: &[u8], at: usize) -> Result<u32, Error> {
    let s = span(bytes, at, 4)?;
    Ok(u32::from_le_bytes([s[0], s[1], s[2], s[3]]))
}

struct Input<'a> {
    bytes: &'a [u8],
    cursor: usize,
}
impl Input<'_> {
    fn take(&mut self, count: usize) -> Result<&[u8], Error> {
        let bytes = span(self.bytes, self.cursor, count)?;
        self.cursor += count;
        Ok(bytes)
    }
    fn byte(&mut self) -> Result<u8, Error> {
        Ok(self.take(1)?[0])
    }
    fn channel(
        &mut self,
        out: &mut [u8],
        start: usize,
        stride: usize,
        count: usize,
        compressed: bool,
    ) -> Result<(), Error> {
        if !compressed {
            for (i, &byte) in self.take(count)?.iter().enumerate() {
                *out.get_mut(start + i * stride).ok_or(Error::InvalidRun)? = byte;
            }
            return Ok(());
        }
        let mut done = 0;
        loop {
            let marker = self.byte()?;
            // C0 is literal. C1..FF consume a value and repeat low-six-bit times.
            let (run, value) = if marker > 192 {
                (usize::from(marker & 63), self.byte()?)
            } else {
                (1, marker)
            };
            for i in 0..run {
                *out.get_mut(start + (done + i) * stride)
                    .ok_or(Error::InvalidRun)? = value;
            }
            done += run;
            if done >= count {
                return Ok(());
            }
        }
    }
}

fn device_rgba(bgra: [u8; 4], flags: u32) -> [u8; 4] {
    let [b, g, r, a] = bgra;
    match flags & 3 {
        2 => [(r >> 4) * 17, (g >> 4) * 17, (b >> 4) * 17, (a >> 4) * 17],
        mode => {
            let mut packed = if mode == 1 && a == 0 {
                0
            } else {
                u32::from(r >> 3) << 11 | u32::from(g >> 2) << 5 | u32::from(b >> 3)
            };
            if mode == 1 && a != 0 && packed == 0 {
                packed = 1;
            }
            [
                ((((packed >> 11) & 31) * 255 + 15) / 31) as u8,
                ((((packed >> 5) & 63) * 255 + 31) / 63) as u8,
                (((packed & 31) * 255 + 15) / 31) as u8,
                if mode == 1 && packed == 0 { 0 } else { 255 },
            ]
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pcx_literal_c0_and_short_packets() {
        let mut input = Input {
            bytes: &[192, 193, 5, 194, 6],
            cursor: 0,
        };
        let mut out = [0; 4];
        input.channel(&mut out, 0, 1, 4, true).unwrap();
        assert_eq!(out, [192, 5, 6, 6]);
        assert_eq!(input.cursor, 5);
        assert_eq!(
            Input {
                bytes: &[193],
                cursor: 0
            }
            .channel(&mut out, 0, 1, 1, true),
            Err(Error::Truncated)
        );
        assert_eq!(
            Input {
                bytes: &[255, 1],
                cursor: 0
            }
            .channel(&mut out, 0, 1, 1, true),
            Err(Error::InvalidRun)
        );
    }

    #[test]
    fn keyed_black_is_opaque_and_alpha_formats_preserve_partial_alpha() {
        assert_eq!(device_rgba([0, 0, 0, 255], 1), [0, 0, 8, 255]);
        assert_eq!(device_rgba([255, 0, 255, 0], 1), [0, 0, 0, 0]);
        assert_eq!(device_rgba([17, 34, 51, 64], 2), [51, 34, 17, 68]);
    }

    #[test]
    fn selected_mip_skips_complete_planar_levels_and_preserves_consumed_length() {
        let mut bytes = vec![0; 342];
        bytes[..8].copy_from_slice(b"SSURFACE");
        for (at, value) in [
            (8, 3_u16),
            (26, 2),
            (28, 2),
            (30, 1),
            (32, 1),
            (34, 1),
            (256, 0),
            (320, 2),
            (322, 32),
        ] {
            bytes[at..at + 2].copy_from_slice(&value.to_le_bytes());
        }
        bytes[36..40].copy_from_slice(&2_u32.to_le_bytes());
        bytes[258..262].copy_from_slice(&320_u32.to_le_bytes());
        bytes.extend([
            1, 2, 3, 4, 17, 34, 51, 68, 85, 102, 119, 136, 0, 64, 128, 255,
        ]);
        bytes.extend([17, 34, 51, 68]);
        let surface = Surface::open(&bytes).unwrap();
        let mip = surface.decode(1).unwrap();
        assert_eq!((mip.width, mip.height), (1, 1));
        assert_eq!(mip.rgba, [51, 34, 17, 68]);
        assert_eq!(mip.consumed, 362);
        assert_eq!(surface.decode(2).unwrap_err(), Error::InvalidMip);
    }
}
