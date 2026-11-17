use super::*;
#[derive(Debug)]
pub enum Motion {
    Path {
        start: [i32; 3],
        end: [i32; 3],
        velocity: [i32; 3],
        extra: bool,
        center: [i32; 3],
    },
    Eagle {
        radius: i32,
        reverse: bool,
    },
    Ferris {
        radius: i32,
    },
    Pirate,
    Yacht {
        rates: [i32; 3],
    },
    Lander,
}
#[derive(Debug)]
pub struct Train {
    pub motion: Motion,
    pub speed: i32,
    pub range: i32,
    pub models: Vec<Arc<Model>>,
    pub propeller: Option<Propeller>,
    pub base_matrix: Matrix,
    pub sound: Option<String>,
    pub origin: [i32; 3],
}
#[derive(Debug)]
pub struct Propeller {
    pub pivot: [i32; 3],
    pub target: i16,
    pub timer: i16,
    pub matrix: Matrix,
    pub rotate_z: bool,
}
impl Train {
    pub fn construct<S: ShapeSource>(
        p: Parameters<'_>,
        pose: &mut Pose,
        r: &mut Resources<'_, S>,
    ) -> Result<Self, Box<dyn Error>> {
        let origin = pose.position;
        let mut speed = p
            .integer(":Speed")
            .or_else(|| p.integer(":speed"))
            .unwrap_or(3);
        let mut range = 160_i32.wrapping_shl(10);
        let mut models = vec![];
        let mut propeller = None;
        let motion = if p.find(":Eagle").is_some() {
            let radius = p
                .integer(":radius")
                .or_else(|| p.integer(":Radius"))
                .map(|v| v.wrapping_shl(10));
            if let Some(radius) = radius {
                speed = eagle_rate(radius, speed)?;
            }
            Motion::Eagle {
                radius: radius.unwrap_or(0),
                reverse: p.find(":Reverse").or_else(|| p.find(":reverse")).is_some(),
            }
        } else if p.find(":Ferris_Wheel").is_some() {
            for k in [":Car", ":Base"] {
                if let Some(mut v) = p.find(k) {
                    models.push(r.shape(v.token_word().ok_or("Missing train shape")?)?);
                }
            }
            Motion::Ferris {
                radius: p.integer(":Radius").unwrap_or(0).wrapping_shl(10),
            }
        } else if p.find(":Pirate_Ship").is_some() {
            speed = speed.wrapping_mul(182);
            Motion::Pirate
        } else if p.find(":Yacht").is_some() {
            let rates = std::array::from_fn(|_| {
                let a = ((crate::random::next(r.random) >> 4) & 3) as i32;
                let b = crate::random::next(r.random);
                (a + 5) * 100 * if b & 16 != 0 { 1 } else { -1 }
            });
            speed = rates[0];
            Motion::Yacht { rates }
        } else if p.find(":Lunar_lander").is_some() {
            Motion::Lander
        } else {
            let mut start = point(p, ":start_pos")?
                .or(point(p, ":Start_pos")?)
                .unwrap_or_default();
            let mut end = point(p, ":end_pos")?
                .or(point(p, ":End_pos")?)
                .unwrap_or_default();
            if p.find(":start_pos").is_some() || p.find(":Start_pos").is_some() {
                pose.position = start;
            }
            let extra = p.find(":extra_dist").is_some();
            let mut center = [0; 3];
            if extra {
                let half = [
                    start[0].wrapping_sub(end[0]) >> 1,
                    0,
                    start[2].wrapping_sub(end[2]) >> 1,
                ];
                range =
                    range.wrapping_add(crate::geometry::approximate_distance(half).wrapping_mul(2));
                for i in [0, 2] {
                    let d = start[i].wrapping_sub(end[i]) >> 1;
                    center[i] = end[i].wrapping_add(d);
                    start[i] = start[i].wrapping_add(d);
                    end[i] = end[i].wrapping_sub(d);
                }
            }
            let mut velocity = std::array::from_fn(|i| end[i].wrapping_sub(start[i]));
            if p.find(":Obs_Plat").is_some() {
                velocity = [
                    0,
                    if start[1] < end[1] {
                        speed
                    } else {
                        speed.wrapping_neg()
                    },
                    0,
                ];
            } else {
                crate::vector::normalize(&mut velocity);
                velocity[0] = velocity[0].wrapping_mul(speed);
                velocity[2] = velocity[2].wrapping_mul(speed);
            }
            if p.find(":Obs_Plat").is_none()
                && let Some(mut v) = p.find(":Propeller")
            {
                models.push(r.shape(v.token_word().ok_or("Missing propeller")?)?);
                let pivot = [int(&mut v)?, int(&mut v)?, int(&mut v)?];
                let timer = (((crate::random::next(r.random) >> 4) & 255) + 144) as i16;
                propeller = Some(Propeller {
                    pivot,
                    target: 400,
                    timer,
                    matrix: pose.matrix,
                    rotate_z: p.find(":RotZ").is_some(),
                });
            }
            Motion::Path {
                start,
                end,
                velocity,
                extra,
                center,
            }
        };
        // The explicit cue range overrides any extra-distance expansion last.
        if let Some(value) = p.integer(":cue_range").or_else(|| p.integer(":Cue_range")) {
            range = value.wrapping_shl(10);
        }
        let base_matrix = if matches!(motion, Motion::Ferris { .. }) {
            let yaw = crate::math::angle(pose.matrix.0[6], pose.matrix.0[8])
                .map_err(|e| format!("Ferris basis: {e:?}"))?;
            Matrix::from_angles([0, yaw.wrapping_neg() as u32, 0])
        } else {
            pose.matrix
        };
        Ok(Self {
            motion,
            speed,
            range,
            models,
            propeller,
            base_matrix,
            sound: p
                .find(":Sound")
                .and_then(|mut v| v.token_word())
                .map(str::to_owned),
            origin,
        })
    }
}

#[allow(clippy::approx_constant)] // Source x87 constant; substituting PI changes the rate.
fn eagle_rate(radius: i32, speed: i32) -> Result<i32, Box<dyn Error>> {
    let twice = radius.wrapping_mul(2);
    if twice == 0 || speed == 0 {
        return Ok(0);
    }
    let value = 65535.0 / (f64::from(twice) * 3.141593 / f64::from(speed));
    let magnitude = value.abs();
    if magnitude >= 9223372036854775808.0 {
        return Ok(0);
    }
    let whole = magnitude as u64;
    let fraction = magnitude - whole as f64;
    let error = (magnitude + 1.0) * 2_f64.powi(-48);
    if fraction <= error || 1.0 - fraction <= error {
        return Err("Eagle rate too close to an x87 truncation boundary".into());
    }
    let rate = whole as u32;
    Ok(if value < 0.0 {
        rate.wrapping_neg()
    } else {
        rate
    } as i32)
}
