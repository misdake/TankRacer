//! Construct real player dynamics from resources/spawn, before any frame snapshot.
use std::{collections::BTreeSet, fs, path::PathBuf, process::Command};
use tankracer_assets::{
    archive::Archive,
    config::{ResolveContext, Surfaces, Tank},
    level::Level,
    model::ShapeArchive,
    parameters::Parameters,
};
use tankracer_game::{
    entity_factory,
    object_query::EntityId,
    pose,
    scene_entity::Handle,
    scene_loading::Registry,
    vehicle_loading::{self, Store},
    vehicle_model::{BuildContext, Bundles, ModelBank, VehicleModel},
    vehicle_startup::Environment,
};
#[path = "common/motion.rs"]
#[allow(dead_code)]
mod common;
use common::Reader;

#[path = "common/player.rs"]
mod player_codec;
use player_codec::encode;

fn shapes(bytes: &[u8]) -> ShapeArchive<'_> {
    let archive = Archive::parse(bytes).unwrap();
    let entry = archive
        .entries()
        .iter()
        .find(|e| e.name.ends_with(".SS"))
        .unwrap();
    ShapeArchive::parse(archive.get(entry.name).unwrap()).unwrap()
}
#[test]
#[ignore = "requires production Player constructor oracle; set TANKRACER_VEHICLE_STARTUP_REFERENCE"]
fn authored_spawns_and_fifteen_resource_profiles_construct_without_snapshots() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    let vehicle_files = ["BRONZE", "SILVER", "GOLD"]
        .map(|tier| fs::read(root.join("TANKS").join(tier).join("FASTFILE.DAT")).unwrap());
    let shared_file = fs::read(root.join("INGAME/FASTFILE.DAT")).unwrap();
    let data = fs::read(root.join("DATA/FASTFILE.DAT")).unwrap();
    let data = Archive::parse(&data).unwrap();
    let surfaces = Surfaces::decode(data.get("SURFS.DAT").unwrap()).unwrap();
    let source = Bundles {
        vehicle: vehicle_files.iter().map(|b| shapes(b)).collect(),
        shared: vec![shapes(&shared_file)],
        level: vec![],
    };
    let mut cases = BTreeSet::new();
    for vehicle in 1..=15 {
        for map in [0, 7] {
            for mode in 0..=1 {
                cases.insert((map, if map == 7 { 2 } else { 0 }, vehicle, mode, 0));
            }
        }
    }
    for (cup, maps) in [
        &[0, 1, 2, 4][..],
        &[3, 2, 6, 1, 5][..],
        &[6, 3, 4, 0, 5, 7][..],
    ]
    .into_iter()
    .enumerate()
    {
        for &map in maps {
            for mode in 0..=1 {
                cases.insert((map, cup, cup * 5 + 1, mode, 0));
            }
        }
    }
    for vehicle in [1, 6, 11] {
        for map in [0, 7] {
            for mode in 0..=1 {
                for variant in 1..=3 {
                    cases.insert((map, if map == 7 { 2 } else { 0 }, vehicle, mode, variant));
                }
            }
        }
    }
    let mut records = 0;
    let mut compared = 0;
    let mut wobbling = 0;
    let mut suppressed = 0;
    for (map, cup, vehicle, mode, variant) in cases {
        let map_name = [
            "ENGLAND", "MEXICO", "SWAMP", "EAST", "THEME", "BIO", "MONACO", "CIRCUIT",
        ][map];
        let level_file = fs::read(root.join("LEVELS").join(map_name).join("FASTFILE.DAT")).unwrap();
        let archive = Archive::parse(&level_file).unwrap();
        let filename = format!("{}{}.DAT", &map_name[..3], ["B", "S", "G"][cup]);
        let level = Level::decode(archive.get(&filename).unwrap()).unwrap();
        let players: Vec<_> = level
            .objects
            .iter()
            .filter(|o| {
                o.class == "Player"
                    && Parameters::new(o.parameters)
                        .unwrap()
                        .integer(":player")
                        .is_some_and(|v| v < 2)
            })
            .collect();
        let output = Command::new(
            std::env::var_os("TANKRACER_VEHICLE_STARTUP_REFERENCE").expect("set oracle"),
        )
        .arg(&root)
        .arg(map.to_string())
        .arg(cup.to_string())
        .arg(vehicle.to_string())
        .arg(mode.to_string())
        .arg(variant.to_string())
        .output()
        .unwrap();
        assert!(
            output.status.success(),
            "map={map} cup={cup} vehicle={vehicle} mode={mode}: {}",
            String::from_utf8_lossy(&output.stderr)
        );
        assert_eq!(&output.stdout[..4], b"VS05");
        let mut r = Reader {
            words: output.stdout[4..]
                .chunks_exact(4)
                .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
                .collect(),
            at: 0,
        };
        let mut count = 0;
        while r.word() != 0 {
            let entity = EntityId(r.word());
            let spawn = pose::Pose {
                position: r.vector(),
                matrix: tankracer_game::math::Matrix(r.vector()),
            };
            let authored_spawn = if variant == 3 {
                pose::Pose::from_level(&tankracer_assets::level::Object {
                    class: "Player",
                    name: "TestSpawn",
                    position: [123456, -1024, -245678],
                    angles: [1638, 8192, 3276],
                    shape: "nil",
                    parameters: "",
                })
            } else if players.is_empty() {
                // frame_game_load's real fallback: create at zero, identity.
                pose::Pose::default()
            } else {
                pose::Pose::from_level(players[count])
            };
            assert_eq!(spawn, authored_spawn, "map={map} cup={cup}");
            let current_mode = r.word();
            let current_map = r.word();
            let special = r.word() != 0;
            let ambient_wobble = r.word() != 0;
            let session_active = r.word() != 0;
            let ambient_phase = r.word();
            let circuit_power_mul = r.signed();
            let gun_pivot = r.vector();
            assert_eq!(current_mode, mode as u32);
            assert_eq!(current_map, map as u32);
            let tank = Tank::decode(data.get(&format!("TANK{vehicle}.DAT")).unwrap()).unwrap();
            let model = VehicleModel::build(
                &tank,
                &source,
                &mut ModelBank::default(),
                &surfaces,
                BuildContext {
                    resolve: ResolveContext {
                        special,
                        player: true,
                        mode: current_mode,
                        map: current_map,
                        circuit_power_mul,
                        ambient_battle: None,
                    },
                    ambient_wobble,
                    session_active,
                },
            )
            .unwrap();
            let registry = Registry::default();
            let mut identity = entity_factory::State::<Handle>::new(128).unwrap();
            identity.entities.sequential = true;
            identity.entities.next_requested = entity;
            let mut store = Store::default();
            let (created, id) = store
                .create_single_player(
                    &mut identity,
                    &registry,
                    vehicle_loading::Request {
                        model,
                        spawn,
                        static_allocation: true,
                        environment: Environment {
                            ambient_wobble,
                            session_active,
                            ambient_phase,
                            gun_pivot,
                        },
                    },
                    100000,
                )
                .unwrap();
            assert_eq!(created, entity);
            let record = identity.entities.record(entity).unwrap();
            assert_eq!(record.object, Some(Handle::Vehicle(id)));
            assert_eq!(record.object.unwrap().obstacle(), None);
            let player = store.entries[id.0].as_ref().unwrap();
            if variant == 1 || variant == 3 {
                assert_ne!(
                    player.physics.motion.poses.physical_position[1],
                    spawn.position[1]
                );
                wobbling += 1;
            } else if variant == 2 {
                assert_eq!(
                    player.physics.motion.poses.physical_position,
                    spawn.position
                );
                suppressed += 1;
            }
            let mut expected = encode(player);
            expected.extend([
                identity.ticks.flags[entity.0 as usize],
                u32::from(record.class == Some(registry.player)),
                identity
                    .ticks
                    .order
                    .iter()
                    .filter(|&&e| e == entity)
                    .count() as u32,
                store.visible.iter().filter(|&&v| v == id).count() as u32,
                store.hidden.iter().filter(|&&v| v == id).count() as u32,
                0,
            ]);
            let actual = &r.words[r.at..r.at + expected.len()];
            for (word, (a, e)) in actual.iter().zip(&expected).enumerate() {
                assert_eq!(
                    a, e,
                    "map={map} cup={cup} vehicle={vehicle} mode={mode} record={count} word={word}"
                );
            }
            r.at += expected.len();
            compared += expected.len();
            records += 1;
            count += 1;
        }
        assert_eq!(count, players.len().max(1));
        assert_eq!(r.word(), count as u32);
        assert_eq!(r.at, r.words.len());
    }
    assert_eq!(records, 122, "constructor hook must actually execute");
    assert_eq!(wobbling, 24);
    assert_eq!(suppressed, 12);
    println!(
        "{records} fresh player constructions / {compared} words; all 8 maps, 3 cups, 15 vehicles, 2 modes; {wobbling} nonzero wobble + {suppressed} suppressed"
    );
}
