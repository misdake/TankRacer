//! Authored collision scenery and route bootstrap. Deferred classes stay explicit.
use crate::{
    object_query::EntityId,
    obstacle_startup::{MaterialId, Materials},
    scene_loading::{self, Loader},
    vehicle_model::{Bundles, ModelBank, Space},
    vehicle_resources::shapes,
    vehicle_runtime::Startup,
};
use std::{collections::BTreeMap, error::Error, sync::Arc};
use tankracer_assets::{archive::Archive, level::Level, model::Model, object_catalog::Catalog};

pub struct Loaded {
    pub entities: Vec<EntityId>,
    pub classes: BTreeMap<String, usize>,
    /// This list is part of the stage contract, not a successful full-world load.
    pub deferred: BTreeMap<String, usize>,
    pub models: ModelBank,
    pub materials: Vec<(String, u32)>,
}
struct MaterialFiles<'a> {
    level: Archive<'a>,
    shared: Archive<'a>,
    bindings: Vec<(String, u32)>,
}
impl Materials for MaterialFiles<'_> {
    fn resolve(&mut self, name: &str, mode: u32) -> Result<Option<MaterialId>, i32> {
        let key = name.to_ascii_lowercase();
        let file = format!("{}.SUR", name.to_ascii_uppercase());
        let Some(bytes) = self.level.get(&file).or_else(|| self.shared.get(&file)) else {
            return Ok(None);
        };
        if bytes.is_empty() {
            return Ok(None);
        }
        if let Some(i) = self.bindings.iter().position(|b| b.0 == key && b.1 == mode) {
            return Ok(Some(MaterialId(i as u32 + 16)));
        }
        self.bindings.push((key, mode));
        // Lower identities are the session's shared effect bindings. These are
        // asset handles; GPU texture creation belongs to the renderer stage.
        Ok(Some(MaterialId(self.bindings.len() as u32 + 15)))
    }
}
impl Loaded {
    pub fn auxiliary_model(&self, name: &str) -> Option<Arc<Model>> {
        self.models.get(name).cloned()
    }
}
fn deferred(class: &str) -> bool {
    [
        "Noisy", "Smoker", "Trackcam", "Bomber", "Train", "Flock", "Powerup", "BckgrdOb", "Lamps",
        "Player", "AITank",
    ]
    .iter()
    .any(|c| class.eq_ignore_ascii_case(c))
}
/// Construct supported classes in file order, then drain the real startup
/// queues before placing a player. Unknown classes are rejected before mutation.
/// Deferred content belongs to the later race, optional-content and CUP stages.
pub fn load(
    startup: &mut Startup,
    data: &[u8],
    level_archive: &[u8],
    shared: &[u8],
    map: &str,
    cup: usize,
) -> Result<Loaded, Box<dyn Error>> {
    if !map.is_ascii() || map.len() < 3 || cup >= 3 {
        return Err("invalid scenery selection".into());
    }
    let map = map.to_ascii_uppercase();
    let map_index = tankracer_assets::level::CUP_MAPS
        .iter()
        .position(|n| *n == map)
        .ok_or("unknown scenery map")?;
    if startup.map() != map_index as u32 {
        return Err("scenery map differs from runtime selection".into());
    }
    let data = Archive::parse(data)?;
    let archive = Archive::parse(level_archive)?;
    let level = Level::decode(
        archive
            .get(&format!("{}{}.DAT", &map[..3], ["B", "S", "G"][cup]))
            .ok_or("missing authored scenery")?,
    )?;
    let catalog = Catalog::decode(data.get("OBPARAMS.DAT").ok_or("missing object settings")?)
        .map_err(|e| format!("object settings: {e:?}"))?;
    let mut materials = MaterialFiles {
        level: Archive::parse(level_archive)?,
        shared: Archive::parse(shared)?,
        bindings: vec![],
    };
    let source = Bundles {
        vehicle: vec![],
        shared: vec![shapes(shared)?],
        level: vec![shapes(level_archive)?],
    };
    let mut loaded = Loaded {
        entities: Vec::new(),
        classes: BTreeMap::new(),
        deferred: BTreeMap::new(),
        models: ModelBank::default(),
        materials: vec![],
    };
    for record in &level.objects {
        if startup.registry.scenery_class(record.class).is_none() {
            if !deferred(record.class) {
                return Err(format!("unknown scenery class: {}", record.class).into());
            }
            *loaded.deferred.entry(record.class.into()).or_default() += 1;
        }
    }
    let low_gravity = startup.low_gravity();
    startup.objects.components.explosion_resources.map_fog = [2, 5, 7].contains(&map_index);
    startup
        .objects
        .components
        .waypoints
        .initialize_grid(&startup.terrain, 100000)
        .map_err(|e| format!("waypoint grid: {e}"))?;
    let mut loader = Loader::new(
        &mut startup.objects,
        scene_loading::Resources {
            source: &source,
            models: &mut loaded.models,
            terrain: &startup.terrain,
            random: &mut startup.shared.random,
            catalog: &catalog,
            materials: &mut materials,
            matrix_tail: Some([0; 3]),
        },
        &startup.registry,
    );
    loader
        .prepare_sign(map_index as u32)
        .map_err(|e| format!("scenery sign: {e:?}"))?;
    loader
        .prepare_bounce(map_index as u32, low_gravity)
        .map_err(|e| format!("scenery bounce: {e:?}"))?;
    for record in level.objects {
        if loader.registry.scenery_class(record.class).is_none() {
            continue;
        }
        let entity = loader
            .load(&mut startup.identity, record)
            .map_err(|e| format!("{} {}: {e:?}", record.class, record.name))?;
        loaded.entities.push(entity);
        *loaded.classes.entry(record.class.into()).or_default() += 1;
    }
    if !startup.objects.components.waypoints.order.is_empty() {
        startup
            .objects
            .components
            .waypoints
            .link(
                &startup.identity.symbols,
                &startup.identity.entities,
                &startup.objects.world.colliders,
                100000,
            )
            .map_err(|e| format!("waypoint linkage: {e}"))?;
    }
    startup
        .identity
        .context()
        .drain(
            scene_loading::STOP,
            startup.registry.environment(100000),
            &mut startup.objects,
        )
        .map_err(|e| format!("scenery startup queues: {e:?}"))?;
    // Lazy resource I/O is replaced by an immutable decoded binding. Effects
    // still allocate their own current geometry at the actual hit boundary.
    for object in startup.objects.components.objects.iter().flatten() {
        if let Some(auxiliary) = &object.data.auxiliary {
            loaded
                .models
                .load_optional(&source, Space::Level, &auxiliary.shape_name)
                .map_err(|e| format!("auxiliary {}: {e:?}", auxiliary.shape_name))?;
        }
    }
    loaded.materials = materials.bindings;
    Ok(loaded)
}
