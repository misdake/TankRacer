use std::{
    fs,
    ops::Range,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_assets::{
    archive::Archive,
    config::{Part, ResolveContext, Surfaces, Tank},
    mesh::Geometry,
    model::ShapeArchive,
};
use tankracer_game::{
    damage,
    effect_pool::{Line, LineHandle, Pool, Quad},
    ground_collision,
    impact_particles::{self, Batch, Context, State, Tuning, Vehicle},
    math::Matrix,
    object_query::EntityId,
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
    fn choose(&mut self, v: &[i32]) -> i32 {
        v[self.word() as usize % v.len()]
    }
}
fn append(out: &mut Vec<u32>, v: impl IntoIterator<Item = i32>) {
    out.extend(v.into_iter().map(|v| v as u32));
}
fn line_words(out: &mut Vec<u32>, line: Line) {
    for p in line.points {
        append(out, p);
    }
    out.extend(line.colors.map(u32::from_le_bytes));
}
fn pools(out: &mut Vec<u32>, pool: &Pool) {
    out.extend([pool.line_count, pool.quad_count]);
    for line in pool.lines {
        line_words(out, line);
    }
    let quad = pool.quads[159];
    for p in quad.points {
        append(out, p);
    }
    out.push(quad.material);
    for b in quad.style.chunks_exact(4) {
        out.push(u32::from_le_bytes(b.try_into().unwrap()));
    }
    out.extend(pool.exhausted_colors.map(u32::from_le_bytes));
    line_words(out, pool.overflow);
}
fn batches(out: &mut Vec<u32>, state: &State) {
    for b in state.batches {
        for p in b.particles {
            append(out, p.distance.map(i32::from));
            append(out, p.speed);
            append(out, p.pitch);
            append(out, p.yaw);
            out.push(p.line.0);
            out.extend(p.colors.into_iter().flatten().map(u32::from));
        }
        out.extend([
            b.kind as u32,
            b.corner,
            b.owner.map_or(0, |id| id.0),
            u32::from(b.active),
            b.age as u32,
            b.frame,
        ]);
    }
}
#[derive(Clone, Copy)]
struct Operation {
    op: u32,
    kind: i32,
    corner: u32,
    handle: LineHandle,
    points: [[i32; 3]; 2],
    colors: [[u8; 4]; 2],
}
struct Fixture {
    state: State,
    pool: Pool,
    vehicles: [Vehicle; 2],
    tuning: Tuning,
    frame: u32,
    seed: u32,
    dt: i32,
    ground: Option<Ground>,
}
struct Ground {
    mode: i32,
    player: bool,
    position: [i32; 3],
    pose: Pose,
    selected: [u8; 4],
    damage: damage::State,
    motion: ground_collision::State,
    contacts: [ground_collision::Contact; 4],
    mesh: Geometry,
}
fn ground_words(out: &mut Vec<u32>, g: &Ground, input: bool) {
    if input {
        out.extend([g.mode as u32, u32::from(g.player)]);
        append(out, g.position);
        append(out, g.pose.position);
        append(out, g.pose.matrix.0);
        out.extend(g.selected.map(u32::from));
    }
    out.extend(
        g.damage
            .body
            .into_iter()
            .chain(g.damage.tracks)
            .map(u32::from),
    );
    append(out, g.motion.velocity);
    out.push(g.motion.yaw as u32);
    for c in g.contacts {
        out.extend([c.kind as i32 as u32, c.corner as u32]);
        append(out, c.normal);
    }
    if input {
        out.extend([g.mesh.vertices.len() as u32, g.mesh.triangles.len() as u32]);
    }
    for v in &g.mesh.vertices {
        append(out, v.map(i32::from));
    }
    for t in &g.mesh.triangles {
        let mut bytes = [0; 24];
        for (i, v) in t.normal.iter().enumerate() {
            bytes[i * 2..i * 2 + 2].copy_from_slice(&v.to_le_bytes());
        }
        bytes[6..8].copy_from_slice(&t.color.to_le_bytes());
        bytes[8..11].copy_from_slice(&t.indices);
        bytes[11] = t.sides;
        for (i, uv) in t.uv.iter().enumerate() {
            bytes[12 + i * 2..14 + i * 2].copy_from_slice(uv);
        }
        bytes[18..20].copy_from_slice(&t.flags.to_le_bytes());
        bytes[20..24].copy_from_slice(&t.material_source_offset.to_le_bytes());
        out.extend(
            bytes
                .chunks_exact(4)
                .map(|b| u32::from_le_bytes(b.try_into().unwrap())),
        );
    }
}
#[derive(Default)]
struct Oracle {
    input: Vec<u8>,
    expected: Vec<u32>,
    records: Vec<(Range<usize>, String)>,
    operations: [usize; 8],
    errors: [usize; 8],
    emitted: usize,
    skipped: usize,
}
fn error(e: impact_particles::Error) -> i32 {
    e.status()
}
impl Oracle {
    fn record(&mut self, fixture: &mut Fixture, actor: usize, operation: Operation, label: String) {
        let f = fixture;
        let v = f.vehicles[actor];
        let mut input = vec![
            operation.op,
            v.entity.0,
            f.frame,
            f.dt as u32,
            f.seed,
            f.state.next,
            v.cooldown,
            v.forward_velocity as u32,
            u32::from(v.external_basis.is_some()),
        ];
        append(&mut input, v.external_basis.unwrap_or(Matrix::IDENTITY).0);
        for p in v.contacts {
            append(&mut input, p);
        }
        append(&mut input, f.tuning.forward_offset);
        append(&mut input, f.tuning.reverse_offset);
        pools(&mut input, &f.pool);
        batches(&mut input, &f.state);
        input.extend([operation.kind as u32, operation.corner, operation.handle.0]);
        for p in operation.points {
            append(&mut input, p);
        }
        input.extend(operation.colors.into_iter().flatten().map(u32::from));
        if operation.op == 7 {
            ground_words(&mut input, f.ground.as_ref().unwrap(), true);
        }
        self.input
            .extend(input.into_iter().flat_map(u32::to_le_bytes));
        let mut result = 0x7182;
        let status = match operation.op {
            1 => {
                result = f.pool.allocate(operation.points, operation.colors).0;
                0
            }
            2 => {
                if f.pool
                    .update(operation.handle, operation.points, operation.colors)
                    .is_ok()
                {
                    0
                } else {
                    -1
                }
            }
            3 => {
                if f.pool.release(operation.handle).is_ok() {
                    0
                } else {
                    -1
                }
            }
            4 => {
                f.pool.reset();
                0
            }
            5 => match f.state.emit(
                &mut f.vehicles[actor],
                operation.corner,
                operation.kind,
                &mut Context {
                    frame: f.frame,
                    random: &mut f.seed,
                    pool: &mut f.pool,
                    tuning: f.tuning,
                },
            ) {
                Ok(emitted) => {
                    self.emitted += usize::from(emitted);
                    self.skipped += usize::from(!emitted);
                    0
                }
                Err(e) => error(e),
            },
            6 => match f.state.tick(
                &mut f.vehicles[actor],
                f.dt,
                &mut Context {
                    frame: f.frame,
                    random: &mut f.seed,
                    pool: &mut f.pool,
                    tuning: f.tuning,
                },
            ) {
                Ok(()) => 0,
                Err(e) => error(e),
            },
            7 => {
                let g = f.ground.as_mut().unwrap();
                let env = ground_collision::Environment {
                    mode: g.mode,
                    player: g.player,
                    forward_velocity: f.vehicles[actor].forward_velocity,
                    position: g.position,
                    external_basis: f.vehicles[actor].external_basis,
                };
                let mut effects = ground_collision::GameEffects::new(
                    &g.contacts,
                    &mut f.state,
                    &mut f.vehicles[actor],
                    Context {
                        frame: f.frame,
                        random: &mut f.seed,
                        pool: &mut f.pool,
                        tuning: f.tuning,
                    },
                    damage::Target {
                        state: &mut g.damage,
                        player: g.player,
                        body_pose: g.pose,
                        body_vertices: g.selected,
                        body_mesh: Some(&mut g.mesh),
                    },
                );
                match g.motion.step(&mut g.contacts, env, &mut effects) {
                    Ok(()) => 0,
                    Err(ground_collision::Error::Arithmetic(_)) => -2,
                    Err(ground_collision::Error::Effect(code)) => code,
                }
            }
            _ => unreachable!(),
        };
        self.operations[operation.op as usize] += 1;
        if status != 0 {
            self.errors[operation.op as usize] += 1;
        }
        let start = self.expected.len();
        self.expected.extend([
            status as u32,
            result,
            f.seed,
            f.vehicles[actor].cooldown,
            f.state.next,
        ]);
        pools(&mut self.expected, &f.pool);
        batches(&mut self.expected, &f.state);
        if operation.op == 7 {
            ground_words(&mut self.expected, f.ground.as_ref().unwrap(), false);
        }
        self.records.push((start..self.expected.len(), label));
    }
    fn compare(self) {
        let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/impact");
        fs::create_dir_all(&folder).unwrap();
        let path = folder.join("input.bin");
        fs::write(&path, self.input).unwrap();
        let result = Command::new(
            std::env::var_os("TANKRACER_IMPACT_REFERENCE").expect("set TANKRACER_IMPACT_REFERENCE"),
        )
        .stdin(Stdio::from(fs::File::open(path).unwrap()))
        .output()
        .unwrap();
        assert!(
            result.status.success(),
            "{:?} {}",
            result.status.code(),
            String::from_utf8_lossy(&result.stderr)
        );
        assert_eq!(&result.stdout[..4], b"IP01");
        let actual: Vec<_> = result.stdout[4..]
            .chunks_exact(4)
            .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
            .collect();
        assert_eq!(actual.len(), self.expected.len());
        for (range, label) in &self.records {
            if let Some((i, (a, b))) = actual[range.clone()]
                .iter()
                .zip(&self.expected[range.clone()])
                .enumerate()
                .find(|(_, (a, b))| a != b)
            {
                panic!("{label}: field {i}, actual={a:#010x}, expected={b:#010x}");
            }
        }
        assert!(self.emitted > 0 && self.skipped > 0 && self.errors[5] > 0 && self.errors[6] > 0);
        eprintln!(
            "{} effect-pool/impact records / {} words exact; operations {:?}, errors {:?}, emitted={}, skipped={}",
            self.records.len(),
            actual.len(),
            self.operations,
            self.errors,
            self.emitted,
            self.skipped
        );
    }
}
fn fixture(rng: &mut Random, case: usize) -> Fixture {
    let mut pool = Pool::default();
    let count = [0, 85, 95, 99, 100][case % 5];
    for i in 0..count {
        pool.allocate([[i * 128, 1024, 2048]; 2], [[i as u8; 4]; 2]);
    }
    pool.quad_count = 1;
    pool.quads[159] = Quad {
        points: std::array::from_fn(|_| std::array::from_fn(|_| rng.signed())),
        material: rng.word(),
        style: std::array::from_fn(|_| rng.word() as u8),
    };
    pool.exhausted_colors = std::array::from_fn(|_| std::array::from_fn(|_| rng.word() as u8));
    pool.overflow = Line {
        points: std::array::from_fn(|_| std::array::from_fn(|_| rng.signed())),
        colors: [[7; 4]; 2],
    };
    let vehicles = std::array::from_fn(|i| {
        let mut contacts: [[i32; 3]; 4] = [
            [-100, 500, 100],
            [-100, 500, -100],
            [100, 500, 100],
            [100, 500, -100],
        ];
        if case.is_multiple_of(11) {
            contacts[1][0] = contacts[0][0].wrapping_add(i32::MIN);
        }
        let matrix = Matrix::from_angles(std::array::from_fn(|_| rng.word() & 65535));
        Vehicle {
            entity: EntityId(i as u32 + 1),
            contacts,
            forward_velocity: rng.choose(&[i32::MIN, -500000, 0, 500000, i32::MAX]),
            external_basis: if case.is_multiple_of(3) {
                None
            } else {
                Some(matrix)
            },
            cooldown: rng.choose(&[0, 499, 500, i32::MAX, -1]) as u32,
        }
    });
    Fixture {
        state: State::default(),
        pool,
        vehicles,
        tuning: if case.is_multiple_of(2) {
            Tuning::default()
        } else {
            Tuning {
                forward_offset: std::array::from_fn(|_| rng.signed() % 10000),
                reverse_offset: std::array::from_fn(|_| rng.signed() % 10000),
            }
        },
        frame: case as u32,
        seed: rng.word(),
        dt: rng.choose(&[i32::MIN, i32::MAX, -1, 0, 1, 16, 40, 60, 61]),
        ground: None,
    }
}
fn real_bodies() -> Vec<(Geometry, [u8; 4])> {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    let packs: Vec<_> = ["BRONZE", "SILVER", "GOLD"]
        .map(|tier| fs::read(root.join("TANKS").join(tier).join("FASTFILE.DAT")).unwrap())
        .into_iter()
        .collect();
    let shared = fs::read(root.join("INGAME/FASTFILE.DAT")).unwrap();
    let shapes = |bytes| {
        let archive = Archive::parse(bytes).unwrap();
        let entry = archive
            .entries()
            .iter()
            .find(|e| e.name.ends_with(".SS"))
            .unwrap();
        ShapeArchive::parse(archive.get(entry.name).unwrap()).unwrap()
    };
    let source = Bundles {
        vehicle: packs.iter().map(|b| shapes(b)).collect(),
        shared: vec![shapes(&shared)],
        level: Vec::new(),
    };
    let data = fs::read(root.join("DATA/FASTFILE.DAT")).unwrap();
    let archive = Archive::parse(&data).unwrap();
    let surfaces = Surfaces::decode(archive.get("SURFS.DAT").unwrap()).unwrap();
    let mut bank = ModelBank::default();
    (1..=15)
        .map(|index| {
            let tank = Tank::decode(archive.get(&format!("TANK{index}.DAT")).unwrap()).unwrap();
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
            (
                model.parts[Part::Body.slot()]
                    .as_ref()
                    .unwrap()
                    .geometry()
                    .clone(),
                model.damage_vertices[0],
            )
        })
        .collect()
}
#[test]
#[ignore = "requires C impact/line-pool oracle; set TANKRACER_IMPACT_REFERENCE"]
fn real_particles_shared_pool_exhaustion_retirement_and_error_prefixes_match_c() {
    let mut rng = Random(0x7631ea90);
    let mut oracle = Oracle::default();
    for case in 0..128 {
        let mut f = fixture(&mut rng, case);
        if case % 16 == 0 {
            f.state.next = 4;
        }
        if case % 16 == 1 {
            let mut b = Batch {
                owner: Some(EntityId(1)),
                active: true,
                frame: u32::MAX,
                age: rng.choose(&[59, 60, 61, i32::MAX]),
                kind: case as i32 % 7,
                ..Batch::default()
            };
            for p in &mut b.particles {
                p.distance = [-32768, 32767];
                p.speed = [rng.signed(), rng.signed()];
                p.pitch = [rng.signed(), rng.signed()];
                p.yaw = [rng.signed(), rng.signed()];
                p.colors = [[1, 2, 3], [4, 5, 6]];
                p.line = f.pool.allocate([[0; 3]; 2], [[0; 4]; 2]);
            }
            b.particles[3].line = LineHandle::RELEASED;
            f.state.batches[0] = b;
        }
        for (step, op) in [5, 6, 5, 5, 5, 5, 6, 6, 6, 5, 6, 6].into_iter().enumerate() {
            if step != 1 {
                f.frame = f.frame.wrapping_add(1);
            }
            let operation = Operation {
                op,
                kind: (case % 9) as i32 - 1,
                corner: (step % 4) as u32,
                handle: LineHandle(0),
                points: [[0; 3]; 2],
                colors: [[0; 4]; 2],
            };
            oracle.record(
                &mut f,
                (case + step / 3) % 2,
                operation,
                format!("particles case={case} step={step} op={op}"),
            );
        }
    }
    for case in 0..256 {
        let mut f = fixture(&mut rng, case);
        if case % 7 == 0 {
            f.pool.line_count = rng.choose(&[-1, 0, 99, 100, 101]) as u32;
        }
        for step in 0..4 {
            let op = 1 + (case + step) % 4;
            let handle = LineHandle(match case % 5 {
                0 => rng.word() % 100,
                1 => u32::MAX,
                2 => 100,
                3 => 0xfe5cbe12,
                _ => 0,
            });
            let operation = Operation {
                op: op as u32,
                kind: 0,
                corner: 0,
                handle,
                points: std::array::from_fn(|_| std::array::from_fn(|_| rng.signed())),
                colors: std::array::from_fn(|_| std::array::from_fn(|_| rng.word() as u8)),
            };
            oracle.record(
                &mut f,
                case % 2,
                operation,
                format!("pool case={case} step={step} op={op} handle={handle:?}"),
            );
        }
    }
    let mut dents = 0;
    let mut particle_batches = 0;
    for (model, (mesh, selected)) in real_bodies().into_iter().enumerate() {
        for case in 0..16 {
            let mut f = fixture(&mut rng, case);
            f.dt = 16;
            f.vehicles[0].forward_velocity = [-500001, 500001, 150000, 150001][case % 4];
            f.ground = Some(Ground {
                mode: [0, 1, 2, 3, 4, 5][case % 6],
                player: case % 5 != 4,
                position: std::array::from_fn(|_| rng.signed() % 100000),
                pose: Pose {
                    position: std::array::from_fn(|_| rng.signed() % 100000),
                    matrix: Matrix::from_angles(std::array::from_fn(|_| rng.word() & 65535)),
                },
                selected,
                damage: damage::State {
                    tracks: [case as u8 % 11; 2],
                    ..damage::State::default()
                },
                motion: ground_collision::State {
                    velocity: [
                        rng.signed() % 500000,
                        rng.signed() % 500000,
                        rng.signed() % 500000,
                    ],
                    yaw: rng.signed(),
                },
                contacts: std::array::from_fn(|i| ground_collision::Contact {
                    kind: if case % 4 == 0 {
                        4
                    } else {
                        [0, 3, 4, 5][(case + i) % 4]
                    },
                    // Rotate logical corners to detect an accidental index binding.
                    corner: ((i + case + 1) % 4) as i32,
                    normal: if case % 7 == 0 {
                        [i32::MIN, 0, i32::MAX]
                    } else {
                        [[4096, 0, 0], [0, -4096, 0], [0, 0, 4096], [-4096, 0, 0]][i]
                    },
                }),
                mesh: mesh.clone(),
            });
            for (step, op) in [7, 6, 7, 6, 7, 6, 7, 6].into_iter().enumerate() {
                f.frame = (case * 100 + step / 2) as u32;
                oracle.record(
                    &mut f,
                    0,
                    Operation {
                        op,
                        kind: 0,
                        corner: 0,
                        handle: LineHandle(0),
                        points: [[0; 3]; 2],
                        colors: [[0; 4]; 2],
                    },
                    format!("ground+real-particles model={model} case={case} step={step}"),
                );
            }
            dents += usize::from(f.ground.as_ref().unwrap().damage.body != [0; 4]);
            particle_batches += usize::from(f.state.batches.iter().any(|b| b.active));
        }
    }
    assert!(dents > 0 && particle_batches > 0);
    eprintln!(
        "composed ground cases: {dents} dented bodies, {particle_batches} with real particle batches"
    );
    eprintln!(
        "fixed shared effects: pool={} bytes, impact ring={} bytes",
        std::mem::size_of::<Pool>(),
        std::mem::size_of::<State>()
    );
    oracle.compare();
}
