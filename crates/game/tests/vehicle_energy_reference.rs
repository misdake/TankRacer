//! Production temporary-line and shield bootstrap/tick parity, sharing one RNG.
use std::{
    fs,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_game::{
    effect_pool::{self, LineHandle, Pool, Quad},
    math::Matrix,
    vehicle_scene::{shield, temporary},
};
struct Fixture {
    temporary: temporary::State,
    shield: shield::State,
    pool: Pool,
    timer: i16,
    slot: i16,
    random: u32,
}
fn append(out: &mut Vec<u32>, values: impl IntoIterator<Item = i32>) {
    out.extend(values.into_iter().map(|v| v as u32));
}
impl Fixture {
    fn words(&self) -> Vec<u32> {
        let mut out = vec![
            u32::from(self.timer as u16),
            u32::from(self.slot as u16),
            self.random,
            self.temporary.available,
        ];
        for slot in 0..4 {
            out.push(self.temporary.timestamps[slot]);
            for row in 0..3 {
                let r = self.temporary.rows[slot][row];
                append(&mut out, r.first);
                append(&mut out, r.last);
                out.extend([
                    u32::from_le_bytes(r.colors),
                    u32::from(r.phases[0]) | (u32::from(r.phases[1]) << 16),
                ]);
                out.extend(
                    self.temporary.lines[slot][row * 3..row * 3 + 3]
                        .iter()
                        .map(|h| h.0),
                );
            }
        }
        for bank in &self.shield.banks {
            for o in bank.orbits {
                append(
                    &mut out,
                    [
                        o.pitch,
                        o.pitch_target,
                        o.yaw,
                        o.yaw_rate,
                        o.yaw_rate_target,
                        o.size,
                    ],
                );
                out.extend([u32::from(o.growing), u32::from(o.spin)]);
                append(&mut out, o.position);
            }
            out.push(bank.tail);
        }
        out.extend([self.pool.line_count, self.pool.quad_count]);
        for l in self.pool.lines.iter().chain([&self.pool.overflow]) {
            append(&mut out, l.points.into_iter().flatten());
            out.extend(l.colors.map(u32::from_le_bytes));
        }
        let q = self.pool.quads[159];
        append(&mut out, q.points.into_iter().flatten());
        out.push(q.material);
        out.extend(
            q.style
                .chunks_exact(4)
                .map(|b| u32::from_le_bytes(b.try_into().unwrap())),
        );
        out.extend(self.pool.exhausted_colors.map(u32::from_le_bytes));
        out
    }
    fn run(&mut self, op: Operation) -> Result<(), i32> {
        self.timer = op.timer;
        self.slot = op.slot;
        match op.kind {
            0 => {
                self.temporary.reset_timestamps();
                self.shield.initialize(&mut self.random);
                Ok(())
            }
            1 => self.temporary.start(
                self.timer,
                &mut self.slot,
                op.position,
                op.time,
                temporary::Context {
                    pool: &mut self.pool,
                    random: &mut self.random,
                },
            ),
            2 => self.temporary.tick(
                self.timer,
                self.slot,
                op.position,
                temporary::Context {
                    pool: &mut self.pool,
                    random: &mut self.random,
                },
            ),
            3 => self.temporary.expire(self.slot, &mut self.pool),
            4 => self.shield.tick(
                shield::Input {
                    enabled: op.enabled,
                    bank: if op.player {
                        1
                    } else if op.network {
                        op.network_bank
                    } else {
                        0
                    },
                    dt: op.dt,
                    position: op.position,
                    basis: op.basis,
                },
                &mut self.random,
            ),
            _ => unreachable!(),
        }
    }
}
#[derive(Clone, Copy)]
struct Operation {
    kind: u32,
    timer: i16,
    slot: i16,
    position: [i32; 3],
    time: u32,
    dt: i32,
    enabled: bool,
    player: bool,
    network: bool,
    network_bank: usize,
    basis: Option<Matrix>,
}
impl Operation {
    fn words(self) -> Vec<u32> {
        let mut out = vec![
            self.kind,
            u32::from(self.timer as u16),
            u32::from(self.slot as u16),
        ];
        append(&mut out, self.position);
        out.extend([
            self.time,
            self.dt as u32,
            u32::from(self.enabled),
            u32::from(self.player),
            u32::from(self.network),
            self.network_bank as u32,
            u32::from(self.basis.is_some()),
        ]);
        append(&mut out, self.basis.unwrap_or_default().0);
        out
    }
}
fn fixture(case: usize) -> Fixture {
    let mut pool = Pool {
        line_count: [0, 91, 100, 99][case % 4],
        ..Pool::default()
    };
    for (i, line) in pool.lines.iter_mut().enumerate() {
        line.points = [[i as i32 * -17, i as i32 + 11, i32::MIN]; 2];
        line.colors = [[i as u8; 4], [255 - i as u8; 4]];
        if i >= [0, 91, 100, 100][case % 4] {
            line.points[0][1] = effect_pool::FREE;
        }
    }
    pool.overflow.points = [[12, 34, 56]; 2];
    pool.quads[159] = Quad {
        points: [[100, 200, 300]; 4],
        material: 1234,
        style: [89; 12],
    };
    pool.exhausted_colors = [[123; 4], [234; 4]];
    let temporary = temporary::State {
        available: [0, 1, 3, u32::MAX][case / 2 % 4],
        timestamps: [[0, 20, 30, 40], [99, 20, 10, 10], [u32::MAX, 99, 1000, 0]][case / 4 % 3],
        lines: std::array::from_fn(|s| {
            std::array::from_fn(|i| {
                if case.is_multiple_of(23) && i == 1 {
                    LineHandle::RELEASED
                } else if case.is_multiple_of(7) && i == 0 {
                    LineHandle::EXHAUSTED
                } else if case.is_multiple_of(11) {
                    LineHandle::OVERFLOW
                } else {
                    LineHandle(((s * 12 + i) % 100) as u32)
                }
            })
        }),
        rows: std::array::from_fn(|s| {
            std::array::from_fn(|r| temporary::Row {
                first: [s as i32 * 100, r as i32 * -10, 5678],
                last: [999, 888, 777],
                colors: [1, 2, 3, 4],
                phases: [123, 456],
            })
        }),
    };
    let mut shield = shield::State::default();
    for (i, bank) in shield.banks.iter_mut().enumerate() {
        bank.tail = i as u32 * 17 + 7;
        for (j, orbit) in bank.orbits.iter_mut().enumerate() {
            let value = [0, 1, -1, i32::MAX, i32::MIN][(case + i + j) % 5];
            *orbit = shield::Orbit {
                pitch: value,
                pitch_target: [value, 100, -100][(case + j) % 3],
                yaw: value,
                yaw_rate: [0, 100, -100, i32::MAX][(case + j) % 4],
                yaw_rate_target: [0, 100, -100, i32::MIN][(case + i + j) % 4],
                size: [199, 200, 201, 499, 500, 501, i32::MAX][(case + i + j) % 7],
                growing: (j % 2) as u8,
                spin: u16::MAX - j as u16,
                position: [case as i32, i as i32 * 100, j as i32 * -100],
            };
        }
    }
    Fixture {
        temporary,
        shield,
        pool,
        timer: 0,
        slot: (case % 4) as i16,
        random: (case as u32).wrapping_mul(982451653),
    }
}
fn advances(mut seed: u32, after: u32) -> u32 {
    for count in 0..=180 {
        if seed == after {
            return count;
        }
        tankracer_game::random::next(&mut seed);
    }
    panic!("unexpected RNG advance count");
}
#[test]
#[ignore = "requires native temporary/shield oracle; set TANKRACER_VEHICLE_ENERGY_REFERENCE"]
fn temporary_slots_and_all_shield_banks_match_c_with_shared_rng() {
    const CASES: usize = 160;
    let mut input = vec![CASES as u32];
    let mut expected = Vec::new();
    let mut bounds = Vec::new();
    let mut errors = 0;
    let mut operations = 0;
    let mut shields_changed = 0;
    let mut refreshed = 0;
    let mut oldest_reused = 0;
    let mut exhausted = 0;
    let mut failures = [0; 5];
    let mut draws = [0_u32; 5];
    for case in 0..CASES {
        let mut f = fixture(case);
        input.extend(f.words());
        let kinds: &[u32] = if case.is_multiple_of(2) {
            &[0, 1, 1, 2, 4, 2, 3, 4]
        } else {
            &[1, 1, 2, 4, 3, 2, 4, 0]
        };
        input.push(kinds.len() as u32);
        expected.push(kinds.len() as u32);
        for (step, kind) in kinds.iter().enumerate() {
            let timer = if *kind == 1 {
                if step == 1 && case % 3 != 0 { 1 } else { 0 }
            } else if *kind == 2 {
                [1, 0, 16, -1][case % 4]
            } else {
                f.timer
            };
            let op = Operation {
                kind: *kind,
                timer,
                slot: f.slot,
                position: [[100000, -1000, -200000], [i32::MAX, i32::MIN, -1], [0; 3]][case % 3],
                time: [0, 1, 999, u32::MAX][(case + step) % 4],
                dt: [16, 40, 33, 65535, -16, 0][(case + step) % 6],
                enabled: !case.is_multiple_of(7),
                player: case.is_multiple_of(3),
                network: case / 7 % 2 == 0,
                network_bank: (case / 3 + step) % 9,
                basis: if case.is_multiple_of(5) {
                    None
                } else {
                    Some(Matrix::from_angles([1234, 5678, 9012]))
                },
            };
            input.extend(op.words());
            let before_shield = f.shield.clone();
            let before_seed = f.random;
            let reuse = f.temporary.available == u32::MAX && op.kind == 1 && op.timer == 0;
            let status = f.run(op);
            errors += usize::from(status.is_err());
            failures[op.kind as usize] += usize::from(status.is_err());
            let count = advances(before_seed, f.random);
            draws[op.kind as usize] += count;
            if op.kind == 1 && op.timer != 0 && status.is_ok() {
                assert_eq!(f.random, before_seed);
                refreshed += 1;
            }
            oldest_reused += usize::from(reuse && status.is_ok());
            shields_changed += usize::from(op.kind == 4 && before_shield != f.shield);
            exhausted += usize::from(
                op.kind == 1
                    && op.timer == 0
                    && f.temporary.lines[f.slot as usize].contains(&LineHandle::EXHAUSTED),
            );
            let start = expected.len();
            expected.extend([status.err().unwrap_or(0) as u32, count]);
            expected.extend(f.words());
            bounds.push((case, step, start, expected.len()));
            operations += 1;
        }
    }
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests");
    fs::create_dir_all(&root).unwrap();
    let path = root.join("vehicle-energy-input.bin");
    fs::write(
        &path,
        input
            .iter()
            .flat_map(|v| v.to_le_bytes())
            .collect::<Vec<_>>(),
    )
    .unwrap();
    let output =
        Command::new(std::env::var_os("TANKRACER_VEHICLE_ENERGY_REFERENCE").expect("set oracle"))
            .stdin(Stdio::from(fs::File::open(path).unwrap()))
            .output()
            .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(&output.stdout[..4], b"VE02");
    let actual: Vec<_> = output.stdout[4..]
        .chunks_exact(4)
        .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
        .collect();
    assert_eq!(actual[0], CASES as u32);
    assert_eq!(actual.len() - 1, expected.len());
    for (word, (a, e)) in actual[1..].iter().zip(&expected).enumerate() {
        if a != e {
            let &(case, step, start, _) = bounds
                .iter()
                .find(|(_, _, s, end)| word >= *s && word < *end)
                .unwrap();
            panic!(
                "case={case} step={step} word={} C={a:08x} Rust={e:08x}",
                word - start
            );
        }
    }
    assert!(
        errors > 0 && shields_changed > 0 && refreshed > 0 && oldest_reused > 0 && exhausted > 0
    );
    assert_eq!(draws[0], CASES as u32 * 180);
    assert!(failures[1] > 0 && failures[2] > 0 && failures[3] > 0);
    println!(
        "{CASES} scenarios / {operations} operations / {} words / {errors} error prefixes; shield changes={shields_changed}, refreshes={refreshed}, oldest reuse={oldest_reused}, full-line effects={exhausted}; draws={draws:?}, failures={failures:?}",
        expected.len()
    );
}
