//! Resource-only preparation shared by native and browser adapters. This keeps
//! the accepted immediate-start slice; complete race bootstrap remains M5.
use crate::{
    pose, scenery,
    vehicle_loading::Request,
    vehicle_model::BuildContext,
    vehicle_resources,
    vehicle_runtime::{Settings, SinglePlayer, Startup},
    vehicle_scene::spray,
    vehicle_startup::Environment,
};
use std::{error::Error, sync::Arc};
use tankracer_assets::{
    archive::Archive, config::ResolveContext, level::Level, terrain::TerrainData,
};

pub use tankracer_assets::level::CUP_MAPS as MAPS;

pub struct Files<'a> {
    pub data: &'a [u8],
    pub vehicles: &'a [&'a [u8]],
    pub shared: &'a [u8],
    pub level: &'a [u8],
}
#[derive(Clone, Copy)]
pub struct Selection {
    pub map: usize,
    pub cup: usize,
    pub vehicle: u32,
}
impl Selection {
    pub fn default_cup(map: usize, vehicle: u32) -> Result<Self, Box<dyn Error>> {
        if map >= MAPS.len() || !(1..=15).contains(&vehicle) {
            return Err("Invalid map or vehicle".into());
        }
        Ok(Self {
            map,
            cup: if map == 7 {
                2
            } else if [3, 5, 6].contains(&map) {
                1
            } else {
                0
            },
            vehicle,
        })
    }
    pub fn from_archive(bytes: &[u8], vehicle: u32) -> Result<Self, Box<dyn Error>> {
        let archive = Archive::parse(bytes)?;
        let map = MAPS
            .iter()
            .position(|name| archive.get(&format!("{}B.DAT", &name[..3])).is_some())
            .ok_or("Unknown level archive")?;
        Self::default_cup(map, vehicle)
    }
}
pub struct Prepared {
    pub startup: Startup,
    pub spawn: pose::Pose,
    pub scenery: Option<scenery::Loaded>,
    resources: vehicle_resources::Prepared,
}
pub struct Loaded {
    pub world: SinglePlayer,
    pub scenery: Option<scenery::Loaded>,
    pub camera: tankracer_assets::object_catalog::Camera,
}
impl Prepared {
    pub fn load(
        terrain: Arc<TerrainData>,
        files: Files<'_>,
        selection: Selection,
        with_scenery: bool,
    ) -> Result<Self, Box<dyn Error>> {
        let Selection { map, cup, vehicle } = selection;
        if map >= MAPS.len() || cup >= 3 {
            return Err("Invalid level selection".into());
        }
        let resources = vehicle_resources::prepare(
            files.data,
            files.vehicles,
            files.shared,
            vehicle,
            BuildContext {
                resolve: ResolveContext {
                    player: true,
                    map: map as u32,
                    ..Default::default()
                },
                ambient_wobble: false,
                session_active: false,
            },
        )?;
        let mut startup = Startup::new(
            terrain,
            Settings {
                map: map as u32,
                mode: 0,
                special: false,
                environment_flag: 0,
                seed: 0,
                spray_materials: spray::Materials {
                    filth: 1,
                    crap: 2,
                    splash: 3,
                },
                light_material: 4,
                hardware: true,
            },
        )
        .map_err(|e| format!("World startup: {e:?}"))?;
        let scenery = with_scenery
            .then(|| {
                scenery::load(
                    &mut startup,
                    files.data,
                    files.level,
                    files.shared,
                    MAPS[map],
                    cup,
                )
            })
            .transpose()?;
        let archive = Archive::parse(files.level)?;
        let script = Level::decode(
            archive
                .get(&format!("{}{}.DAT", &MAPS[map][..3], ["B", "S", "G"][cup]))
                .ok_or("Missing player level")?,
        )?;
        let spawn = script
            .objects
            .iter()
            .find(|o| o.class == "Player")
            .map(pose::Pose::from_level)
            .unwrap_or_default();
        Ok(Self {
            startup,
            spawn,
            scenery,
            resources,
        })
    }
    pub fn finish(self) -> Result<Loaded, Box<dyn Error>> {
        let mut world = self
            .startup
            .finish(
                self.resources.surfaces,
                Request {
                    model: self.resources.model,
                    spawn: self.spawn,
                    static_allocation: true,
                    environment: Environment {
                        ambient_wobble: false,
                        session_active: false,
                        ambient_phase: 0,
                        gun_pivot: [0; 3],
                    },
                },
            )
            .map_err(|e| format!("Player startup: {e:?}"))?;
        // Route links and the actual Player must both exist before this query.
        // The camera-free physics fixture with no authored scenery has no route.
        if self.scenery.is_some() {
            world
                .initialize_player_route(100000)
                .map_err(|e| format!("Player route startup: {e:?}"))?;
        }
        Ok(Loaded {
            world,
            scenery: self.scenery,
            camera: self.resources.camera,
        })
    }
}
