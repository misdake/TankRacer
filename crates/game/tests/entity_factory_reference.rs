use std::{
    fs,
    path::PathBuf,
    process::{Command, Stdio},
    sync::Arc,
};
use tankracer_game::{
    dispatch::{Class, ClassId, MethodId},
    entities,
    entity_factory::{BaseMethod, Context, Environment, Error, Invocation, Methods, Slots, State},
    lifecycle,
    object_query::EntityId,
    spatial,
};
const UNCHANGED: u32 = 0x7182;
#[derive(Clone)]
struct Fixture {
    capacity: u32,
    actions: Vec<[u32; 4]>,
    failure: [u32; 2],
    steps: Vec<[u32; 3]>,
}
impl Fixture {
    fn new(capacity: u32, steps: Vec<[u32; 3]>) -> Self {
        Self {
            capacity,
            steps,
            actions: Vec::new(),
            failure: [0; 2],
        }
    }
}
#[derive(Default)]
struct Calls {
    objects: u32,
    trace: Vec<[u32; 4]>,
    actions: Vec<[u32; 4]>,
    failure: [u32; 2],
}
fn code(error: Error) -> i32 {
    match error {
        Error::Entities(entities::Error::InvalidLimit) => -1,
        Error::Entities(entities::Error::Limit) => -3,
        Error::Lifecycle(lifecycle::Error::Spatial(spatial::Error::InvalidLimit)) => -1,
        Error::Lifecycle(lifecycle::Error::Spatial(spatial::Error::Limit)) => -3,
        Error::InvalidReservation(_) => -4,
        Error::Dispatch(tankracer_game::dispatch::Error::Method(code)) => code,
        v => panic!("invalid oracle input: {v:?}"),
    }
}
fn operation(
    context: &mut Context<'_, u32>,
    calls: &mut Calls,
    env: Environment<'_>,
    op: [u32; 3],
) -> Result<u32, Error> {
    let [op, a, b] = op;
    let id = EntityId(a);
    match op {
        1 => context.entities.reset(a).map_err(Error::Entities)?,
        2 => context.entities.grow().map_err(Error::Entities)?,
        3 => {
            return context
                .entities
                .new_record(id, env.work_limit)
                .map(|v| v.0)
                .map_err(Error::Entities);
        }
        4 => {
            return context
                .entities
                .unlink_free(id, b)
                .map(|v| v.0)
                .map_err(Error::Entities);
        }
        5 => context.entities.release_id(id).map_err(Error::Entities)?,
        6 => {
            context.entities.detach(id).map_err(Error::Entities)?;
            return Ok(1);
        }
        7 => return context.create(ClassId(a as usize), env, calls).map(|v| v.0),
        8 => context.fill_reserved(id, ClassId(b as usize), env, calls)?,
        9 => context.destroy(id, env, calls)?,
        10 => {
            context.entities.sequential = b != 0;
            context.entities.next_requested = id;
        }
        11 if b == 0 => return Err(Error::Entities(entities::Error::InvalidLimit)),
        11 => {
            return context
                .ticks
                .enable(id)
                .map(u32::from)
                .map_err(Error::Lifecycle);
        }
        12 => {
            return context
                .ticks
                .disable(id, b)
                .map(u32::from)
                .map_err(Error::Lifecycle);
        }
        13 | 14 => context
            .queues
            .queue(context.ticks, id, op == 14, b)
            .map_err(Error::Lifecycle)?,
        15 => context
            .queues
            .cancel_notification(context.ticks, id, b)
            .map_err(Error::Lifecycle)?,
        16 => context.tick(
            1,
            Environment {
                work_limit: b,
                ..env
            },
            calls,
        )?,
        17 => context.drain(
            2,
            Environment {
                work_limit: b,
                ..env
            },
            calls,
        )?,
        18 => {
            let method = [
                BaseMethod::Identity,
                BaseMethod::Zero,
                BaseMethod::One,
                BaseMethod::EnableTick,
                BaseMethod::DisableTick,
                BaseMethod::QueueDestroy,
                BaseMethod::ReleaseId,
                BaseMethod::Detach,
            ][b as usize];
            let mut result = UNCHANGED;
            method
                .invoke(
                    Invocation {
                        context: Context {
                            entities: context.entities,
                            symbols: context.symbols,
                            ticks: context.ticks,
                            queues: context.queues,
                            current_slot: context.current_slot,
                        },
                        entity: id,
                        payload: None,
                        result: &mut result,
                    },
                    env,
                )
                .map_err(|code| Error::Dispatch(tankracer_game::dispatch::Error::Method(code)))?;
            return Ok(result);
        }
        _ => unreachable!(),
    }
    Ok(UNCHANGED)
}
impl Methods<u32> for Calls {
    fn invoke(
        &mut self,
        method: MethodId,
        mut call: Invocation<'_, u32>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        let id = call.entity;
        let kind = method.0 % 4;
        self.trace.push([
            u32::from(method.0),
            id.0,
            *call.context.current_slot,
            call.context
                .entities
                .record(id)
                .unwrap()
                .object
                .unwrap_or(0),
        ]);
        let ordinal = self.trace.len() as u32;
        if kind == 0 {
            self.objects += 1;
            call.context.entities.record_mut(id).unwrap().object = Some(self.objects);
            call.context
                .ticks
                .flags
                .resize(call.context.entities.capacity() as usize, 0);
            call.context.ticks.flags[id.0 as usize] = 0;
        }
        for index in 0..self.actions.len() {
            let [trigger, op, a, b] = self.actions[index];
            if trigger == ordinal {
                operation(&mut call.context, self, env, [op, a, b]).map_err(code)?;
            }
        }
        *call.context.current_slot = call.context.current_slot.wrapping_add(5);
        if ordinal == self.failure[0] {
            return Err(self.failure[1] as i32);
        }
        if kind == 3 {
            call.context
                .entities
                .release_id(id)
                .map_err(Error::Entities)
                .map_err(code)?;
        }
        *call.result = id.0;
        Ok(())
    }
}
fn classes() -> Vec<Arc<Class>> {
    (0..5)
        .map(|class| {
            Arc::new(Class {
                maximum_slot: if class == 1 { 0 } else { 3 },
                methods: (0..4)
                    .map(|kind| {
                        if matches!((class, kind), (2, 0) | (3, 1 | 3) | (4, 2)) {
                            None
                        } else {
                            Some(MethodId(class * 4 + kind))
                        }
                    })
                    .collect(),
            })
        })
        .collect()
}
fn env(classes: &[Arc<Class>]) -> Environment<'_> {
    Environment {
        classes,
        slots: Slots {
            allocate: 0,
            initialize: 1,
            destroy: 2,
            release: 3,
        },
        work_limit: 100000,
    }
}
fn fixtures() -> Vec<Fixture> {
    let mut result = vec![
        Fixture::new(
            4,
            vec![
                [3, 0, 0],
                [3, 0, 0],
                [5, 1, 0],
                [3, 0, 0],
                [3, 3, 0],
                [3, 0, 0],
            ],
        ),
        Fixture::new(
            4,
            vec![
                [2, 0, 0],
                [3, 100, 0],
                [3, 0, 0],
                [3, 0, 0],
                [3, 0, 0],
                [3, 0, 0],
            ],
        ),
        Fixture::new(
            4,
            vec![
                [3, 200, 0],
                [3, 0, 0],
                [3, 200, 0],
                [6, 200, 0],
                [3, 200, 0],
            ],
        ),
        Fixture::new(
            8,
            vec![
                [4, 0, 1],
                [4, 5, 1],
                [4, 5, 2],
                [4, 5, 3],
                [4, 5, 5],
                [3, 5, 0],
                [3, 5, 0],
                [4, 1, 0],
                [5, 5, 0],
                [3, 0, 0],
            ],
        ),
        Fixture::new(
            4,
            vec![
                [10, 20, 1],
                [7, 0, 0],
                [10, 20, 0],
                [7, 0, 0],
                [10, 0, 1],
                [7, 0, 0],
                [7, 0, 0],
            ],
        ),
        Fixture::new(
            4,
            vec![
                [3, 3, 0],
                [8, 3, 0],
                [8, 3, 0],
                [9, 3, 0],
                [7, 0, 0],
                [8, 0, 0],
                [8, 1000, 0],
            ],
        ),
        Fixture::new(
            8,
            vec![
                [7, 0, 0],
                [7, 1, 0],
                [7, 2, 0],
                [7, 3, 0],
                [7, 4, 0],
                [9, 1, 0],
                [9, 2, 0],
                [9, 3, 0],
                [9, 4, 0],
                [9, 5, 0],
            ],
        ),
        Fixture::new(
            4,
            vec![
                [7, 0, 0],
                [7, 0, 0],
                [1, 4, 0],
                [3, 0, 0],
                [1, 8, 0],
                [3, 0, 0],
            ],
        ),
    ];
    for failure_at in 1..=4 {
        for failure in [-13, 1] {
            let mut fixture = Fixture::new(
                4,
                vec![[10, 2, 1], [7, 0, 0], [8, 2, 0], [9, 2, 0], [7, 0, 0]],
            );
            fixture.failure = [failure_at, failure as u32];
            result.push(fixture);
        }
    }
    let mut nested = Fixture::new(4, vec![[7, 0, 0], [9, 1, 0], [7, 0, 0]]);
    nested.actions = vec![[1, 7, 1, 0], [2, 7, 0, 0], [6, 7, 0, 0]];
    result.push(nested);
    let mut ticking = Fixture::new(
        8,
        vec![
            [7, 0, 0],
            [7, 0, 0],
            [7, 0, 0],
            [7, 0, 0],
            [11, 1, 20],
            [11, 2, 20],
            [11, 3, 20],
            [16, 0, 20],
            [16, 0, 20],
        ],
    );
    ticking.actions = vec![[9, 12, 2, 20], [9, 11, 4, 20], [10, 12, 1, 20]];
    result.push(ticking);
    for failure in [0, 1, -13] {
        let mut draining = Fixture::new(
            8,
            vec![
                [7, 0, 0],
                [7, 0, 0],
                [7, 0, 0],
                [13, 1, 20],
                [14, 1, 20],
                [14, 2, 20],
                [17, 0, 20],
                [17, 0, 20],
            ],
        );
        draining.actions = vec![[9, 13, 3, 20]];
        if failure != 0 {
            draining.failure = [9, failure as u32];
        }
        result.push(draining);
    }
    for limit in 0..=4 {
        let fixture = Fixture::new(
            4,
            vec![
                [7, 0, 0],
                [7, 0, 0],
                [11, 1, 20],
                [11, 2, 20],
                [16, 0, limit],
                [14, 1, 20],
                [17, 0, limit],
            ],
        );
        result.push(fixture);
    }
    result.push(Fixture::new(
        8,
        vec![
            [7, 0, 0],
            [7, 0, 0],
            [18, 1, 0],
            [18, 1, 1],
            [18, 1, 2],
            [18, 1, 3],
            [18, 1, 3],
            [18, 1, 4],
            [18, 1, 5],
            [18, 1, 5],
            [15, 1, 20],
            [18, 2, 6],
            [3, 0, 0],
            [18, 2, 7],
        ],
    ));
    let registry = classes();
    let env = env(&registry);
    let mut random = 0x9c82751a_u32;
    let mut word = || {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        random
    };
    for case in 0..128 {
        let mut fixture = Fixture::new([3, 8, 32][case % 3], Vec::new());
        let mut state = State::new(fixture.capacity).unwrap();
        state.current_slot = 9;
        let mut context = state.context();
        let mut calls = Calls::default();
        for _ in 0..12 {
            let records: Vec<_> = (1..context.entities.capacity())
                .filter(|&id| context.entities.record(EntityId(id)).is_some())
                .collect();
            let initialized: Vec<_> = records
                .iter()
                .copied()
                .filter(|&id| {
                    context
                        .entities
                        .record(EntityId(id))
                        .unwrap()
                        .class
                        .is_some()
                })
                .collect();
            let op = match word() % 7 {
                0 => [3, if word() & 1 == 0 { 0 } else { 1 + word() % 150 }, 0],
                1 if !records.is_empty() => [5, records[word() as usize % records.len()], 0],
                2 if !records.is_empty() => [8, records[word() as usize % records.len()], 0],
                3 if !initialized.is_empty() => {
                    [9, initialized[word() as usize % initialized.len()], 0]
                }
                4 => [4, word() % context.entities.capacity(), 1 + word() % 5],
                5 if !records.is_empty() => [6, records[word() as usize % records.len()], 0],
                _ => [7, [0, 2, 4][word() as usize % 3], 0],
            };
            // Use the model only to keep subsequent inputs within valid records;
            // every selected operation and full state is then compared with C.
            let _ = operation(&mut context, &mut calls, env, op);
            fixture.steps.push(op);
        }
        result.push(fixture);
    }
    result
}
fn snapshot(
    out: &mut Vec<u32>,
    context: &Context<'_, u32>,
    calls: &Calls,
    status: i32,
    result: u32,
) {
    let entities = &context.entities;
    out.extend([
        status as u32,
        result,
        entities.capacity(),
        entities.record_count(),
        entities.free_head().0,
        entities.high_water().0,
        u32::from(entities.sequential),
        entities.next_requested.0,
        *context.current_slot,
        calls.objects,
    ]);
    for index in 0..entities.capacity() {
        let id = EntityId(index);
        let record = entities.record(id);
        out.extend([
            u32::from(record.is_some()),
            record.and_then(|v| v.class).map_or(0, |v| v.0 as u32 + 1),
            record.and_then(|v| v.object).unwrap_or(0),
            entities.next_free(id).unwrap().0,
            record
                .and_then(|v| v.object)
                .map_or(u32::MAX, |_| context.ticks.flags[index as usize]),
        ]);
    }
    out.push(context.ticks.next.map_or(0, |id| id.0));
    for list in [
        &context.ticks.order,
        &context.queues.notifications.iter().copied().collect(),
        &context.queues.destruction.iter().copied().collect(),
    ] {
        out.push(list.len() as u32);
        out.extend(list.iter().map(|id| id.0));
    }
    out.push(calls.trace.len() as u32);
    out.extend(calls.trace.iter().flatten());
}
#[test]
#[ignore = "requires C entity factory oracle; set TANKRACER_ENTITY_FACTORY_REFERENCE"]
fn identity_reuse_requested_growth_recursive_initialization_and_failure_prefixes_match_c() {
    let fixtures = fixtures();
    let registry = classes();
    let env = env(&registry);
    let (mut input, mut expected, mut ranges) = (Vec::<u32>::new(), Vec::new(), Vec::new());
    let (mut errors, mut callbacks, mut records) = (0, 0, 0);
    for (case, fixture) in fixtures.iter().enumerate() {
        let mut state = State::new(fixture.capacity).unwrap();
        state.current_slot = 9;
        let mut context = state.context();
        let mut calls = Calls {
            actions: fixture.actions.clone(),
            failure: fixture.failure,
            ..Calls::default()
        };
        input.extend([
            fixture.steps.len() as u32,
            fixture.capacity,
            fixture.actions.len() as u32,
        ]);
        input.extend(fixture.actions.iter().flatten());
        input.extend(fixture.failure);
        for (step, &op) in fixture.steps.iter().enumerate() {
            input.extend(op);
            let result = operation(&mut context, &mut calls, env, op);
            let (status, result) = match result {
                Ok(v) => (0, v),
                Err(error) => {
                    errors += 1;
                    (code(error), UNCHANGED)
                }
            };
            let start = expected.len();
            snapshot(&mut expected, &context, &calls, status, result);
            ranges.push((
                start..expected.len(),
                format!("case {case}, step {step}, op {op:?}"),
            ));
            records += 1;
        }
        callbacks += calls.trace.len();
    }
    let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/entity-factory");
    fs::create_dir_all(&folder).unwrap();
    let path = folder.join("input.bin");
    fs::write(
        &path,
        input
            .iter()
            .flat_map(|v| v.to_le_bytes())
            .collect::<Vec<_>>(),
    )
    .unwrap();
    let output = Command::new(
        std::env::var_os("TANKRACER_ENTITY_FACTORY_REFERENCE")
            .expect("set TANKRACER_ENTITY_FACTORY_REFERENCE"),
    )
    .stdin(Stdio::from(fs::File::open(path).unwrap()))
    .output()
    .unwrap();
    assert!(
        output.status.success(),
        "{:?}: {}",
        output.status.code(),
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(&output.stdout[..4], b"EF01");
    let words: Vec<_> = output.stdout[4..]
        .chunks_exact(4)
        .map(|v| u32::from_le_bytes(v.try_into().unwrap()))
        .collect();
    if let Some(index) = words.iter().zip(&expected).position(|(a, b)| a != b) {
        let (range, label) = ranges
            .iter()
            .find(|(range, _)| range.contains(&index))
            .unwrap();
        panic!(
            "first difference {label}, offset {}: C {}, Rust {}",
            index - range.start,
            words[index],
            expected[index]
        );
    }
    assert_eq!(words.len(), expected.len());
    assert!(errors > 10 && callbacks > 100 && records > 1000);
    eprintln!(
        "{} scenarios / {records} operations / {} words exact; {callbacks} callbacks, {errors} error prefixes",
        fixtures.len(),
        words.len()
    );
}
