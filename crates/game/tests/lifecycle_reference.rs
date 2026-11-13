use std::{
    fs,
    ops::Range,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_game::{
    dispatch::{self, Class, MethodId},
    lifecycle::{Error, Event, Queues, Ticks},
    object_query::EntityId,
    spatial,
};

#[derive(Clone, Default)]
struct Fixture {
    ticks: Vec<u32>,
    notifications: Vec<u32>,
    destruction: Vec<u32>,
    actions: Vec<[u32; 4]>,
    failure: [u32; 2],
    steps: Vec<[u32; 3]>,
}
struct Calls {
    actions: Vec<[u32; 4]>,
    failure: [u32; 2],
    trace: Vec<[u32; 3]>,
    current_slot: u32,
    live: [bool; 7],
    records: u32,
}
fn code(error: Error<EntityId>) -> i32 {
    match error {
        Error::Method(v) => v,
        Error::Spatial(spatial::Error::InvalidLimit) => -1,
        Error::Spatial(spatial::Error::Limit) => -3,
        v => panic!("invalid oracle fixture {v:?}"),
    }
}
impl Calls {
    fn operation(
        &mut self,
        ticks: &mut Ticks<EntityId>,
        queues: &mut Queues<EntityId>,
        op: [u32; 3],
    ) -> Result<(), i32> {
        let [op, id, limit] = op;
        let id = EntityId(id);
        match op {
            1 if limit == 0 => Err(-1),
            1 => ticks.enable(id).map(|_| ()).map_err(code),
            2 => ticks.disable(id, limit).map(|_| ()).map_err(code),
            3 | 4 => queues.queue(ticks, id, op == 4, limit).map_err(code),
            5 => queues.cancel_notification(ticks, id, limit).map_err(code),
            6 => ticks
                .run(limit, |ticks, id| self.invoke(ticks, queues, 0, id))
                .map_err(code),
            7 => queues
                .drain(ticks, limit, |queues, ticks, event| match event {
                    Event::Stop(id) => self.invoke(ticks, queues, 1, id),
                    Event::Destroy(id) => {
                        if self.live[id.0 as usize] {
                            self.invoke(ticks, queues, 2, id)?;
                            self.invoke(ticks, queues, 3, id)?;
                            self.records -= 1;
                        }
                        Ok(())
                    }
                })
                .map_err(code),
            _ => unreachable!(),
        }
    }
    fn invoke(
        &mut self,
        ticks: &mut Ticks<EntityId>,
        queues: &mut Queues<EntityId>,
        kind: u32,
        id: EntityId,
    ) -> Result<(), i32> {
        let class = Class {
            maximum_slot: 3,
            methods: (0..4).map(|v| Some(MethodId(v))).collect(),
        };
        let mut slot = self.current_slot;
        let mut output = 0;
        let status = dispatch::call_entity(
            &class,
            kind,
            &mut slot,
            &mut output,
            |method, slot, result| {
                self.trace.push([u32::from(method.0), id.0, *slot]);
                let ordinal = self.trace.len() as u32;
                for index in 0..self.actions.len() {
                    let [trigger, op, target, limit] = self.actions[index];
                    if trigger == ordinal {
                        self.operation(ticks, queues, [op, target, limit])?;
                    }
                }
                if ordinal == self.failure[0] {
                    return Err(self.failure[1] as i32);
                }
                if kind == 3 {
                    self.live[id.0 as usize] = false;
                }
                *result = id.0;
                Ok(())
            },
        );
        self.current_slot = slot;
        status.map_err(|v| match v {
            dispatch::Error::Method(v) => v,
            _ => panic!("invalid slot"),
        })
    }
}
fn list(out: &mut Vec<u32>, ids: impl IntoIterator<Item = u32>) {
    let start = out.len();
    out.push(0);
    out.extend(ids);
    out[start] = (out.len() - start - 1) as u32;
}
fn snapshot(
    out: &mut Vec<u32>,
    status: i32,
    calls: &Calls,
    ticks: &Ticks<EntityId>,
    queues: &Queues<EntityId>,
) {
    out.extend([status as u32, calls.current_slot, calls.records]);
    for id in 1..=6 {
        out.extend([ticks.flags[id], u32::from(calls.live[id])]);
    }
    out.push(ticks.next.map_or(0, |id| id.0));
    list(out, ticks.order.iter().map(|id| id.0));
    list(out, queues.notifications.iter().map(|id| id.0));
    list(out, queues.destruction.iter().map(|id| id.0));
    out.push(calls.trace.len() as u32);
    out.extend(calls.trace.iter().flatten());
}
fn fixtures() -> Vec<Fixture> {
    let mut fixtures = vec![
        Fixture {
            ticks: vec![1, 2, 3],
            actions: vec![[1, 2, 2, 10], [1, 1, 4, 10], [2, 2, 1, 10]],
            steps: vec![[6, 0, 20], [6, 0, 20]],
            ..Fixture::default()
        },
        // Appending in the last callback must wait until the following pass.
        Fixture {
            ticks: vec![1, 2, 3],
            actions: vec![[3, 1, 4, 10]],
            steps: vec![[6, 0, 20], [6, 0, 20]],
            ..Fixture::default()
        },
        Fixture {
            ticks: vec![1, 2, 3],
            actions: vec![[1, 2, 2, 10], [1, 1, 2, 10]],
            steps: vec![[6, 0, 20]],
            ..Fixture::default()
        },
        Fixture {
            steps: vec![
                [3, 1, 10],
                [4, 1, 10],
                [3, 2, 10],
                [3, 1, 10],
                [5, 1, 1],
                [4, 1, 10],
                [7, 0, 20],
            ],
            ..Fixture::default()
        },
        // Destruction-phase notifications stay queued until the next drain.
        Fixture {
            destruction: vec![1],
            actions: vec![[2, 3, 6, 10]],
            steps: vec![[7, 0, 20], [7, 0, 20]],
            ..Fixture::default()
        },
        // A callback can requeue its already-popped notification.
        Fixture {
            notifications: vec![1, 2],
            actions: vec![[1, 3, 1, 10]],
            steps: vec![[7, 0, 20]],
            ..Fixture::default()
        },
        // Clearing bit 32 happens even when the cancellation search then fails.
        Fixture {
            notifications: vec![1, 2, 3],
            steps: vec![[5, 3, 1], [4, 3, 10], [7, 0, 20]],
            ..Fixture::default()
        },
    ];
    for limit in 0..=8 {
        for ticks in [vec![], vec![1, 2, 3]] {
            fixtures.push(Fixture {
                ticks,
                steps: vec![[6, 0, limit]],
                ..Fixture::default()
            });
        }
        for notifications in [vec![], vec![1, 2, 3]] {
            fixtures.push(Fixture {
                notifications,
                destruction: vec![4, 5],
                steps: vec![[7, 0, limit]],
                ..Fixture::default()
            });
        }
    }
    for failure_at in 1..=5 {
        for failure in [1, -7, -19] {
            fixtures.push(Fixture {
                ticks: vec![1, 2, 3],
                failure: [failure_at, failure as u32],
                steps: vec![[6, 0, 20], [6, 0, 20]],
                ..Fixture::default()
            });
            fixtures.push(Fixture {
                notifications: vec![1],
                destruction: vec![2, 3],
                failure: [failure_at, failure as u32],
                steps: vec![[7, 0, 20]],
                ..Fixture::default()
            });
        }
    }
    let mut rng = 0x3482a719_u32;
    let mut word = || {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        rng
    };
    for _ in 0..512 {
        let mut f = Fixture::default();
        for id in 1..=6 {
            if word() & 1 != 0 {
                f.ticks.push(id);
            }
            if word() & 3 == 0 {
                f.notifications.push(id);
            }
        }
        for _ in 0..12 {
            f.actions.push([
                1 + word() % 10,
                [1, 2, 3, 5][word() as usize % 4],
                1 + word() % 6,
                word() % 9,
            ]);
        }
        for _ in 0..12 {
            f.steps.push([
                [1, 2, 3, 5, 6][word() as usize % 5],
                1 + word() % 6,
                word() % 9,
            ]);
        }
        f.steps.extend([[4, 6, 20], [7, 0, 30]]);
        fixtures.push(f);
    }
    fixtures
}

#[test]
#[ignore = "requires C lifecycle oracle; set TANKRACER_LIFECYCLE_REFERENCE"]
fn live_tick_mutation_deferred_order_and_failure_prefixes_match_c() {
    let fixtures = fixtures();
    let mut input = Vec::<u32>::new();
    let mut expected = Vec::new();
    let mut ranges = Vec::<(Range<usize>, String)>::new();
    let (mut errors, mut methods, mut destructions) = (0, 0, 0);
    for (case, fixture) in fixtures.iter().enumerate() {
        let mut ticks = Ticks::<EntityId> {
            flags: vec![8; 7],
            order: fixture.ticks.iter().copied().map(EntityId).collect(),
            next: None,
        };
        let mut queues = Queues {
            notifications: fixture
                .notifications
                .iter()
                .copied()
                .map(EntityId)
                .collect(),
            destruction: fixture.destruction.iter().copied().map(EntityId).collect(),
        };
        for &id in &ticks.order {
            ticks.flags[id.0 as usize] |= 16;
        }
        for &id in queues.notifications.iter().chain(&queues.destruction) {
            ticks.flags[id.0 as usize] |= 32;
        }
        let mut calls = Calls {
            actions: fixture.actions.clone(),
            failure: fixture.failure,
            trace: Vec::new(),
            current_slot: 9,
            live: [true; 7],
            records: 6,
        };
        input.push(fixture.steps.len() as u32);
        input.extend(&ticks.flags[1..]);
        list(&mut input, fixture.ticks.iter().copied());
        list(&mut input, fixture.notifications.iter().copied());
        list(&mut input, fixture.destruction.iter().copied());
        input.push(fixture.actions.len() as u32);
        input.extend(fixture.actions.iter().flatten());
        input.extend(fixture.failure);
        for (step, &op) in fixture.steps.iter().enumerate() {
            input.extend(op);
            let result = calls.operation(&mut ticks, &mut queues, op);
            errors += usize::from(result.is_err());
            let start = expected.len();
            snapshot(
                &mut expected,
                result.err().unwrap_or(0),
                &calls,
                &ticks,
                &queues,
            );
            ranges.push((
                start..expected.len(),
                format!("case {case}, step {step}, op {op:?}"),
            ));
        }
        methods += calls.trace.len();
        destructions += 6 - calls.records;
    }
    let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/lifecycle");
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
        std::env::var_os("TANKRACER_LIFECYCLE_REFERENCE")
            .expect("set TANKRACER_LIFECYCLE_REFERENCE"),
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
    assert_eq!(&output.stdout[..4], b"LC01");
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
            "first difference at {index} ({label}), record offset {}: C {:?}, Rust {:?}",
            index - range.start,
            &words[range.start..range.end.min(words.len())],
            &expected[range.clone()]
        );
    }
    assert_eq!(words.len(), expected.len());
    for (range, label) in ranges {
        assert_eq!(words[range.clone()], expected[range], "{label}");
    }
    assert!(methods > 1000 && destructions > 10 && errors > 100);
    eprintln!(
        "{} scenarios / {} words exact; {methods} callbacks, {destructions} released records, {errors} error prefixes",
        fixtures.len(),
        words.len()
    );
}
