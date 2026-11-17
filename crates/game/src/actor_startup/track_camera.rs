//! Authored Trackcam initialization (43D250 / 43D2A0), without frame updates.
use super::*;
#[derive(Debug, Clone, Copy, Default)]
pub struct Axis {
    pub speed: i16,
    pub initial: i16,
    pub range: i16,
    pub travel: i16,
    pub decay: [u8; 2],
}
#[derive(Debug)]
pub struct TrackCamera {
    pub pose: Pose,
    pub flags: u32,
    pub range: i32,
    pub displacement: [i32; 3],
    pub original_displacement: [i32; 3],
    pub angles: [u32; 3],
    pub movement: [Axis; 3],
    pub max_move: i16,
    pub move_travel: i16,
    pub move_decay: [u8; 2],
    pub zoom: Axis,
    pub rotation: [i16; 2],
}
fn find<'a>(p: Parameters<'a>, key: &str) -> Option<Values<'a>> {
    p.find(key).or_else(|| {
        let mut alias = key.to_owned();
        alias[1..2].make_ascii_uppercase();
        p.find(&alias)
    })
}
fn number(p: Parameters<'_>, key: &str) -> Result<Option<i32>, Box<dyn Error>> {
    find(p, key).map(|mut v| int(&mut v)).transpose()
}
fn float(p: Parameters<'_>, key: &str, fixed: bool) -> Result<Option<i32>, Box<dyn Error>> {
    find(p, key)
        .map(|mut v| {
            v.real32()
                .map(|n| {
                    if fixed {
                        fixed1024(n)
                    } else {
                        n.trunc() as i32
                    }
                })
                .ok_or_else(|| "Missing Trackcam value".into())
        })
        .transpose()
}
fn rate(n: i32) -> i16 {
    (n.wrapping_shl(8) / 1000) as i16
}
impl TrackCamera {
    pub fn construct(
        p: Parameters<'_>,
        pose: Pose,
        near: [i32; 3],
    ) -> Result<Self, Box<dyn Error>> {
        let mode = if find(p, ":fixed_cam").is_some() {
            1
        } else if find(p, ":pan_cam").is_some() {
            2
        } else if find(p, ":follow_cam").is_some() {
            4
        } else if find(p, ":chase_cam").is_some() {
            8
        } else {
            0
        };
        let mut s = Self {
            pose,
            flags: if mode == 8 { 0 } else { mode },
            range: number(p, ":range")?.unwrap_or(0).wrapping_shl(10),
            displacement: near,
            original_displacement: [0; 3],
            angles: [0; 3],
            movement: [Axis::default(); 3],
            max_move: 0,
            move_travel: 0,
            move_decay: [0; 2],
            zoom: Axis::default(),
            rotation: [0; 2],
        };
        if mode == 1 || mode == 2 {
            for (i, key) in [":x_move_speed", ":y_move_speed", ":z_move_speed"]
                .into_iter()
                .enumerate()
            {
                if let Some(n) = float(p, key, true)? {
                    s.movement[i].speed = rate(n);
                    s.movement[i].initial = rate(n);
                }
            }
            s.max_move = float(p, ":max_move_dist", false)?
                .unwrap_or(0)
                .wrapping_shl(10) as i16;
            s.move_decay = [
                number(p, ":move_slow_mul")?.unwrap_or(0) as u8,
                number(p, ":move_slow_shf")?.unwrap_or(0) as u8,
            ];
            if mode == 2 {
                s.zoom.speed = rate(float(p, ":zoom_speed", true)?.unwrap_or(0));
                s.zoom.initial = s.zoom.speed;
                s.zoom.range = float(p, ":max_zoom_dist", false)?
                    .unwrap_or(0)
                    .wrapping_shl(10) as i16;
                s.zoom.decay = [
                    number(p, ":zoom_slow_mul")?.unwrap_or(0) as u8,
                    number(p, ":zoom_slow_shf")?.unwrap_or(0) as u8,
                ];
            }
        } else if mode == 4 || mode == 8 {
            if let Some(mut v) = find(p, ":disp") {
                s.displacement = [real(&mut v)?, real(&mut v)?, real(&mut v)?];
                s.original_displacement = s.displacement;
            } else if mode == 4 {
                s.original_displacement = s.displacement;
            }
            if mode == 8 {
                s.angles = pose
                    .matrix
                    .angles()
                    .map_err(|e| format!("Trackcam angles: {e:?}"))?
                    .0;
            } else {
                for (i, key) in [":y_rotspeed", ":x_rotspeed"].into_iter().enumerate() {
                    s.rotation[i] = ((number(p, key)?.unwrap_or(0) as i16 as i32)
                        .wrapping_mul(46592)
                        / 1000) as i16;
                }
                for (axis, prefix) in [(2, "z"), (1, "y")] {
                    if let Some(n) = float(p, &format!(":{prefix}_resize_speed"), true)? {
                        s.movement[axis].speed = rate(n);
                        s.movement[axis].initial = rate(n);
                        s.movement[axis].range =
                            float(p, &format!(":{prefix}_resize_range"), true)?.unwrap_or(0) as i16;
                        s.movement[axis].decay = [
                            number(p, &format!(":{prefix}_decel_mul"))?.unwrap_or(0) as u8,
                            number(p, &format!(":{prefix}_decel_shf"))?.unwrap_or(0) as u8,
                        ];
                    }
                }
            }
        }
        Ok(s)
    }
}
