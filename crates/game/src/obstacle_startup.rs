//! General Obstacle construction (41E9A0). Subclasses supply their initialized
//! scalar defaults before this parent runs; class scheduling remains separate.
mod effects;
use crate::{
    dispatch::ClassId,
    geometry::Bounds,
    object_query::{Collider, EntityId, ObstacleId},
    obstacle_collision,
    pose::Pose,
    scene_meshes::MeshId,
    scene_startup::{self, Error, Resources, Visibility},
    spatial::{self, Cell},
    vehicle_model::{ShapeSource, Space},
};
pub use effects::{Animation, Debris, MaterialId, Materials, Piece, Sprite};
use std::sync::Arc;
use tankracer_assets::{
    config::Gain, level, model::Model, object_catalog::Catalog, parameters::Parameters,
};

#[derive(Debug, Clone, Copy, Default)]
pub struct Initial {
    pub weight: i32,
    pub threshold: i32,
    pub activity: i32,
    pub sort_position: i16,
}
pub struct Environment<'a> {
    pub catalog: &'a Catalog,
    pub materials: &'a mut dyn Materials,
    /// Explicit caller-owned tail of the original temporary 12-word matrix.
    pub matrix_tail: Option<[i32; 3]>,
}
#[derive(Debug, Clone)]
pub struct Auxiliary {
    pub shape_name: String,
    pub gravity: i32,
    pub bounce: Gain,
    pub friction: Gain,
    pub push: Gain,
    pub velocity: Gain,
    pub spin: Gain,
}
pub struct Obstacle {
    pub collider: Collider,
    pub source: Option<Arc<Model>>,
    pub mesh: Option<MeshId>,
    pub visibility: Visibility,
    pub sort_position: i16,
    pub weight: i32,
    pub threshold: i32,
    pub activity: i32,
    pub collision_margin: u16,
    pub minimum_activation: i32,
    pub auxiliary: Option<Auxiliary>,
    pub debris: Option<Debris>,
}
pub struct PlacedObstacle {
    pub id: ObstacleId,
    pub cell: Option<Cell>,
    pub source: Option<Arc<Model>>,
    pub mesh: Option<MeshId>,
    pub visibility: Visibility,
    pub sort_position: i16,
    /// Subclass impact threshold (+7C), distinct from activation speed (+94)
    /// stored in the collision world's Object.threshold.
    pub impact_threshold: i32,
    pub collision_margin: u16,
    pub auxiliary: Option<Auxiliary>,
    pub debris: Option<Debris>,
}
fn auxiliary(p: Parameters<'_>) -> Result<Option<Auxiliary>, Error> {
    let Some(mut v) = p.find(":shatter").or_else(|| p.find(":Shatter")) else {
        return Ok(None);
    };
    let name = v.word().ok_or(Error::MissingValue)?;
    if name.len() >= 40 {
        return Err(Error::Parameters);
    }
    let pair = |name, default| {
        if let Some(mut values) = p.find(name) {
            Ok(Gain {
                multiplier: values.integer().unwrap_or(1) as u16,
                shift: values.integer().unwrap_or(0) as u16,
            })
        } else {
            Ok(default)
        }
    };
    Ok(Some(Auxiliary {
        shape_name: name.into(),
        gravity: p.integer(":shat_gravity").unwrap_or(150),
        bounce: pair(
            ":shat_bounce",
            Gain {
                multiplier: 500,
                shift: 10,
            },
        )?,
        friction: pair(
            ":shat_friction",
            Gain {
                multiplier: 800,
                shift: 10,
            },
        )?,
        push: pair(
            ":shat_push",
            Gain {
                multiplier: 150,
                shift: 10,
            },
        )?,
        velocity: pair(
            ":shat_vel",
            Gain {
                multiplier: 1,
                shift: 13,
            },
        )?,
        spin: pair(
            ":shat_rotvel",
            Gain {
                multiplier: 1,
                shift: 16,
            },
        )?,
    }))
}
impl Obstacle {
    /// Install classes whose enter method uses 41C230's ordinary obstacle grid.
    /// The class owner supplies its real method table; this installs data only.
    /// Special enter methods (Crush, pickup, scenery) use their own placement.
    pub fn install_in_grid(
        self,
        world: &mut obstacle_collision::World,
        class: ClassId,
    ) -> Result<PlacedObstacle, Error> {
        let entered = self.visibility.entered;
        let mut placed = self.install_components(world, class)?;
        if entered {
            placed.cell = Some(spatial::insert(
                &mut world.grid,
                placed.id,
                &world.colliders[placed.id.0],
                placed.source.as_ref().map(|m| m.radius),
            ));
        }
        Ok(placed)
    }
    /// Install component data; runtime class methods own membership changes.
    pub fn install_components(
        self,
        world: &mut obstacle_collision::World,
        class: ClassId,
    ) -> Result<PlacedObstacle, Error> {
        let id = ObstacleId(world.colliders.len());
        if world.objects.len() != id.0 || world.classes.get(class.0).is_none() {
            return Err(Error::InvalidWorld);
        }
        world.colliders.push(self.collider);
        world.objects.push(obstacle_collision::Object {
            class,
            weight: self.weight,
            threshold: self.minimum_activation,
            activity: self.activity,
            flags: 0,
            mesh_bounds: self.source.as_ref().map_or([[0; 3]; 2], |m| m.bounds),
        });
        Ok(PlacedObstacle {
            id,
            cell: None,
            source: self.source,
            mesh: self.mesh,
            visibility: self.visibility,
            sort_position: self.sort_position,
            impact_threshold: self.threshold,
            collision_margin: self.collision_margin,
            auxiliary: self.auxiliary,
            debris: self.debris,
        })
    }
    /// Build parent-owned components. The world/class owner must still execute
    /// enter/leave membership and the final virtual lifecycle method in order.
    pub fn build_components<S: ShapeSource>(
        record: &level::Object<'_>,
        entity: EntityId,
        initial: Initial,
        r: &mut Resources<'_, S>,
        env: &mut Environment<'_>,
    ) -> Result<Self, Error> {
        let p = Parameters::new(record.parameters).map_err(|_| Error::Parameters)?;
        let visibility = if record.class.eq_ignore_ascii_case("Powerup") {
            // 410730 removes Powerup from the ordinary render list and returns
            // before generic lap rules; its own constructor controls respawn.
            Visibility {
                distance: 150,
                add_lap: 0,
                remove_lap: 0,
                entered: false,
            }
        } else {
            scene_startup::visibility(p, r.random)?
        };
        Self::build_visible(record, entity, initial, visibility, r, env)
    }
    /// The live reader has already run activation/lap membership. Reuse that
    /// visibility state without consuming its RNG a second time.
    pub(crate) fn build_visible<S: ShapeSource>(
        record: &level::Object<'_>,
        entity: EntityId,
        initial: Initial,
        mut visibility: Visibility,
        r: &mut Resources<'_, S>,
        env: &mut Environment<'_>,
    ) -> Result<Self, Error> {
        let p = Parameters::new(record.parameters).map_err(|_| Error::Parameters)?;
        let pose = Pose::from_level(record);
        let source = if record.shape.eq_ignore_ascii_case("nil") {
            None
        } else {
            r.models
                .load_optional(
                    r.source,
                    if ["Powerup", "Mine", "NetPower", "NetMine"]
                        .iter()
                        .any(|c| record.class.eq_ignore_ascii_case(c))
                    {
                        Space::Shared
                    } else {
                        Space::Level
                    },
                    record.shape,
                )
                .map_err(Error::Shape)?
        };
        let mesh = source
            .as_ref()
            .map(|model| r.meshes.bind(record.shape, model));
        if let Some(distance) = p.integer(":vis_dist") {
            visibility.distance = distance as u16;
            if let Some(mesh) = mesh {
                r.meshes
                    .set_hint(mesh, distance as u16)
                    .ok_or(Error::MissingShape)?;
            }
        }
        let preset = p
            .find(":type")
            .or_else(|| p.find(":Type"))
            .and_then(|mut v| v.word())
            .and_then(|name| env.catalog.get(name));
        let mut margin = preset.map_or(0, |v| i32::from(v.collision_margin));
        if let Some(value) = p.integer(":expand_coll_radius") {
            margin = value;
        } else if margin == 0 {
            margin = 1536;
        }
        let weight = p
            .integer(":solidity")
            .or_else(|| p.integer(":Solidity"))
            .unwrap_or_else(|| preset.map_or(initial.weight, |v| i32::from(v.solidity)));
        let collider = Collider {
            entity,
            pose,
            external_basis: Some(pose.matrix),
            radius: source.as_ref().map_or(0, |m| m.radius),
            bounds: source.as_ref().map_or(
                Bounds {
                    min: [0; 3],
                    max: [0; 3],
                },
                |m| scene_startup::bounds(m, margin),
            ),
        };
        let auxiliary = auxiliary(p)?;
        let debris = Debris::build(p, entity, pose, r, env)?;
        Ok(Self {
            collider,
            source,
            mesh,
            visibility,
            sort_position: p
                .integer(":sortpos")
                .map_or(initial.sort_position, |v| v as i16),
            weight,
            threshold: initial.threshold,
            activity: initial.activity,
            collision_margin: margin as u16,
            minimum_activation: p
                .integer(":min_act_speed")
                .or_else(|| p.integer(":Min_Act_Speed"))
                .unwrap_or(0)
                .wrapping_shl(10),
            auxiliary,
            debris,
        })
    }
}
