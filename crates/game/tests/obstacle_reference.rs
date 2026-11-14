use std::{
    fs,
    ops::Range,
    path::PathBuf,
    process::{Command, Stdio},
    sync::Arc,
};
use tankracer_game::{
    dispatch::{self, Class, ClassId, MethodId},
    geometry::Bounds,
    math::Matrix,
    object_query::{Collider, EntityId, ObstacleId},
    obstacle_collision::{
        self as obstacle, Effects, Environment, Grid, Invocation, Object, Shared, State, Tuning,
        World,
    },
    pose::Pose,
    random,
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
struct Probe {
    calls: u32,
    trace: u32,
    fail_at: u32,
    fail_status: i32,
    delta: i32,
    actions: u32,
}
impl Probe {
    fn call(&mut self, tag: u32, seed: &mut u32) -> Result<(), i32> {
        random::next(seed);
        self.calls += 1;
        self.trace = self.trace.wrapping_mul(37).wrapping_add(tag);
        if self.calls == self.fail_at && self.fail_status != 0 {
            Err(self.fail_status)
        } else {
            Ok(())
        }
    }
}
impl Effects for Probe {
    fn method(&mut self, method: MethodId, call: Invocation<'_>) -> Result<(), i32> {
        let entity = call.world.colliders[call.target.0].entity.0;
        let status = self.call(
            u32::from(method.0) * 256 + entity * 16 + call.vehicle.entity.0,
            call.random,
        );
        *call.current_slot = call.current_slot.wrapping_add(5);
        *call.result = entity + call.vehicle.entity.0 + (*call.random & 255);
        call.vehicle.velocity[0] = call.vehicle.velocity[0].wrapping_add(self.delta);
        call.vehicle.velocity[1] = call.vehicle.velocity[1].wrapping_sub(self.delta);
        call.vehicle.forward_velocity = call.vehicle.forward_velocity.wrapping_neg();
        if self.actions & 1 != 0 {
            call.world.colliders[call.target.0].pose.position[0] =
                call.world.colliders[call.target.0].pose.position[0].wrapping_add(self.delta);
        }
        if self.actions & 2 != 0 {
            call.vehicle.player = false;
        }
        if self.actions & 4 != 0 {
            for cell in &mut call.world.grid.cells {
                cell.clear();
            }
            call.world.deformables.clear();
        }
        status
    }
    fn feedback(&mut self, intensity: i32, vehicle: &mut State, seed: &mut u32) -> Result<(), i32> {
        let result = self.call(1000 + intensity as u32, seed);
        vehicle.velocity[1] = vehicle.velocity[1].wrapping_add(self.delta);
        result
    }
    fn impact(
        &mut self,
        vehicle: &mut State,
        contact: usize,
        kind: i32,
        seed: &mut u32,
    ) -> Result<(), i32> {
        let result = self.call(2000 + contact as u32 * 8 + kind as u32, seed);
        vehicle.velocity[0] = vehicle.velocity[0].wrapping_add(self.delta);
        vehicle.forward_velocity = vehicle.forward_velocity.wrapping_neg();
        result
    }
}
fn scale(s: tankracer_game::collision::Scale) -> u32 {
    u32::from(s.multiplier) | (u32::from(s.shift) << 16)
}
fn encode(
    world: &World,
    state: &State,
    env: Environment,
    shared: &Shared,
    probe: &Probe,
) -> Vec<u32> {
    let mut out = vec![
        env.mode as u32,
        env.dt as u32,
        u32::from(env.feedback),
        env.obstacle_slot,
        env.deformable_slot,
        scale(env.tuning.local_z),
        scale(env.tuning.final_scale),
        shared.random,
        shared.current_slot,
        probe.fail_at,
        probe.fail_status as u32,
        probe.delta as u32,
        probe.actions,
        state.class.0 as u32,
    ];
    append(&mut out, state.base_position);
    append(&mut out, state.position);
    append(&mut out, state.previous_position);
    append(&mut out, state.velocity);
    out.extend([state.forward_velocity as u32, u32::from(state.player)]);
    for p in state.contacts {
        append(&mut out, p);
    }
    out.extend([
        state.impact_accumulator as u32,
        state.collision_size,
        state.material_tag as u32,
        state.deformable.map_or(0, |id| id.0),
    ]);
    for (c, p) in world.colliders.iter().zip(&world.objects) {
        out.push(p.class.0 as u32);
        append(&mut out, c.pose.position);
        append(&mut out, c.pose.matrix.0);
        out.push(u32::from(c.external_basis.is_some()));
        append(&mut out, c.external_basis.unwrap_or(Matrix::IDENTITY).0);
        out.push(u32::from(c.radius));
        append(&mut out, c.bounds.min);
        append(&mut out, c.bounds.max);
        out.extend([
            p.weight as u32,
            p.threshold as u32,
            p.activity as u32,
            p.flags,
        ]);
        for v in p.mesh_bounds {
            append(&mut out, v.map(i32::from));
        }
    }
    for class in &world.classes {
        out.push(class.maximum_slot as u32);
        out.extend(
            class
                .methods
                .iter()
                .map(|m| m.map_or(u32::MAX, |m| u32::from(m.0))),
        );
    }
    append(&mut out, world.grid.origin);
    out.extend(world.grid.shifts);
    for list in [&world.grid.cells[0], &world.deformables] {
        out.push(list.len() as u32);
        out.extend(list.iter().map(|id| id.0 as u32));
    }
    out
}
fn output(
    world: &World,
    state: &State,
    shared: &Shared,
    probe: &Probe,
    code: i32,
    result: u32,
) -> Vec<u32> {
    let mut out = vec![code as u32, result];
    append(&mut out, state.velocity);
    out.extend([
        state.forward_velocity as u32,
        u32::from(state.player),
        state.impact_accumulator as u32,
        state.collision_size,
        state.material_tag as u32,
        state.deformable.map_or(0, |id| id.0),
        shared.random,
        shared.current_slot,
        probe.calls,
        probe.trace,
    ]);
    for c in &world.colliders {
        append(&mut out, c.pose.position);
    }
    out.extend([
        world.grid.cells[0].len() as u32,
        world.deformables.len() as u32,
    ]);
    out
}
fn code(e: obstacle::Error) -> i32 {
    match e {
        obstacle::Error::Arithmetic(_)
        | obstacle::Error::Query(tankracer_game::object_query::Error::Arithmetic) => -2,
        obstacle::Error::Feedback(c)
        | obstacle::Error::Impact(c)
        | obstacle::Error::Dispatch(dispatch::Error::Method(c)) => c,
        e => panic!("unexpected fixture error {e:?}"),
    }
}
#[test]
#[ignore = "requires C obstacle oracle; set TANKRACER_OBSTACLE_REFERENCE"]
fn responses_thresholds_synchronous_dispatch_lists_rng_and_error_prefixes_match_c() {
    let mut rng = Random(0x1922ce37);
    let (mut input, mut expected, mut records) =
        (Vec::new(), Vec::new(), Vec::<(Range<usize>, String)>::new());
    let (mut hits, mut callbacks, mut errors, mut skipped) = (0, 0, 0, 0);
    for op in 1..=3 {
        for case in 0..512 {
            let tuning = if case % 4 == 0 {
                Tuning {
                    local_z: tankracer_game::collision::Scale {
                        multiplier: rng.word() as u16,
                        shift: rng.word() as u8,
                    },
                    final_scale: tankracer_game::collision::Scale {
                        multiplier: rng.word() as u16,
                        shift: rng.word() as u8,
                    },
                }
            } else {
                Tuning::default()
            };
            let env = Environment {
                mode: rng.choose(&[0, 1, 2, 3, 4, 5]),
                dt: rng.choose(&[i32::MIN, -1, 0, 1, 16, 40, i32::MAX]),
                feedback: case % 3 != 0,
                player_class: ClassId(0),
                breakable_class: ClassId(1),
                obstacle_slot: rng.word() % 4,
                deformable_slot: rng.word() % 4,
                tuning,
            };
            let classes = (0..4)
                .map(|_| {
                    Arc::new(Class {
                        maximum_slot: rng.choose(&[-1, 0, 1, 2]),
                        methods: (0..3)
                            .map(|_| match rng.word() % 3 {
                                0 => None,
                                n => Some(MethodId(n as u16)),
                            })
                            .collect(),
                    })
                })
                .collect();
            let colliders = (0..2)
                .map(|i| {
                    let matrix = if case % 5 == 0 {
                        Matrix::from_angles(std::array::from_fn(|_| rng.word() & 65535))
                    } else {
                        Matrix::IDENTITY
                    };
                    Collider {
                        entity: EntityId(i + 2),
                        pose: Pose {
                            position: [i as i32 * 1000, 0, 0],
                            matrix,
                        },
                        external_basis: (case % 2 != 0).then_some(matrix),
                        radius: rng.choose(&[64, 128, 1024, 65535]) as u16,
                        bounds: Bounds {
                            min: [-3000; 3],
                            max: [3000; 3],
                        },
                    }
                })
                .collect();
            let properties = (0..2)
                .map(|_| Object {
                    class: ClassId((1 + rng.word() % 3) as usize),
                    weight: rng.choose(&[-1, 0, 8, 64, 128, i32::MAX]),
                    threshold: rng.choose(&[-1, 0, 150000, 1000000, i32::MAX]),
                    activity: rng.choose(&[0, 1, -1]),
                    flags: rng.word() % 4,
                    mesh_bounds: [[-500; 3], [500; 3]],
                })
                .collect();
            let candidates = if case % 7 == 0 {
                Vec::new()
            } else if case % 2 == 0 {
                vec![ObstacleId(0), ObstacleId(1)]
            } else {
                vec![ObstacleId(1), ObstacleId(0)]
            };
            let mut world = World {
                colliders,
                objects: properties,
                classes,
                grid: Grid {
                    origin: [-10000; 2],
                    shifts: [12, 12],
                    cells: std::array::from_fn(|_| candidates.clone()),
                },
                deformables: candidates,
            };
            let position = if case % 9 == 0 {
                [1000000; 3]
            } else if op == 1 {
                [-4000, 0, 0]
            } else {
                [0; 3]
            };
            let mut state = State {
                entity: EntityId(1),
                class: ClassId(if case % 4 == 0 { 2 } else { 0 }),
                base_position: [0; 3],
                angular: [0; 3],
                blast: Default::default(),
                reference_basis: tankracer_game::math::Matrix::IDENTITY,
                external_basis: None,
                position,
                previous_position: position.map(|v| v.wrapping_sub(4000)),
                velocity: if case % 11 == 0 {
                    [i32::MIN, i32::MAX, 0]
                } else {
                    [rng.choose(&[120000, 200000, 200001, 500000]), 1234, 900000]
                },
                forward_velocity: rng.choose(&[i32::MIN, -1, 0, 1, 400000]),
                player: case % 4 != 0,
                contacts: [[0; 3]; 4],
                impact_accumulator: rng.signed(),
                collision_size: rng.word(),
                material_tag: rng.signed(),
                deformable: Some(EntityId(7)),
            };
            let mut shared = Shared {
                random: rng.word(),
                current_slot: rng.word(),
            };
            let (fail_at, fail_status, delta, actions) = (
                rng.word() % 6,
                rng.choose(&[0, 1, -13]),
                rng.choose(&[-100000, 0, 100, 100000, i32::MAX]),
                rng.word() % 8,
            );
            for tick in 0..4 {
                let mut probe = Probe {
                    calls: 0,
                    trace: 0,
                    fail_at,
                    fail_status,
                    delta,
                    actions,
                };
                let mut words = vec![op];
                words.extend(encode(&world, &state, env, &shared, &probe));
                input.extend(words.into_iter().flat_map(u32::to_le_bytes));
                let mut result = 0x7182;
                let status = if op == 3 {
                    let table = Arc::clone(&world.classes[world.objects[0].class.0]);
                    match dispatch::call(
                        &table,
                        env.obstacle_slot,
                        &mut shared.current_slot,
                        &mut result,
                        |method, current_slot, result| {
                            probe.method(
                                method,
                                Invocation {
                                    dt: env.dt,
                                    target: ObstacleId(0),
                                    vehicle: &mut state,
                                    world: &mut world,
                                    random: &mut shared.random,
                                    current_slot,
                                    result,
                                },
                            )
                        },
                    ) {
                        Ok(()) => 0,
                        Err(dispatch::Error::Method(c)) => c,
                        e => panic!("unexpected fixture {e:?}"),
                    }
                } else {
                    let response = if op == 1 {
                        obstacle::obstacles(&mut world, &mut state, env, &mut shared, &mut probe)
                    } else {
                        obstacle::deformables(&mut world, &mut state, env, &mut shared, &mut probe)
                    };
                    match response {
                        Ok(hit) => {
                            hits += usize::from(hit.is_some());
                            0
                        }
                        Err(e) => code(e),
                    }
                };
                if status != 0 {
                    errors += 1;
                }
                if probe.calls == 0 {
                    skipped += 1;
                }
                callbacks += probe.calls;
                let start = expected.len();
                expected.extend(output(&world, &state, &shared, &probe, status, result));
                records.push((start..expected.len(),format!("op={op} case={case} tick={tick} env={env:?} fail={fail_at}/{fail_status} actions={actions} delta={delta}")));
            }
        }
    }
    let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/obstacle");
    fs::create_dir_all(&folder).unwrap();
    let path = folder.join("input.bin");
    fs::write(&path, input).unwrap();
    let result = Command::new(
        std::env::var_os("TANKRACER_OBSTACLE_REFERENCE").expect("set TANKRACER_OBSTACLE_REFERENCE"),
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
    assert_eq!(&result.stdout[..4], b"OB01");
    let words: Vec<_> = result.stdout[4..]
        .chunks_exact(4)
        .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
        .collect();
    assert_eq!(words.len(), expected.len());
    for (range, label) in &records {
        assert_eq!(words[range.clone()], expected[range.clone()], "{label}");
    }
    assert!(hits > 0 && callbacks > 0 && errors > 0 && skipped > 0);
    eprintln!(
        "{} obstacle/deformable/dispatch records / {} words exact; hits={hits} callbacks={callbacks} errors={errors} no-callback={skipped}",
        records.len(),
        words.len()
    );
}
