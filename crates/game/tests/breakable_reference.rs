use std::{
    fs,
    ops::Range,
    path::PathBuf,
    process::{Command, Stdio},
    sync::Arc,
};
use tankracer_game::{
    breakable::{self, BREAKABLE_HIT, ENABLE_TICK, Environment as HitEnvironment, Store},
    dispatch::{Class, ClassId, MethodId},
    geometry::Bounds,
    lifecycle::Ticks,
    object_query::{Collider, EntityId, ObstacleId},
    obstacle_collision::{
        self as obstacle, Effects, Environment, Grid, Invocation, Object, Shared, State, Tuning,
        World,
    },
    pose::Pose,
    random,
    spatial::Cell,
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
fn list(out: &mut Vec<u32>, ids: &[ObstacleId]) {
    out.push(ids.len() as u32);
    out.extend(ids.iter().map(|id| id.0 as u32));
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
    fn call(&mut self, tag: u32, store: &mut Store, call: Invocation<'_>) -> Result<(), i32> {
        random::next(call.random);
        self.calls += 1;
        self.trace = self.trace.wrapping_mul(37).wrapping_add(tag);
        let s = store.states[call.target.0].as_mut().unwrap();
        s.angular_velocity = s.angular_velocity.wrapping_add(self.delta);
        s.rotation = s.rotation.wrapping_add(self.delta);
        s.turn_rate = s.turn_rate.wrapping_sub(self.delta);
        let p = &mut call.world.objects[call.target.0];
        p.activity = p.activity.wrapping_add(self.delta);
        p.flags ^= 16;
        call.vehicle.forward_velocity = call.vehicle.forward_velocity.wrapping_neg();
        if self.actions & 1 != 0
            && let Some(f) = &mut s.effect_flags
        {
            *f ^= 16;
        }
        if self.actions & 2 != 0 {
            call.vehicle.player = false;
        }
        if self.calls == self.fail_at && self.fail_status != 0 {
            Err(self.fail_status)
        } else {
            Ok(())
        }
    }
}
impl breakable::Services for Probe {
    fn method(
        &mut self,
        method: MethodId,
        store: &mut Store,
        call: Invocation<'_>,
        has_payload: bool,
    ) -> Result<(), i32> {
        let entity = call.world.colliders[call.target.0].entity.0;
        let tag = u32::from(method.0) * 65536
            + entity * 256
            + u32::from(has_payload) * 128
            + if has_payload {
                call.vehicle.entity.0
            } else {
                0
            };
        *call.current_slot = call.current_slot.wrapping_add(7);
        *call.result = 1234;
        self.call(tag, store, call)
    }
    fn sound(&mut self, store: &mut Store, call: Invocation<'_>) -> Result<(), i32> {
        let entity = call.world.colliders[call.target.0].entity.0;
        self.call(0x600000 + entity, store, call)
    }
    fn feedback(
        &mut self,
        intensity: i32,
        store: &mut Store,
        call: Invocation<'_>,
    ) -> Result<(), i32> {
        let entity = call.world.colliders[call.target.0].entity.0;
        self.call(0x900000 + entity * 65536 + intensity as u32, store, call)
    }
}
struct Methods {
    store: Store,
    env: HitEnvironment,
    probe: Probe,
}
impl Effects for Methods {
    fn method(&mut self, method: MethodId, call: Invocation<'_>) -> Result<(), i32> {
        match method {
            BREAKABLE_HIT => breakable::collide(&mut self.store, call, self.env, &mut self.probe),
            _ => breakable::Services::method(&mut self.probe, method, &mut self.store, call, true),
        }
    }
    fn feedback(&mut self, _: i32, _: &mut State, _: &mut u32) -> Result<(), i32> {
        panic!("ordinary obstacle uses its scene feedback")
    }
    fn impact(&mut self, _: &mut State, _: usize, _: i32, _: &mut u32) -> Result<(), i32> {
        panic!("ordinary obstacle has no ground impacts")
    }
}
fn encode(world: &World, vehicle: &State, shared: &Shared, methods: &Methods) -> Vec<u32> {
    let env = methods.env;
    let p = &methods.probe;
    let mut out = vec![
        env.minimum_turn_rate as u32,
        env.maximum_turn_rate as u32,
        u32::from(env.feedback),
        env.work_limit,
        40,
        shared.random,
        shared.current_slot,
        p.fail_at,
        p.fail_status as u32,
        p.delta as u32,
        p.actions,
    ];
    append(&mut out, vehicle.base_position);
    append(&mut out, vehicle.position);
    append(&mut out, vehicle.previous_position);
    append(&mut out, vehicle.velocity);
    out.extend([
        vehicle.forward_velocity as u32,
        u32::from(vehicle.player),
        vehicle.impact_accumulator as u32,
        vehicle.collision_size,
    ]);
    for i in 0..3 {
        let c = &world.colliders[i];
        let p = &world.objects[i];
        let s = methods.store.states[i].as_ref().unwrap();
        out.push(u32::from(methods.store.live[i]));
        append(&mut out, c.pose.position);
        append(&mut out, c.pose.matrix.0);
        out.push(u32::from(c.radius));
        append(&mut out, c.bounds.min);
        append(&mut out, c.bounds.max);
        out.extend([
            p.weight as u32,
            p.threshold as u32,
            p.activity as u32,
            p.flags,
            s.angular_velocity as u32,
            s.rotation as u32,
            s.turn_rate as u32,
            s.last_vehicle.map_or(0, |id| id.0),
            u32::from(s.has_sound),
            u32::from(s.auxiliary),
            s.effect_flags.map_or(u32::MAX, u32::from),
            s.cell.map_or(u32::MAX, Cell::packed),
            methods.store.ticks.flags[i],
            s.attached.len() as u32,
        ]);
        out.extend(s.attached.iter().map(|id| id.map_or(0, |id| id.0)));
    }
    for c in &world.classes {
        out.push(c.maximum_slot as u32);
        out.extend(
            c.methods
                .iter()
                .map(|m| m.map_or(u32::MAX, |m| u32::from(m.0))),
        );
    }
    append(&mut out, world.grid.origin);
    out.extend(world.grid.shifts);
    for cell in &world.grid.cells {
        list(&mut out, cell);
    }
    list(&mut out, &methods.store.ticks.order);
    out
}
fn output(
    world: &World,
    vehicle: &State,
    shared: &Shared,
    methods: &Methods,
    status: i32,
    result: u32,
) -> Vec<u32> {
    let mut out = vec![
        status as u32,
        result,
        shared.random,
        shared.current_slot,
        methods.probe.calls,
        methods.probe.trace,
    ];
    append(&mut out, vehicle.velocity);
    out.extend([
        vehicle.forward_velocity as u32,
        u32::from(vehicle.player),
        vehicle.impact_accumulator as u32,
        vehicle.collision_size,
    ]);
    for i in 0..3 {
        let p = &world.objects[i];
        let s = methods.store.states[i].as_ref().unwrap();
        out.extend([
            p.activity as u32,
            p.flags,
            s.angular_velocity as u32,
            s.rotation as u32,
            s.turn_rate as u32,
            s.last_vehicle.map_or(0, |id| id.0),
            methods.store.ticks.flags[i],
            s.effect_flags.map_or(u32::MAX, u32::from),
        ]);
    }
    list(&mut out, &methods.store.ticks.order);
    for cell in &world.grid.cells {
        list(&mut out, cell);
    }
    out
}
#[test]
#[ignore = "requires C breakable oracle; set TANKRACER_BREAKABLE_REFERENCE"]
fn real_hit_activation_spatial_removal_nested_chains_and_failed_prefixes_match_c() {
    let mut rng = Random(0x91281fa1);
    let (mut input, mut expected, mut records) =
        (Vec::new(), Vec::new(), Vec::<(Range<usize>, String)>::new());
    let (mut errors, mut callbacks, mut active, mut hits) = (0, 0, 0, 0);
    for op in 1..=2 {
        for case in 0..512 {
            let colliders = (0..3)
                .map(|i| Collider {
                    entity: EntityId(i + 2),
                    pose: Pose {
                        position: [i as i32 * 10000, 0, 0],
                        ..Pose::default()
                    },
                    external_basis: Some(tankracer_game::math::Matrix::IDENTITY),
                    radius: rng.choose(&[64, 128, 512, 65535]) as u16,
                    bounds: Bounds {
                        min: [-3000; 3],
                        max: [3000; 3],
                    },
                })
                .collect();
            let objects = (0..3)
                .map(|i| Object {
                    class: ClassId(i),
                    weight: rng.choose(&[0, 64, 128]),
                    threshold: 0,
                    activity: rng.choose(&[0, 0, 0, 1, -1]),
                    flags: rng.choose(&[0, 0, 2, 4, 8, 16, 32, 1]) as u32,
                    mesh_bounds: [[-500; 3], [500; 3]],
                })
                .collect();
            let classes = (0..3)
                .map(|_| {
                    Arc::new(Class {
                        maximum_slot: rng.choose(&[-1, 0, 1, 2, 3]),
                        methods: vec![
                            match rng.word() % 5 {
                                0 => None,
                                1 => Some(MethodId(3)),
                                _ => Some(ENABLE_TICK),
                            },
                            Some(MethodId(3)),
                            Some(MethodId(4)),
                            if rng.word().is_multiple_of(5) {
                                None
                            } else {
                                Some(BREAKABLE_HIT)
                            },
                        ],
                    })
                })
                .collect();
            let mut world = World {
                colliders,
                objects,
                classes,
                grid: Grid {
                    origin: [-5000; 2],
                    shifts: [12; 2],
                    cells: std::array::from_fn(|_| Vec::new()),
                },
                deformables: Vec::new(),
            };
            let mut store = Store {
                states: (0..3)
                    .map(|i| {
                        Some(breakable::State {
                            angular_velocity: rng.signed(),
                            rotation: rng.signed(),
                            turn_rate: rng.signed(),
                            last_vehicle: Some(EntityId(7)),
                            has_sound: case % 3 != 0,
                            auxiliary: case % 3 == 0,
                            effect_flags: if case % 4 == 0 {
                                None
                            } else {
                                Some(rng.word() as u8)
                            },
                            cell: if case % 7 == 0 {
                                None
                            } else {
                                Some(Cell {
                                    x: rng.choose(&[-1, 0, 1, 7, 8]) as i16,
                                    z: rng.choose(&[0, 1, 7, 8, 65535]) as u16,
                                })
                            },
                            attached: vec![
                                Some(EntityId((i + 1) % 3 + 2)),
                                None,
                                Some(EntityId(i + 2)),
                                Some(EntityId(5)),
                            ]
                            .into(),
                        })
                    })
                    .collect(),
                live: vec![true, case % 5 != 0, case % 7 != 0],
                ticks: Ticks {
                    flags: vec![0; 3],
                    order: Vec::new(),
                    next: None,
                },
            };
            let env = HitEnvironment {
                minimum_turn_rate: rng.choose(&[i32::MIN, -100, 0, 100, 100000]),
                maximum_turn_rate: rng.choose(&[-100, 0, 100, 100000, i32::MAX]),
                activate_slot: 0,
                auxiliary_slot: 1,
                effect_slot: 2,
                collision_slot: 3,
                feedback: case % 3 != 0,
                work_limit: rng.choose(&[0, 1, 2, 4, 16, 100]) as u32,
            };
            if case % 16 == 1 {
                store.live.fill(true);
                for i in 0..3 {
                    world.objects[i].activity = 0;
                    world.objects[i].flags = 0;
                    world.classes[i] = Arc::new(Class {
                        maximum_slot: 3,
                        methods: vec![Some(ENABLE_TICK), None, None, Some(BREAKABLE_HIT)],
                    });
                    let s = store.states[i].as_mut().unwrap();
                    s.has_sound = false;
                    s.auxiliary = false;
                    s.effect_flags = None;
                }
            }
            if op == 1 && case == 0 {
                world.objects[0].activity = 0;
                world.objects[0].flags = 0;
                world.classes[0] = Arc::new(Class {
                    maximum_slot: 3,
                    methods: vec![Some(ENABLE_TICK), None, None, Some(BREAKABLE_HIT)],
                });
                let s = store.states[0].as_mut().unwrap();
                s.has_sound = false;
                s.auxiliary = false;
                s.effect_flags = None;
                s.attached = vec![None; 65537].into();
            }
            for cell in &mut world.grid.cells {
                *cell = (0..(rng.word() % 5))
                    .map(|_| ObstacleId((rng.word() % 3) as usize))
                    .filter(|id| store.live[id.0])
                    .collect();
            }
            world.grid.cells[8] = vec![ObstacleId(0)];
            let env = if case % 16 == 1 || op == 1 && case == 0 {
                HitEnvironment {
                    work_limit: 100,
                    ..env
                }
            } else {
                env
            };
            let probe = Probe {
                calls: 0,
                trace: 0,
                fail_at: rng.word() % 7,
                fail_status: rng.choose(&[-13, 0, 1]),
                delta: rng.choose(&[-1, 0, 1, 100000, i32::MAX]),
                actions: rng.word() % 4,
            };
            let mut methods = Methods { store, env, probe };
            let mut vehicle = State {
                entity: EntityId(1),
                class: ClassId(3),
                base_position: [0; 3],
                position: [-4000, 0, 0],
                previous_position: [-8000, 0, 0],
                velocity: if case % 7 == 0 {
                    [i32::MIN, i32::MAX, -1]
                } else {
                    [rng.signed() % 1000000, 0, rng.signed() % 1000000]
                },
                forward_velocity: rng.choose(&[i32::MIN, i32::MAX, -1, 0, 1, 250000, 500000]),
                player: case % 4 != 0,
                contacts: [[0; 3]; 4],
                impact_accumulator: rng.signed(),
                collision_size: rng.word(),
                material_tag: 0,
                deformable: None,
            };
            let mut shared = Shared {
                random: rng.word(),
                current_slot: rng.word(),
            };
            let collision_env = Environment {
                mode: 0,
                dt: 40,
                feedback: env.feedback,
                player_class: ClassId(3),
                breakable_class: ClassId(0),
                obstacle_slot: 3,
                deformable_slot: 3,
                tuning: Tuning::default(),
            };
            for tick in 0..4 {
                methods.probe.calls = 0;
                methods.probe.trace = 0;
                let mut words = vec![op];
                words.extend(encode(&world, &vehicle, &shared, &methods));
                input.extend(words.into_iter().flat_map(u32::to_le_bytes));
                let mut result = 0x7182;
                let status = if op == 1 {
                    match breakable::collide(
                        &mut methods.store,
                        Invocation {
                            dt: 40,
                            target: ObstacleId(0),
                            vehicle: &mut vehicle,
                            world: &mut world,
                            random: &mut shared.random,
                            current_slot: &mut shared.current_slot,
                            result: &mut result,
                        },
                        env,
                        &mut methods.probe,
                    ) {
                        Ok(()) => 0,
                        Err(code) => code,
                    }
                } else {
                    match obstacle::obstacles(
                        &mut world,
                        &mut vehicle,
                        collision_env,
                        &mut shared,
                        &mut methods,
                    ) {
                        Ok(hit) => {
                            hits += usize::from(hit.is_some());
                            0
                        }
                        Err(obstacle::Error::Dispatch(
                            tankracer_game::dispatch::Error::Method(code),
                        )) => code,
                        Err(obstacle::Error::Arithmetic(_)) => -2,
                        e => panic!("unexpected collision error {e:?}"),
                    }
                };
                if status != 0 {
                    errors += 1;
                }
                callbacks += methods.probe.calls;
                active += methods.store.ticks.order.len();
                let start = expected.len();
                expected.extend(output(&world, &vehicle, &shared, &methods, status, result));
                records.push((
                    start..expected.len(),
                    format!("op={op} case={case} tick={tick} env={env:?}"),
                ));
            }
        }
    }
    let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/breakable");
    fs::create_dir_all(&folder).unwrap();
    let path = folder.join("input.bin");
    fs::write(&path, input).unwrap();
    let result = Command::new(
        std::env::var_os("TANKRACER_BREAKABLE_REFERENCE")
            .expect("set TANKRACER_BREAKABLE_REFERENCE"),
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
    assert_eq!(&result.stdout[..4], b"BK01");
    let words: Vec<_> = result.stdout[4..]
        .chunks_exact(4)
        .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
        .collect();
    assert_eq!(words.len(), expected.len());
    for (range, label) in &records {
        assert_eq!(words[range.clone()], expected[range.clone()], "{label}");
    }
    assert!(errors > 0 && callbacks > 0 && active > 0 && hits > 0);
    eprintln!(
        "{} breakable/collision records / {} words exact; errors={errors} callbacks={callbacks} active observations={active} collision hits={hits}",
        records.len(),
        words.len()
    );
}
