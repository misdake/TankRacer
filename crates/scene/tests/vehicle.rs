//! Actual authored assets, independent matrix expectations and private geometry.
use glam::Vec3;
use tankracer_assets::{
    config::{Part, ResolveContext},
    mesh::Geometry,
};
use tankracer_game::{
    body,
    math::{Axis, Matrix},
    pose,
    vehicle_model::{BuildContext, VehicleModel},
    vehicle_resources,
};
use tankracer_scene::{
    vehicle::{PARTS, VehicleScene},
    world::BatchKind,
};

struct Files {
    data: Vec<u8>,
    shared: Vec<u8>,
    vehicles: [Vec<u8>; 3],
}
impl Files {
    fn load() -> Self {
        let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
        let read = |name| std::fs::read(root.join(name)).unwrap();
        Self {
            data: read("DATA/FASTFILE.DAT"),
            shared: read("INGAME/FASTFILE.DAT"),
            vehicles: [
                "TANKS/BRONZE/FASTFILE.DAT",
                "TANKS/SILVER/FASTFILE.DAT",
                "TANKS/GOLD/FASTFILE.DAT",
            ]
            .map(read),
        }
    }
    fn model(&self, id: u32) -> VehicleModel {
        vehicle_resources::prepare(
            &self.data,
            &self.vehicles.each_ref().map(Vec::as_slice),
            &self.shared,
            id,
            BuildContext {
                resolve: ResolveContext {
                    player: true,
                    ..Default::default()
                },
                ambient_wobble: false,
                session_active: false,
            },
        )
        .unwrap()
        .model
    }
    fn scene(&self, model: &VehicleModel) -> VehicleScene {
        self.vehicles
            .iter()
            .find_map(|v| VehicleScene::load(model, v, &self.shared).ok())
            .expect("vehicle material archive")
    }
}
fn geometry(model: &VehicleModel) -> Vec<Geometry> {
    PARTS
        .into_iter()
        .filter_map(|p| model.parts[p.slot()].as_ref().map(|m| m.geometry().clone()))
        .collect()
}

#[test]
fn all_fifteen_cars_bind_authored_materials_and_world_part_poses() {
    let files = Files::load();
    let mut total_triangles = 0;
    for id in 1..=15 {
        let model = files.model(id);
        let snapshot = geometry(&model);
        let mut scene = files.scene(&model);
        assert_eq!(scene.parts.len(), snapshot.len());
        assert!(scene.parts.iter().any(|p| p.kind == Part::Body));
        assert!(!scene.materials.atlas.premultiplied_rgba.is_empty());
        for yaw in [0, 8192, 16384, 49152] {
            let mut basis = Matrix::IDENTITY;
            basis.rotate(Axis::Y, yaw);
            basis.rotate(Axis::X, 2048);
            basis.rotate(Axis::Z, 1024);
            let mut poses = pose::State {
                physical_position: [123456, 23456, -345678],
                body: pose::Pose {
                    matrix: basis,
                    ..Default::default()
                },
                ..Default::default()
            };
            poses
                .update(
                    &model.pose_config([0; 3]),
                    [160, -240, 320, 80],
                    &pose::Environment {
                        timer: 0,
                        ambient_wobble: false,
                        session_active: false,
                        ambient_phase: 0,
                        recoil_mode: 0,
                        turret_yaw: yaw.wrapping_add(4096) as i16,
                        recoil: -80,
                        gun_pitch: -300,
                        external: pose::ExternalBasis::Body,
                    },
                )
                .unwrap();
            let poses_before = poses.clone();
            scene.refresh(&model, &poses).unwrap();
            for part in &scene.parts {
                assert_eq!(part.revision, 1, "pose changes must not rebuild geometry");
                let source = model.parts[part.kind.slot()].as_ref().unwrap();
                for (batch, normals) in part.geometry.batches.iter().zip(&part.layer_normals) {
                    if tankracer_scene::vehicle::layer::eligible(part.kind) {
                        assert_eq!(batch.vertices.len(), normals.len());
                        for (v, n) in batch.vertices.iter().zip(normals) {
                            assert!(
                                source.geometry().vertices.iter().enumerate().any(|(i, p)| {
                                    let point = [
                                        f32::from(p[0]) / 128.,
                                        f32::from(p[1]) / 128.,
                                        -f32::from(p[2]) / 128.,
                                    ];
                                    let source_n =
                                        source.source.vertex_normals.as_ref().unwrap()[i];
                                    point == v.position
                                        && source_n.map(f32::from) == [n[0], n[1], n[2]]
                                }),
                                "normal stream stays aligned after winding/batching"
                            );
                        }
                    } else {
                        assert!(normals.is_empty());
                    }
                }
                assert!(
                    part.layer_normals[BatchKind::Transparent.slot()]
                        .iter()
                        .all(|n| n[3] == 0.),
                    "authored BLOB faces have depth-writing base materials"
                );
                let pose = match part.kind {
                    Part::Body => poses.body,
                    Part::Turret => poses.turret,
                    Part::Gun => poses.gun,
                    Part::LeftTrack => poses.tracks[0],
                    Part::RightTrack => poses.tracks[1],
                    Part::Under => poses.attachments[0],
                    Part::End => poses.attachments[1],
                    Part::Bottom => pose::Pose {
                        position: poses.bottom_position,
                        matrix: poses.reference,
                    },
                    _ => unreachable!(),
                };
                let mut triangles = 0;
                for (slot, batch) in part.geometry.batches.iter().enumerate() {
                    if slot != BatchKind::Transparent.slot() {
                        assert!(batch.indices.len().is_multiple_of(3));
                    }
                    for vertex in &batch.vertices {
                        let local = Vec3::from_array(vertex.position);
                        let raw = source
                            .geometry()
                            .vertices
                            .iter()
                            .find(|p| {
                                let expected =
                                    Vec3::new(p[0] as f32, p[1] as f32, -f32::from(p[2])) / 128.;
                                (expected - local).length() < 1e-5
                            })
                            .expect(
                                "local model coordinates have exactly one world-unit conversion",
                            );
                        // Independent source row-vector calculation: model units
                        // are /128, published physics translation is /1024.
                        let world: [f32; 3] = std::array::from_fn(|i| {
                            let rotated = (0..3)
                                .map(|j| {
                                    f64::from(raw[j]) * f64::from(pose.matrix.0[j * 3 + i]) / 4096.
                                })
                                .sum::<f64>();
                            ((rotated + f64::from(pose.position[i]) / 8.) / 128.) as f32
                        });
                        let expected = Vec3::new(world[0], world[1], -world[2]);
                        assert!(
                            (part.transform.transform_point3(local) - expected).length() < 0.0001,
                            "car={id} part={:?}",
                            part.kind
                        );
                    }
                    triangles += batch.indices.len() / 3;
                }
                assert!(triangles > 0);
                total_triangles += triangles;
            }
            assert_eq!(poses, poses_before);
            assert_eq!(geometry(&model), snapshot);
        }
    }
    eprintln!("vehicle render: 15 cars / 4 articulated poses / {total_triangles} triangles");
}

#[test]
fn recoil_damage_and_hidden_faces_refresh_only_the_changed_part() {
    let files = Files::load();
    let mut model = files.model(1);
    let mut scene = files.scene(&model);
    let poses = pose::State::default();
    scene.refresh(&model, &poses).unwrap();
    let original = scene.revision;
    let gun = model.parts[Part::Gun.slot()].as_mut().unwrap();
    let mut articulation = body::Articulation {
        recoil: -160,
        ..Default::default()
    };
    let indices = model.gun_vertices;
    let original_z = model.gun_original_z;
    gun.edit(|geometry| {
        articulation.step(
            16,
            &body::Tuning::default(),
            Some(body::GunMesh {
                vertices: &mut geometry.vertices,
                indices,
                original_z,
            }),
        )
    })
    .unwrap()
    .unwrap();
    scene.refresh(&model, &poses).unwrap();
    assert_eq!(scene.revision, original + 1);
    assert!(
        scene
            .parts
            .iter()
            .filter(|p| p.kind != Part::Gun)
            .all(|p| p.revision == 1)
    );
    model.parts[Part::Body.slot()]
        .as_mut()
        .unwrap()
        .edit(|body| {
            body.vertices[0][0] += 16;
            for tri in &mut body.triangles {
                tri.flags |= 64;
            }
        })
        .unwrap();
    scene.refresh(&model, &poses).unwrap();
    assert_eq!(scene.revision, original + 2);
    let hull = scene.parts.iter().find(|p| p.kind == Part::Body).unwrap();
    assert!(hull.geometry.batches.iter().all(|b| b.indices.is_empty()));
    let snapshot = geometry(&model);
    scene.refresh(&model, &poses).unwrap();
    assert_eq!(
        scene.revision,
        original + 2,
        "stationary redraw must not rebuild"
    );
    assert_eq!(geometry(&model), snapshot);
}

#[test]
fn normal_layer_threshold_and_hidden_faces_refresh_aligned_banks() {
    let files = Files::load();
    let mut model = files.model(1);
    let mut scene = files.scene(&model);
    for y in [1500, 1501] {
        model.parts[Part::Body.slot()]
            .as_mut()
            .unwrap()
            .edit(|body| {
                for face in &mut body.triangles {
                    face.normal[1] = y;
                }
            })
            .unwrap();
        scene.refresh(&model, &pose::State::default()).unwrap();
        let body = scene.parts.iter().find(|p| p.kind == Part::Body).unwrap();
        let count = body.layer_normals.iter().map(Vec::len).sum::<usize>();
        assert!(count > 0);
        assert!(
            body.layer_normals
                .iter()
                .flatten()
                .all(|n| n[3] == f32::from(u8::from(y == 1501)))
        );
    }
    model.parts[Part::Body.slot()]
        .as_mut()
        .unwrap()
        .edit(|body| {
            for face in &mut body.triangles {
                face.flags |= 64;
            }
        })
        .unwrap();
    scene.refresh(&model, &pose::State::default()).unwrap();
    assert!(
        scene
            .parts
            .iter()
            .find(|p| p.kind == Part::Body)
            .unwrap()
            .layer_normals
            .iter()
            .all(Vec::is_empty)
    );
}

#[test]
fn cockpit_uses_only_bottom_and_pixel_uv_sight_with_recoil() {
    use tankracer_game::{
        dispatch::ClassId,
        object_query::EntityId,
        vehicle_startup::{Environment, Player},
    };
    let files = Files::load();
    let model = files.model(1);
    let mut scene = files.scene(&model);
    let mut player = Player::construct(
        EntityId(0),
        ClassId(0),
        model,
        pose::Pose::default(),
        Environment {
            ambient_wobble: false,
            session_active: false,
            ambient_phase: 0,
            gun_pivot: [0; 3],
        },
    )
    .unwrap();
    assert_eq!(scene.visible_parts(), 11);
    assert!(
        scene
            .parts
            .iter()
            .filter(|p| scene.visible(p))
            .all(|p| p.kind != Part::Bottom)
    );
    scene.cockpit = true;
    assert_eq!(scene.visible_parts(), 1);
    assert_eq!(
        scene.parts.iter().find(|p| scene.visible(p)).unwrap().kind,
        Part::Bottom
    );
    let matrix = Matrix::from_angles([713, 3901, 179]);
    player.physics.motion.poses.turret = pose::Pose {
        position: [12345, 5678, 23456],
        matrix,
    };
    for recoil in [0, -128, 100] {
        player.physics.motion.articulation.recoil = recoil;
        let before = player.physics.motion.clone();
        scene.refresh_player(&player).unwrap();
        let z = 5120 + 4 * recoil.min(0);
        let expected = std::array::from_fn(|i| {
            before.poses.turret.position[i] + ((matrix.0[6 + i] * z) >> 12)
        });
        assert_eq!(
            scene.sight_position,
            tankracer_scene::transform::physics_position(expected)
        );
        assert_eq!(player.physics.motion, before);
    }
    let camera = tankracer_scene::camera::Camera::looking_at(Vec3::ZERO, Vec3::NEG_Z);
    let sight = scene.sight_transform(&camera);
    assert!((sight.x_axis.truncate().length() * 2. - 300. / 1024.).abs() < 1e-6);
    assert_eq!(sight.w_axis.truncate(), scene.sight_position);
    let uv: Vec<_> = scene.sight.vertices.iter().map(|v| v.uv).collect();
    assert!(((uv[1][0] - uv[0][0]) * scene.materials.atlas.width as f32 - 63.).abs() < 1e-5);
    assert!(((uv[0][1] - uv[3][1]) * scene.materials.atlas.height as f32 - 63.).abs() < 1e-5);
    assert_eq!(scene.sight_kind, BatchKind::Transparent);
    assert!(
        scene
            .sight
            .vertices
            .iter()
            .all(|v| v.color == [1.; 4] && v.double_sided == 1.)
    );
}

#[test]
fn projected_shadow_and_shading_read_the_canonical_contacts_without_mutation() {
    use tankracer_game::{
        dispatch::ClassId,
        object_query::EntityId,
        vehicle_startup::{Environment, Player},
    };
    let files = Files::load();
    let model = files.model(1);
    let mut scene = files.scene(&model);
    let mut player = Player::construct(
        EntityId(0),
        ClassId(0),
        model,
        pose::Pose::default(),
        Environment {
            ambient_wobble: false,
            session_active: false,
            ambient_phase: 0,
            gun_pivot: [0; 3],
        },
    )
    .unwrap();
    scene.refresh_player(&player).unwrap();
    assert!(scene.aerial.is_some());
    player.physics.motion.lean.longitudinal.position = -731;
    player.physics.motion.lean.lateral.position = 917;
    let motion_before = player.physics.motion.clone();
    scene.refresh_player(&player).unwrap();
    assert_eq!(player.physics.motion, motion_before);
    let first = scene.aerial.as_ref().unwrap().poses[0];
    let mut basis = motion_before.poses.body.matrix;
    basis.rotate(Axis::X, (-731_i32) as u32);
    basis.rotate(Axis::Z, 917);
    assert_eq!(
        first.matrix, basis,
        "aerial bends from the hull matrix, not the turret matrix"
    );
    player.prefix.spin.speed = 100;
    player.prefix.spin.disabled = 1;
    scene.refresh_player(&player).unwrap();
    assert_eq!(scene.presentation.shield_timer, 100);
    assert!(scene.presentation.invincible);
    assert_eq!(scene.presentation.mode(Part::Body), 0);
    assert_eq!(scene.presentation.mode(Part::End), 1);
    scene.presentation.hide_aerial = true;
    assert!(!scene.aerial_visible());
    scene.presentation.hide_aerial = false;
    assert!(
        !scene.shadow.visible,
        "no uninitialized origin shadow before the first contact update"
    );
    let current = [5000, 6000, 7000];
    let previous = [4900, 5900, 6800];
    let contacts = [
        body::Contact {
            world: [4000, 5000, 6000],
            gap: 80,
        },
        body::Contact {
            world: [6000, 5100, 6000],
            gap: 160,
        },
        body::Contact {
            world: [4000, 5300, 8000],
            gap: 240,
        },
        body::Contact {
            world: [6000, 5400, 8000],
            gap: 320,
        },
    ];
    player
        .physics
        .motion
        .hull
        .refresh(current, previous, contacts);
    player.physics.motion.ready = true;
    for count in [0, 1, 2, 4, 255] {
        player.physics.contact.sampling.primary.special_count = count;
        let before = player.physics.motion.clone();
        scene.refresh_player(&player).unwrap();
        assert_eq!(scene.shaded, matches!(count, 2 | 4));
        assert_eq!(player.physics.motion, before);
    }
    assert!(scene.shadow.visible);
    assert_eq!(scene.shadow.geometry.indices.len(), 6);
    for (vertex, contact) in scene.shadow.geometry.vertices.iter().zip([
        contacts[1],
        contacts[3],
        contacts[2],
        contacts[0],
    ]) {
        let expected: [f32; 3] = std::array::from_fn(|i| {
            let local = contact.world[i] - previous[i] - if i == 1 { contact.gap } else { 0 };
            let local = local / 8; // Independent signed division truncates toward zero.
            let world = (current[i] + local * 8) as f32 / 1024.;
            if i == 2 { -world } else { world }
        });
        assert_eq!(
            scene
                .shadow
                .transform
                .transform_point3(Vec3::from_array(vertex.position))
                .to_array(),
            expected
        );
    }
    let positions: Vec<_> = scene
        .shadow
        .geometry
        .vertices
        .iter()
        .map(|v| Vec3::from_array(v.position))
        .collect();
    assert!(
        (positions[1] - positions[0])
            .cross(positions[2] - positions[0])
            .length()
            > 1.
    );
    let revision = scene.shadow.revision;
    scene.refresh_player(&player).unwrap();
    assert_eq!(
        scene.shadow.revision, revision,
        "redraw keeps projection resident"
    );
}

#[test]
#[ignore = "requires TANKRACER_ORIGINAL_EXE, Python pefile/unicorn; only tiny original UV routines run"]
fn original_track_uvs_match_gpu_coefficients_and_wrapping_travel() {
    use std::{
        io::Write,
        process::{Command, Stdio},
    };
    use tankracer_scene::vehicle::tracks::Tracks;
    let original = std::env::var_os("TANKRACER_ORIGINAL_EXE").expect("TANKRACER_ORIGINAL_EXE");
    let files = Files::load();
    let models: Vec<_> = (1..=15).map(|id| files.model(id)).collect();
    let mut cases = Vec::new();
    for car in 0..15 {
        for phase in if car == 0 {
            (0..40).collect::<Vec<_>>()
        } else {
            vec![0, 7, 39]
        } {
            cases.push((car, [phase << 17, (39 - phase) << 17], 0, 0, false));
        }
    }
    for (speed, yaw, locked) in [
        (400000, 0, false),
        (-400000, 0, false),
        (1234, -1931, false),
        (-1234, 1931, false),
        (300000, 100000, false),
        (100000, i32::MIN, false),
        (12345, 134217728, false),
        (400000, 1000, true),
    ] {
        cases.push((0, [u32::MAX - 10, 10], speed, yaw, locked));
    }
    let mut input = vec![cases.len() as u32];
    for &(car, travel, speed, yaw, locked) in &cases {
        input.extend(travel);
        input.extend([speed as u32, yaw as u32, u32::from(locked)]);
        for (side, kind) in [Part::LeftTrack, Part::RightTrack].into_iter().enumerate() {
            let geometry = models[car].parts[kind.slot()].as_ref().unwrap().geometry();
            input.push(geometry.triangles.len() as u32);
            input.extend(
                geometry
                    .triangles
                    .iter()
                    .flat_map(|t| t.uv.into_iter().flatten().map(u32::from)),
            );
            for group in &models[car].tracks[side].groups {
                input.push(group.len() as u32);
                for r in group {
                    input.extend([
                        u32::from(r.triangle),
                        u32::from(r.corner),
                        u32::from(r.snapped_v),
                    ]);
                }
            }
        }
    }
    let script =
        std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/reference_tracks.py");
    let mut child = Command::new("python")
        .arg(script)
        .arg(original)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .spawn()
        .unwrap();
    child
        .stdin
        .take()
        .unwrap()
        .write_all(
            &input
                .into_iter()
                .flat_map(u32::to_le_bytes)
                .collect::<Vec<_>>(),
        )
        .unwrap();
    let output = child.wait_with_output().unwrap();
    assert!(output.status.success());
    let words: Vec<_> = output
        .stdout
        .chunks_exact(4)
        .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
        .collect();
    let mut cursor = words.iter().copied();
    let mut corners = 0;
    for (car, travel, speed, yaw, locked) in cases {
        let model = &models[car];
        let mut scene = files.scene(model);
        let mut tracks = Tracks { travel };
        tracks.advance(speed, yaw, locked);
        assert_eq!(
            tracks.travel,
            [cursor.next().unwrap(), cursor.next().unwrap()]
        );
        scene.tracks = tracks;
        scene.refresh(model, &pose::State::default()).unwrap();
        for kind in [Part::LeftTrack, Part::RightTrack] {
            let mesh = model.parts[kind.slot()].as_ref().unwrap();
            let mut expected = mesh.geometry().clone();
            for triangle in &mut expected.triangles {
                for uv in &mut triangle.uv {
                    *uv = [cursor.next().unwrap() as u8, cursor.next().unwrap() as u8];
                }
            }
            let expected = scene
                .materials
                .prepare(&expected, &mesh.source.materials)
                .unwrap();
            let part = scene.parts.iter().find(|p| p.kind == kind).unwrap();
            for ((batch, scroll), expected) in part
                .geometry
                .batches
                .iter()
                .zip(&part.geometry.uv_scroll)
                .zip(&expected.batches)
            {
                assert_eq!(batch.vertices.len(), scroll.len());
                for ((vertex, scroll), expected) in
                    batch.vertices.iter().zip(scroll).zip(&expected.vertices)
                {
                    let v = if scroll[3] > 0. {
                        scroll[2]
                            + (vertex.uv[1] - scroll[2]
                                + scroll[0] * part.uv_phase[0]
                                + scroll[1] * part.uv_phase[1])
                                .rem_euclid(scroll[3])
                    } else {
                        vertex.uv[1]
                    };
                    assert_eq!(vertex.uv[0], expected.uv[0]);
                    assert!(
                        (v - expected.uv[1]).abs() < 1e-6,
                        "car={} part={kind:?} phase={:?}",
                        car + 1,
                        part.uv_phase
                    );
                    corners += 1;
                }
            }
        }
    }
    assert!(cursor.next().is_none());
    eprintln!(
        "original tracks: 90 cases / {corners} GPU corners; all 15 cars, 40 phase pairs, clamp/reverse/turn/lock/wrap"
    );
}
