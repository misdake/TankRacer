use super::*;
#[derive(Debug)]
pub struct Emitter {
    pub timer: i32,
    pub gun: bool,
    pub size: i32,
    pub offset: [i32; 3],
    pub pitch: i32,
    pub trigger: u32,
    pub bounds: [i32; 4],
    pub period: i32,
    pub model: Option<Arc<Model>>,
    pub texture: Option<String>,
    pub color: [u8; 3],
    pub alpha: u8,
    pub glow: [i32; 3],
    pub asteroid: bool,
}
impl Emitter {
    pub fn construct<S: ShapeSource>(
        p: Parameters<'_>,
        pose: Pose,
        r: &mut Resources<'_, S>,
    ) -> Result<Self, Box<dyn Error>> {
        let timer = ((crate::random::next(r.random) >> 4) & 1023) as i32;
        let style = p.find(":bomber_type").and_then(|mut v| v.token_word());
        let gun = style.is_some_and(|s| s.eq_ignore_ascii_case("GUN"));
        let mut size = if gun {
            2000
        } else if style.is_some_and(|s| s.eq_ignore_ascii_case("SKY")) {
            3000
        } else if style.is_none() {
            4000
        } else {
            0
        };
        if let Some(mut v) = p.find(":size") {
            size = real(&mut v)?;
        }
        let mut offset = integer3(p, ":offset")?
            .map(|v| {
                if gun {
                    pose.matrix.local_to_world(v)
                } else {
                    v
                }
            })
            .unwrap_or([-59392, 76800, 0]);
        let mut bounds = [0; 4];
        let mut trigger = 0;
        if let Some(mut v) = p.find(":trigger_type")
            && v.token_word()
                .is_some_and(|s| s.eq_ignore_ascii_case("BOX"))
        {
            trigger = 1;
            let x = real(&mut v)?;
            let z = real(&mut v)?;
            let w = real(&mut v)?;
            let h = real(&mut v)?;
            bounds = [
                x.wrapping_sub(w >> 1),
                z.wrapping_sub(h >> 1),
                x.wrapping_add(w >> 1),
                z.wrapping_add(h >> 1),
            ];
        }
        let model = p
            .find(":bomb_shape")
            .map(|mut v| r.shape(v.token_word().ok_or("Missing bomb shape")?))
            .transpose()?;
        let color = integer3(p, ":bomb_col")?
            .unwrap_or([128, 50, 50])
            .map(|v| v as u8);
        let alpha = match p.find(":bomb_alpha").and_then(|mut v| v.token_word()) {
            Some(s) if s.eq_ignore_ascii_case("NONE") => 255,
            Some(s) if s.eq_ignore_ascii_case("ADD") => 1,
            None => 1,
            _ => 0,
        };
        let asteroid = p.find(":asteroid").is_some();
        if asteroid {
            offset = [-37888, 30720, 0];
        }
        Ok(Self {
            timer,
            gun,
            size,
            offset,
            pitch: p.integer(":x_angle").unwrap_or(0).wrapping_mul(182),
            trigger,
            bounds,
            period: p
                .integer(":bomb_times")
                .map_or(2500, |v| v.wrapping_mul(1000)),
            model,
            texture: p
                .find(":bomb_tex")
                .and_then(|mut v| v.token_word())
                .map(str::to_owned),
            color,
            alpha,
            glow: integer3(p, ":bomb_glow_offset")?.unwrap_or_default(),
            asteroid,
        })
    }
}
