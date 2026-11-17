//! LAMPS.DAT has a count followed by position/RGB/type and optional flash times.
use super::*;
#[derive(Debug)]
pub struct Lamp {
    pub position: [i32; 3],
    pub color: [u8; 3],
    pub kind: u8,
    pub visible: u8,
    pub flash_times: [i16; 2],
    pub alternate: u8,
    pub remaining: i16,
}
#[derive(Debug)]
pub struct Lamps {
    pub entries: Vec<Lamp>,
    pub size: [i32; 2],
    pub depth_offset: i32,
}
impl Lamps {
    pub fn load(bytes: &[u8]) -> Result<Self, Box<dyn Error>> {
        let text = std::str::from_utf8(bytes)?;
        let mut tokens = text.split_ascii_whitespace();
        let count: usize = tokens.next().ok_or("Missing lamp count")?.parse()?;
        if count > 100000 {
            return Err("Lamp count exceeds traversal limit".into());
        }
        let mut entries = Vec::with_capacity(count);
        for _ in 0..count {
            let mut position = [0; 3];
            for p in &mut position {
                *p = fixed1024(tokens.next().ok_or("Missing lamp position")?.parse()?);
            }
            let mut color = [0; 3];
            for c in &mut color {
                *c = tokens.next().ok_or("Missing lamp color")?.parse::<i32>()? as u8;
            }
            let kind = tokens.next().ok_or("Missing lamp type")?.parse::<i32>()? as u8;
            let mut flash_times = [0; 2];
            let mut alternate = 0;
            if kind == 2 {
                for t in &mut flash_times {
                    *t = tokens
                        .next()
                        .ok_or("Missing lamp interval")?
                        .parse::<i32>()? as i16;
                }
                alternate = tokens.next().ok_or("Missing lamp phase")?.parse::<i32>()? as u8;
            }
            entries.push(Lamp {
                position,
                color,
                kind,
                visible: 0,
                flash_times,
                alternate,
                remaining: 0,
            });
        }
        Ok(Self {
            entries,
            size: [1024; 2],
            depth_offset: -10,
        })
    }
}
