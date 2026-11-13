//! Branch/short-sequence comparison with the immutable C physics modules.
use std::{
    fs,
    ops::Range,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_game::{
    contact::{self, Coefficient},
    suspension::{self, Environment, Point, State},
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
}

fn config(raw: [u32; 11]) -> contact::Config {
    contact::Config {
        lower: raw[0] as i32,
        upper: raw[1] as i32,
        force_positive: Coefficient::from_packed(raw[2]),
        force_negative: Coefficient::from_packed(raw[3]),
        output_gain: Coefficient::from_packed(raw[4]),
        tilt_gain: Coefficient::from_packed(raw[5]),
        shape_shift: raw[6],
        restore_positive: Coefficient::from_packed(raw[7]),
        restore_negative: Coefficient::from_packed(raw[8]),
        damping_shift: (raw[9] >> 24) as u8,
        travel_dead_step: raw[10] as i16,
    }
}
fn pack_state(s: &State) -> Vec<u32> {
    let mut out = Vec::with_capacity(40);
    for p in s.points {
        out.extend([
            p.kind as u32,
            p.gap as u32,
            p.corner,
            p.travel as u32,
            p.rate as u32,
            p.output as u32,
        ]);
    }
    out.extend(s.angular.map(|x| x as u32));
    out.extend(s.tilt.map(|x| x as u32));
    out.extend(
        [
            s.penetrating_input_sum,
            s.travel_sum,
            s.vertical_delta,
            s.world_vy,
            s.position_y,
            s.timer,
        ]
        .map(|x| x as u32),
    );
    out.extend(s.auxiliary_type.map(|x| x as u32));
    out.extend(s.auxiliary_cached.map(|x| x as u32));
    out.extend([s.max_penetration as u32, s.last_penetration as u32]);
    assert_eq!(out.len(), 40);
    out
}
fn pack_env(e: &Environment) -> Vec<u32> {
    let mut out = vec![
        e.dt as u32,
        e.environment_flag as u32,
        e.session_active as u32,
        e.mode as u32,
        u32::from(e.is_player),
    ];
    out.extend(e.base.map(|x| x as u32));
    out.extend(e.region_min.map(|x| x as u32));
    out.extend(e.region_max.map(|x| x as u32));
    out.extend([
        u32::from(e.player_multiplier),
        e.player_shift,
        u32::from(e.other_multiplier),
        e.other_shift,
        e.penetration_shift,
    ]);
    out.extend(e.auxiliary_query_type.map(|x| x as u32));
    assert_eq!(out.len(), 18);
    out
}

#[derive(Default)]
struct Oracle {
    input: Vec<u8>,
    expected: Vec<u32>,
    records: Vec<(Range<usize>, String)>,
}
impl Oracle {
    fn record(&mut self, op: u32, input: &[u32], expected: &[u32], label: String) {
        self.input.extend(op.to_le_bytes());
        self.input
            .extend(input.iter().flat_map(|x| x.to_le_bytes()));
        let start = self.expected.len();
        self.expected.extend(expected);
        self.records.push((start..self.expected.len(), label));
    }
    fn contact(
        &mut self,
        raw: [u32; 11],
        mut state: contact::State,
        force: i32,
        dt: i32,
        corner: u32,
        aux: i32,
    ) {
        let mut input = raw.to_vec();
        input.extend(
            [
                state.travel,
                state.rate,
                state.output,
                state.tilt[0],
                state.tilt[1],
                force,
                dt,
            ]
            .map(|x| x as u32),
        );
        input.extend([corner, aux as u32]);
        state.step(&config(raw), force, dt, corner, aux);
        self.record(
            1,
            &input,
            &[
                state.travel,
                state.rate,
                state.output,
                state.tilt[0],
                state.tilt[1],
            ]
            .map(|x| x as u32),
            format!("contact force={force} dt={dt} corner={corner} aux={aux} config={raw:?}"),
        );
    }
    fn step(
        &mut self,
        raw: [u32; 11],
        state: &mut State,
        e: &Environment,
        fail: i32,
        write_failure: bool,
        label: String,
    ) {
        let mut input = raw.to_vec();
        input.extend(pack_state(state));
        input.extend(pack_env(e));
        input.extend([fail as u32, u32::from(write_failure)]);
        let mut calls = 0_u32;
        let mut trace = 0_u32;
        let status = state
            .step_resolved(&config(raw), e, |side, kind| {
                calls += 1;
                trace = (trace << 4) | (side as u32 + 1);
                if side as i32 == fail {
                    if write_failure {
                        *kind = e.auxiliary_query_type[side];
                    }
                    Err(-9)
                } else {
                    *kind = e.auxiliary_query_type[side];
                    Ok(())
                }
            })
            .err()
            .unwrap_or(0);
        let mut expected = vec![status as u32];
        expected.extend(pack_state(state));
        expected.extend([calls, trace]);
        self.record(2, &input, &expected, label);
    }
    fn helpers(&mut self, kinds: [i32; 4], travel: [i32; 4], r: &mut Random) {
        let pivots = std::array::from_fn(|_| r.signed());
        let (base, floor, multiplier, shift, a, b) = (
            r.signed(),
            r.signed(),
            r.word(),
            r.word(),
            r.signed(),
            r.signed(),
        );
        let mut input = kinds.map(|x| x as u32).to_vec();
        input.extend(travel.map(|x| x as u32));
        input.extend(pivots.map(|x| x as u32));
        input.extend([
            base as u32,
            floor as u32,
            multiplier,
            shift,
            a as u32,
            b as u32,
        ]);
        let mut expected = suspension::layout(kinds, pivots).map(|x| x as u32).to_vec();
        expected
            .push(suspension::load(kinds, travel, base, floor, multiplier as u16, shift) as u32);
        expected.push(suspension::agree(a, b) as u32);
        self.record(3, &input, &expected, format!("helpers kinds={kinds:?}"));
    }
    fn compare(self) {
        let executable = std::env::var_os("TANKRACER_SUSPENSION_REFERENCE")
            .expect("set TANKRACER_SUSPENSION_REFERENCE");
        let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..");
        let folder = root.join("build/tests/suspension");
        fs::create_dir_all(&folder).unwrap();
        let input = folder.join("input.bin");
        fs::write(&input, self.input).unwrap();
        let output = Command::new(executable)
            .stdin(Stdio::from(fs::File::open(input).unwrap()))
            .output()
            .unwrap();
        assert!(
            output.status.success(),
            "{}",
            String::from_utf8_lossy(&output.stderr)
        );
        assert_eq!(&output.stdout[..4], b"SC01");
        let actual: Vec<u32> = output.stdout[4..]
            .chunks_exact(4)
            .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
            .collect();
        assert_eq!(actual.len(), self.expected.len());
        for (range, label) in &self.records {
            assert_eq!(
                actual[range.clone()],
                self.expected[range.clone()],
                "{label}"
            );
        }
        eprintln!(
            "{} contact/suspension/helper records, {} output words exactly match C",
            self.records.len(),
            actual.len()
        );
    }
}

fn random_config(r: &mut Random) -> [u32; 11] {
    let (a, b) = (r.signed(), r.signed());
    [
        a.min(b) as u32,
        a.max(b) as u32,
        r.word(),
        r.word(),
        r.word(),
        r.word(),
        r.word(),
        r.word(),
        r.word(),
        r.word(),
        r.word(),
    ]
}

#[test]
#[ignore = "requires the C contact/suspension oracle; set TANKRACER_SUSPENSION_REFERENCE"]
fn branch_boundaries_errors_and_short_sequences_match_c() {
    let mut oracle = Oracle::default();
    let mut r = Random(0x51c70a39);
    let boundary = [
        i32::MIN,
        i32::MIN + 1,
        -4915200,
        -32769,
        -32768,
        -401,
        -400,
        -76,
        -75,
        -74,
        -1,
        0,
        1,
        74,
        75,
        76,
        400,
        401,
        32767,
        32768,
        i32::MAX,
    ];
    let normal = [
        (-12000_i32) as u32,
        12000,
        0x00100007,
        0x00100005,
        0x00082000,
        0x00041000,
        5,
        0x000c0001,
        0x000d0001,
        0x08000000,
        2,
    ];
    for corner in [0, 1, 2, 3, 4, u32::MAX] {
        for aux in [0, 1, 2, 3] {
            for force in boundary {
                for travel in [-12001, -12000, -1, 0, 1, 12000, 12001] {
                    oracle.contact(
                        normal,
                        contact::State {
                            travel,
                            rate: force,
                            output: 99,
                            tilt: [123, -456],
                        },
                        force,
                        17,
                        corner,
                        aux,
                    );
                }
            }
        }
    }
    for i in 0..6000 {
        let raw = random_config(&mut r);
        let state = contact::State {
            travel: r.signed(),
            rate: r.signed(),
            output: r.signed(),
            tilt: [r.signed(), r.signed()],
        };
        let force = r.choose(&boundary);
        let dt = r.signed();
        let corner = r.word() % 6;
        oracle.contact(raw, state, force, dt, corner, i % 4);
    }
    // All contact combinations include missing/unknown kinds, not just a grounded mask.
    for mask in 0..625 {
        let mut value = mask;
        let kinds = std::array::from_fn(|_| {
            let k = value % 5;
            value /= 5;
            k
        });
        let travel = std::array::from_fn(|_| r.choose(&boundary));
        oracle.helpers(kinds, travel, &mut r);
    }
    for i in 0..4000 {
        let raw = if i < 2000 {
            normal
        } else {
            random_config(&mut r)
        };
        let mut state = State {
            points: std::array::from_fn(|j| Point {
                kind: (r.word() % 5) as i32,
                gap: r.choose(&boundary),
                corner: j as u32,
                travel: r.choose(&boundary),
                rate: r.signed(),
                output: r.signed(),
            }),
            angular: [r.signed(), r.signed()],
            tilt: [r.signed(), r.signed()],
            penetrating_input_sum: r.signed(),
            travel_sum: r.signed(),
            vertical_delta: r.signed(),
            max_penetration: r.signed(),
            last_penetration: r.signed(),
            world_vy: r.choose(&boundary),
            position_y: r.signed(),
            timer: r.choose(&boundary),
            auxiliary_type: [(r.word() % 4) as i32, (r.word() % 4) as i32],
            auxiliary_cached: [(r.word() % 2) as i32, (r.word() % 2) as i32],
        };
        let env = Environment {
            dt: if i < 2000 {
                [0, 1, 16, 17, 33][i % 5]
            } else {
                r.signed()
            },
            environment_flag: (r.word() % 2) as i32,
            session_active: (r.word() % 2) as i32,
            mode: if i % 2 == 0 { 5 } else { 1 },
            is_player: i % 3 == 0,
            base: [if i % 2 == 0 { 0 } else { 11 }, 0],
            region_min: [-10; 2],
            region_max: [10; 2],
            player_multiplier: r.word() as u16,
            player_shift: r.word(),
            other_multiplier: r.word() as u16,
            other_shift: r.word(),
            penetration_shift: r.word(),
            auxiliary_query_type: [(r.word() % 4) as i32, (r.word() % 4) as i32],
        };
        // Reuse updated state for four ticks; fail before/after kind writes too.
        for tick in 0..4 {
            let fail = if tick == 3 { i as i32 % 3 - 1 } else { -1 };
            oracle.step(
                raw,
                &mut state,
                &env,
                fail,
                i % 2 == 0,
                format!("suspension case={i} tick={tick} fail={fail}"),
            );
            if tick == 1 {
                state.auxiliary_cached = [0; 2];
            }
        }
    }
    oracle.compare();
}
