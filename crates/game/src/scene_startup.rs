//! Authored class construction. Resource resolution, geometry ownership and RNG
//! remain explicit; no original-memory arena or test snapshot initializes state.
use crate::{
    crush, deformable_hit, deformation,
    dispatch::ClassId,
    fixed::trunc_shift,
    geometry::Bounds,
    object_query::{Collider, EntityId, ObstacleId},
    obstacle_collision,
    pose::Pose,
    random,
    scene_collision::{DEFORMABLE_HIT, Deformable, Sampling, Surfaces},
    scene_meshes::{MeshId, Meshes},
    terrain,
    vehicle_model::{ModelBank, ShapeSource, Space},
};
use std::sync::Arc;
use tankracer_assets::{
    level,
    model::Model,
    parameters::{Parameters, Values},
    terrain::TerrainData,
};

pub struct Resources<'a, S: ShapeSource> {
    pub source: &'a S,
    pub models: &'a mut ModelBank,
    pub meshes: &'a mut Meshes,
    pub terrain: &'a TerrainData,
    pub random: &'a mut u32,
}
#[derive(Debug)]
pub enum Error {
    Actor(String),
    Parameters,
    MissingValue,
    Shape(crate::vehicle_model::Error),
    MissingShape,
    WrongClass,
    UnsupportedParentParameter(&'static str),
    RecordCount,
    Terrain(terrain::Error),
    InvalidWorld,
    Material(i32),
    MissingMatrixTail,
    Arithmetic,
}
#[derive(Debug, Clone, Copy)]
pub struct Visibility {
    pub distance: u16,
    pub add_lap: i16,
    pub remove_lap: i16,
    pub entered: bool,
}
pub struct Crush {
    pub collider: Collider,
    pub weight: i32,
    /// Immutable material/normal/bounds provenance; current vertices live only
    /// in the scene mesh store addressed by surface.mesh.
    pub source: Arc<Model>,
    pub surface: Deformable,
    pub visibility: Visibility,
    pub ground_height: i32,
}
/// Completed parent components, before Crush replaces geometry and allocates
/// deformation records. Memberships and parent-owned effects stay in the scene.
pub(crate) struct CrushParent {
    pub collider: Collider,
    pub source: Option<Arc<Model>>,
    pub mesh: Option<MeshId>,
    pub visibility: Visibility,
    pub weight: i32,
    pub sort_position: i16,
}
/// Noncollision components retained after installing the authoritative collider
/// and deformable state. The scheduler/render scene owns these handles too.
pub struct PlacedCrush {
    pub id: ObstacleId,
    pub source: Arc<Model>,
    pub visibility: Visibility,
    pub ground_height: i32,
}
pub(crate) fn value(values: &mut Values<'_>) -> Result<i32, Error> {
    values.integer().ok_or(Error::MissingValue)
}
pub(crate) fn shape<S: ShapeSource>(
    r: &mut Resources<'_, S>,
    name: &str,
) -> Result<Arc<Model>, Error> {
    r.models
        .load(r.source, Space::Level, name)
        .map_err(Error::Shape)?
        .ok_or(Error::MissingShape)
}
pub(crate) fn bounds(model: &Model, margin: i32) -> Bounds {
    Bounds {
        min: model.bounds[0].map(|v| i32::from(v).wrapping_mul(8).wrapping_sub(margin)),
        max: model.bounds[1].map(|v| i32::from(v).wrapping_mul(8).wrapping_add(margin)),
    }
}

pub(crate) fn visibility(p: Parameters<'_>, random: &mut u32) -> Result<Visibility, Error> {
    let mut visibility = Visibility {
        distance: 150,
        add_lap: 0,
        remove_lap: 0,
        entered: true,
    };
    if let Some(mut values) = p.find(":lap_to_add") {
        let mut lap = value(&mut values)?;
        if lap == -99 {
            lap = (((random::next(random) >> 4) & 65535) % 3 + 1) as i32;
        }
        if lap == 2 || lap == 3 {
            visibility.add_lap = (lap - 1) as i16;
            visibility.entered = false;
        }
    }
    if let Some(lap) = p.integer(":lap_to_remove") {
        visibility.remove_lap = lap.wrapping_sub(1) as i16;
    }
    Ok(visibility)
}
fn crush_mesh<S: ShapeSource>(
    p: Parameters<'_>,
    r: &mut Resources<'_, S>,
    parent: &mut CrushParent,
) -> Result<(Arc<Model>, MeshId, Bounds), Error> {
    let mut loaded = parent.source.take();
    let mut mesh = parent.mesh;
    let mut collider_bounds = parent.collider.bounds;
    if let Some(mut values) = p.find(":multi_crush") {
        let name = values.word().ok_or(Error::MissingValue)?;
        if name.len() >= 32 {
            return Err(Error::Parameters);
        }
        let model = shape(r, name)?;
        let source = r.meshes.bind(name, &model);
        mesh = Some(r.meshes.private_copy(source).ok_or(Error::MissingShape)?);
        collider_bounds = bounds(&model, 1536);
        loaded = Some(model);
    }
    let source = loaded.ok_or(Error::MissingShape)?;
    let mesh = mesh.ok_or(Error::MissingShape)?;
    // The subclass applies this again after replacing the model.
    if let Some(distance) = p.integer(":vis_dist") {
        parent.visibility.distance = distance as u16;
        r.meshes
            .set_hint(mesh, distance as u16)
            .ok_or(Error::MissingShape)?;
    }
    Ok((source, mesh, collider_bounds))
}
fn glows(
    p: Parameters<'_>,
    pose: Pose,
    random: &mut u32,
) -> Result<(Vec<deformation::Record>, i32), Error> {
    let mut records = Vec::new();
    let mut siren_timer = 0;
    if let Some(mut values) = p.find(":police_glow") {
        let count = value(&mut values)?;
        if !(0..=65536).contains(&count) {
            return Err(Error::RecordCount);
        }
        records.reserve_exact(count as usize);
        for _ in 0..count {
            let color = [
                value(&mut values)? as u8,
                value(&mut values)? as u8,
                value(&mut values)? as u8,
            ];
            let position = [
                value(&mut values)?,
                value(&mut values)?,
                value(&mut values)?,
            ];
            let rotated = pose.matrix.local_to_world(position);
            let world_position = std::array::from_fn(|i| rotated[i].wrapping_add(pose.position[i]));
            records.push(deformation::Record {
                position,
                world_position,
                color,
                state: u8::from(random::next(random) & 16 != 0),
                timer: ((random::next(random) >> 4) & 511) as i32,
            });
        }
        if p.find(":siren").is_some() {
            siren_timer = 22050;
        }
    }
    Ok((records, siren_timer))
}
fn vertex_flags(vertex_count: usize, random: &mut u32) -> Vec<u8> {
    let mut vertex_flags = vec![0; vertex_count];
    for flags in &mut vertex_flags {
        if (random::next(random) >> 4) & 1 != 0 {
            *flags |= 4;
            if (random::next(random) >> 4) & 1 != 0 {
                *flags |= 1;
            }
            if (random::next(random) >> 4) & 1 != 0 {
                *flags |= 2;
            }
        }
    }
    vertex_flags
}

impl Crush {
    pub fn install(
        self,
        world: &mut obstacle_collision::World,
        surfaces: &mut Surfaces,
        class: ClassId,
    ) -> Result<PlacedCrush, Error> {
        let entered = self.visibility.entered;
        let placed = self.install_components(world, surfaces, class)?;
        if entered {
            world.deformables.push(placed.id);
        }
        Ok(placed)
    }
    /// Runtime enter/leave methods own collision and draw membership.
    pub fn install_components(
        self,
        world: &mut obstacle_collision::World,
        surfaces: &mut Surfaces,
        class: ClassId,
    ) -> Result<PlacedCrush, Error> {
        let index = world.colliders.len();
        if world.objects.len() != index
            || surfaces.entries.len() > index
            || surfaces.meshes.get(self.surface.mesh).is_none()
            || !world
                .classes
                .get(class.0)
                .is_some_and(|c| c.methods.contains(&Some(DEFORMABLE_HIT)))
        {
            return Err(Error::InvalidWorld);
        }
        let id = ObstacleId(index);
        let mesh_bounds = self.source.bounds;
        world.colliders.push(self.collider);
        world.objects.push(obstacle_collision::Object {
            class,
            weight: self.weight,
            mesh_bounds,
            threshold: 0,
            activity: 0,
            flags: 0,
        });
        surfaces.entries.resize_with(index, || None);
        surfaces.entries.push(Some(self.surface));
        Ok(PlacedCrush {
            id,
            source: self.source,
            visibility: self.visibility,
            ground_height: self.ground_height,
        })
    }
    /// Complete component construction for shipped Crush parameters. Membership
    /// changes are returned as visibility state for the world scheduler to bind.
    pub fn build<S: ShapeSource>(
        record: &level::Object<'_>,
        entity: EntityId,
        r: &mut Resources<'_, S>,
    ) -> Result<Self, Error> {
        if !record.class.eq_ignore_ascii_case("Crush") {
            return Err(Error::WrongClass);
        }
        let p = Parameters::new(record.parameters).map_err(|_| Error::Parameters)?;
        // These general Obstacle features need their own constructor/data. Do
        // not silently discard them if a future resource adds one to Crush.
        for name in [
            ":type",
            ":Type",
            ":shatter",
            ":Shatter",
            ":shatter3d",
            ":shatter3dvfp",
        ] {
            if p.find(name).is_some() {
                return Err(Error::UnsupportedParentParameter(name));
            }
        }
        let pose = Pose::from_level(record);
        let mut visibility = visibility(p, r.random)?;
        let weight = p
            .integer(":solidity")
            .or_else(|| p.integer(":Solidity"))
            .unwrap_or(8);
        let material = p.integer(":sortpos").unwrap_or(0) as i16;
        let source = if record.shape.eq_ignore_ascii_case("nil") {
            None
        } else {
            Some(shape(r, record.shape)?)
        };
        let mesh = source
            .as_ref()
            .map(|model| r.meshes.bind(record.shape, model));
        let collider_bounds = source.as_ref().map_or(
            Bounds {
                min: [0; 3],
                max: [0; 3],
            },
            |model| bounds(model, p.integer(":expand_coll_radius").unwrap_or(1536)),
        );
        if let Some(distance) = p.integer(":vis_dist") {
            visibility.distance = distance as u16;
            if let Some(mesh) = mesh {
                r.meshes
                    .set_hint(mesh, distance as u16)
                    .ok_or(Error::MissingShape)?;
            }
        }
        Self::finish(
            record,
            CrushParent {
                collider: Collider {
                    entity,
                    pose,
                    external_basis: Some(pose.matrix),
                    radius: source.as_ref().map_or(0, |model| model.radius),
                    bounds: collider_bounds,
                },
                source,
                mesh,
                visibility,
                weight,
                sort_position: material,
            },
            crush::Initial::default(),
            r,
        )
    }
    /// Complete only the subclass. The live reader has already run real parent
    /// activation, lap entry/exit and resource construction in source order.
    pub(crate) fn finish<S: ShapeSource>(
        record: &level::Object<'_>,
        mut parent: CrushParent,
        initial: crush::Initial,
        r: &mut Resources<'_, S>,
    ) -> Result<Self, Error> {
        let p = Parameters::new(record.parameters).map_err(|_| Error::Parameters)?;
        let pose = parent.collider.pose;
        let (source, mesh, mut collider_bounds) = crush_mesh(p, r, &mut parent)?;
        let (records, siren_timer) = glows(p, pose, r.random)?;
        let sampling_bounds = [
            [collider_bounds.min[0], collider_bounds.max[0]],
            [collider_bounds.min[2], collider_bounds.max[2]],
        ];
        for axis in [0, 2] {
            collider_bounds.min[axis] =
                trunc_shift(collider_bounds.min[axis].wrapping_mul(1300), 10);
            collider_bounds.max[axis] =
                trunc_shift(collider_bounds.max[axis].wrapping_mul(1300), 10);
        }
        let mut ground_height = 0;
        terrain::height(
            r.terrain,
            pose.position[0],
            pose.position[2],
            &mut ground_height,
        )
        .map_err(Error::Terrain)?;
        let vertex_count = r
            .meshes
            .get(mesh)
            .ok_or(Error::MissingShape)?
            .vertices
            .len();
        let vertex_flags = vertex_flags(vertex_count, r.random);
        Ok(Self {
            collider: Collider {
                radius: source.radius,
                bounds: collider_bounds,
                ..parent.collider
            },
            weight: parent.weight,
            source,
            visibility: parent.visibility,
            ground_height,
            surface: Deformable {
                runtime: crush::State {
                    siren_timer,
                    private_mesh: p.find(":multi_crush").is_some(),
                    highlighted: initial.highlighted,
                    ..crush::State::default()
                },
                config: deformable_hit::Config {
                    floor: p.integer(":crush_limit").map_or(initial.floor, |v| v >> 3),
                    compression: p.integer(":crush_amount").unwrap_or(initial.compression),
                    drift_shift: p
                        .integer(":horiz_amount")
                        .map_or(initial.drift_shift, |v| v as u32),
                    material: parent.sort_position,
                    has_sound: false,
                },
                state: deformable_hit::State::default(),
                mesh,
                vertex_flags,
                sampling: Sampling {
                    bounds: sampling_bounds,
                    edge_cap: p
                        .integer(":max_height")
                        .unwrap_or(initial.edge_cap)
                        .clamp(0, 65535),
                    state: deformation::State {
                        records_attached: p.find(":police_glow").is_some(),
                        pose: deformation::Pose {
                            origin: pose.position,
                            inverse_basis: pose.matrix.0,
                        },
                        records,
                        dirty: 0,
                    },
                },
            },
        })
    }
}
