use std::{
    fs,
    ops::Range,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_assets::{
    archive::Archive,
    config::{Part, ResolveContext, Surfaces, Tank},
    mesh::{Geometry, Triangle},
    model::ShapeArchive,
};
use tankracer_game::{
    damage, ground_collision,
    math::Matrix,
    pose::Pose,
    vehicle_model::{BuildContext, Bundles, ModelBank, VehicleModel},
};

struct Random(u32);
impl Random {
    fn word(&mut self) -> u32 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 17;
        self.0 ^= self.0 << 5;
        self.0
    }
    fn signed(&mut self) -> i32 {
        self.word() as i32
    }
    fn choose(&mut self, values: &[i32]) -> i32 {
        values[self.word() as usize % values.len()]
    }
    fn pose(&mut self, boundary: bool) -> Pose {
        Pose {
            position: std::array::from_fn(|_| {
                if boundary {
                    self.signed()
                } else {
                    self.signed() % 100000
                }
            }),
            matrix: if boundary {
                Matrix(std::array::from_fn(|_| self.signed()))
            } else {
                Matrix::from_angles(std::array::from_fn(|_| self.word() & 65535))
            },
        }
    }
}
fn append(out: &mut Vec<u32>, words: impl IntoIterator<Item = i32>) {
    out.extend(words.into_iter().map(|v| v as u32));
}
fn face_words(t: &Triangle) -> [u32; 6] {
    let mut b = [0; 24];
    for (i, n) in t.normal.iter().enumerate() {
        b[i * 2..i * 2 + 2].copy_from_slice(&n.to_le_bytes());
    }
    b[6..8].copy_from_slice(&t.color.to_le_bytes());
    b[8..11].copy_from_slice(&t.indices);
    b[11] = t.sides;
    for (i, uv) in t.uv.iter().enumerate() {
        b[12 + i * 2..14 + i * 2].copy_from_slice(uv);
    }
    b[18..20].copy_from_slice(&t.flags.to_le_bytes());
    b[20..24].copy_from_slice(&t.material_source_offset.to_le_bytes());
    std::array::from_fn(|i| u32::from_le_bytes(b[i * 4..i * 4 + 4].try_into().unwrap()))
}
fn mesh_words(out: &mut Vec<u32>, mesh: &Geometry, header: bool) {
    if header {
        out.extend([mesh.vertices.len() as u32, mesh.triangles.len() as u32]);
    }
    for v in &mesh.vertices {
        append(out, v.map(i32::from));
    }
    for t in &mesh.triangles {
        out.extend(face_words(t));
    }
}
fn state_words(out: &mut Vec<u32>, state: &damage::State) {
    out.extend(
        state
            .body
            .into_iter()
            .chain(state.turret)
            .chain(state.tracks)
            .map(u32::from),
    );
}
struct Oracle {
    input: Vec<u8>,
    expected: Vec<u32>,
    records: Vec<(Range<usize>, String)>,
    counts: [usize; 5],
}
impl Oracle {
    fn record(&mut self, op: u32, input: Vec<u32>, output: Vec<u32>, label: String) {
        self.input.extend(op.to_le_bytes());
        self.input
            .extend(input.into_iter().flat_map(u32::to_le_bytes));
        let start = self.expected.len();
        self.expected.extend(output);
        self.records.push((start..self.expected.len(), label));
        self.counts[op as usize] += 1;
    }
    fn compare(self) {
        let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/damage");
        fs::create_dir_all(&folder).unwrap();
        let path = folder.join("input.bin");
        fs::write(&path, self.input).unwrap();
        let result = Command::new(
            std::env::var_os("TANKRACER_DAMAGE_REFERENCE").expect("set TANKRACER_DAMAGE_REFERENCE"),
        )
        .stdin(Stdio::from(fs::File::open(path).unwrap()))
        .output()
        .unwrap();
        assert!(
            result.status.success(),
            "{}",
            String::from_utf8_lossy(&result.stderr)
        );
        assert_eq!(&result.stdout[..4], b"DM01");
        let words: Vec<_> = result.stdout[4..]
            .chunks_exact(4)
            .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
            .collect();
        assert_eq!(words.len(), self.expected.len());
        for (range, label) in &self.records {
            assert_eq!(
                words[range.clone()],
                self.expected[range.clone()],
                "{label}"
            );
        }
        eprintln!(
            "{} damage/normal records, {} words exact; operations {:?}",
            self.records.len(),
            words.len(),
            self.counts
        );
    }
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

// Deterministic service probe. The geometry mutation is the actual Rust damage
// module; the impact probe checks synchronous ordering/failure, not particles.
struct Effects<'a> {
    damage: &'a mut damage::State,
    body: &'a mut Geometry,
    pose: Pose,
    selected: [u8; 4],
    player: bool,
    seed: u32,
    counter: u32,
    calls: u32,
    trace: u32,
    fail_at: u32,
    fail_status: i32,
}
impl ground_collision::Effects for Effects<'_> {
    fn impact(&mut self, index: usize, kind: i32) -> Result<(), i32> {
        self.seed = self.seed.wrapping_mul(0x19660d).wrapping_add(0x3c6ef35f);
        self.counter = self.counter.wrapping_add(1);
        self.calls += 1;
        self.trace = self
            .trace
            .wrapping_mul(16)
            .wrapping_add(index as u32 * 2 + u32::from(kind == 4) + 1);
        if self.calls == self.fail_at && self.fail_status != 0 {
            Err(self.fail_status)
        } else {
            Ok(())
        }
    }
    fn track_damage(&mut self, kind: i32, world: [i32; 3]) -> Result<(), i32> {
        if self.damage.hit_track(kind) && self.player {
            self.damage
                .hit(
                    damage::Part::Body,
                    self.pose,
                    world,
                    self.selected,
                    self.body,
                )
                .map_err(|_| -1)?;
        }
        Ok(())
    }
}

#[test]
#[ignore = "requires C damage oracle; set TANKRACER_DAMAGE_REFERENCE"]
fn real_models_damage_short_sequences_normal_repair_and_integer_boundaries_match_c() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    let packs: Vec<_> = ["BRONZE", "SILVER", "GOLD"]
        .map(|tier| fs::read(root.join("TANKS").join(tier).join("FASTFILE.DAT")).unwrap())
        .into_iter()
        .collect();
    let shared = fs::read(root.join("INGAME/FASTFILE.DAT")).unwrap();
    let source = Bundles {
        vehicle: packs.iter().map(|b| shapes(b)).collect(),
        shared: vec![shapes(&shared)],
        level: Vec::new(),
    };
    let data = fs::read(root.join("DATA/FASTFILE.DAT")).unwrap();
    let archive = Archive::parse(&data).unwrap();
    let surfaces = Surfaces::decode(archive.get("SURFS.DAT").unwrap()).unwrap();
    let mut bank = ModelBank::default();
    let mut random = Random(0x19620e81);
    let mut oracle = Oracle {
        input: Vec::new(),
        expected: Vec::new(),
        records: Vec::new(),
        counts: [0; 5],
    };
    let boundaries = [
        i32::MIN,
        i32::MAX,
        -65536,
        -32769,
        -4096,
        -1,
        0,
        1,
        4096,
        32767,
        65536,
    ];
    let (mut ground_errors, mut impacts, mut dents) = (0, 0, 0);
    for vehicle in 1..=15 {
        let tank = Tank::decode(archive.get(&format!("TANK{vehicle}.DAT")).unwrap()).unwrap();
        let model = VehicleModel::build(
            &tank,
            &source,
            &mut bank,
            &surfaces,
            BuildContext {
                resolve: ResolveContext {
                    player: true,
                    ..ResolveContext::default()
                },
                ambient_wobble: false,
                session_active: true,
            },
        )
        .unwrap();
        let originals = [
            model.parts[Part::Body.slot()]
                .as_ref()
                .unwrap()
                .geometry()
                .clone(),
            model.parts[Part::Turret.slot()]
                .as_ref()
                .unwrap()
                .geometry()
                .clone(),
        ];
        for case in 0..96 {
            let mut meshes = originals.clone();
            let mut state = damage::State::default();
            if case % 4 == 0 {
                state.tracks = [random.choose(&[0, 8, 9, 10, 11, 255]) as u8; 2];
            }
            if case % 11 == 0 {
                state.body = [2, 0, 0, 255];
                state.turret = [0, 1, 3, 0];
            }
            let selected = if case % 7 == 0 {
                [[0; 4]; 2]
            } else {
                model.damage_vertices
            };
            if case % 5 == 0 {
                // Signed-16 extremes and degenerate edges, without invalid asset indices.
                for mesh in &mut meshes {
                    for v in &mut mesh.vertices {
                        for c in v {
                            *c = random.choose(&boundaries) as i16;
                        }
                    }
                }
                if case % 10 == 0 {
                    for mesh in &mut meshes {
                        mesh.vertices.fill([0; 3]);
                    }
                }
            }
            let body_pose = random.pose(case % 6 == 0);
            let turret_pose = random.pose(case % 6 == 0);
            let mut previous = [0; 3];
            for tick in 0..4 {
                let op = if case % 8 == 0 {
                    0
                } else {
                    1 + (random.word() % 3)
                };
                let kind = if op == 0 {
                    random.word() % meshes[0].vertices.len() as u32
                } else {
                    random.choose(&[i32::MIN, -1, 0, 1, 2, 3, i32::MAX]) as u32
                };
                let player = case % 3 != 0;
                let world = if tick % 2 == 1 {
                    previous
                } else if case % 6 == 0 {
                    std::array::from_fn(|_| random.choose(&boundaries))
                } else {
                    let local = [
                        random.choose(&[-9000, -1, 0, 1, 9000]),
                        0,
                        random.choose(&[-9000, -1, 0, 1, 9000]),
                    ];
                    let pose = if op == 2 { turret_pose } else { body_pose };
                    let v = pose.matrix.local_to_world(local);
                    std::array::from_fn(|i| v[i].wrapping_add(pose.position[i]))
                };
                previous = world;
                let mut input = Vec::new();
                state_words(&mut input, &state);
                input.extend([u32::from(player), kind]);
                for pose in [body_pose, turret_pose] {
                    append(&mut input, pose.position);
                    append(&mut input, pose.matrix.0);
                }
                input.extend(selected.into_iter().flatten().map(u32::from));
                append(&mut input, world);
                for mesh in &meshes {
                    mesh_words(&mut input, mesh, true);
                }
                match op {
                    0 => damage::repair_normals(&mut meshes[0], kind as u8).unwrap(),
                    1 | 2 => {
                        let part = if op == 1 {
                            damage::Part::Body
                        } else {
                            damage::Part::Turret
                        };
                        let i = (op - 1) as usize;
                        state
                            .hit(
                                part,
                                if op == 1 { body_pose } else { turret_pose },
                                world,
                                selected[i],
                                &mut meshes[i],
                            )
                            .unwrap();
                    }
                    3 => {
                        if state.hit_track(kind as i32) && player {
                            state
                                .hit(
                                    damage::Part::Body,
                                    body_pose,
                                    world,
                                    selected[0],
                                    &mut meshes[0],
                                )
                                .unwrap();
                        }
                    }
                    _ => unreachable!(),
                }
                let mut output = vec![0];
                state_words(&mut output, &state);
                for mesh in &meshes {
                    mesh_words(&mut output, mesh, false);
                }
                oracle.record(op,input,output,format!("vehicle={vehicle} case={case} tick={tick} op={op} kind={kind} player={player}"));
            }
        }
        for case in 0..64 {
            let mut meshes = originals.clone();
            let mut damage = damage::State {
                tracks: [random.choose(&[0, 8, 9, 10, 11, 255]) as u8; 2],
                ..damage::State::default()
            };
            let body_pose = random.pose(case % 6 == 0);
            let turret_pose = random.pose(false);
            let mut state = ground_collision::State {
                velocity: std::array::from_fn(|_| random.signed() % 1000000),
                yaw: random.choose(&boundaries),
            };
            let mut contacts = std::array::from_fn(|_| ground_collision::Contact {
                kind: random.choose(&[0, 1, 3, 4, 5]) as i16,
                corner: random.choose(&[-2, -1, 0, 1, 2, 3, 4]),
                normal: if case % 6 == 0 {
                    std::array::from_fn(|_| random.choose(&boundaries))
                } else {
                    std::array::from_fn(|_| random.signed() % 4097)
                },
            });
            if case % 8 == 0 {
                for c in &mut contacts {
                    c.kind = 3;
                }
            }
            if case % 8 == 1 {
                for c in &mut contacts {
                    c.kind = 4;
                    c.normal = [4096, 0, 0];
                }
            }
            let env = ground_collision::Environment {
                mode: random.choose(&[0, 1, 2, 3, 4, 5]),
                player: case % 3 != 0,
                forward_velocity: random.choose(&[
                    i32::MIN,
                    -450001,
                    -450000,
                    -150001,
                    -150000,
                    0,
                    150000,
                    150001,
                    450000,
                    450001,
                ]),
                position: body_pose.position,
                external_basis: if case % 2 == 0 {
                    None
                } else {
                    Some(random.pose(false).matrix)
                },
            };
            let fail_at = if case % 4 == 0 {
                0
            } else {
                1 + random.word() % 4
            };
            let fail_status = random.choose(&[-13, 1, 0]);
            let (mut seed, mut counter) = (random.word(), random.word());
            for tick in 0..4 {
                let mut input = Vec::new();
                state_words(&mut input, &damage);
                input.extend([u32::from(env.player), 0]);
                for pose in [body_pose, turret_pose] {
                    append(&mut input, pose.position);
                    append(&mut input, pose.matrix.0);
                }
                input.extend(model.damage_vertices.into_iter().flatten().map(u32::from));
                append(&mut input, env.position);
                for mesh in &meshes {
                    mesh_words(&mut input, mesh, true);
                }
                append(&mut input, state.velocity);
                input.extend([
                    state.yaw as u32,
                    env.forward_velocity as u32,
                    env.mode as u32,
                    u32::from(env.external_basis.is_some()),
                ]);
                append(&mut input, env.external_basis.unwrap_or(Matrix::IDENTITY).0);
                for c in contacts {
                    input.extend([c.kind as u32, c.corner as u32]);
                    append(&mut input, c.normal);
                }
                input.extend([fail_at, fail_status as u32, seed, counter]);
                let mut effects = Effects {
                    damage: &mut damage,
                    body: &mut meshes[0],
                    pose: body_pose,
                    selected: model.damage_vertices[0],
                    player: env.player,
                    seed,
                    counter,
                    calls: 0,
                    trace: 0,
                    fail_at,
                    fail_status,
                };
                let status = match state.step(&mut contacts, env, &mut effects) {
                    Ok(()) => 0,
                    Err(ground_collision::Error::Arithmetic(_)) => -2,
                    Err(ground_collision::Error::Effect(code)) => code,
                };
                seed = effects.seed;
                counter = effects.counter;
                let calls = effects.calls;
                let trace = effects.trace;
                if status != 0 {
                    ground_errors += 1;
                }
                impacts += calls;
                dents += damage.body.iter().filter(|&&v| v != 0).count();
                let mut output = vec![status as u32];
                state_words(&mut output, &damage);
                for mesh in &meshes {
                    mesh_words(&mut output, mesh, false);
                }
                append(&mut output, state.velocity);
                output.push(state.yaw as u32);
                for c in contacts {
                    append(&mut output, c.normal);
                }
                output.extend([seed, counter, calls, trace]);
                oracle.record(4,input,output,format!("ground vehicle={vehicle} case={case} tick={tick} env={env:?} fail={fail_at}/{fail_status}"));
            }
        }
    }
    assert!(ground_errors > 0 && impacts > 0 && dents > 0);
    eprintln!(
        "ground errors={ground_errors}, impact calls={impacts}, damaged-quadrant observations={dents}"
    );
    oracle.compare();
}
