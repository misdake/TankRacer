//! Authored routes share the real factory, symbols, lifecycle and world poses.
use std::{
    fs,
    io::Write,
    path::PathBuf,
    process::{Command, Stdio},
    sync::Arc,
};
use tankracer_assets::{archive::Archive, level::Level, terrain::TerrainData};
use tankracer_game::{
    scenery,
    symbols::Binding,
    vehicle_runtime::{Settings, Startup},
    vehicle_scene::spray,
    waypoint,
};
#[path = "common/waypoint.rs"]
mod route_codec;
fn settings(map: u32) -> Settings {
    Settings {
        map,
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
    }
}
#[test]
#[ignore = "requires native route oracle; set TANKRACER_WAYPOINT_REFERENCE"]
fn authored_factory_link_grid_weather_and_queries_match_c() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    let data = fs::read(root.join("DATA/FASTFILE.DAT")).unwrap();
    let shared = fs::read(root.join("INGAME/FASTFILE.DAT")).unwrap();
    let maps = [
        "ENGLAND", "MEXICO", "SWAMP", "EAST", "THEME", "BIO", "MONACO", "CIRCUIT",
    ];
    let mut records = 0;
    let mut queries = 0_usize;
    let mut empty_levels = 0;
    let mut compared = 0;
    let mut crossings = 0;
    let mut selected = 0;
    for (map, name) in maps.iter().enumerate() {
        let file = fs::read(root.join("LEVELS").join(name).join("FASTFILE.DAT")).unwrap();
        let archive = Archive::parse(&file).unwrap();
        let bsm = archive
            .entries()
            .iter()
            .find(|e| e.name.ends_with(".BSM"))
            .unwrap();
        let terrain = Arc::new(TerrainData::decode_bsm(archive.get(bsm.name).unwrap()).unwrap());
        for cup in 0..3 {
            let level = Level::decode(
                archive
                    .get(&format!("{}{}.DAT", &name[..3], ["B", "S", "G"][cup]))
                    .unwrap(),
            )
            .unwrap();
            let mut s = Startup::new(Arc::clone(&terrain), settings(map as u32)).unwrap();
            let loaded = scenery::load(&mut s, &data, &file, &shared, name, cup).unwrap();
            let mut script = String::from("FSTPC 5\n");
            for r in level
                .objects
                .iter()
                .filter(|r| s.registry.scenery_class(r.class).is_some())
            {
                let p = r.position.map(|v| v as f64 / 1024.);
                let a = r.angles.map(|v| v as i32 / 182);
                script.push_str(&format!(
                    "{} {} {} {} {} {} {} {} {} ({})\n",
                    r.class, r.name, p[0], p[1], p[2], a[0], a[1], a[2], r.shape, r.parameters
                ));
            }
            script.push_str("Paths ()");
            let mut bytes = (script.len() as u32).to_le_bytes().to_vec();
            bytes.extend(script.bytes());
            let empty = s.objects.components.waypoints.order.is_empty();
            if empty {
                assert_eq!(map, 7, "only unused CIRCUIT B/S resources omit the route");
                assert!(cup < 2);
                assert_eq!(
                    s.objects.components.waypoints.link(
                        &s.identity.symbols,
                        &s.identity.entities,
                        &s.objects.world.colliders,
                        100000
                    ),
                    Err(-4)
                );
                empty_levels += 1;
            }
            let mut expected = vec![if empty { (-4_i32) as u32 } else { 0 }];
            expected.extend(route_codec::snapshot(&s.objects, &s.identity));
            records += s.objects.components.waypoints.order.len();
            let view = waypoint::View {
                routes: &s.objects.components.waypoints,
                world: &s.objects.world,
                entities: &s.identity.entities,
            };
            for &id in &view.routes.order {
                let c = &s.objects.world.colliders[id.0];
                let point = view.routes.point(id).unwrap();
                let mut positions = vec![c.pose.position];
                for delta in [
                    [0, 0, -1],
                    [0, 0, 1],
                    [10000, 0, 10000],
                    [51199, 0, -51199],
                    [51200, 0, 51200],
                    [153599, 0, 0],
                    [153600, 0, 0],
                    [i32::MIN, 0, i32::MAX],
                ] {
                    positions.push(std::array::from_fn(|i| {
                        c.pose.position[i].wrapping_add(delta[i])
                    }));
                }
                if let Binding::Direct(Some(next)) = point.next[0] {
                    let next = s
                        .identity
                        .entities
                        .record(next)
                        .unwrap()
                        .object
                        .unwrap()
                        .obstacle()
                        .unwrap();
                    let c = &s.objects.world.colliders[next.0];
                    positions.push(std::array::from_fn(|i| {
                        c.pose.position[i].wrapping_add(c.pose.matrix.0[6 + i].wrapping_mul(2))
                    }));
                }
                for (sample, pos) in positions.into_iter().enumerate() {
                    let cases: &[(u32, u32)] = if sample == 0 {
                        &[
                            (0, 0),
                            (0, 1),
                            (0, 3),
                            (0, 10),
                            (1, 0),
                            (1, 1),
                            (1, 3),
                            (1, 10),
                            (0, 100000),
                            (1, 100000),
                            (2, 100000),
                        ]
                    } else {
                        &[(0, 100000), (1, 100000), (2, 100000)]
                    };
                    for &(op, limit) in cases {
                        let lap = queries as i32 % 3;
                        let side = (queries as u32 / 3) % 2;
                        bytes.extend(
                            [
                                op,
                                c.entity.0,
                                pos[0] as u32,
                                pos[1] as u32,
                                pos[2] as u32,
                                lap as u32,
                                side,
                                limit,
                            ]
                            .into_iter()
                            .flat_map(u32::to_le_bytes),
                        );
                        match op {
                            0 => {
                                let result = view.progress([pos[0], pos[2]], c.entity, lap, limit);
                                if result.as_ref().is_ok_and(|s| *s != -1) {
                                    selected += 1;
                                }
                                expected.extend([
                                    result.as_ref().err().copied().unwrap_or(0) as u32,
                                    result.map_or(0xdeadbeef, |v| v as u32),
                                ]);
                            }
                            1 => {
                                let result = view.nearest(pos, limit);
                                expected.extend([
                                    result.as_ref().err().copied().unwrap_or(0) as u32,
                                    result.map_or(0xdeadbeef, |v| v.map_or(0, |e| e.0)),
                                ]);
                            }
                            _ => {
                                let result = view.checkpoint(pos, c.entity, side as usize);
                                if result.as_ref().is_ok_and(|s| s.next.is_some()) {
                                    crossings += 1;
                                }
                                expected.extend([
                                    result.as_ref().err().copied().unwrap_or(0) as u32,
                                    result
                                        .as_ref()
                                        .map_or(0xdeadbeef, |v| v.next.map_or(0, |e| e.0)),
                                    result.map_or(0xdeadbeef, |v| u32::from(v.finishes_lap)),
                                ]);
                            }
                        }
                        queries += 1;
                    }
                }
            }
            let mut child = Command::new(
                std::env::var_os("TANKRACER_WAYPOINT_REFERENCE").expect("set route oracle"),
            )
            .arg(&root)
            .arg(map.to_string())
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .unwrap();
            let mut stdin = child.stdin.take().unwrap();
            let writer = std::thread::spawn(move || stdin.write_all(&bytes).unwrap());
            let output = child.wait_with_output().unwrap();
            writer.join().unwrap();
            assert!(
                output.status.success(),
                "map={name} cup={cup}: {}",
                String::from_utf8_lossy(&output.stderr)
            );
            assert_eq!(&output.stdout[..4], b"WP02");
            let words: Vec<_> = output.stdout[4..]
                .chunks_exact(4)
                .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
                .collect();
            assert_eq!(words.len(), expected.len(), "framing map={name} cup={cup}");
            for (field, (a, b)) in expected.iter().zip(words).enumerate() {
                assert_eq!(*a, b, "map={name} cup={cup} field={field}");
            }
            compared += expected.len();
            assert_eq!(loaded.deferred.get("WayPoint"), None);
        }
    }
    assert!(crossings > 0 && selected > 0);
    println!(
        "WayPoint: 24 resources ({empty_levels} empty route boundaries) / {records} records / {queries} queries / {compared} words; {crossings} crossings / {selected} route selections"
    );
}
