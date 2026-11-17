use super::*;
#[derive(Debug, Clone, Copy)]
pub struct Bird {
    pub pose: Pose,
    pub speed: i32,
    pub active: bool,
}
#[derive(Debug)]
pub struct Flock {
    pub kind: u32,
    pub range: i32,
    pub maneuver: i16,
    pub birds: [Bird; 8],
    pub models: Vec<Arc<Model>>,
    pub extrema: Vec<[usize; 2]>,
}
impl Flock {
    pub fn construct<S: ShapeSource>(
        p: Parameters<'_>,
        pose: &mut Pose,
        r: &mut Resources<'_, S>,
    ) -> Result<Self, Box<dyn Error>> {
        let kind = if p.find(":bat").is_some() {
            2
        } else if p.find(":chicken").is_some() {
            3
        } else {
            1
        };
        let shape = match kind {
            2 => "batty.sm",
            3 => "chicken.sm",
            _ => "birdy2.sm",
        };
        let source = r.shape(shape)?;
        let mut models = if kind == 3 {
            vec![source]
        } else {
            vec![Arc::new((*source).clone()), Arc::new((*source).clone())]
        };
        for model in &mut models {
            let m = Arc::make_mut(model);
            m.radius = m.radius.wrapping_shl(1);
            m.render_hint = 150;
        }
        let extrema = if kind == 3 {
            vec![]
        } else {
            models
                .iter()
                .map(|m| {
                    let mut lo = 0;
                    let mut hi = 0;
                    for (i, p) in m.geometry.vertices.iter().enumerate() {
                        if p[0] > m.geometry.vertices[hi][0] {
                            hi = i;
                        } else if p[0] < m.geometry.vertices[lo][0] {
                            lo = i;
                        }
                    }
                    [lo, hi]
                })
                .collect()
        };
        let mut ground = crate::terrain::MISS_HEIGHT;
        crate::terrain::height(r.terrain, pose.position[0], pose.position[2], &mut ground)
            .map_err(|e| format!("Flock ground: {e:?}"))?;
        pose.position[1] = ground;
        let maneuver = p.integer(":manu").unwrap_or(68) as i16;
        let birds = std::array::from_fn(|_| {
            let x = ((crate::random::next(r.random) >> 4) & 8191) as i32 - 4096;
            let z = ((crate::random::next(r.random) >> 4) & 8191) as i32 - 4096;
            let yaw = (crate::random::next(r.random) >> 4) & 65535;
            let speed = (((crate::random::next(r.random) >> 4) & 63) as i32)
                .wrapping_add(i32::from(maneuver));
            Bird {
                pose: Pose {
                    position: [
                        pose.position[0].wrapping_add(x),
                        ground.wrapping_add(250),
                        pose.position[2].wrapping_add(z),
                    ],
                    matrix: Matrix::from_angles([0, yaw, 0]),
                },
                speed,
                active: true,
            }
        });
        Ok(Self {
            kind,
            range: p.integer(":activate_range").unwrap_or(20).wrapping_shl(10),
            maneuver: maneuver.wrapping_add(32),
            birds,
            models,
            extrema,
        })
    }
}
