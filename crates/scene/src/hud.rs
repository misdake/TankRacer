//! Original single-player HUD geometry (frame_hud_draw), independent of WebGL.
//! This slice draws existing clock/vehicle state; pickup and lap-record owners
//! append their own entries when those interactions are integrated.
mod tables;
use tankracer_game::session::Session;
use tankracer_render::material::{AlphaEncoding, Blend, Material, Pipeline};

/// Keyed native UI or SRCALPHA/INVSRCALPHA at alpha 128. The atlas is
/// premultiplied, so blended GPU output uses the equivalent ONE/INVSRCALPHA.
pub fn material(blend: bool) -> Material {
    Material {
        pipeline: Pipeline {
            blend: blend.then_some(Blend::PREMULTIPLIED_ALPHA),
            ..Pipeline::OVERLAY
        },
        alpha_cutoff: if blend { None } else { Some(0.5) },
        texture_alpha: AlphaEncoding::Premultiplied,
        output_alpha: if blend {
            AlphaEncoding::Premultiplied
        } else {
            AlphaEncoding::Straight
        },
        ..Material::opaque([1.; 4])
    }
}

pub const RESOURCES: [(usize, &str); 10] = [
    (0, "CFONT1.SUR"),
    (1, "CFONT2.SUR"),
    (2, "CFONT3.SUR"),
    (3, "DOTDIAL.SUR"),
    (4, "SPEEDO.SUR"),
    (5, ""),
    (16, "SHOOT.SUR"),
    (17, "GOG.SUR"),
    (18, "GOO.SUR"),
    (19, "GOEX.SUR"),
];
pub const MAPS: [&str; 8] = [
    "ENGMAP.SUR",
    "MEXMAP.SUR",
    "SWAMAP.SUR",
    "EASMAP.SUR",
    "THEMAP.SUR",
    "BIOMAP.SUR",
    "MONMAP.SUR",
    "CIRMAP.SUR",
];
#[derive(Debug, Clone, Copy)]
pub struct Snapshot {
    pub map: usize,
    pub countdown: i32,
    pub release: i32,
    pub elapsed: i32,
    pub position: [i32; 3],
    pub speed: i32,
    pub needle: i32,
    pub cockpit: bool,
    pub turret_yaw: i16,
}
impl Snapshot {
    pub fn from_session(s: &Session) -> Option<Self> {
        let c = s.clock()?;
        let p = s.player();
        Some(Self {
            map: s.world().settings().map as usize,
            countdown: c.countdown,
            release: c.release,
            elapsed: p.race.elapsed,
            position: p.physics.motion.poses.body.position,
            speed: p.physics.powertrain.local_velocity[2],
            needle: if p.race.driving {
                p.physics.powertrain.engine_display
            } else {
                p.prefix.countdown_rpm
            },
            cockpit: s.camera().mode == 2,
            turret_yaw: p.physics.pose_input.turret_yaw,
        })
    }
}
#[derive(Debug, Clone, PartialEq)]
pub struct Quad {
    pub resource: usize,
    pub positions: [[f32; 2]; 4],
    pub uv: [[f32; 2]; 4],
    pub color: [u8; 4],
    pub blend: bool,
}
fn trunc_shift(v: i32, s: u32) -> i32 {
    if v < 0 {
        v.wrapping_neg().wrapping_shr(s).wrapping_neg()
    } else {
        v.wrapping_shr(s)
    }
}
fn rect(
    out: &mut Vec<Quad>,
    resource: usize,
    pos: [i32; 2],
    size: [i32; 2],
    uv: [i32; 4],
    rgb: u32,
) {
    quad(
        out,
        resource,
        pos,
        [0, 0, size[0], size[1]],
        uv,
        rgb,
        None,
        false,
    );
}
#[allow(clippy::too_many_arguments)] // The original sprite primitive has independent rect/UV/style.
fn quad(
    out: &mut Vec<Quad>,
    resource: usize,
    pos: [i32; 2],
    bounds: [i32; 4],
    uv: [i32; 4],
    rgb: u32,
    angle: Option<u32>,
    blend: bool,
) {
    let dim = if resource == 2 {
        128.
    } else if (17..=19).contains(&resource) {
        32.
    } else {
        64.
    };
    let positions = std::array::from_fn(|i| {
        let a = if i == 0 || i == 3 {
            bounds[0]
        } else {
            bounds[2]
        };
        let b = if i < 2 { bounds[1] } else { bounds[3] };
        let offset = if let Some(angle) = angle {
            let c = tankracer_game::math::cosine(angle);
            let s = tankracer_game::math::sine(angle);
            [
                ((c.wrapping_mul(a).wrapping_sub(s.wrapping_mul(b))) >> 10) as f32 / 4.,
                ((s.wrapping_mul(a).wrapping_add(c.wrapping_mul(b))) >> 10) as f32 / 4.,
            ]
        } else {
            [a as f32, b as f32]
        };
        [pos[0] as f32 + offset[0], pos[1] as f32 + offset[1]]
    });
    out.push(Quad {
        resource,
        positions,
        uv: std::array::from_fn(|i| {
            [
                if i == 0 || i == 3 {
                    uv[0] as f32 / dim
                } else {
                    uv[2] as f32 / dim
                },
                if i < 2 {
                    uv[1] as f32 / dim
                } else {
                    uv[3] as f32 / dim
                },
            ]
        }),
        color: [
            (rgb >> 16) as u8,
            (rgb >> 8) as u8,
            rgb as u8,
            if blend { 128 } else { 255 },
        ],
        blend,
    });
}
fn glyph(out: &mut Vec<Quad>, ch: u8, pos: [i32; 2], large: bool, rgb: u32) {
    if large {
        if ch.is_ascii_digit() {
            let i = i32::from(ch - b'0');
            rect(
                out,
                2,
                pos,
                [31, 32],
                [
                    (i & 3) * 32,
                    (i >> 2) * 32,
                    (i & 3) * 32 + 31,
                    (i >> 2) * 32 + 32,
                ],
                rgb,
            );
        }
        return;
    }
    if ch.is_ascii_digit() {
        let i = i32::from(ch - b'0');
        rect(
            out,
            2,
            pos,
            [16, 16],
            [
                (i & 3) * 16 + 64,
                (i >> 2) * 16 + 65,
                (i & 3) * 16 + 80,
                (i >> 2) * 16 + 81,
            ],
            rgb,
        );
        return;
    }
    if (160..=163).contains(&ch) {
        let p = [[32, 32], [48, 32], [0, 48], [16, 48]][usize::from(ch - 160)];
        rect(
            out,
            2,
            pos,
            [16, 16],
            [p[0] + 64, p[1] + 65, p[0] + 80, p[1] + 81],
            rgb,
        );
        return;
    }
    let ch = ch.to_ascii_uppercase();
    let (resource, u, v) = match ch {
        b'A'..=b'P' => {
            let i = i32::from(ch - b'A');
            (0, (i & 3) * 16, (i >> 2) * 16)
        }
        b'Q'..=b'Z' => {
            let i = i32::from(ch - b'Q');
            (1, (i & 3) * 16, (i >> 2) * 16)
        }
        b':' => (1, 32, 32),
        b'/' => (1, 48, 32),
        b'-' => (1, 0, 48),
        b'?' => (1, 16, 48),
        _ => return,
    };
    rect(
        out,
        resource,
        pos,
        [16, 16],
        [u, v + 1, u + 16, v + 17],
        rgb,
    );
}
fn text(out: &mut Vec<Quad>, s: &[u8], mut pos: [i32; 2], rgb: u32) {
    for &ch in s {
        glyph(out, ch, pos, false, rgb);
        pos[0] += i32::from(tables::ADVANCE[usize::from(ch)]);
    }
}
pub fn plan(s: Snapshot) -> Result<Vec<Quad>, &'static str> {
    if s.map >= 8 {
        return Err("Invalid HUD map");
    }
    let mut out = Vec::with_capacity(40);
    let white = 0xffffff;
    let gold = 0xd2c31e;
    rect(&mut out, 4, [560, 380], [64, 64], [0, 0, 64, 64], white);
    rect(&mut out, 5, [16, 400], [64, 64], [0, 0, 64, 64], white);
    if s.cockpit {
        rect(&mut out, 16, [288, 400], [64, 64], [0, 0, 64, 64], white);
    }
    let needle = trunc_shift(s.needle.wrapping_abs().wrapping_mul(120), 10).min(32768);
    quad(
        &mut out,
        3,
        [592, 418],
        [-2, -23, 2, 11],
        [0, 0, 5, 35],
        white,
        Some(needle.wrapping_sub(16384) as u32),
        false,
    );
    if s.cockpit {
        quad(
            &mut out,
            3,
            [319, 431],
            [-7, -23, 7, 9],
            [49, 0, 63, 32],
            white,
            Some(i32::from(s.turret_yaw).wrapping_neg() as u32),
            false,
        );
    }
    let scale = tables::SCALE[s.map];
    let [x, z, _] = tables::ORIGIN[s.map];
    let marker = [
        16 + 28
            + trunc_shift(
                s.position[0]
                    .wrapping_sub(x)
                    .wrapping_mul((scale & 65535) as i32),
                scale >> 16,
            ),
        400 + 28
            + trunc_shift(
                z.wrapping_sub(s.position[2])
                    .wrapping_mul((scale & 65535) as i32),
                scale >> 16,
            ),
    ];
    rect(&mut out, 3, marker, [8, 8], [0, 56, 8, 64], white);
    if s.countdown != 0 {
        let digit = if s.countdown > 2000 {
            3
        } else if s.countdown > 1000 {
            2
        } else {
            1
        };
        let size = ((s.countdown - (digit - 1) * 1000) >> 3) + 32;
        quad(
            &mut out,
            2,
            [320 - (size >> 1), 240 - (size >> 1)],
            [0, 0, size, size],
            [
                (digit & 3) * 32,
                (digit >> 2) * 32,
                (digit & 3) * 32 + 31,
                (digit >> 2) * 32 + 32,
            ],
            white,
            None,
            true,
        );
    }
    if s.release != 0 {
        let size = (s.release >> 3) + 32;
        let x = 320 - (((size >> 1) + size * 2) >> 1);
        let y = 240 - (size >> 1);
        for i in 0..3 {
            quad(
                &mut out,
                17 + i as usize,
                [x + i * size, y],
                [0, 0, size, size],
                [0, 0, 31, 32],
                white,
                None,
                true,
            );
        }
    }
    // This runnable slice has not crossed a lap or added opponents. These are
    // the actual initial lap/rank, not a fabricated progression/standings model.
    glyph(&mut out, b'1', [16, 16], true, gold);
    text(&mut out, b"/3", [48, 32], gold);
    text(&mut out, b"LAP", [26, 48], gold);
    let minutes = ((s.elapsed / 60000) as u32 & 255).min(99);
    let separator = if (s.elapsed / 500) & 1 != 0 { ' ' } else { ':' };
    let timer = format!(
        "{minutes}:{:02}{separator}{:02}",
        (s.elapsed / 1000) % 60,
        (s.elapsed / 10) % 100
    );
    text(
        &mut out,
        timer.as_bytes(),
        [520 + if minutes < 10 { 16 } else { 0 }, 48],
        gold,
    );
    glyph(&mut out, b'1', [576, 16], true, gold);
    glyph(&mut out, 160, [608, 16], false, gold);
    let speed = trunc_shift(
        ((s.speed.wrapping_abs() >> 6).wrapping_mul(1000) >> 10).wrapping_mul(3600) / 1000,
        8,
    );
    text(
        &mut out,
        (speed as u32).to_string().as_bytes(),
        [
            544 + if speed < 10 {
                32
            } else if speed < 100 {
                16
            } else {
                0
            },
            448,
        ],
        gold,
    );
    // The current original setting selects native unit glyph 180.
    rect(&mut out, 2, [592, 448], [31, 16], [32, 96, 63, 112], gold);
    Ok(out)
}
