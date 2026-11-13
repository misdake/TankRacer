use std::{
    fs,
    ops::Range,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_game::{
    collision::{self, Scale, Trajectory},
    math::Matrix,
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
    fn values(&mut self, boundary: bool) -> [i32; 3] {
        std::array::from_fn(|_| {
            if boundary {
                self.choose(&[
                    i32::MIN,
                    i32::MAX,
                    -262145,
                    -262144,
                    -4096,
                    -1,
                    0,
                    1,
                    4096,
                    262144,
                    262145,
                ])
            } else {
                self.signed() % 1000000
            }
        })
    }
    fn scale(&mut self, enabled: bool) -> Option<Scale> {
        enabled.then(|| Scale {
            multiplier: self.word() as u16,
            shift: self.word() as u8,
        })
    }
}
fn append(out: &mut Vec<u32>, v: impl IntoIterator<Item = i32>) {
    out.extend(v.into_iter().map(|v| v as u32));
}
fn scale(out: &mut Vec<u32>, s: Option<Scale>) {
    out.extend(s.map_or([0, 0], |s| {
        [1, u32::from(s.multiplier) | (u32::from(s.shift) << 16)]
    }));
}
#[derive(Default)]
struct Oracle {
    input: Vec<u8>,
    expected: Vec<u32>,
    records: Vec<(Range<usize>, String)>,
    counts: [usize; 6],
    errors: usize,
}
impl Oracle {
    fn record(&mut self, op: u32, input: Vec<u32>, out: Vec<u32>, label: String) {
        self.input.extend(op.to_le_bytes());
        self.input
            .extend(input.into_iter().flat_map(u32::to_le_bytes));
        let start = self.expected.len();
        self.expected.extend(out);
        self.records.push((start..self.expected.len(), label));
        self.counts[op as usize] += 1;
    }
    fn compare(self) {
        let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/collision");
        fs::create_dir_all(&folder).unwrap();
        let path = folder.join("input.bin");
        fs::write(&path, self.input).unwrap();
        let result = Command::new(
            std::env::var_os("TANKRACER_COLLISION_REFERENCE")
                .expect("set TANKRACER_COLLISION_REFERENCE"),
        )
        .stdin(Stdio::from(fs::File::open(path).unwrap()))
        .output()
        .unwrap();
        assert!(
            result.status.success(),
            "{}",
            String::from_utf8_lossy(&result.stderr)
        );
        assert_eq!(&result.stdout[..4], b"CL01");
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
            "{} collision records / {} words exact; operations {:?}, errors {}",
            self.records.len(),
            words.len(),
            self.counts,
            self.errors
        );
    }
}
#[test]
#[ignore = "requires C collision oracle; set TANKRACER_COLLISION_REFERENCE"]
fn reflections_exchange_approach_and_binary_constant_match_by_branch_and_short_sequence() {
    let mut rng = Random(0x9e71c319);
    let mut oracle = Oracle::default();
    let mut reflected = 0;
    let mut separating = 0;
    let mut weak = 0;
    let mut strong = 0;
    for case in 0..2048 {
        let boundary = case % 6 == 0;
        let mut normal = rng.values(boundary);
        let mut velocity = rng.values(boundary);
        if case % 16 == 0 {
            normal = [0; 3];
        }
        let local = rng.scale(case & 1 != 0);
        let final_scale = rng.scale(case & 2 != 0);
        let mode = rng.choose(&[-1, 0, 1, 2, 3, 4, 5, 6]);
        let player = case % 3 != 0;
        for tick in 0..4 {
            let mut input = Vec::new();
            append(&mut input, normal);
            append(&mut input, velocity);
            scale(&mut input, local);
            scale(&mut input, final_scale);
            input.extend([mode as u32, u32::from(player)]);
            let mut out = vec![0];
            match collision::respond(normal, velocity, local, final_scale, mode, player) {
                Ok(r) => {
                    append(&mut out, r.normal);
                    append(&mut out, r.velocity);
                    out.extend([r.approach as u32, r.speed_units as u32]);
                    if r.approach > 0 {
                        reflected += 1;
                    } else {
                        separating += 1;
                    }
                    velocity = r.velocity;
                    normal = r.normal;
                }
                Err(_) => {
                    oracle.errors += 1;
                    out[0] = (-2_i32) as u32;
                    out.extend((0..8).map(|i| 0x7b000000 + i));
                }
            }
            oracle.record(
                1,
                input,
                out,
                format!(
                    "wall case={case} tick={tick} mode={mode} local={local:?} final={final_scale:?}"
                ),
            );
        }
    }
    for case in 0..2048 {
        let boundary = case % 6 == 0;
        let matrix = if boundary {
            Matrix(std::array::from_fn(|_| rng.signed()))
        } else {
            Matrix::from_angles(std::array::from_fn(|_| rng.word() & 65535))
        };
        let mut mine = rng.values(boundary);
        let mut other = rng.values(boundary);
        let mut local = rng.values(boundary);
        let mut normal = rng.values(boundary);
        let matrix = if case % 8 == 0 {
            Matrix::IDENTITY
        } else {
            matrix
        };
        if case % 8 == 0 {
            mine = [0; 3];
            other = [10000, 0, 1000000];
            local = [0; 3];
            normal = [0, 0, 4096];
        }
        for tick in 0..4 {
            // Both a snapshot of the current other and a distinct saved input
            // exercise the source's separate pointer/input meanings.
            let incoming = if tick == 3 {
                rng.values(boundary)
            } else {
                other
            };
            let mut input = Vec::new();
            append(&mut input, matrix.0);
            append(&mut input, local);
            append(&mut input, mine);
            append(&mut input, other);
            append(&mut input, normal);
            append(&mut input, incoming);
            let d = collision::exchange(matrix, local, &mut mine, &mut other, normal, incoming);
            if d <= 2048 {
                weak += 1;
            } else {
                strong += 1;
            }
            let mut out = vec![0, d as u32];
            append(&mut out, mine);
            append(&mut out, other);
            oracle.record(2, input, out, format!("pair case={case} tick={tick}"));
        }
    }
    let boundary = [
        i32::MIN,
        i32::MAX,
        -100000,
        -21,
        -20,
        -11,
        -10,
        -1,
        0,
        1,
        10,
        11,
        20,
        21,
        100000,
    ];
    for n in boundary.into_iter().chain((0..2048).map(|_| rng.signed())) {
        oracle.record(
            3,
            vec![n as u32],
            vec![collision::binary_one_point_seven(n) as u32],
            format!("binary1.7 {n}"),
        );
    }
    for x in boundary {
        for z in boundary {
            oracle.record(
                4,
                vec![x as u32, z as u32],
                vec![collision::direction(x, z)],
                format!("direction {x},{z}"),
            );
        }
    }
    for case in 0..2048 {
        let boundary = case % 4 == 0;
        let a = Trajectory {
            position: rng.values(boundary),
            velocity: rng.values(boundary),
        };
        let b = Trajectory {
            position: rng.values(boundary),
            velocity: rng.values(boundary),
        };
        let dt = rng.choose(&[i32::MIN, i32::MAX, -1, 0, 1, 16, 40, 100]);
        let mut input = Vec::new();
        for t in [a, b] {
            append(&mut input, t.position);
            append(&mut input, t.velocity);
        }
        input.push(dt as u32);
        oracle.record(
            5,
            input,
            vec![u32::from(collision::approaching(a, b, dt))],
            format!("approaching case={case}"),
        );
    }
    assert!(reflected > 0 && separating > 0 && weak > 0 && strong > 0);
    eprintln!(
        "branches: reflected={reflected} separating={separating} weak={weak} strong={strong}"
    );
    oracle.compare();
}
