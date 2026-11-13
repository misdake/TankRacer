//! Construct real player dynamics from resources/spawn, before any frame snapshot.
use std::{collections::BTreeSet, fs, path::PathBuf, process::Command};
use tankracer_assets::{
    archive::Archive,
    config::{Part, ResolveContext, Surfaces, Tank},
    level::Level,
    model::ShapeArchive,
    parameters::Parameters,
};
use tankracer_game::{
    dispatch::ClassId,
    object_query::EntityId,
    pose,
    vehicle_model::{BuildContext, Bundles, ModelBank, VehicleModel},
    vehicle_startup::{Environment, Player},
};
#[path = "common/motion.rs"]
#[allow(dead_code)]
mod common;
use common::Reader;

fn append(out: &mut Vec<u32>, values: impl IntoIterator<Item = i32>) {
    out.extend(values.into_iter().map(|v| v as u32));
}
fn encode(player: &Player) -> Vec<u32> {
    let v = &player.physics;
    let mut out = common::words(&v.motion);
    let p = &v.powertrain;
    append(&mut out, p.local_velocity);
    append(&mut out, [p.track_speed, p.boost_target, p.engine_display]);
    out.extend([u32::from(p.was_active), p.gear as u8 as u32, p.load as u32]);
    for (point, response) in v
        .contact
        .sampling
        .primary
        .points
        .iter()
        .zip(&v.contact.response.points)
    {
        append(&mut out, point.local);
        append(&mut out, [point.kind, point.previous_gap, point.raw_height]);
        out.push(point.flags);
        append(&mut out, point.normal);
        out.push(response.corner);
        append(&mut out, [response.rate, response.output]);
    }
    for point in player.model.auxiliary_local {
        append(&mut out, point);
    }
    let c = &v.collision;
    append(&mut out, [v.yaw_lock, i32::from(c.disabled), c.timer]);
    out.push(c.collision_size);
    out.extend([
        c.material as u32,
        c.impact_accumulator as u32,
        u32::from(c.deformable.is_some()),
        c.obstacle_impulse as u32,
    ]);
    append(&mut out, c.tank_delta);
    append(&mut out, c.ground_delta);
    let p = &player.prefix;
    append(
        &mut out,
        [
            p.alternating_sign,
            p.countdown_throttle,
            p.countdown_rpm,
            p.target,
        ],
    );
    append(
        &mut out,
        [i32::from(p.temporary_timer), i32::from(p.temporary_slot)],
    );
    out.extend([p.restore_sidegrip.packed(), p.sidegrip.packed()]);
    let b = &p.boost;
    append(
        &mut out,
        [
            b.extra_speed,
            b.power,
            b.power_loss,
            i32::from(b.timer),
            b.camera_velocity,
            b.camera_timer,
        ],
    );
    out.extend([
        b.target.0,
        u32::from(b.active),
        u32::from(b.applied),
        u32::from(b.phase),
    ]);
    let s = &p.spin;
    out.extend([u32::from(s.flags), u32::from(s.visible)]);
    append(&mut out, [s.hold, s.color_timer]);
    out.extend([s.color, s.target_color]);
    append(&mut out, [s.angle, s.speed]);
    out.push(u32::from(s.disabled));
    if let Some(gun) = &player.model.parts[Part::Gun.slot()] {
        out.push(1);
        out.extend(player.model.gun_vertices.map(u32::from));
        append(&mut out, player.model.gun_original_z.map(i32::from));
        out.push(gun.geometry().vertices.len() as u32);
        for vertex in &gun.geometry().vertices {
            append(&mut out, vertex.map(i32::from));
        }
    } else {
        out.extend([0; 10]);
    }
    out.push(u32::from(player.lights.brake_mode));
    out.extend(player.lights.active.map(u32::from));
    out.extend(player.lights.handles.map(|h| h.0));
    out.extend(player.marks.active.map(u32::from));
    append(
        &mut out,
        player.marks.endpoints.into_iter().flatten().flatten(),
    );
    append(&mut out, player.marks.movement);
    out.extend(player.marks.handles.map(|h| h.0));
    out.extend([player.marks.count, player.marks.head, player.marks.tail]);
    for p in player.spray.particles {
        append(&mut out, p.position);
        append(&mut out, p.velocity);
        append(
            &mut out,
            [
                i32::from(p.life),
                i32::from(p.size as i16),
                i32::from(p.floor),
                i32::from(p.rotation as i16),
            ],
        );
        out.extend([u32::from(p.kind as u8), u32::from(p.side)]);
    }
    out.extend([
        player.spray.timer as u32,
        player.spray.side,
        player.spray.count,
    ]);
    for p in player.smoke.particles {
        append(&mut out, p.position);
        append(&mut out, p.velocity);
        append(
            &mut out,
            [
                i32::from(p.life),
                i32::from(p.size as i16),
                i32::from(p.rotation as i16),
            ],
        );
        out.extend([u32::from(p.direction), u32::from(p.expanding)]);
    }
    out.extend([
        player.smoke.timer as u32,
        player.smoke.side,
        player.smoke.force as u32,
        player.smoke.count,
    ]);
    out.extend(player.smoke.damage.map(u32::from));
    out.extend(player.smoke.sockets.map(u32::from));
    append(&mut out, player.smoke.damage_timers);
    out
}
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
        assert_eq!(&output.stdout[..4], b"VS03");
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
            let player = Player::construct(
                entity,
                ClassId(6),
                model,
                spawn,
                Environment {
                    ambient_wobble,
                    session_active,
                    ambient_phase,
                    gun_pivot,
                },
            )
            .unwrap();
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
            let expected = encode(&player);
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
