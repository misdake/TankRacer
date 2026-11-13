use std::{
    fs,
    ops::Range,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_game::{
    attitude,
    contact::Coefficient,
    math::{self, Axis, Matrix},
    vector, velocity,
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
}
#[derive(Default)]
struct Oracle {
    input: Vec<u8>,
    expected: Vec<u32>,
    records: Vec<(Range<usize>, u32, String)>,
    counts: [usize; 14],
    errors: [usize; 14],
}
impl Oracle {
    fn record(
        &mut self,
        op: u32,
        input: impl IntoIterator<Item = u32>,
        out: impl IntoIterator<Item = u32>,
        label: String,
    ) {
        self.input.extend(op.to_le_bytes());
        self.input
            .extend(input.into_iter().flat_map(u32::to_le_bytes));
        let start = self.expected.len();
        self.expected.extend(out);
        self.records.push((start..self.expected.len(), op, label));
        self.counts[op as usize] += 1;
    }
    fn status(&mut self, op: u32, result: Result<(), math::Error>) -> u32 {
        if result.is_ok() {
            0
        } else {
            self.errors[op as usize] += 1;
            (-2_i32) as u32
        }
    }
    fn compare(self) {
        let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/math");
        fs::create_dir_all(&folder).unwrap();
        let file = folder.join("input.bin");
        fs::write(&file, self.input).unwrap();
        let output = Command::new(
            std::env::var_os("TANKRACER_MATH_REFERENCE").expect("set TANKRACER_MATH_REFERENCE"),
        )
        .stdin(Stdio::from(fs::File::open(file).unwrap()))
        .output()
        .unwrap();
        assert!(
            output.status.success(),
            "{}",
            String::from_utf8_lossy(&output.stderr)
        );
        assert_eq!(&output.stdout[..4], b"MA01");
        let actual: Vec<_> = output.stdout[4..]
            .chunks_exact(4)
            .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
            .collect();
        assert_eq!(actual.len(), self.expected.len());
        for (range, op, label) in &self.records {
            assert_eq!(
                actual[range.clone()],
                self.expected[range.clone()],
                "op={op} {label}"
            );
        }
        assert!(self.errors[2] > 0 && self.errors[5] > 0 && self.errors[7] > 0);
        eprintln!(
            "{} exact math/attitude/velocity records, {} words; op counts={:?}, errors={:?}",
            self.records.len(),
            actual.len(),
            self.counts,
            self.errors
        );
    }
}
fn words(values: impl IntoIterator<Item = i32>) -> Vec<u32> {
    values.into_iter().map(|v| v as u32).collect()
}
fn attitude_words(s: &attitude::State) -> Vec<u32> {
    let mut out = words(s.reference.0);
    out.extend(words(s.matrix.0));
    out.extend(words(s.position));
    out.extend(words(s.angular));
    out.extend(words([s.longitudinal_delta, s.lateral_delta]));
    out.extend(words(s.pivot));
    assert_eq!(out.len(), 29);
    out
}
fn normalize(oracle: &mut Oracle, mut matrix: Matrix) {
    let input = words(matrix.0);
    let status = oracle.status(5, matrix.normalize());
    let mut out = vec![status];
    out.extend(words(matrix.0));
    oracle.record(5, input, out, "normalize".into());
}

#[test]
#[ignore = "requires the C math oracle; set TANKRACER_MATH_REFERENCE"]
fn tables_quadrants_rotations_partial_errors_velocity_and_short_attitude_sequences_match_c() {
    let mut oracle = Oracle::default();
    let mut r = Random(0x444120);
    // Every stored sine and cosine word, including the nonideal extra quarter cycle.
    for index in 0..16384 {
        let angle = index * 4;
        oracle.record(
            1,
            [angle],
            [math::sine(angle) as u32, math::cosine(angle) as u32],
            format!("angle={angle}"),
        );
    }
    let values = [
        i32::MIN,
        i32::MIN + 1,
        -65537,
        -65536,
        -65535,
        -500001,
        -500000,
        -1,
        0,
        1,
        255,
        256,
        257,
        65535,
        65536,
        65537,
        500000,
        500001,
        i32::MAX,
    ];
    let mut pairs = Vec::new();
    for n in values {
        for d in values {
            pairs.push((n, d));
        }
    }
    for index in 0..256 {
        for sign in [-1, 1] {
            for quadrant in [-1, 1] {
                pairs.push((index * sign, 256 * quadrant));
            }
        }
    }
    for (n, d) in pairs {
        let result = math::angle(n, d);
        let status = oracle.status(2, result.map(|_| ()));
        oracle.record(
            2,
            words([n, d]),
            [status, result.unwrap_or(0x12345678) as u32],
            format!("n={n} d={d}"),
        );
        oracle.record(
            13,
            words([n, d]),
            [math::length2(n, d) as u32],
            format!("length2 {n},{d}"),
        );
    }
    for angles in [
        [0, 0, 0],
        [16384, 32768, 49152],
        [65535, 65536, 65536],
        [1, 2, u32::MAX],
        [0, 0, 0x10000],
    ] {
        oracle.record(
            3,
            angles,
            words(Matrix::from_angles(angles).0),
            format!("Euler={angles:?}"),
        );
    }
    normalize(&mut oracle, Matrix([65407, 339, 0, 0, 4096, 0, 0, 0, 4096])); // Zero first-row denominator.
    normalize(&mut oracle, Matrix([4000, 1, 0, 65407, 339, 0, 0, 0, 4096])); // Written first row then error.
    for case in 0..2048 {
        let angles = std::array::from_fn(|_| r.word());
        let source = if case < 1024 {
            Matrix::from_angles(angles)
        } else {
            Matrix(std::array::from_fn(|_| r.signed()))
        };
        oracle.record(
            3,
            angles,
            words(Matrix::from_angles(angles).0),
            format!("Euler case={case}"),
        );
        normalize(&mut oracle, source);
        for (axis, which) in [(Axis::X, 0), (Axis::Y, 1), (Axis::Z, 2)] {
            let angle = [0, 1, 65535, 65536, u32::MAX, r.word()][case % 6];
            let mut matrix = source;
            let mut input = words(source.0);
            input.extend([which, angle]);
            matrix.rotate(axis, angle);
            oracle.record(
                4,
                input,
                words(matrix.0),
                format!("case={case} axis={axis:?} angle={angle}"),
            );
        }
        let position = std::array::from_fn(|_| r.signed());
        let pivot = std::array::from_fn(|_| r.signed());
        let angles = std::array::from_fn(|_| r.signed());
        let mut input = words(source.0);
        input.extend(words(position));
        input.extend(words(pivot));
        input.extend(words(angles));
        let mut matrix = source;
        let mut moved = position;
        matrix.rotate_pivot(&mut moved, pivot, angles);
        let mut out = words(matrix.0);
        out.extend(words(moved));
        oracle.record(6, input, out, format!("pivot case={case}"));
        let src = if case < values.len() * values.len() {
            [
                values[case % values.len()],
                values[case * 3 % values.len()],
                values[case / values.len()],
            ]
        } else {
            std::array::from_fn(|_| r.signed())
        };
        for (direction, inverse) in [
            (velocity::Direction::WorldToBody, 0),
            (velocity::Direction::BodyToWorld, 1),
        ] {
            for large in [false, true] {
                let mut input = words(src);
                input.extend(words(source.0));
                input.extend([inverse, u32::from(large)]);
                let mut out = words(velocity::transform(src, source, direction, large));
                out.push(u32::from(velocity::large(src)));
                oracle.record(
                    8,
                    input,
                    out,
                    format!("velocity case={case} inverse={inverse} large={large}"),
                );
            }
        }
        let mut normal = src;
        vector::normalize(&mut normal);
        oracle.record(9, words(src), words(normal), format!("vector norm {case}"));
        let other = std::array::from_fn(|_| r.signed());
        let mut input = words(src);
        input.extend(words(other));
        oracle.record(
            10,
            input,
            words(vector::cross(src, other)),
            format!("cross {case}"),
        );
        let angle = r.word();
        let mut input = words(src);
        input.push(angle);
        oracle.record(
            11,
            input,
            words(vector::yaw(src, angle)),
            format!("yaw {case}"),
        );
        let delta = r.signed();
        let yaw = r.signed();
        let config = velocity::YawConfig {
            axis: if case % 2 == 0 { [0, 0, -4096] } else { other },
            decelerating: Coefficient {
                multiplier: r.word() as u16,
                shift: r.word() as u8,
            },
            accelerating: Coefficient {
                multiplier: r.word() as u16,
                shift: r.word() as u8,
            },
        };
        let mut input = words(src);
        input.extend(words([delta, yaw]));
        input.extend(words(config.axis));
        input.extend([
            u32::from(config.decelerating.multiplier)
                | (u32::from(config.decelerating.shift) << 16),
            u32::from(config.accelerating.multiplier)
                | (u32::from(config.accelerating.shift) << 16),
        ]);
        oracle.record(
            12,
            input,
            [0, velocity::correct_yaw(src, delta, yaw, &config) as u32],
            format!("yaw correction {case}"),
        );
        let mut state = attitude::State {
            reference: source,
            matrix: source,
            position,
            angular: src,
            longitudinal_delta: delta,
            lateral_delta: yaw,
            pivot,
        };
        // Include upright/overturned guards and an error before pivot/normalization.
        if case % 4 == 0 {
            state.reference = Matrix([0, 4096, 0, 4096, 0, 0, 0, -4096, 0]);
        }
        if case % 17 == 0 {
            state.reference.0[1] = i32::MIN;
        }
        let gains = attitude::Config {
            longitudinal: config.decelerating,
            lateral: config.accelerating,
        };
        for tick in 0..4 {
            let dt = if case % 2 == 0 { 17 } else { r.signed() };
            let frame = [0, 1, 9, 10, u32::MAX][(case + tick) % 5];
            let mut input = attitude_words(&state);
            input.extend([
                u32::from(gains.longitudinal.multiplier),
                u32::from(gains.longitudinal.shift),
                u32::from(gains.lateral.multiplier),
                u32::from(gains.lateral.shift),
                dt as u32,
                frame,
            ]);
            let status = oracle.status(7, state.step(&gains, dt, frame));
            let mut out = vec![status];
            out.extend(attitude_words(&state));
            oracle.record(
                7,
                input,
                out,
                format!("attitude case={case} tick={tick} frame={frame}"),
            );
        }
    }
    oracle.compare();
}
