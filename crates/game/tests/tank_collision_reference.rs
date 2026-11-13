use std::{
    fs,
    ops::Range,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_assets::mesh::{Geometry, Triangle};
use tankracer_game::{
    damage,
    geometry::Bounds,
    math::Matrix,
    object_query::{Collider, EntityId, Tank, TankId},
    pose::Pose,
    tank_collision::{self, Environment, PeerSnapshot, State},
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
fn mesh() -> Geometry {
    Geometry {
        vertices: vec![
            [-120, 40, 240],
            [120, 40, 240],
            [-120, 40, -240],
            [120, 40, -240],
        ],
        triangles: [[0, 1, 2], [2, 1, 3]]
            .map(|indices| Triangle {
                source_offset: 0,
                normal: [0, 4096, 0],
                color: 0,
                indices,
                sides: 0,
                uv: [[0; 2]; 3],
                flags: 0,
                material: None,
                material_source_offset: 0,
                billboard: None,
            })
            .to_vec(),
    }
}
#[derive(Clone)]
struct DamageBody {
    state: damage::State,
    pose: Pose,
    selected: [u8; 4],
    mesh: Geometry,
}
struct Effects<'a> {
    bodies: &'a mut [DamageBody; 2],
    players: [bool; 2],
    seed: u32,
    counter: u32,
    calls: u32,
    trace: u32,
    fail_at: u32,
    fail_status: i32,
}
impl tank_collision::Effects for Effects<'_> {
    fn impact(&mut self, tank: TankId, index: usize, kind: i32) -> Result<(), i32> {
        self.seed = self.seed.wrapping_mul(0x19660d).wrapping_add(0x3c6ef35f);
        self.counter = self.counter.wrapping_add(1);
        self.calls += 1;
        self.trace = self
            .trace
            .wrapping_mul(32)
            .wrapping_add(tank.0 as u32 * 8 + index as u32 * 2 + u32::from(kind == 4) + 1);
        if self.calls == self.fail_at && self.fail_status != 0 {
            Err(self.fail_status)
        } else {
            Ok(())
        }
    }
    fn track_damage(&mut self, tank: TankId, kind: i32, world: [i32; 3]) -> Result<(), i32> {
        let b = &mut self.bodies[tank.0];
        if b.state.hit_track(kind) && self.players[tank.0] {
            b.state
                .hit(damage::Part::Body, b.pose, world, b.selected, &mut b.mesh)
                .map_err(|_| -1)?;
        }
        Ok(())
    }
}
fn actor_words(out: &mut Vec<u32>, tank: &Tank, state: &State, body: &DamageBody) {
    let c = tank.collider;
    out.push(c.entity.0);
    append(out, c.pose.position);
    append(out, c.pose.matrix.0);
    out.push(u32::from(c.external_basis.is_some()));
    append(out, c.external_basis.unwrap_or(Matrix::IDENTITY).0);
    out.push(u32::from(c.radius));
    append(out, c.bounds.min);
    append(out, c.bounds.max);
    out.extend([
        u32::from(tank.slot),
        u32::from(tank.active),
        u32::from(tank.disabled),
        u32::from(tank.ready),
    ]);
    append(out, tank.overlap_bounds.min);
    append(out, tank.overlap_bounds.max);
    for p in tank.contacts {
        append(out, p);
    }
    append(out, state.velocity);
    append(out, state.local_velocity);
    append(out, state.previous_position);
    out.extend([state.collision_timer as u32, u32::from(state.player)]);
    damage_words(out, &body.state);
    append(out, body.pose.position);
    append(out, body.pose.matrix.0);
    out.extend(body.selected.map(u32::from));
    geometry_words(out, &body.mesh);
}
fn damage_words(out: &mut Vec<u32>, s: &damage::State) {
    out.extend(
        s.body
            .into_iter()
            .chain(s.turret)
            .chain(s.tracks)
            .map(u32::from),
    );
}
fn geometry_words(out: &mut Vec<u32>, mesh: &Geometry) {
    for v in &mesh.vertices {
        append(out, v.map(i32::from));
    }
    for t in &mesh.triangles {
        append(out, t.normal.map(i32::from));
    }
}
fn actor_output(out: &mut Vec<u32>, s: &State, b: &DamageBody) {
    append(out, s.velocity);
    out.push(s.collision_timer as u32);
    damage_words(out, &b.state);
    geometry_words(out, &b.mesh);
}
fn peer_words(out: &mut Vec<u32>, peer: Option<PeerSnapshot>) {
    out.push(peer.map_or(u32::MAX, |p| p.tank.0 as u32));
    append(out, peer.map_or([0; 2], |p| p.velocity_xz));
}
fn error(e: tank_collision::Error) -> i32 {
    match e {
        tank_collision::Error::Limit => -3,
        tank_collision::Error::Effect(code) => code,
        tank_collision::Error::Query(tankracer_game::object_query::Error::Arithmetic) => -2,
        e => panic!("unexpected fixture error {e:?}"),
    }
}
#[derive(Default)]
struct Oracle {
    input: Vec<u8>,
    expected: Vec<u32>,
    records: Vec<(Range<usize>, String)>,
    statuses: [[usize; 4]; 2],
    hits: usize,
    impacts: usize,
    damage: usize,
}
impl Oracle {
    fn record(&mut self, op: u32, input: Vec<u32>, output: Vec<u32>, label: String) {
        self.input.extend(op.to_le_bytes());
        self.input
            .extend(input.into_iter().flat_map(u32::to_le_bytes));
        let start = self.expected.len();
        let code = output[0] as i32;
        self.statuses[(op - 1) as usize][if code < 0 {
            if code == -3 { 2 } else { 3 }
        } else {
            code as usize
        }] += 1;
        self.expected.extend(output);
        self.records.push((start..self.expected.len(), label));
    }
    fn compare(self) {
        let folder =
            PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/tank-collision");
        fs::create_dir_all(&folder).unwrap();
        let path = folder.join("input.bin");
        fs::write(&path, self.input).unwrap();
        let result = Command::new(
            std::env::var_os("TANKRACER_TANK_COLLISION_REFERENCE")
                .expect("set TANKRACER_TANK_COLLISION_REFERENCE"),
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
        assert_eq!(&result.stdout[..4], b"TC01");
        let words: Vec<_> = result.stdout[4..]
            .chunks_exact(4)
            .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
            .collect();
        assert_eq!(
            &words[..12],
            &[-1024_i32, 0, 0, 1024, 0, 0, 0, 0, 1024, 0, 0, -1024].map(|v| v as u32)
        );
        let actual = &words[12..];
        assert_eq!(actual.len(), self.expected.len());
        for (range, label) in &self.records {
            assert_eq!(
                actual[range.clone()],
                self.expected[range.clone()],
                "{label}"
            );
        }
        eprintln!(
            "{} tank overlap/dispatch records / {} words exact; zero/overlap/limit/effect {:?}; dispatch hits {}, impacts {}, damage observations {}",
            self.records.len(),
            actual.len(),
            self.statuses,
            self.hits,
            self.impacts,
            self.damage
        );
    }
}
#[test]
#[ignore = "requires C tank collision oracle; set TANKRACER_TANK_COLLISION_REFERENCE"]
fn ordered_overlap_damage_effect_errors_and_query_dispatch_match_c() {
    let mut rng = Random(0x912f0c6d);
    let mut oracle = Oracle::default();
    for op in 1..=2 {
        for case in 0..1024 {
            let own = case % 2;
            let env = Environment {
                mode: if case % 16 == 0 {
                    4
                } else {
                    rng.choose(&[0, 1, 2, 3, 4, 5])
                },
                dt: rng.choose(&[0, 1, 16, 40, 100, i32::MAX]),
                separation_limit: if case % 64 == 0 {
                    0
                } else {
                    rng.choose(&[1, 2, 4, 16]) as u32
                },
            };
            let mut geometry = std::array::from_fn::<_, 2, _>(|i| {
                let position = if i == 0 {
                    [0; 3]
                } else {
                    [rng.signed() % 2000, 0, rng.signed() % 2000]
                };
                let basis = if case % 3 == 0 {
                    Matrix::from_angles(std::array::from_fn(|_| rng.word() & 65535))
                } else {
                    Matrix::IDENTITY
                };
                let bounds = Bounds {
                    min: [-3000; 3],
                    max: [3000; 3],
                };
                let collider = Collider {
                    entity: EntityId(i as u32 + 1),
                    pose: Pose {
                        position,
                        matrix: basis,
                    },
                    external_basis: if case % 3 == 0 { None } else { Some(basis) },
                    radius: 512,
                    bounds,
                };
                let contacts = std::array::from_fn(|p| {
                    let v = basis.local_to_world([
                        if p & 1 == 0 { -900 } else { 900 },
                        0,
                        if p & 2 == 0 { -900 } else { 900 },
                    ]);
                    std::array::from_fn(|j| v[j].wrapping_add(position[j]))
                });
                Tank {
                    collider,
                    slot: i as u8,
                    active: case % 17 != 0,
                    disabled: case % 19 == 0,
                    ready: case % 23 != 0,
                    overlap_bounds: bounds,
                    contacts,
                }
            });
            let mut states = std::array::from_fn(|i| State {
                velocity: std::array::from_fn(|_| rng.signed() % 700000),
                local_velocity: std::array::from_fn(|_| rng.signed() % 700000),
                previous_position: geometry[i]
                    .collider
                    .pose
                    .position
                    .map(|v| v.wrapping_sub(1000)),
                collision_timer: rng.signed(),
                player: (case + i) % 3 != 0,
            });
            if case % 5 == 0 {
                geometry[1].collider.pose.position = [0, 0, 20000];
                geometry[1].contacts = [[0, 0, 20000]; 4];
            }
            if case % 16 == 2 || case % 16 == 3 {
                geometry[0].collider.pose.matrix = Matrix::IDENTITY;
                geometry[1].collider.pose.matrix = Matrix::IDENTITY;
                geometry[0].collider.external_basis = None;
                geometry[1].collider.external_basis = None;
                geometry[0].contacts = [[50000; 3]; 4];
                geometry[1].contacts = [[50000; 3]; 4];
                geometry[1].collider.pose.position =
                    [0, 0, if case % 16 == 2 { 10000 } else { 1000 }];
                states[0].previous_position = [0, 0, -12000];
                states[1].previous_position = [0, 0, 12000];
                states[0].velocity = [0, 0, 300000];
                states[1].velocity = [0, 0, -300000];
            }
            let env = if case % 16 == 0 {
                for t in &mut geometry {
                    t.collider.pose.matrix = Matrix([0; 9]);
                    t.active = true;
                    t.disabled = false;
                    t.ready = true;
                }
                geometry[1].collider.pose.position = [0, 0, 10000];
                states[0].velocity = [0, 0, 300000];
                states[1].velocity = [0; 3];
                Environment {
                    mode: 4,
                    dt: 40,
                    separation_limit: 1,
                }
            } else {
                env
            };
            let mut bodies = std::array::from_fn(|i| DamageBody {
                state: damage::State {
                    tracks: [rng.choose(&[0, 8, 9, 10, 11, 255]) as u8; 2],
                    ..damage::State::default()
                },
                pose: geometry[i].collider.pose,
                selected: if case % 7 == 0 { [0; 4] } else { [0, 1, 2, 3] },
                mesh: mesh(),
            });
            let mut peer = (case % 3 != 0).then(|| PeerSnapshot {
                tank: TankId(1 - own),
                velocity_xz: [rng.signed(), rng.signed()],
            });
            let speed = rng.choose(&[
                i32::MIN,
                -1,
                0,
                250000,
                250001,
                449999,
                450000,
                450001,
                i32::MAX,
            ]);
            let fail_at = if case % 4 == 0 {
                0
            } else {
                rng.choose(&[1, 2, 3, 4]) as u32
            };
            let fail_status = rng.choose(&[-13, 1, 0]);
            let (mut seed, mut counter) = (rng.word(), rng.word());
            let mut slots = [None; 9];
            slots[0] = Some(TankId(0));
            slots[1] = Some(TankId(1));
            for tick in 0..4 {
                let mut input = vec![
                    own as u32,
                    speed as u32,
                    env.mode as u32,
                    env.dt as u32,
                    env.separation_limit,
                ];
                peer_words(&mut input, peer);
                input.extend([fail_at, fail_status as u32, seed, counter]);
                for i in 0..2 {
                    actor_words(&mut input, &geometry[i], &states[i], &bodies[i]);
                }
                input.extend(slots.map(|id| id.map_or(u32::MAX, |id| id.0 as u32)));
                let players = states.map(|s| s.player);
                let mut effects = Effects {
                    bodies: &mut bodies,
                    players,
                    seed,
                    counter,
                    calls: 0,
                    trace: 0,
                    fail_at,
                    fail_status,
                };
                let code = if op == 1 {
                    let pair_states = states.get_disjoint_mut([own, 1 - own]).unwrap();
                    match (tank_collision::Pair {
                        ids: [TankId(own), TankId(1 - own)],
                        geometry: [&geometry[own], &geometry[1 - own]],
                        states: pair_states,
                    })
                    .resolve_overlap(speed, env, &mut effects)
                    {
                        Ok(hit) => i32::from(hit),
                        Err(e) => error(e),
                    }
                } else {
                    match tank_collision::step(
                        TankId(own),
                        &geometry,
                        &mut states,
                        &slots,
                        env,
                        &mut peer,
                        &mut effects,
                    ) {
                        Ok(hit) => {
                            oracle.hits += usize::from(hit.is_some());
                            0
                        }
                        Err(e) => error(e),
                    }
                };
                seed = effects.seed;
                counter = effects.counter;
                let (calls, trace) = (effects.calls, effects.trace);
                oracle.impacts += calls as usize;
                oracle.damage += bodies
                    .iter()
                    .map(|b| b.state.body.iter().filter(|&&v| v != 0).count())
                    .sum::<usize>();
                let mut output = vec![code as u32];
                for i in 0..2 {
                    actor_output(&mut output, &states[i], &bodies[i]);
                }
                peer_words(&mut output, peer);
                output.extend([seed, counter, calls, trace]);
                oracle.record(op,input,output,format!("op={op} case={case} tick={tick} own={own} speed={speed} env={env:?} fail={fail_at}/{fail_status}"));
            }
        }
    }
    assert!(oracle.hits > 0 && oracle.impacts > 0 && oracle.damage > 0);
    for counts in oracle.statuses {
        assert!(counts[0] > 0 && counts[2] > 0 && counts[3] > 0);
    }
    oracle.compare();
}
