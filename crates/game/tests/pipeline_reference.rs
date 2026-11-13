//! Composition tests use real C world initialization, not post-state classifications.
use std::{
    fs,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_assets::{archive::Archive, terrain::TerrainData};
use tankracer_game::{
    contact::{self, Coefficient},
    contact_pipeline::{self as pipeline, Pipeline},
    deformation, motion, pose, powertrain,
    sampling::Transform,
    suspension, terrain,
};

fn coefficient(multiplier: u16, shift: u8) -> Coefficient {
    Coefficient { multiplier, shift }
}
fn packed(gain: Coefficient) -> u32 {
    u32::from(gain.multiplier) | (u32::from(gain.shift) << 16)
}
fn signed(out: &mut Vec<u32>, values: impl IntoIterator<Item = i32>) {
    out.extend(values.into_iter().map(|v| v as u32));
}

fn sampling(s: &pipeline::Sampling, seed: u32, data: &TerrainData) -> Vec<u32> {
    let mut out = Vec::new();
    for p in &s.primary.points {
        signed(&mut out, p.local);
        signed(&mut out, p.world);
        signed(
            &mut out,
            [p.kind, p.gap, p.previous_gap, p.raw_height, p.travel],
        );
        out.push(p.flags);
        signed(&mut out, p.normal);
    }
    signed(
        &mut out,
        [
            s.primary.load,
            s.primary.airborne_count,
            s.primary.special_count,
            s.primary.flagged_count,
            i32::from(s.primary.has_extra),
        ],
    );
    signed(&mut out, s.primary.extra);
    signed(&mut out, s.primary.surfaces);
    out.push(s.primary.flagged_ground_count as u32);
    out.extend(
        s.primary
            .nodes
            .map(|node| node.map_or(0, |id| 0x5dab54 + data.node(id).source_offset)),
    );
    signed(
        &mut out,
        [
            s.effect.timer,
            s.effect.restore,
            s.effect.active,
            s.effect.phase,
            s.effect.impulse[0],
            s.effect.impulse[1],
            s.effect.pose,
        ],
    );
    out.extend([seed, u32::from(s.effect.clear_player_slot)]);
    for a in &s.auxiliary {
        signed(&mut out, a.world);
        signed(&mut out, [a.kind, a.cached, a.surface]);
    }
    signed(&mut out, s.materials);
    out.extend([s.fallback_mode as u32, u32::from(s.feedback_required)]);
    assert_eq!(out.len(), 103);
    out
}
fn response(s: &suspension::State) -> Vec<u32> {
    let mut out = Vec::new();
    for p in s.points {
        signed(
            &mut out,
            [p.kind, p.gap, p.corner as i32, p.travel, p.rate, p.output],
        );
    }
    signed(&mut out, s.angular);
    signed(&mut out, s.tilt);
    signed(
        &mut out,
        [
            s.penetrating_input_sum,
            s.travel_sum,
            s.vertical_delta,
            s.world_vy,
            s.position_y,
            s.timer,
        ],
    );
    signed(&mut out, s.auxiliary_type);
    signed(&mut out, s.auxiliary_cached);
    signed(&mut out, [s.max_penetration, s.last_penetration]);
    assert_eq!(out.len(), 40);
    out
}
fn vehicle_state(
    s: &Pipeline,
    m: &motion::State,
    p: &powertrain::State,
    pose: &pose::Config,
    seed: u32,
    feedback: u32,
) -> Vec<u32> {
    let mut out = Vec::new();
    for i in 0..4 {
        let a = &s.sampling.primary.points[i];
        let b = s.response.points[i];
        signed(&mut out, a.local);
        signed(&mut out, m.contacts[i].world);
        signed(
            &mut out,
            [
                a.kind,
                m.contacts[i].gap,
                a.previous_gap,
                a.raw_height,
                b.corner as i32,
                m.travel[i],
                b.rate,
                b.output,
            ],
        );
        signed(&mut out, a.normal);
        out.push(a.flags);
    }
    let a = &s.sampling.primary;
    signed(
        &mut out,
        [
            p.load,
            a.airborne_count,
            a.special_count,
            a.flagged_count,
            i32::from(a.has_extra),
        ],
    );
    signed(&mut out, a.extra);
    signed(
        &mut out,
        [
            m.angular[0],
            m.angular[2],
            s.response.tilt[0],
            s.response.tilt[1],
            m.rates.penetrating_force,
            m.rates.travel_sum,
            s.response.vertical_delta,
            m.velocity[1],
            m.poses.physical_position[1],
            s.response.timer,
        ],
    );
    let e = &s.sampling.effect;
    signed(
        &mut out,
        [
            e.timer,
            e.restore,
            e.active,
            e.phase,
            m.articulation.recoil_rate,
            m.articulation.pitch_rate,
            pose.turret_offset[2],
        ],
    );
    out.push(seed);
    for a in &s.sampling.auxiliary {
        signed(&mut out, a.world);
        signed(&mut out, [a.kind, a.cached]);
    }
    signed(&mut out, s.sampling.materials);
    signed(&mut out, m.pivot);
    out.push(feedback);
    assert_eq!(out.len(), 115);
    out
}
fn environment(
    e: &pipeline::Environment,
    noise: [u16; 16],
    s: &suspension::Environment,
) -> Vec<u32> {
    let mut out = Vec::new();
    for t in e.tracks {
        signed(&mut out, t.basis);
        signed(&mut out, t.origin);
    }
    out.extend(noise.map(u32::from));
    out.extend([
        u32::from(e.force_surface),
        u32::from(e.effect_gain.multiplier),
        u32::from(e.effect_gain.shift),
    ]);
    out.extend([
        e.load.base as u32,
        e.load.floor as u32,
        u32::from(e.load.gain.multiplier),
        u32::from(e.load.gain.shift),
    ]);
    for local in e.auxiliary_local {
        signed(&mut out, local);
    }
    out.extend([e.dt as u32, e.frame, e.player]);
    signed(
        &mut out,
        [
            s.dt,
            s.environment_flag,
            s.session_active,
            s.mode,
            i32::from(s.is_player),
        ],
    );
    signed(&mut out, s.base);
    signed(&mut out, s.region_min);
    signed(&mut out, s.region_max);
    out.extend([
        u32::from(s.player_multiplier),
        s.player_shift,
        u32::from(s.other_multiplier),
        s.other_shift,
        s.penetration_shift,
    ]);
    signed(&mut out, s.auxiliary_query_type);
    assert_eq!(out.len(), 74);
    out
}
fn config(c: &contact::Config) -> [u32; 11] {
    [
        c.lower as u32,
        c.upper as u32,
        packed(c.force_positive),
        packed(c.force_negative),
        packed(c.output_gain),
        packed(c.tilt_gain),
        c.shape_shift,
        packed(c.restore_positive),
        packed(c.restore_negative),
        u32::from(c.damping_shift) << 24,
        c.travel_dead_step as u32,
    ]
}
fn deform_input(geometry: &deformation::Geometry<'_>, state: &deformation::State) -> Vec<u32> {
    let mut out = Vec::new();
    signed(&mut out, state.pose.origin);
    signed(&mut out, state.pose.inverse_basis);
    signed(&mut out, geometry.sample_bounds.into_iter().flatten());
    signed(&mut out, geometry.edges);
    out.extend([
        geometry.edge_cap as u32,
        u32::from(state.dirty),
        geometry.vertices.len() as u32,
    ]);
    signed(
        &mut out,
        geometry.vertices.iter().flatten().map(|&v| i32::from(v)),
    );
    out.push(state.records.len() as u32);
    for r in &state.records {
        signed(&mut out, r.position);
        out.push(u32::from_le_bytes([
            r.state, r.color[0], r.color[1], r.color[2],
        ]));
    }
    out
}

#[test]
#[ignore = "requires the composed native oracle; set TANKRACER_PIPELINE_REFERENCE"]
fn all_maps_composed_contact_flow_and_short_sequences_match_c() {
    compare(false);
}
#[test]
#[ignore = "requires extended C vehicle-contact oracle; set TANKRACER_PIPELINE_REFERENCE"]
fn vehicle_adapter_shared_fields_transaction_and_world_effects_match_c() {
    compare(true);
}
fn compare(vehicle: bool) {
    let assets = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join(if vehicle {
        "../../build/tests/vehicle-contact"
    } else {
        "../../build/tests/pipeline"
    });
    fs::create_dir_all(&folder).unwrap();
    let branch_contact = contact::Config {
        lower: -12000,
        upper: 12000,
        force_positive: coefficient(7, 16),
        force_negative: coefficient(5, 16),
        output_gain: coefficient(8192, 8),
        tilt_gain: coefficient(4096, 4),
        shape_shift: 5,
        restore_positive: coefficient(1, 12),
        restore_negative: coefficient(1, 13),
        damping_shift: 8,
        travel_dead_step: 2,
    };
    let branch_gains: Vec<_> = (0..10).map(|i| coefficient(i * 3, 4)).collect();
    let config_bytes = fs::read(assets.join("DATA/FASTFILE.DAT")).unwrap();
    let config_archive = Archive::parse(&config_bytes).unwrap();
    let surfaces =
        tankracer_assets::config::Surfaces::decode(config_archive.get("SURFS.DAT").unwrap())
            .unwrap();
    let tanks: Vec<_> = (1..=15)
        .map(|i| {
            tankracer_assets::config::Tank::decode(
                config_archive.get(&format!("TANK{i}.DAT")).unwrap(),
            )
            .unwrap()
        })
        .collect();
    let noise = pipeline::initialize_noise(&mut 0x12345678);
    let layout = [101, 202, 303, 404, 505];
    let mut total = 0;
    let mut errors = 0;
    let mut kinds = [0; 5];
    let mut clears = 0;
    let mut deformations = 0;
    let mut lazy_queries = 0;
    let mut mixed_materials = 0;
    let mut service_errors = 0;
    for map in [
        "ENGLAND", "MEXICO", "SWAMP", "EAST", "THEME", "BIO", "MONACO", "CIRCUIT",
    ] {
        let bytes = fs::read(assets.join("LEVELS").join(map).join("FASTFILE.DAT")).unwrap();
        let archive = Archive::parse(&bytes).unwrap();
        let file = archive
            .entries()
            .iter()
            .find(|e| e.name.ends_with(".BSM"))
            .unwrap();
        let mut data = TerrainData::decode_bsm(archive.get(file.name).unwrap()).unwrap();
        let centers: Vec<[i32; 3]> = data
            .nodes
            .iter()
            .map(|node| {
                let mesh = &data.meshes[node.mesh as usize];
                let tri = &mesh.triangles[0];
                std::array::from_fn(|i| {
                    mesh.position[i].wrapping_mul(8).wrapping_add(
                        tri.indices
                            .iter()
                            .map(|&v| i32::from(mesh.vertices[v as usize][i]))
                            .sum::<i32>()
                            * 8
                            / 3,
                    )
                })
            })
            .collect();
        let fallback_centers: Vec<[i32; 3]> = data
            .fallback
            .nodes
            .iter()
            .filter_map(|node| {
                let mesh = &data.meshes[node.mesh as usize];
                mesh.face_centers.first().map(|center| {
                    std::array::from_fn(|i| {
                        mesh.position[i]
                            .wrapping_add(i32::from(center[i]))
                            .wrapping_mul(8)
                    })
                })
            })
            .collect();
        let mut input = Vec::new();
        let mut expected = Vec::new();
        let mut ranges = Vec::new();
        for case in 0..192 {
            let resource = &tanks[case % 15].suspension;
            let authored = case >= 96;
            let contact = if authored {
                contact::Config::try_from(resource).unwrap()
            } else {
                branch_contact
            };
            let gains: Vec<Coefficient> = if authored {
                surfaces.entries.map(|s| s.bump.into()).to_vec()
            } else {
                branch_gains.clone()
            };
            let mut origins = [
                centers[case * 37 % centers.len()],
                centers[(case * 37 + if case % 3 == 0 { 13 } else { 0 }) % centers.len()],
            ];
            if case % 7 == 0 && !fallback_centers.is_empty() {
                origins = [fallback_centers[case % fallback_centers.len()]; 2];
            }
            if case % 13 == 0 {
                origins[0][0] = i32::MIN;
                origins[1][0] = i32::MAX;
            }
            for origin in &mut origins {
                let mut height = 0;
                if terrain::height(&data, origin[0], origin[2], &mut height) == Ok(true) {
                    origin[1] = height;
                }
            }
            let gap = [-800, -76, -75, 0, 74, 75, 200][case % 7];
            let mut state = Pipeline::default();
            for (i, p) in state.sampling.primary.points.iter_mut().enumerate() {
                p.local = [if i % 2 == 0 { 0 } else { 101 }, gap + i as i32 * 3, 0];
                p.world = [999; 3];
                p.previous_gap = -137;
                p.raw_height = 991;
                p.flags = 0x4321;
                p.travel = case as i32 * 7 - 500;
                p.normal = [31, 32, 33];
            }
            state.sampling.primary.extra = [991, -992, 993, -994];
            state.sampling.effect = pipeline::SurfaceEffect {
                timer: [-1, 0, 1, 17, 34, 32767][case % 6],
                restore: -123,
                active: (case % 2) as i32,
                phase: i32::MAX - 100,
                impulse: [55, -77],
                pose: 12345,
                clear_player_slot: false,
            };
            state.response.world_vy = -5;
            state.response.position_y = origins[0][1];
            state.response.timer = 1;
            for (i, p) in state.response.points.iter_mut().enumerate() {
                p.corner = i as u32;
                p.rate = case as i32 * 13 - 200;
            }
            let mut motion = motion::State {
                angular: [state.response.angular[0], 17, state.response.angular[1]],
                velocity: [31, state.response.world_vy, -19],
                contacts: state.sampling.primary.points.each_ref().map(|p| {
                    tankracer_game::body::Contact {
                        world: p.world,
                        gap: p.gap,
                    }
                }),
                travel: state.sampling.primary.points.each_ref().map(|p| p.travel),
                pivot: state.pivot,
                rates: motion::Rates {
                    travel_sum: state.response.travel_sum,
                    penetrating_force: state.response.penetrating_input_sum,
                    ..motion::Rates::default()
                },
                ..motion::State::default()
            };
            motion.poses.physical_position[1] = state.response.position_y;
            motion.articulation.recoil_rate = state.sampling.effect.impulse[0];
            motion.articulation.pitch_rate = state.sampling.effect.impulse[1];
            let mut power = powertrain::State {
                load: state.sampling.primary.load,
                ..powertrain::State::default()
            };
            let mut pose = pose::Config {
                track_offsets: [[0; 3]; 2],
                turret_offset: [0, 0, state.sampling.effect.pose],
                gun_offset: [0; 3],
                attachment_offsets: [[0; 3]; 2],
                bottom_offset: [0; 3],
                track_length: 1,
                travel_gain: tankracer_assets::config::Gain::default(),
                gun_pivot: [0; 3],
            };
            let geometry = deformation::Geometry {
                sample_bounds: [[-1000, 1000]; 2],
                edges: [-16000, 16000, -16000, 16000],
                edge_cap: 65536,
                vertices: &[[-128, 20, 0], [0, 40, 0], [128, 80, 0]],
            };
            let mut deform = deformation::State {
                records_attached: true,
                pose: deformation::Pose {
                    origin: origins[0],
                    ..deformation::Pose::default()
                },
                dirty: 0,
                records: vec![
                    deformation::Record {
                        world_position: [0; 3],
                        timer: 0,
                        position: [0; 3],
                        state: 0,
                        color: [11; 3],
                    },
                    deformation::Record {
                        world_position: [0; 3],
                        timer: 0,
                        position: [0; 3],
                        state: 1,
                        color: [22; 3],
                    },
                    deformation::Record {
                        world_position: [0; 3],
                        timer: 0,
                        position: [i32::MAX; 3],
                        state: 1,
                        color: [33; 3],
                    },
                ],
            };
            let has_deform = case % 3 == 0;
            let mut seed = 0x12345678;
            for tick in 0..4 {
                let mut env = pipeline::Environment {
                    tracks: origins.map(|origin| Transform {
                        origin,
                        basis: if case % 2 == 0 {
                            [4096, 0, 0, 0, 4096, 0, 0, 0, 4096]
                        } else {
                            [2896, 0, -2896, 0, 4096, 0, 2896, 0, 2896]
                        },
                    }),
                    auxiliary_local: [[0, 0, 0], [33, 75, -33]],
                    force_surface: case % 5 == 0,
                    effect_gain: if authored {
                        coefficient(3, 9)
                    } else {
                        coefficient(17, 2)
                    },
                    load: if authored {
                        resource.into()
                    } else {
                        pipeline::LoadConfig {
                            base: 1024,
                            floor: 31,
                            gain: coefficient(7, 3),
                        }
                    },
                    dt: [0, 1, 16, 17, 33][case % 5],
                    frame: tick as u32,
                    player: if case % 2 == 0 { 1 } else { 0 },
                };
                if tick == 2 {
                    env.tracks[0].origin[1] = env.tracks[0].origin[1].wrapping_add(151);
                }
                if vehicle {
                    env.force_surface &= env.player != 0;
                }
                let se = suspension::Environment {
                    dt: env.dt,
                    environment_flag: (case % 2) as i32,
                    session_active: (case % 3) as i32,
                    mode: if case % 4 == 0 { 5 } else { 1 },
                    is_player: case % 2 == 0,
                    base: [0; 2],
                    region_min: [-10; 2],
                    region_max: [10; 2],
                    player_multiplier: 3,
                    player_shift: 2,
                    other_multiplier: 5,
                    other_shift: 3,
                    penetration_shift: if authored {
                        u32::from(resource.susp_to_shape.shift as u8)
                    } else {
                        3
                    },
                    auxiliary_query_type: [91, 92],
                };
                let patch = if case % 11 == 0 && tick == 3 {
                    let point = &state.sampling.primary.points[(case / 11) % 4];
                    let world = env.tracks[((case / 11) % 4) / 2].point(point.local);
                    terrain::select(&data, world[0], world[2])
                        .unwrap()
                        .map(|s| s.triangle.source_offset)
                } else {
                    None
                };
                let patch_location = patch.and_then(|at| {
                    data.meshes.iter().enumerate().find_map(|(m, mesh)| {
                        mesh.triangles
                            .iter()
                            .position(|t| t.source_offset == at)
                            .map(|t| (m, t))
                    })
                });
                let previous = patch_location.map(|(m, t)| {
                    let old = data.meshes[m].triangles[t].normal[1];
                    data.meshes[m].triangles[t].normal[1] = 0;
                    old
                });
                let sample_only = case % 4 == 0 && tick % 2 == 0;
                let sample_only = sample_only && !vehicle;
                motion.poses.tracks = env.tracks.map(|t| pose::Pose {
                    position: t.origin,
                    matrix: tankracer_game::math::Matrix(t.basis),
                });
                motion.poses.base_position[0] = se.base[0];
                motion.poses.base_position[2] = se.base[1];
                let fail_clear = vehicle && case % 9 == 0;
                let mut request = vec![
                    if vehicle {
                        3
                    } else if sample_only {
                        2
                    } else {
                        1
                    },
                    patch.map_or(0, |at| 0x5dab54 + at + 2),
                ];
                request.extend(sampling(&state.sampling, seed, &data));
                request.extend(response(&state.response));
                signed(&mut request, state.pivot);
                request.extend(environment(&env, noise, &se));
                request.extend(gains.iter().copied().map(packed));
                request.extend(config(&contact));
                signed(&mut request, layout);
                request.push(u32::from(has_deform));
                if has_deform {
                    request.extend(deform_input(&geometry, &deform));
                }
                if vehicle {
                    request.push(u32::from(fail_clear));
                }
                input.extend(request.into_iter().flat_map(u32::to_le_bytes));
                let world = pipeline::World {
                    terrain: &data,
                    surface_bumps: &gains,
                    noise,
                };
                let deformable = has_deform.then_some(pipeline::Deformable {
                    geometry,
                    state: &mut deform,
                });
                let mut binding = deformable;
                let before_aux = state.sampling.auxiliary.clone();
                let mut slot_value = 0x12345678;
                let mut feedback = 0x7182;
                let result = if vehicle {
                    state
                        .step_vehicle(
                            pipeline::Vehicle {
                                motion: &mut motion,
                                powertrain: &mut power,
                                pose: &mut pose,
                            },
                            &world,
                            &pipeline::Config {
                                sampling: &env,
                                suspension: &se,
                                contact: &contact,
                                layout,
                            },
                            &mut seed,
                            binding.as_mut(),
                            |slot| {
                                assert_eq!(slot, env.player as u8);
                                if fail_clear {
                                    return false;
                                }
                                slot_value = 0;
                                true
                            },
                        )
                        .map(|value| {
                            feedback = u32::from(value);
                        })
                } else if sample_only {
                    state
                        .sampling
                        .sample(&world, &env, &mut seed, binding.as_mut())
                } else {
                    state.step(
                        &world,
                        &pipeline::Config {
                            sampling: &env,
                            suspension: &se,
                            contact: &contact,
                            layout,
                        },
                        &mut seed,
                        binding.as_mut(),
                    )
                };
                let status = match result {
                    Ok(()) => 0,
                    Err(pipeline::Error::Terrain(terrain::Error::Arithmetic)) => -2,
                    Err(pipeline::Error::EffectSlotWrite(_)) => {
                        service_errors += 1;
                        -1
                    }
                    Err(e) => panic!("unexpected error {e:?}"),
                };
                errors += usize::from(status < 0);
                clears += usize::from(if vehicle {
                    slot_value == 0
                } else {
                    state.sampling.clear_player_slot.is_some()
                });
                for p in &state.sampling.primary.points {
                    if (0..5).contains(&p.kind) {
                        kinds[p.kind as usize] += 1;
                    }
                }
                mixed_materials += usize::from(
                    state.sampling.primary.surfaces[0] != state.sampling.primary.surfaces[2],
                );
                lazy_queries += usize::from(
                    before_aux
                        .iter()
                        .zip(&state.sampling.auxiliary)
                        .any(|(a, b)| a.world != b.world),
                );
                deformations += usize::from(deform.dirty != 0);
                let start = expected.len();
                expected.push(status as u32);
                if vehicle {
                    expected.extend(vehicle_state(
                        &state, &motion, &power, &pose, seed, feedback,
                    ));
                } else {
                    expected.extend(sampling(&state.sampling, seed, &data));
                    expected.extend(response(&state.response));
                    signed(&mut expected, state.pivot);
                }
                expected.push(if vehicle {
                    slot_value
                } else if state.sampling.clear_player_slot.is_some() {
                    0
                } else {
                    0x12345678
                });
                if has_deform {
                    expected.push(u32::from(deform.dirty));
                    expected.extend(deform.records.iter().map(|r| {
                        u32::from_le_bytes([r.state, r.color[0], r.color[1], r.color[2]])
                    }));
                }
                ranges.push((start..expected.len(), case, tick));
                if let (Some((m, t)), Some(old)) = (patch_location, previous) {
                    data.meshes[m].triangles[t].normal[1] = old;
                }
                // The full vehicle caller clears auxiliary caches between ticks.
                if tick == 1 {
                    for a in &mut state.sampling.auxiliary {
                        a.cached = 0;
                    }
                }
            }
        }
        let file = folder.join(format!("{map}.input.bin"));
        fs::write(&file, input).unwrap();
        let output = Command::new(
            std::env::var_os("TANKRACER_PIPELINE_REFERENCE")
                .expect("set TANKRACER_PIPELINE_REFERENCE"),
        )
        .arg(&assets)
        .arg(map)
        .stdin(Stdio::from(fs::File::open(file).unwrap()))
        .output()
        .unwrap();
        assert!(
            output.status.success(),
            "{map}: {}",
            String::from_utf8_lossy(&output.stderr)
        );
        assert_eq!(&output.stdout[..4], b"CP01");
        assert_eq!(
            u32::from_le_bytes(output.stdout[4..8].try_into().unwrap()),
            0x5dab54
        );
        let actual: Vec<_> = output.stdout[8..]
            .chunks_exact(4)
            .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
            .collect();
        assert_eq!(actual.len(), expected.len());
        for (range, case, tick) in &ranges {
            assert_eq!(
                actual[range.clone()],
                expected[range.clone()],
                "{map} case={case} tick={tick}"
            );
        }
        total += ranges.len();
        eprintln!(
            "{map}: {} composed contact records / {} words exact",
            ranges.len(),
            actual.len()
        );
    }
    assert!(
        errors > 0 && clears > 0 && lazy_queries > 0 && deformations > 0 && mixed_materials > 0
    );
    if vehicle {
        assert!(service_errors > 0);
    }
    assert!(
        kinds.iter().all(|&n| n > 0),
        "must exercise kinds 0..4: {kinds:?}"
    );
    eprintln!(
        "{total} records; errors={errors} (effect-slot writes={service_errors}), effect clears={clears}, auxiliary updates={lazy_queries}, deform updates={deformations}, mixed materials={mixed_materials}, kinds={kinds:?}"
    );
}
