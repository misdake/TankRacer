//! 41DC00/41D460: mutable mesh fragments, including paired-face state and
//! native two-draw scatter. Resource loading and virtual completion stay owned
//! by the scene; no vertex/center copy is made during a simulation update.
use crate::{
    fixed::{Coefficient, trunc_shift},
    geometry::approximate_distance,
    math::{self, Matrix},
    obstacle_startup::Auxiliary,
    random,
    scene_meshes::FragmentMesh,
};
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Face {
    pub velocity: [i32; 3],
    pub angular: [i32; 3],
    pub life: i32,
    pub extra_vertex: i16,
    /// 0: pair head, 1: skipped pair tail, 2: single triangle.
    pub mode: u16,
}
#[derive(Clone, Debug)]
pub struct State {
    pub faces: Vec<Face>,
    pub retired: u32,
    pub ground_height: i32,
    /// Persistent prepared resources (used by later capture/restore classes).
    pub prepared: bool,
    pub lifetime: i32,
}
impl State {
    pub fn new(count: usize) -> Self {
        Self {
            faces: vec![Face::default(); count],
            retired: 0,
            ground_height: 0,
            prepared: false,
            lifetime: 2000,
        }
    }
}
pub struct Hit {
    pub velocity: Option<[i32; 3]>,
    pub basis: Option<Matrix>,
    pub height: i32,
    /// Explicit caller-owned stack tail for degenerate paired triangles.
    pub paired_tail: Option<u16>,
}
fn random_signed(seed: &mut u32) -> i32 {
    let sign = random::next(seed);
    let value = ((random::next(seed) >> 5) & 32767) as i32;
    if sign & 32 != 0 {
        value + 20000
    } else {
        -20000 - value
    }
}
fn scatter(seed: &mut u32, gain: Coefficient, speed: i32, angular: bool) -> i32 {
    let draw = |seed: &mut u32| {
        if angular {
            random_signed(seed)
        } else {
            ((random::next(seed) >> 4) & 65535) as i32 - 32767
        }
    };
    let product = draw(seed)
        .wrapping_mul(i32::from(gain.multiplier))
        .wrapping_mul(speed);
    let negative = product < 0;
    let product = draw(seed)
        .wrapping_mul(i32::from(gain.multiplier))
        .wrapping_mul(speed);
    // The FIRST product chooses the sign branch; the SECOND is shifted.
    if negative {
        product
            .wrapping_neg()
            .wrapping_shr(u32::from(gain.shift))
            .wrapping_neg()
    } else {
        product.wrapping_shr(u32::from(gain.shift))
    }
}
fn work(used: &mut u32, limit: u32) -> Result<(), i32> {
    if *used >= limit {
        Err(-3)
    } else {
        *used += 1;
        Ok(())
    }
}
pub fn activate(
    state: &mut State,
    mesh: FragmentMesh<'_>,
    config: &Auxiliary,
    hit: Hit,
    seed: &mut u32,
    limit: u32,
) -> Result<(), i32> {
    if limit == 0 {
        return Err(-1);
    }
    if state.faces.len() != mesh.geometry.triangles.len() {
        return Err(-1);
    }
    let mut velocity = hit.velocity.unwrap_or([0; 3]).map(|v| trunc_shift(v, 6));
    if let Some(basis) = hit.basis {
        velocity = basis.world_to_local(velocity);
    }
    let speed = if hit.velocity.is_none() {
        100
    } else {
        approximate_distance(velocity) >> 6
    };
    let push = Coefficient::from(config.push);
    velocity = velocity.map(|v| push.apply_truncated(v));
    state.ground_height = hit.height;
    let mut used = 0;
    for face in &mut state.faces {
        work(&mut used, limit)?;
        face.velocity = std::array::from_fn(|i| {
            velocity[i].wrapping_add(scatter(seed, config.velocity.into(), speed, false))
        });
        face.angular = std::array::from_fn(|_| scatter(seed, config.spin.into(), speed, true));
        let r = random::next(seed);
        face.mode = 2;
        face.extra_vertex = -1;
        face.life = (((r >> 4) & 1023) as i32).wrapping_add(state.lifetime);
    }
    let mut tail = hit.paired_tail;
    let mut i = 0;
    while i + 1 < state.faces.len() {
        work(&mut used, limit)?;
        mesh.geometry.triangles[i].flags |= 1;
        mesh.geometry.triangles[i + 1].flags |= 1;
        let first = mesh.geometry.triangles[i].indices;
        let second = mesh.geometry.triangles[i + 1].indices;
        if first.iter().any(|v| second.contains(v)) {
            for v in second {
                if !first.contains(&v) {
                    tail = Some(u16::from(v));
                }
            }
            state.faces[i].mode = 0;
            i += 1;
            state.faces[i].mode = 1;
            state.faces[i].extra_vertex = tail.ok_or(-4)? as i16;
        }
        i += 1;
    }
    *mesh.radius = 5120;
    state.retired = 0;
    Ok(())
}
fn vertex(
    vertices: &mut [[i16; 3]],
    index: usize,
    step: [i32; 3],
    center: [i32; 3],
    angular: [i32; 3],
    dt: i32,
) -> Result<(), i32> {
    let point = vertices.get_mut(index).ok_or(-1)?;
    for i in 0..3 {
        point[i] = point[i].wrapping_add(step[i] as i16);
    }
    let v = std::array::from_fn::<_, 3, _>(|i| i32::from(point[i]).wrapping_sub(center[i]));
    let rotation = |axis| {
        let angle = dt.wrapping_mul(angular[axis]) as u32;
        (math::cosine(angle), math::sine(angle))
    };
    let (c, s) = rotation(1);
    let x = trunc_shift(c.wrapping_mul(v[0]).wrapping_sub(s.wrapping_mul(v[2])), 12);
    let z = trunc_shift(c.wrapping_mul(v[2]).wrapping_add(s.wrapping_mul(v[0])), 12);
    let (c, s) = rotation(0);
    let y = trunc_shift(c.wrapping_mul(v[1]).wrapping_add(s.wrapping_mul(z)), 12);
    let z = trunc_shift(c.wrapping_mul(z).wrapping_sub(s.wrapping_mul(v[1])), 12);
    let (c, s) = rotation(2);
    let result = [
        trunc_shift(c.wrapping_mul(x).wrapping_sub(s.wrapping_mul(y)), 12),
        trunc_shift(c.wrapping_mul(y).wrapping_add(s.wrapping_mul(x)), 12),
        z,
    ];
    for i in 0..3 {
        point[i] = center[i].wrapping_add(result[i]) as i16;
    }
    Ok(())
}
/// Returns completion. A pair spends one tick loop step and retires two faces.
pub fn tick(
    state: &mut State,
    mesh: FragmentMesh<'_>,
    config: &Auxiliary,
    position: [i32; 3],
    dt: i32,
    limit: u32,
) -> Result<bool, i32> {
    if limit == 0 {
        return Err(-1);
    }
    let mut i = 0;
    let mut used = 0;
    while i < state.faces.len() {
        work(&mut used, limit)?;
        let paired = state.faces[i].mode == 0;
        let face = &mut state.faces[i];
        if face.life > 0 {
            face.life = face.life.wrapping_sub(dt);
            if face.life <= 0 {
                state.retired = state.retired.wrapping_add(1);
                mesh.geometry.triangles.get_mut(i).ok_or(-1)?.flags |= 64;
                if paired {
                    state.retired = state.retired.wrapping_add(1);
                    mesh.geometry.triangles.get_mut(i + 1).ok_or(-1)?.flags |= 64;
                }
            } else {
                // Gravity is per update here; unlike debris it has no dt scale.
                face.velocity[1] = face.velocity[1].wrapping_sub(config.gravity);
                let tri = mesh.geometry.triangles.get(i).ok_or(-1)?.indices;
                let mut minimum = i16::MAX;
                for v in tri {
                    minimum = minimum.min(mesh.geometry.vertices.get(v as usize).ok_or(-1)?[1]);
                }
                if position[1].wrapping_add(i32::from(minimum).wrapping_mul(8))
                    <= state.ground_height
                    && face.velocity[1] < 0
                {
                    // Source alternates friction/bounce across all SIX words,
                    // including angular X and Z; preserve its unusual policy.
                    for (j, value) in face
                        .velocity
                        .iter_mut()
                        .chain(&mut face.angular)
                        .enumerate()
                    {
                        let scaled = Coefficient::from(if j & 1 == 0 {
                            config.friction
                        } else {
                            config.bounce
                        })
                        .apply_truncated(*value);
                        *value = if j & 1 == 0 {
                            scaled
                        } else {
                            scaled.wrapping_neg()
                        };
                    }
                }
                let step = face.velocity.map(|v| trunc_shift(v.wrapping_mul(dt), 11));
                let mut center = [0; 3];
                for (axis, value) in center.iter_mut().enumerate() {
                    *value = i32::from(
                        *mesh.centers[axis]
                            .as_ref()
                            .and_then(|v| v.get(i))
                            .ok_or(-1)?,
                    );
                }
                let angular = face.angular;
                for v in tri {
                    vertex(
                        &mut mesh.geometry.vertices,
                        v as usize,
                        step,
                        center,
                        angular,
                        dt,
                    )?;
                }
                if paired {
                    let extra = state.faces.get(i + 1).ok_or(-1)?.extra_vertex;
                    vertex(
                        &mut mesh.geometry.vertices,
                        extra as usize,
                        step,
                        center,
                        angular,
                        dt,
                    )?;
                }
                for (axis, delta) in step.into_iter().enumerate() {
                    let value = mesh.centers[axis]
                        .as_mut()
                        .and_then(|v| v.get_mut(i))
                        .ok_or(-1)?;
                    *value = value.wrapping_add(delta as i16);
                }
            }
        }
        i += if paired { 2 } else { 1 };
    }
    Ok(state.retired == state.faces.len() as u32)
}
