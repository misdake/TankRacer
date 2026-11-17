//! Initial pickup pose, parachute auxiliary and authored lap visibility.
use super::*;
use tankracer_assets::object_catalog;
#[derive(Debug)]
pub struct Pickup {
    pub kind: u32,
    pub model: Arc<Model>,
    pub parachute: Arc<Model>,
    pub ground: i32,
    pub fall_range: i32,
    pub phase: u16,
    pub added_lap: i16,
    pub regeneration: i32,
    pub restitution: [u32; 2],
    pub auxiliary: [u16; 6],
    pub mass: i32,
}
pub struct Setup {
    pub map: usize,
    pub cup: usize,
    pub config: object_catalog::Pickup,
    pub tilt: u8,
    pub lap: u8,
    pub geometry: Geometry,
}
#[derive(Debug, Default)]
pub struct Geometry {
    pub separation: i32,
    pub diameter: u32,
}
impl Default for Setup {
    fn default() -> Self {
        Self {
            map: 0,
            cup: 0,
            config: Default::default(),
            tilt: 0,
            lap: 1,
            geometry: Default::default(),
        }
    }
}
impl Pickup {
    pub fn construct<S: ShapeSource>(
        record: level::Object<'_>,
        p: Parameters<'_>,
        pose: &mut Pose,
        r: &mut Resources<'_, S>,
        setup: &mut Setup,
    ) -> Result<Self, Box<dyn Error>> {
        let kinds = [
            "",
            "cmine.sm",
            "cemine.sm",
            "chomer.sm",
            "cufo.sm",
            "cconc.sm",
            "",
            "cturbo.sm",
            "cshield.sm",
            "cshell.sm",
            "cwarp.sm",
        ];
        let authored_kind = kinds
            .iter()
            .position(|n| n.eq_ignore_ascii_case(record.shape))
            .ok_or("Unknown pickup model")? as u32;
        // CUP only publishes the pickups installed by load_resources. An
        // authored but unavailable model keeps the zero-initialized type.
        let kind = match authored_kind {
            9 => 0,
            2 if setup.cup == 0 => 0,
            10 if setup.cup != 2 => 0,
            kind => kind,
        };
        let model = r
            .models
            .load(r.source, Space::Shared, record.shape)
            .map_err(|e| format!("Pickup model: {e:?}"))?
            .ok_or("Missing pickup model")?;
        let parachute = r
            .models
            .load(r.source, Space::Shared, "Cpara.sm")
            .map_err(|e| format!("Pickup parachute: {e:?}"))?
            .ok_or("Missing Cpara.sm")?;
        if setup.geometry.separation == 0 {
            let height = |m: &Model| i32::from(m.bounds[1][1]) - i32::from(m.bounds[0][1]);
            setup.geometry.separation = (height(&parachute) + height(&model)).wrapping_mul(8);
        }
        if setup.geometry.diameter == 0 {
            setup.geometry.diameter = u32::from(model.radius) * 2;
        }
        let extra = setup.config.extra_range[setup.cup];
        let mut fall_range = 130_i32
            .wrapping_add(extra)
            .wrapping_add(if setup.map == 7 {
                setup.config.circuit_extra
            } else {
                0
            })
            .wrapping_mul(1024);
        if let Some(mut values) = p.find(":fall_range") {
            // A final token with no remaining delimiter does not publish its
            // integer. EAST ships this form: the original then scales the
            // existing setup range again, rather than using the final token.
            let value = values.integer().unwrap_or(fall_range);
            fall_range = value
                .wrapping_mul(1024)
                .wrapping_add(extra.wrapping_mul(1024));
        }
        pose.position[1] = pose.position[1].wrapping_add(0x5000);
        let words = [
            0xf6332499_u32,
            0x5e3b8a98,
            0x43297240,
            0x9c5ae86d,
            0x42afb54b,
            0x37fd96c4,
            0xe0feec81,
            0x00661412,
        ];
        let bytes = words.map(u32::to_le_bytes).concat();
        let angle = u32::from((bytes[usize::from(setup.tilt)] & 3) + 8) * 182;
        pose.matrix = Matrix::from_angles([angle, 0, angle]);
        setup.tilt = (setup.tilt + 1) % 31;
        let mut ground = crate::terrain::MISS_HEIGHT;
        crate::terrain::height(r.terrain, pose.position[0], pose.position[2], &mut ground)
            .map_err(|e| format!("Pickup terrain: {e:?}"))?;
        let phase = u16::from(setup.lap);
        if p.find(":incr_lap_for_next").is_some() {
            setup.lap = setup.lap.wrapping_add(1);
        }
        if let Some(mut v) = p.find(":aspect") {
            let mut angles = [0; 3];
            for a in &mut angles {
                *a = (f64::from(v.real32().ok_or("Missing pickup aspect")?) * 182.0).trunc() as i64
                    as u32;
            }
            pose.matrix = Matrix::from_angles(angles);
        }
        Ok(Self {
            kind,
            model,
            parachute,
            ground,
            fall_range,
            phase,
            added_lap: if phase == 2 || phase == 3 {
                phase as i16 - 1
            } else {
                0
            },
            regeneration: -1,
            restitution: [0x000a01f4, 0x000a0320],
            auxiliary: [150, 8, 1, 10, 5, 18],
            mass: 75,
        })
    }
}
