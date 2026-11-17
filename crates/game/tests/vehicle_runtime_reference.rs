//! Both runtimes start from files; compare real scheduled vehicle updates.
use std::{collections::BTreeSet, fs, path::PathBuf, process::Command, sync::Arc};
use tankracer_assets::{
    archive::Archive,
    config::{Part, ResolveContext},
    terrain::TerrainData,
};
use tankracer_game::{
    drive,
    obstacle_collision::Invocation,
    player_input, pose,
    scene_objects::collision::{Collision, Services, Sound},
    vehicle_loading::Request,
    vehicle_model::BuildContext,
    vehicle_resources,
    vehicle_runtime::{Devices, Settings, SinglePlayer},
    vehicle_scene::spray,
    vehicle_startup::Environment,
};
#[path = "common/motion.rs"]
#[allow(dead_code)]
mod common;
#[path = "common/player.rs"]
mod player_codec;
#[path = "common/vehicle_scene.rs"]
#[allow(dead_code)]
mod scene_codec;

struct Headless;
impl Services for Headless {
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
#[path = "common/runtime.rs"]
mod runtime_codec;
use runtime_codec::words;
#[test]
#[ignore = "requires fresh native vehicle oracle; set TANKRACER_VEHICLE_RUNTIME_REFERENCE"]
fn fresh_resource_only_runtime_matches_native_updates() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    let data_file = fs::read(root.join("DATA/FASTFILE.DAT")).unwrap();
    let vehicles = ["BRONZE", "SILVER", "GOLD"]
        .map(|tier| fs::read(root.join("TANKS").join(tier).join("FASTFILE.DAT")).unwrap());
    let shared = fs::read(root.join("INGAME/FASTFILE.DAT")).unwrap();
    let maps = [
        "ENGLAND", "MEXICO", "SWAMP", "EAST", "THEME", "BIO", "MONACO", "CIRCUIT",
    ];
    let mut cases = BTreeSet::new();
    for map in 0..8 {
        for car in [1, 6, 11] {
            cases.insert((map, car, map % 2));
        }
    }
    for car in 1..=15 {
        cases.insert((0, car, car % 2));
    }
    let mut updates = 0;
    let mut compared = 0;
    for (map, car, mode, controls) in cases
        .into_iter()
        .flat_map(|(m, c, g)| [0, 1, 2].map(|k| (m, c, g, k)))
    {
        let steps = if controls == 0 && map == 0 && car == 1 {
            96
        } else {
            24
        };
        let output = Command::new(
            std::env::var_os("TANKRACER_VEHICLE_RUNTIME_REFERENCE").expect("set oracle"),
        )
        .arg(&root)
        .arg(map.to_string())
        .arg(car.to_string())
        .arg(mode.to_string())
        .arg(steps.to_string())
        .arg(controls.to_string())
        .output()
        .unwrap();
        assert!(
            output.status.success(),
            "map={map} car={car}: {}",
            String::from_utf8_lossy(&output.stderr)
        );
        assert_eq!(&output.stdout[..4], b"VR01");
        let mut r = common::Reader {
            words: output.stdout[4..]
                .chunks_exact(4)
                .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
                .collect(),
            at: 0,
        };
        let file = fs::read(root.join("LEVELS").join(maps[map]).join("FASTFILE.DAT")).unwrap();
        let archive = Archive::parse(&file).unwrap();
        let bsm = archive
            .entries()
            .iter()
            .find(|e| e.name.ends_with(".BSM"))
            .unwrap();
        let terrain = Arc::new(TerrainData::decode_bsm(archive.get(bsm.name).unwrap()).unwrap());
        let resources = vehicle_resources::prepare(
            &data_file,
            &vehicles.iter().map(Vec::as_slice).collect::<Vec<_>>(),
            &shared,
            car as u32,
            BuildContext {
                resolve: ResolveContext {
                    player: true,
                    mode: mode as u32,
                    map: map as u32,
                    ..ResolveContext::default()
                },
                ambient_wobble: false,
                session_active: false,
            },
        )
        .unwrap();
        assert!(resources.model.parts[Part::Body.slot()].is_some());
        let mut game = SinglePlayer::new(
            terrain,
            resources.surfaces,
            Request {
                model: resources.model,
                spawn: pose::Pose::default(),
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
                mode: mode as i32,
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
        .unwrap();
        if controls == 2 {
            let v = &mut game.vehicles.entries[0].as_mut().unwrap().physics;
            v.motion.velocity = [20000, 0, 500000];
            v.powertrain.local_velocity = [20000, 0, 500000];
            v.collision.collision_size = 300;
            v.collision.impact_accumulator = 80;
            v.collision.ground_delta = [64000, -32000, 2000];
            v.collision.tank_delta = [50000, 0, 0];
            v.collision.obstacle_impulse = -50000;
        }
        let mut braked = false;
        for tick in 0..=steps {
            if tick != 0 {
                let i = tick - 1;
                assert_eq!(r.word(), 0, "C status map={map} car={car} tick={tick}");
                assert_eq!(r.word(), 1, "C after-pose calls");
                game.race_active = controls != 2 || i % 12 != 7;
                let result = if controls != 0 {
                    game.step_keys(
                        player_input::Keys {
                            throttle: i % 12 < 8 && i != 1,
                            brake: i % 12 == 9,
                            left: i % 12 == 10,
                            right: (4..8).contains(&(i % 12)) || i % 12 == 10,
                            auxiliary: controls == 2 && (4..8).contains(&(i % 12)),
                            ..Default::default()
                        },
                        [16, 17, 20, 12][i % 4],
                        controls == 2 || i >= 3,
                        if i == 0 { 1 } else { 2 },
                        &mut Headless,
                    )
                } else {
                    game.step(
                        drive::Input {
                            throttle: if i % 12 < 8 { 65536 } else { 0 },
                            brake: if i % 12 == 9 { 65536 } else { 0 },
                            steering: if (4..8).contains(&(i % 12)) { 12000 } else { 0 },
                            ..drive::Input::default()
                        },
                        [16, 17, 20, 12][i % 4],
                        i >= 3,
                        &mut Headless,
                    )
                };
                result.unwrap();
                if controls == 2 && tick == 1 {
                    assert_ne!(
                        game.vehicles.entries[0]
                            .as_ref()
                            .unwrap()
                            .physics
                            .collision
                            .collision_size,
                        300,
                        "Player indication prefix must consume initial collision inputs"
                    );
                }
                updates += 1;
            }
            braked |= game.vehicles.entries[0]
                .as_ref()
                .unwrap()
                .input
                .drive
                .alternate;
            let actual = words(&game, controls != 0);
            for (word, a) in actual.iter().enumerate() {
                assert_eq!(
                    *a,
                    r.word(),
                    "map={map} car={car} mode={mode} controls={controls} tick={tick} word={word}"
                );
            }
            compared += actual.len();
        }
        assert_eq!(r.at, r.words.len(), "oracle framing");
        if controls == 2 && map == 0 && car == 1 {
            assert!(braked, "high-speed handbrake override must activate");
        }
    }
    println!(
        "fresh native runtime: {updates} updates / {compared} fields; direct physics and CUP input, all 8 maps and 15 cars"
    );
}
