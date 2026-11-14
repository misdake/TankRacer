//! Real resource-built scenery and Player share one scheduler/collision state.
use std::{
    fs,
    io::Write,
    path::PathBuf,
    process::{Command, Stdio},
    sync::Arc,
};
use tankracer_assets::{
    archive::Archive, config::ResolveContext, level::Level, terrain::TerrainData,
};
use tankracer_game::{
    drive,
    math::Matrix,
    obstacle_collision::Invocation,
    pose,
    scene_objects::collision::{Collision, Services, Sound},
    scenery,
    vehicle_loading::Request,
    vehicle_model::BuildContext,
    vehicle_resources,
    vehicle_runtime::{Devices, Settings, SinglePlayer, Startup},
    vehicle_scene::spray,
    vehicle_startup::Environment,
};
#[path = "common/motion.rs"]
#[allow(dead_code)]
mod common;
#[path = "common/player.rs"]
mod player_codec;
#[path = "common/waypoint.rs"]
mod route_codec;
struct Headless<'a>(&'a scenery::Loaded);
impl Services for Headless<'_> {
    fn auxiliary_model(
        &mut self,
        name: &str,
    ) -> Result<Option<Arc<tankracer_assets::model::Model>>, i32> {
        Ok(self.0.auxiliary_model(name))
    }
    fn sound(&mut self, _: Sound, _: &mut Collision<'_>, _: Invocation<'_>) -> Result<(), i32> {
        Ok(())
    }
    fn feedback(&mut self, _: i32, _: &mut Collision<'_>, _: Invocation<'_>) -> Result<(), i32> {
        Ok(())
    }
}
impl Devices for Headless<'_> {
    fn surface_feedback(&mut self) -> Result<(), i32> {
        Ok(())
    }
    fn impact_feedback(&mut self, _: i32) -> Result<(), i32> {
        Ok(())
    }
}
fn append(out: &mut Vec<u32>, values: impl IntoIterator<Item = i32>) {
    out.extend(values.into_iter().map(|v| v as u32));
}
fn list(out: &mut Vec<u32>, values: impl IntoIterator<Item = u32>) {
    let at = out.len();
    out.push(0);
    out.extend(values);
    out[at] = (out.len() - at - 1) as u32;
}
fn snapshot(game: &SinglePlayer, n: usize) -> Vec<(String, Vec<u32>)> {
    let player = game.vehicles.entries[0].as_ref().unwrap();
    let mut words = player_codec::encode(player);
    append(
        &mut words,
        [
            player.physics.blast.health,
            player.physics.blast.protection_timer,
        ],
    );
    words.push(u32::from(player.physics.blast.angular_enabled));
    words.extend([
        game.shared.random,
        game.settings.environment_flag as u32,
        n as u32,
    ]);
    let mut sections = vec![("player".into(), words)];
    for (index, object) in game.objects.components.objects[..n].iter().enumerate() {
        let mut out = vec![u32::from(object.is_some())];
        let Some(object) = object else {
            sections.push((format!("object {index}"), out));
            continue;
        };
        let collider = &game.objects.world.colliders[index];
        let properties = &game.objects.world.objects[index];
        let data = &object.data;
        let class = properties.class;
        if class == game.registry.waypoint {
            out.extend([7, game.identity.ticks.flags[collider.entity.0 as usize]]);
            sections.push((format!("waypoint {index}"), out));
            continue;
        }
        out.extend([
            if class == game.registry.fall {
                2
            } else if class == game.registry.crush {
                1
            } else if class == game.registry.bounce {
                3
            } else if class == game.registry.closer {
                4
            } else if class == game.registry.spin {
                5
            } else if class == game.registry.mover {
                6
            } else {
                0
            },
            game.identity.ticks.flags[collider.entity.0 as usize],
        ]);
        append(&mut out, collider.pose.position);
        append(&mut out, collider.pose.matrix.0);
        out.push(u32::from(collider.external_basis.is_some()));
        append(
            &mut out,
            collider.external_basis.unwrap_or(Matrix::IDENTITY).0,
        );
        out.push(u32::from(data.mesh.is_some()));
        if data.mesh.is_some() {
            out.push(u32::from(collider.radius));
        }
        append(&mut out, collider.bounds.min);
        append(&mut out, collider.bounds.max);
        append(
            &mut out,
            [properties.weight, properties.threshold, properties.activity],
        );
        out.extend([
            data.cell.map_or(u32::MAX, |c| c.packed()),
            data.visibility.distance as i16 as i32 as u32,
            data.sort_position as i32 as u32,
            data.visibility.add_lap as i32 as u32,
            data.visibility.remove_lap as i32 as u32,
        ]);
        if class == game.registry.fall {
            let fall = game.objects.components.falls[index].as_ref().unwrap();
            out.push(properties.flags);
            append(
                &mut out,
                [fall.angular_velocity, fall.rotation, fall.turn_rate],
            );
            out.extend([
                fall.last_vehicle.map_or(0, |v| v.0),
                fall.slope_limit as u32,
                u32::from(fall.basis_backup.is_some()),
            ]);
            if let Some(basis) = fall.basis_backup {
                append(&mut out, basis.0);
            }
        }
        if class == game.registry.crush {
            let surface = game.objects.components.surfaces.entries[index]
                .as_ref()
                .unwrap();
            append(&mut out, [surface.config.floor, surface.config.compression]);
            out.extend([
                surface.config.drift_shift,
                u32::from(surface.state.dirty),
                surface.state.last_vehicle.map_or(0, |v| v.0),
                surface.state.sound_timer as u32,
            ]);
            append(&mut out, surface.sampling.bounds.into_iter().flatten());
            out.extend([
                surface.sampling.edge_cap as u32,
                u32::from(surface.sampling.state.dirty),
                surface.sampling.state.records.len() as u32,
            ]);
            for r in &surface.sampling.state.records {
                append(&mut out, r.position);
                append(&mut out, r.world_position);
                out.push(u32::from(r.state));
                out.extend(r.color.map(u32::from));
                out.push(r.timer as u32);
            }
        }
        if class == game.registry.bounce {
            let b = game.objects.components.bounces[index].as_ref().unwrap();
            let s = b.state;
            let c = b.config;
            append(&mut out, s.velocity);
            append(&mut out, [s.spin, s.ground]);
            out.extend([
                u32::from(s.timer),
                s.age as i32 as u32,
                u32::from(s.airborne),
                u32::from(s.reverse_spin),
                u32::from(s.special),
                u32::from(s.raised),
                c.elasticity as i32 as u32,
                u32::from(c.friction as u8),
                u32::from(c.height_factor as u8),
            ]);
            append(
                &mut out,
                [
                    c.hit_spin.multiplier,
                    c.hit_spin.shift,
                    c.slow_spin.multiplier,
                    c.slow_spin.shift,
                ]
                .map(|v| i32::from(v as i16)),
            );
            append(&mut out, [i32::from(c.deflection), c.gravity]);
            out.push(u32::from(c.height_period));
        }
        if class == game.registry.closer {
            let c = game.objects.components.closers[index]
                .as_ref()
                .unwrap()
                .state;
            out.extend([
                c.speed as u32,
                c.flags,
                match c.phase {
                    tankracer_game::closer::Phase::Uninitialized => 0,
                    tankracer_game::closer::Phase::Waiting => 1,
                    tankracer_game::closer::Phase::Closing => 2,
                    tankracer_game::closer::Phase::Fragments => 3,
                },
                object.auxiliary_active as i32 as u32,
            ]);
        }
        if class == game.registry.spin || class == game.registry.mover {
            let b = game.objects.components.spins[index].as_ref().unwrap();
            let (s, c) = (b.state, b.config);
            append(&mut out, s.velocity);
            out.extend([
                s.yawing,
                c.elasticity as i32 as u32,
                u32::from(c.friction as u8),
                u32::from(s.timer),
                s.pivot_axis,
            ]);
            append(&mut out, [s.ground, s.edge_ground]);
            append(&mut out, s.normal);
            append(&mut out, s.angular_velocity);
            append(&mut out, [i32::from(c.yaw_factor)]);
            for g in [c.hit_x, c.hit_z, c.hit_slow, c.yaw_slow, c.flatten] {
                append(
                    &mut out,
                    [i32::from(g.multiplier as i16), i32::from(g.shift as i16)],
                );
            }
            append(
                &mut out,
                [
                    i32::from(c.stop_yaw),
                    c.deflection,
                    i32::from(object.auxiliary_active),
                ],
            );
        }
        if class == game.registry.mover {
            let m = game.objects.components.movers[index].as_ref().unwrap();
            out.push(m.flags);
            append(
                &mut out,
                [m.distance, m.travel, m.speed, m.activation_range],
            );
        }
        let geometry = data
            .mesh
            .and_then(|id| game.objects.components.surfaces.meshes.get(id));
        out.push(u32::from(data.debris.is_some()));
        if let Some(d) = &data.debris {
            out.extend([d.flags, u32::from(d.retired)]);
            append(&mut out, d.offset);
            append(
                &mut out,
                [d.ground_height, d.gravity, d.friction, d.direction],
            );
            out.push(d.pieces.len() as u32);
            for p in &d.pieces {
                append(&mut out, p.position);
                append(&mut out, p.velocity);
                append(&mut out, p.matrix.0);
                append(&mut out, p.translation);
                append(&mut out, p.angular);
                append(
                    &mut out,
                    [
                        p.previous_tilt,
                        i32::from(p.velocity_gain),
                        i32::from(p.spin_gain),
                    ],
                );
                out.extend([p.flags, p.sprite_angle as u32]);
            }
        }
        out.push(geometry.map_or(0, |g| g.vertices.len()) as u32);
        if let Some(g) = geometry {
            for v in &g.vertices {
                append(&mut out, v.map(i32::from));
            }
        }
        sections.push((format!("object {index} entity {}", collider.entity.0), out));
    }
    let mut out = Vec::new();
    for ids in [
        &game.objects.components.ordinary,
        &game.objects.components.special,
        &game.objects.components.lap_objects,
        &game.objects.components.signs,
    ] {
        list(
            &mut out,
            ids.iter()
                .map(|id| game.objects.world.colliders[id.0].entity.0),
        );
    }
    list(&mut out, game.identity.ticks.order.iter().map(|id| id.0));
    list(
        &mut out,
        game.identity.queues.notifications.iter().map(|id| id.0),
    );
    list(
        &mut out,
        game.identity.queues.destruction.iter().map(|id| id.0),
    );
    for cell in &game.objects.world.grid.cells {
        list(
            &mut out,
            cell.iter()
                .map(|id| game.objects.world.colliders[id.0].entity.0),
        );
    }
    sections.push(("memberships".into(), out));
    let w = &game.objects.components.explosion_world;
    let mut out = vec![w.count as u32, u32::from(w.overflow), u32::from(w.fog)];
    for slot in &w.slots {
        out.extend([slot.entity.map_or(0, |e| e.0), slot.frame]);
    }
    let mut live = game
        .objects
        .components
        .explosions
        .iter()
        .enumerate()
        .filter_map(|(i, e)| e.as_ref().map(|e| (i, e)))
        .collect::<Vec<_>>();
    live.sort_by_key(|(i, _)| game.objects.world.colliders[*i].entity.0);
    out.push(live.len() as u32);
    for (id, e) in live {
        let c = &game.objects.world.colliders[id];
        out.extend([c.entity.0, game.identity.ticks.flags[c.entity.0 as usize]]);
        append(&mut out, c.pose.position);
        append(&mut out, c.pose.matrix.0);
        out.push(e.slot as i32 as u32);
        sprite(&mut out, &e.sprite);
        append(&mut out, e.velocity);
        append(&mut out, [e.timer, e.angular_velocity]);
        out.push(u32::from(e.visible));
        for s in &e.smoke {
            sprite(&mut out, &s.sprite);
            append(&mut out, s.velocity);
            append(&mut out, [s.lift, s.angular_velocity]);
            out.push(u32::from(s.active));
        }
        out.extend([
            e.smoke_timer as u32,
            u32::from(e.smoke_count),
            e.band,
            e.ring,
        ]);
    }
    sections.push(("explosions".into(), out));
    sections.push((
        "waypoints".into(),
        route_codec::snapshot(&game.objects, &game.identity),
    ));
    sections
}
fn sprite(out: &mut Vec<u32>, s: &tankracer_game::explosion::Sprite) {
    out.push(u32::from(s.material != 0));
    append(out, s.position);
    out.push(s.flags);
    out.push(s.blend as u32);
    append(out, s.size);
    out.extend(s.uv.map(u32::from));
    out.extend(s.color.map(u32::from));
    out.push(s.rotation as i16 as i32 as u32);
}
fn settings(map: usize) -> Settings {
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
    }
}
#[test]
#[ignore = "requires registered scenery/runtime C oracle; set TANKRACER_SCENERY_RUNTIME_REFERENCE"]
fn authored_scenery_and_player_update_the_same_components_as_c() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    let data = fs::read(root.join("DATA/FASTFILE.DAT")).unwrap();
    let bronze = fs::read(root.join("TANKS/BRONZE/FASTFILE.DAT")).unwrap();
    let shared = fs::read(root.join("INGAME/FASTFILE.DAT")).unwrap();
    let names = [
        "ENGLAND", "MEXICO", "SWAMP", "EAST", "THEME", "BIO", "MONACO", "CIRCUIT",
    ];
    let mut compared = 0;
    let mut updates = 0;
    let mut activated = 0;
    let mut deformed = 0;
    let mut bounced = 0;
    let mut closing = 0;
    let mut shattered = 0;
    let mut spun = 0;
    let mut moved = 0;
    let mut handed_off = 0;
    let mut exploded = 0;
    for (map, name) in names.iter().enumerate() {
        let mut scenarios: Vec<_> = if map == 0 {
            (0..8).map(|target| (target, false)).collect()
        } else {
            vec![(0, false)]
        };
        scenarios.push((if map == 0 { 4 } else { 0 }, true));
        if [2, 3].contains(&map) {
            scenarios.extend([(8, false), (9, false)]);
        }
        if map == 5 {
            scenarios.extend((10..=17).map(|target| (target, false)));
            scenarios.extend([(19, false), (20, false)]);
        }
        for (target, low_gravity) in scenarios {
            let cup = if map == 7 {
                2
            } else if [3, 5, 6].contains(&map) {
                1
            } else {
                0
            };
            let mut file = fs::read(root.join("LEVELS").join(name).join("FASTFILE.DAT")).unwrap();
            if target == 12 {
                // Declared authored variation exercises the inherited Spin hit
                // without depending on the still-unbound explosion class. Equal
                // byte width preserves the archive directory and all resources.
                let needle = b":explode true";
                let at: Vec<_> = file
                    .windows(needle.len())
                    .enumerate()
                    .filter_map(|(i, b)| (b == needle).then_some(i))
                    .collect();
                assert_eq!(at.len(), 9); // Three robots in each of BIO B/S/G.
                for i in at {
                    file[i + 9..i + 13].copy_from_slice(b"faux");
                }
            }
            let archive = Archive::parse(&file).unwrap();
            let bsm = archive
                .entries()
                .iter()
                .find(|e| e.name.ends_with(".BSM"))
                .unwrap();
            let terrain =
                Arc::new(TerrainData::decode_bsm(archive.get(bsm.name).unwrap()).unwrap());
            let mut setting = settings(map);
            setting.environment_flag = i32::from(low_gravity);
            let mut startup = Startup::new(terrain, setting).unwrap();
            let loaded = scenery::load(&mut startup, &data, &file, &shared, name, cup).unwrap();
            let n = loaded.entities.len();
            if map == 0 {
                assert_eq!(n, 177);
                assert_eq!(loaded.classes.get("Fall"), Some(&96));
                assert_eq!(loaded.classes.get("Bounce"), Some(&29));
            }
            let level = Level::decode(
                archive
                    .get(&format!("{}{}.DAT", &name[..3], ["B", "S", "G"][cup]))
                    .unwrap(),
            )
            .unwrap();
            let mut script = String::from("FSTPC 5\n");
            for r in level.objects.iter().filter(|r| {
                [
                    "Obstacle", "Crush", "Fall", "Bounce", "Closer", "Spin", "Mover", "WayPoint",
                ]
                .contains(&r.class)
            }) {
                let p = r.position.map(|v| v as f64 / 1024.);
                let a = r.angles.map(|v| v as i32 / 182);
                script.push_str(&format!(
                    "{} {} {} {} {} {} {} {} {} ({})\n",
                    r.class, r.name, p[0], p[1], p[2], a[0], a[1], a[2], r.shape, r.parameters
                ));
            }
            script.push_str("Paths ()");
            let mut spawn = [0; 3];
            let mut velocity = [0; 3];
            let mut camera = [0; 3];
            if target != 0 {
                let wanted = if target >= 10 {
                    startup.registry.mover
                } else if target >= 8 {
                    startup.registry.spin
                } else if target >= 6 {
                    startup.registry.closer
                } else if target >= 4 {
                    startup.registry.bounce
                } else if target == 3 {
                    startup.registry.crush
                } else {
                    startup.registry.fall
                };
                let index = startup
                    .objects
                    .world
                    .objects
                    .iter()
                    .enumerate()
                    .filter(|(i, p)| {
                        p.class == wanted
                            && startup.objects.components.objects[*i]
                                .as_ref()
                                .unwrap()
                                .data
                                .visibility
                                .entered
                    })
                    .nth(usize::from(target == 2))
                    .unwrap()
                    .0;
                spawn = startup.objects.world.colliders[index].pose.position;
                spawn[1] += 1500;
                spawn[2] += if target == 5 { 500 } else { -500 };
                velocity = [0, 0, if target == 5 { -250000 } else { 600001 }];
                if target == 6 {
                    spawn[0] += 38000;
                    spawn[2] += 38500;
                    velocity = [0; 3];
                }
                if target == 9 {
                    spawn[0] -= 500;
                    spawn[2] += 500;
                    velocity = [600001, 0, 0];
                }
                if (10..12).contains(&target) || [15, 20].contains(&target) {
                    camera = spawn.map(|v| v >> 3);
                    spawn[0] += 38000;
                    spawn[2] += 500;
                    velocity = [0; 3];
                    if target == 11 {
                        camera = [0; 3];
                    }
                }
            }
            let resources = vehicle_resources::prepare(
                &data,
                &[&bronze],
                &shared,
                1,
                BuildContext {
                    resolve: ResolveContext {
                        player: true,
                        map: map as u32,
                        ..Default::default()
                    },
                    ambient_wobble: false,
                    session_active: false,
                },
            )
            .unwrap();
            let mut game = startup
                .finish(
                    resources.surfaces,
                    Request {
                        model: resources.model,
                        spawn: pose::Pose {
                            position: spawn,
                            ..Default::default()
                        },
                        static_allocation: true,
                        environment: Environment {
                            ambient_wobble: false,
                            session_active: false,
                            ambient_phase: 0,
                            gun_pivot: [0; 3],
                        },
                    },
                )
                .unwrap();
            let player = game.vehicles.entries[0].as_mut().unwrap();
            player.physics.motion.previous_position = spawn;
            player.physics.motion.velocity = velocity;
            game.camera_position = camera;
            let steps = if target == 0 {
                4
            } else if target >= 14 {
                16
            } else {
                12
            };
            if [15, 20].contains(&target) {
                for k in 0..16 {
                    game.objects.components.frame_group =
                        if target == 20 { (15 - k) % 7 } else { 0 };
                    game.objects
                        .spawn_explosion(
                            &mut game.identity,
                            spawn,
                            &mut game.shared.random,
                            game.registry.environment(100000),
                        )
                        .unwrap();
                }
                game.objects.components.frame_group = 0;
                game.identity
                    .context()
                    .drain(
                        tankracer_game::scene_loading::STOP,
                        game.registry.environment(100000),
                        &mut game.objects,
                    )
                    .unwrap();
            }
            let mut input = Vec::new();
            input.extend((script.len() as u32).to_le_bytes());
            input.extend((n as u32).to_le_bytes());
            input.extend(u32::from(low_gravity).to_le_bytes());
            for v in spawn.into_iter().chain(velocity).chain(camera) {
                input.extend(v.to_le_bytes());
            }
            input.extend(script.bytes());
            let mut child = Command::new(
                std::env::var_os("TANKRACER_SCENERY_RUNTIME_REFERENCE")
                    .expect("set scenery oracle"),
            )
            .arg(&root)
            .arg(map.to_string())
            .arg("1")
            .arg(steps.to_string())
            .arg(target.to_string())
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .unwrap();
            child.stdin.take().unwrap().write_all(&input).unwrap();
            let output = child.wait_with_output().unwrap();
            assert!(
                output.status.success(),
                "map={map} target={target} status={:?}: {}",
                output.status.code(),
                String::from_utf8_lossy(&output.stderr)
            );
            assert_eq!(&output.stdout[..4], b"SR07");
            let mut r = common::Reader {
                words: output.stdout[4..]
                    .chunks_exact(4)
                    .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
                    .collect(),
                at: 0,
            };
            assert_eq!(
                r.word(),
                game.vehicles.entries[0].as_ref().unwrap().physics.entity.0
            );
            for tick in 0..=steps {
                if tick != 0 {
                    if target == 19 && tick == 13 {
                        for e in game.objects.components.explosions.iter_mut().flatten() {
                            e.smoke_count = 9;
                            e.smoke_timer = 96;
                        }
                    }
                    if [14, 16, 17].contains(&target) && tick == 13 {
                        for e in game.objects.components.explosions.iter_mut().flatten() {
                            e.sprite.uv[0] = 96;
                            e.sprite.uv[1] = 96;
                            e.timer = 31;
                            e.smoke_count = 10;
                            for s in &mut e.smoke {
                                s.active = 0;
                            }
                            if target == 17 {
                                e.smoke[0].active = 1;
                                e.smoke[0].sprite.color = [1; 3];
                            }
                        }
                    }
                    if target == 16 && tick == 14 {
                        let p = game.vehicles.entries[0]
                            .as_ref()
                            .unwrap()
                            .physics
                            .motion
                            .poses
                            .base_position;
                        game.objects
                            .spawn_explosion(
                                &mut game.identity,
                                p,
                                &mut game.shared.random,
                                game.registry.environment(100000),
                            )
                            .unwrap();
                    }
                    let status = r.word() as i32;
                    let result = game.step(
                        drive::Input {
                            throttle: 65536,
                            ..Default::default()
                        },
                        16,
                        true,
                        &mut Headless(&loaded),
                    );
                    assert_eq!(status, 0, "C map={map} target={target} tick={tick}");
                    result.unwrap();
                    updates += 1;
                }
                for (section, words) in snapshot(&game, n) {
                    for (word, value) in words.iter().enumerate() {
                        assert_eq!(
                            *value,
                            r.word(),
                            "map={map} target={target} low_gravity={low_gravity} tick={tick} {section} word={word}"
                        );
                    }
                    compared += words.len();
                }
            }
            assert_eq!(r.at, r.words.len());
            activated += game
                .objects
                .world
                .objects
                .iter()
                .filter(|p| p.class == game.registry.fall && p.flags & 1 != 0)
                .count();
            deformed += game
                .objects
                .components
                .surfaces
                .entries
                .iter()
                .flatten()
                .filter(|s| s.state.dirty != 0)
                .count();
            let hits = game
                .objects
                .components
                .bounces
                .iter()
                .flatten()
                .filter(|b| b.state.age != 0 || b.state.airborne != 0)
                .count();
            if (4..6).contains(&target) {
                assert!(hits > 0, "targeted Bounce must activate");
            }
            bounced += hits;
            let phase = game
                .objects
                .components
                .closers
                .iter()
                .flatten()
                .map(|c| c.state.phase)
                .collect::<Vec<_>>();
            if target == 6 {
                assert!(phase.contains(&tankracer_game::closer::Phase::Closing));
                closing += 1;
            }
            if target == 7 {
                assert!(phase.contains(&tankracer_game::closer::Phase::Fragments));
                shattered += 1;
            }
            if (8..10).contains(&target) {
                let hits = game
                    .objects
                    .components
                    .spins
                    .iter()
                    .flatten()
                    .filter(|b| b.state.timer != 0)
                    .count();
                assert!(hits > 0, "targeted Spin must update");
                spun += hits;
            }
            if (10..12).contains(&target) {
                let actors = game
                    .objects
                    .components
                    .movers
                    .iter()
                    .flatten()
                    .collect::<Vec<_>>();
                assert!(actors.iter().any(|m| m.flags & 1 != 0));
                let moving = actors.iter().filter(|m| m.travel != 0).count();
                if target == 10 {
                    assert!(moving > 0);
                    moved += moving;
                } else {
                    assert_eq!(moving, 0);
                }
            }
            if target == 12 {
                assert!(
                    game.objects
                        .components
                        .movers
                        .iter()
                        .flatten()
                        .any(|m| m.flags & 64 != 0),
                    "nonexploding Mover must hand motion to Spin"
                );
                handed_off += 1;
            }
            if target == 13 {
                assert!(
                    game.objects.components.explosion_world.count > 0,
                    "original Mover must spawn a real explosion"
                );
                assert!(
                    game.objects.components.objects.iter().flatten().any(|o| o
                        .data
                        .debris
                        .as_ref()
                        .is_some_and(|d| d.flags & 1 != 0)),
                    "original Mover debris must activate"
                );
                exploded += 1;
            }
            if [14, 17].contains(&target) {
                assert_eq!(game.objects.components.explosion_world.count, 0);
            }
            if [15, 20].contains(&target) {
                assert_eq!(game.objects.components.explosion_world.count, 15);
                assert_eq!(game.objects.components.explosion_world.overflow, 1);
                assert!(game.objects.components.explosion_world.fog);
            }
            if target == 16 {
                assert_eq!(game.objects.components.explosion_world.count, 1);
            }
            if target == 19 {
                assert!(
                    game.objects
                        .components
                        .explosions
                        .iter()
                        .flatten()
                        .any(|e| e.smoke_count == 10 && e.smoke[9].active != 0)
                );
            }
        }
    }
    assert!(activated > 0, "real Fall collision must execute");
    assert!(deformed > 0, "real deformable hit must execute");
    println!(
        "scenery runtime: {updates} updates / {compared} fields; {activated} activated Fall, {deformed} deformed Crush, {bounced} hit Bounce, {closing} closing / {shattered} shattered Closer, {spun} hit Spin, {moved} active Mover / {handed_off} Spin handoff / {exploded} original exploding Mover"
    );
}
