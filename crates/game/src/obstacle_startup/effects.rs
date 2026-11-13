use super::{Environment, Error, Resources, ShapeSource};
use crate::vehicle_model::Space;
use crate::{
    math::{self, Matrix},
    object_query::EntityId,
    pose::Pose,
    random, scene_startup, terrain,
};
use std::sync::Arc;
use tankracer_assets::{
    model::Model,
    parameters::{Parameters, Values, fixed1024},
};

/// Logical resource identity supplied by the asset owner, never a WebGL object.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MaterialId(pub u32);
pub trait Materials {
    fn resolve(&mut self, name: &str, mode: u32) -> Result<Option<MaterialId>, i32>;
}
#[derive(Debug)]
pub struct Sprite {
    pub material: Option<MaterialId>,
    pub position: [i32; 3],
    pub flags: u32,
    pub timer: i32,
    pub size: [i32; 2],
    pub uv: [u8; 4],
    pub color: [u8; 3],
}
#[derive(Debug)]
pub struct Animation {
    pub material: Option<MaterialId>,
    pub position: [i32; 3],
    pub blend: i32,
    pub sheet_size: u16,
    pub cell_size: u16,
    pub initial_size: i32,
    pub cycles: u16,
    pub growth: i16,
    pub timer: i16,
    pub cycle: u16,
    pub size: [i32; 2],
    pub uv: [u8; 4],
    pub color: [u8; 3],
}
#[derive(Debug)]
pub struct Piece {
    pub source: Option<Arc<Model>>,
    pub position: [i32; 3],
    pub velocity: [i32; 3],
    pub matrix: Matrix,
    pub translation: [i32; 3],
    pub angular: [i32; 3],
    pub previous_tilt: i32,
    pub velocity_gain: i16,
    pub spin_gain: i16,
    pub flags: u32,
    pub sprite_angle: i32,
}
#[derive(Debug)]
pub struct Debris {
    pub pieces: Vec<Piece>,
    pub retired: u16,
    pub flags: u32,
    pub sprite: Option<Sprite>,
    pub animation: Option<Animation>,
    pub offset: [i32; 3],
    pub ground_height: i32,
    pub gravity: i32,
    pub friction: i32,
    pub direction: i32,
}
fn value(v: &mut Values<'_>) -> Result<i32, Error> {
    scene_startup::value(v)
}
fn word<'a>(v: &mut Values<'a>, capacity: usize) -> Result<&'a str, Error> {
    let result = v.word().ok_or(Error::MissingValue)?;
    if result.len() >= capacity {
        return Err(Error::Parameters);
    }
    Ok(result)
}
fn material(
    v: &mut Values<'_>,
    capacity: usize,
    mode: u32,
    env: &mut Environment<'_>,
) -> Result<Option<MaterialId>, Error> {
    env.materials
        .resolve(word(v, capacity)?, mode)
        .map_err(Error::Material)
}
fn animation(
    p: Parameters<'_>,
    entity: EntityId,
    pose: Pose,
    env: &mut Environment<'_>,
) -> Result<Option<Animation>, Error> {
    let Some(mut v) = p.find(":anim") else {
        return Ok(None);
    };
    let name = word(&mut v, 32)?;
    let blend = word(&mut v, 8)?;
    // Immutable original strings at 52E1C8/4/0: NONE/SUB/ADD. Keep the
    // original unknown-token fallback explicit instead of guessing a blend.
    let blend = if blend.eq_ignore_ascii_case("NONE") {
        -1
    } else if blend.eq_ignore_ascii_case("SUB") {
        0
    } else if blend.eq_ignore_ascii_case("ADD") {
        1
    } else {
        entity.0 as i32
    };
    let material = env.materials.resolve(name, 1).map_err(Error::Material)?;
    let sheet_size = value(&mut v)? as u16;
    let cell_size = value(&mut v)? as u16;
    let initial_size = value(&mut v)?;
    let cycles = value(&mut v)? as u16;
    let growth = value(&mut v)? as i16;
    Ok(Some(Animation {
        material,
        position: pose.position,
        blend,
        sheet_size,
        cell_size,
        initial_size,
        cycles,
        growth,
        timer: 0,
        cycle: 0,
        size: [0; 2],
        uv: [0, 0, cell_size as u8, cell_size as u8],
        color: [255; 3],
    }))
}
impl Debris {
    pub(super) fn build<S: ShapeSource>(
        p: Parameters<'_>,
        entity: EntityId,
        pose: Pose,
        r: &mut Resources<'_, S>,
        env: &mut Environment<'_>,
    ) -> Result<Option<Self>, Error> {
        let (mut v, sprite_mode) = if let Some(v) = p.find(":shatter3d") {
            (v, false)
        } else if let Some(v) = p.find(":shatter3dvfp") {
            (v, true)
        } else {
            return Ok(None);
        };
        let sprite = if sprite_mode {
            let material = material(&mut v, 64, 2, env)?;
            let size = value(&mut v)?;
            Some(Sprite {
                material,
                position: [0; 3],
                flags: u32::MAX,
                timer: 0,
                size: [size; 2],
                uv: [0, 0, 31, 31],
                color: [127; 3],
            })
        } else {
            None
        };
        let count = value(&mut v)? as i16;
        if count < 0 {
            return Err(Error::RecordCount);
        }
        let mut pieces = Vec::with_capacity(count as usize);
        for _ in 0..count {
            let source = if sprite_mode {
                None
            } else {
                // 500850 returns a null shape for an absent resource. Some
                // shipped records contain legacy N/D tokens; still consume
                // their numeric fields and all six random draws as native.
                r.models
                    .load_optional(r.source, Space::Level, word(&mut v, 64)?)
                    .map_err(Error::Shape)?
            };
            let velocity_gain = value(&mut v)? as i16;
            let spin_gain = value(&mut v)? as i16;
            let (matrix, translation, angular, sprite_angle) = if sprite_mode {
                let angle = ((random::next(r.random) >> 4) & 65535) as i32;
                let angular = ((random::next(r.random) >> 4) & 255) as i32 - 128;
                (Matrix([0; 9]), [0; 3], [0, angular, 0], angle)
            } else {
                let angles = std::array::from_fn(|_| (random::next(r.random) >> 4) & 65535);
                let matrix = Matrix::from_angles(angles);
                let translation = env.matrix_tail.ok_or(Error::MissingMatrixTail)?;
                let angular =
                    std::array::from_fn(|_| ((random::next(r.random) >> 4) & 255) as i32 - 128);
                (matrix, translation, angular, 0)
            };
            pieces.push(Piece {
                source,
                position: pose.position,
                velocity: [0; 3],
                matrix,
                translation,
                angular,
                previous_tilt: 0,
                velocity_gain,
                spin_gain,
                flags: 0,
                sprite_angle,
            });
        }
        let mut flags = if sprite_mode { 128 } else { 0 };
        for (name, mask) in [
            (":still_fall", 16),
            (":bang", 2),
            (":slower_explode", 2048),
            (":kick", 4),
        ] {
            if p.find(name).is_some() {
                flags |= mask;
            }
        }
        let animation = animation(p, entity, pose, env)?;
        if let Some(mut values) = p.find(":terrys") {
            flags |= 256;
            for piece in &mut pieces {
                let mut position = [0; 3];
                for (i, value) in position.iter_mut().enumerate() {
                    *value = fixed1024(values.real32().ok_or(Error::MissingValue)?);
                    if i == 1 && *value == 0 {
                        piece.flags |= 8;
                    }
                }
                piece.velocity = [0; 3];
                if piece.flags & 8 != 0 {
                    let angle =
                        math::angle(position[0], position[2]).map_err(|_| Error::Arithmetic)?;
                    piece.matrix =
                        Matrix::from_angles([0, (32760_i32.wrapping_sub(angle)) as u32, 0]);
                } else {
                    piece.matrix = pose.matrix;
                    piece.translation = [0; 3];
                }
                piece.angular = [if piece.flags & 8 != 0 { 8000 } else { 0 }, 0, 0];
                piece.position =
                    std::array::from_fn(|i| position[i].wrapping_add(pose.position[i]));
                piece.previous_tilt = -1;
            }
        }
        let offset = if let Some(mut values) = p.find(":shat3d_offset") {
            [
                value(&mut values)?.wrapping_shl(10),
                value(&mut values)?.wrapping_shl(10),
                value(&mut values)?.wrapping_shl(10),
            ]
        } else {
            [0; 3]
        };
        let mut direction = 0;
        if let Some(value) = p.integer(":bounce_dir") {
            direction = value.wrapping_mul(182);
            while direction > 32760 {
                direction = direction.wrapping_sub(65520);
            }
            while direction <= -32760 {
                direction = direction.wrapping_add(65520);
            }
            flags |= 64;
        }
        let ground_height = if let Some(height) = p.integer(":track_height") {
            height
        } else {
            let mut height = 0;
            terrain::height(r.terrain, pose.position[0], pose.position[2], &mut height)
                .map_err(Error::Terrain)?;
            height
        };
        Ok(Some(Self {
            pieces,
            retired: 0,
            flags,
            sprite,
            animation,
            offset,
            ground_height,
            gravity: p.integer(":shat3d_grav").unwrap_or(4096),
            friction: p.integer(":shat3d_fric").unwrap_or(2048),
            direction,
        }))
    }
}
