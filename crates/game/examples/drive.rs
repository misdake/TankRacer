//! Terrain/vehicle milestone runner. No C library, dump, emulator or server.
use std::{error::Error, fs, path::PathBuf, sync::Arc, time::Instant};
use tankracer_assets::{
    archive::Archive, config::ResolveContext, level::Level, terrain::TerrainData,
};
use tankracer_game::{
    drive,
    obstacle_collision::Invocation,
    pose,
    scene_objects::collision::{self, Collision, Sound},
    vehicle_loading::Request,
    vehicle_model::BuildContext,
    vehicle_resources,
    vehicle_runtime::{Devices, Settings, SinglePlayer},
    vehicle_scene::spray,
    vehicle_startup::Environment,
};
struct Headless;
impl collision::Services for Headless {
    fn sound(&mut self, _: Sound, _: &mut Collision<'_>, _: Invocation<'_>) -> Result<(), i32> {
        Ok(())
    }
    fn feedback(&mut self, _: i32, _: &mut Collision<'_>, _: Invocation<'_>) -> Result<(), i32> {
        Ok(())
    }
}
impl Devices for Headless {
    fn surface_feedback(&mut self) -> Result<(), i32> {
        Ok(())
    }
    fn impact_feedback(&mut self, _: i32) -> Result<(), i32> {
        Ok(())
    }
}
fn main() -> Result<(), Box<dyn Error>> {
    let args: Vec<_> = std::env::args().skip(1).collect();
    let root = args
        .first()
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game"));
    let map: usize = args.get(1).map(|v| v.parse()).transpose()?.unwrap_or(0);
    let car: usize = args.get(2).map(|v| v.parse()).transpose()?.unwrap_or(1);
    let ticks: usize = args.get(3).map(|v| v.parse()).transpose()?.unwrap_or(240);
    let maps = [
        "ENGLAND", "MEXICO", "SWAMP", "EAST", "THEME", "BIO", "MONACO", "CIRCUIT",
    ];
    if map >= maps.len() || !(1..=15).contains(&car) || ticks == 0 {
        return Err("map 0..7, car 1..15, ticks > 0".into());
    }
    let data_file = fs::read(root.join("DATA/FASTFILE.DAT"))?;
    let files = ["BRONZE", "SILVER", "GOLD"]
        .map(|tier| fs::read(root.join("TANKS").join(tier).join("FASTFILE.DAT")))
        .into_iter()
        .collect::<Result<Vec<_>, _>>()?;
    let shared = fs::read(root.join("INGAME/FASTFILE.DAT"))?;
    let level_file = fs::read(root.join("LEVELS").join(maps[map]).join("FASTFILE.DAT"))?;
    let archive = Archive::parse(&level_file)?;
    let bsm = archive
        .entries()
        .iter()
        .find(|e| e.name.ends_with(".BSM"))
        .ok_or("missing world")?;
    let terrain = Arc::new(TerrainData::decode_bsm(
        archive.get(bsm.name).ok_or("missing terrain")?,
    )?);
    let cup = if map == 7 {
        2
    } else if [3, 5, 6].contains(&map) {
        1
    } else {
        0
    };
    let level = Level::decode(
        archive
            .get(&format!("{}{}.DAT", &maps[map][..3], ["B", "S", "G"][cup]))
            .ok_or("missing level")?,
    )?;
    let spawn = level
        .objects
        .iter()
        .find(|o| o.class == "Player")
        .map(pose::Pose::from_level)
        .unwrap_or_default();
    let resources = vehicle_resources::prepare(
        &data_file,
        &files.iter().map(Vec::as_slice).collect::<Vec<_>>(),
        &shared,
        car as u32,
        BuildContext {
            resolve: ResolveContext {
                player: true,
                map: map as u32,
                ..ResolveContext::default()
            },
            ambient_wobble: false,
            session_active: false,
        },
    )?;
    let mut game = SinglePlayer::new(
        terrain,
        resources.surfaces,
        Request {
            model: resources.model,
            spawn,
            static_allocation: true,
            environment: Environment {
                ambient_wobble: false,
                session_active: false,
                ambient_phase: 0,
                gun_pivot: [0; 3],
            },
        },
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
    .map_err(|e| format!("startup: {e:?}"))?;
    let initial = game.vehicles.entries[0]
        .as_ref()
        .ok_or("missing player")?
        .physics
        .motion
        .poses
        .physical_position;
    let begin = Instant::now();
    let mut grounded = 0;
    for tick in 0..ticks {
        game.step(
            drive::Input {
                throttle: 65536,
                steering: if (ticks / 3..ticks / 2).contains(&tick) {
                    12000
                } else {
                    0
                },
                ..drive::Input::default()
            },
            16,
            true,
            &mut Headless,
        )
        .map_err(|e| format!("tick {tick}: {e:?}"))?;
        grounded += usize::from(
            game.vehicles.entries[0]
                .as_ref()
                .unwrap()
                .physics
                .contact
                .sampling
                .primary
                .points
                .iter()
                .any(|p| p.kind < 2),
        );
    }
    let micros = begin.elapsed().as_micros();
    let player = game.vehicles.entries[0]
        .as_ref()
        .ok_or("missing final player")?;
    let final_position = player.physics.motion.poses.physical_position;
    if initial == final_position || grounded == 0 {
        return Err("vehicle did not move on real terrain".into());
    }
    println!(
        "{} car={} ticks={} start={initial:?} end={final_position:?} velocity={:?} grounded={} rng={} us/tick={:.2}",
        maps[map],
        car,
        ticks,
        player.physics.motion.velocity,
        grounded,
        game.shared.random,
        micros as f64 / ticks as f64
    );
    Ok(())
}
