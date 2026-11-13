//! Semantic pose/mesh state; legacy matrix padding has no physics/render consumer.
use std::{
    fs,
    ops::Range,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_assets::{
    archive::Archive,
    config::{Gain, Tank},
};
use tankracer_game::{
    body,
    math::{Axis, Matrix},
    pose,
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
#[derive(Default)]
struct Oracle {
    input: Vec<u8>,
    expected: Vec<u32>,
    records: Vec<(Range<usize>, u32, String)>,
    counts: [usize; 5],
    pose_errors: usize,
}
impl Oracle {
    fn record(&mut self, op: u32, input: Vec<u32>, out: Vec<u32>, label: String) {
        self.input.extend(op.to_le_bytes());
        self.input
            .extend(input.into_iter().flat_map(u32::to_le_bytes));
        let start = self.expected.len();
        self.expected.extend(out);
        self.records.push((start..self.expected.len(), op, label));
        self.counts[op as usize] += 1;
    }
    fn compare(self) {
        let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/pose-body");
        fs::create_dir_all(&folder).unwrap();
        let file = folder.join("input.bin");
        fs::write(&file, self.input).unwrap();
        let output = Command::new(
            std::env::var_os("TANKRACER_POSE_BODY_REFERENCE")
                .expect("set TANKRACER_POSE_BODY_REFERENCE"),
        )
        .stdin(Stdio::from(fs::File::open(file).unwrap()))
        .output()
        .unwrap();
        assert!(
            output.status.success(),
            "{}",
            String::from_utf8_lossy(&output.stderr)
        );
        assert_eq!(&output.stdout[..4], b"PB01");
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
        assert!(self.pose_errors > 0);
        eprintln!(
            "{} exact pose/body records, {} words; counts={:?}, pose errors={}",
            self.records.len(),
            actual.len(),
            self.counts,
            self.pose_errors
        );
    }
}
fn append(out: &mut Vec<u32>, values: impl IntoIterator<Item = i32>) {
    out.extend(values.into_iter().map(|v| v as u32));
}
fn pose_words(p: pose::Pose) -> Vec<u32> {
    let mut out = p.position.map(|v| v as u32).to_vec();
    append(&mut out, p.matrix.0);
    out
}
fn state(s: &pose::State) -> Vec<u32> {
    let mut out = s.physical_position.map(|v| v as u32).to_vec();
    append(&mut out, s.reference.0);
    append(&mut out, s.base_position);
    for p in [
        s.body,
        s.tracks[0],
        s.tracks[1],
        s.turret,
        s.gun,
        s.attachments[0],
        s.attachments[1],
    ] {
        out.extend(pose_words(p));
    }
    append(&mut out, s.bottom_position);
    out.extend([u32::from(s.recoil_factor), u32::from(s.recoil_shift)]);
    assert_eq!(out.len(), 104);
    out
}
fn config(c: &pose::Config) -> Vec<u32> {
    let mut out = Vec::new();
    for v in c.track_offsets {
        append(&mut out, v);
    }
    append(&mut out, c.turret_offset);
    append(&mut out, c.gun_offset);
    for v in c.attachment_offsets {
        append(&mut out, v);
    }
    append(&mut out, c.bottom_offset);
    out.extend([c.track_length as u32, c.travel_gain.packed()]);
    append(&mut out, c.gun_pivot);
    assert_eq!(out.len(), 26);
    out
}
fn env(e: &pose::Environment) -> Vec<u32> {
    let (tag, matrix) = match e.external {
        pose::ExternalBasis::None => (0, Matrix::IDENTITY),
        pose::ExternalBasis::Body => (1, Matrix::IDENTITY),
        pose::ExternalBasis::Custom(m) => (2, m),
    };
    let mut out = vec![
        u32::from(e.timer),
        u32::from(e.ambient_wobble),
        u32::from(e.session_active),
        e.ambient_phase,
        e.recoil_mode as u32,
        e.turret_yaw as u32,
        e.recoil as u32,
        e.gun_pitch as u32,
        tag,
    ];
    append(&mut out, matrix.0);
    assert_eq!(out.len(), 18);
    out
}

#[test]
#[ignore = "requires C pose/body oracle; set TANKRACER_POSE_BODY_REFERENCE"]
fn poses_articulation_lean_and_contact_hulls_match_c_by_branch_and_short_sequence() {
    let bytes = fs::read(
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game/DATA/FASTFILE.DAT"),
    )
    .unwrap();
    let archive = Archive::parse(&bytes).unwrap();
    let tanks: Vec<_> = (1..=15)
        .map(|i| Tank::decode(archive.get(&format!("TANK{i}.DAT")).unwrap()).unwrap())
        .collect();
    let values = [
        i32::MIN,
        -32769,
        -2731,
        -2730,
        -1457,
        -1456,
        -501,
        -500,
        -1,
        0,
        1,
        199,
        200,
        201,
        364,
        365,
        2730,
        2731,
        32767,
        i32::MAX,
    ];
    let mut oracle = Oracle::default();
    let mut r = Random(0x42f220);
    for case in 0..2048 {
        let matrix = if case % 2 == 0 {
            Matrix::from_angles(std::array::from_fn(|_| r.word()))
        } else {
            Matrix(std::array::from_fn(|_| r.signed()))
        };
        let mut s = pose::State {
            physical_position: std::array::from_fn(|_| r.signed()),
            reference: Matrix([99; 9]),
            base_position: [999; 3],
            body: pose::Pose {
                matrix,
                position: [888; 3],
            },
            tracks: [pose::Pose {
                matrix: Matrix([77; 9]),
                position: [66; 3],
            }; 2],
            turret: pose::Pose {
                matrix: Matrix([55; 9]),
                position: [44; 3],
            },
            gun: pose::Pose {
                matrix: Matrix([33; 9]),
                position: [22; 3],
            },
            attachments: [pose::Pose {
                matrix: Matrix([11; 9]),
                position: [-99; 3],
            }; 2],
            bottom_position: [-88; 3],
            recoil_factor: 99,
            recoil_shift: 98,
        };
        let mut c = pose::Config::from_tank(
            &tanks[case % 15],
            if case % 31 == 0 { i32::MIN } else { 3300 },
            std::array::from_fn(|_| r.choose(&values)),
        );
        let mut travel = std::array::from_fn(|_| r.choose(&values));
        if case % 17 == 0 {
            c.travel_gain = Gain {
                multiplier: 65535,
                shift: 0,
            };
            travel = [0, 0, i32::MIN, 0];
        }
        if case % 23 == 0 {
            c.travel_gain.shift = 65535;
        }
        let mut e = pose::Environment {
            timer: if case % 3 == 0 { 0 } else { r.word() as u16 },
            ambient_wobble: case % 2 == 0,
            session_active: case % 5 == 0,
            ambient_phase: r.word(),
            recoil_mode: [0, 1, 3, 5, -1][case % 5],
            turret_yaw: r.word() as i16,
            recoil: r.choose(&values),
            gun_pitch: r.choose(&values),
            external: match case % 3 {
                0 => pose::ExternalBasis::None,
                1 => pose::ExternalBasis::Body,
                _ => pose::ExternalBasis::Custom(Matrix(std::array::from_fn(|_| r.signed()))),
            },
        };
        for tick in 0..4 {
            let mut input = state(&s);
            input.extend(config(&c));
            append(&mut input, travel);
            input.extend(env(&e));
            assert_eq!(input.len(), 152);
            let status = if s.update(&c, travel, &e).is_ok() {
                0
            } else {
                oracle.pose_errors += 1;
                (-2_i32) as u32
            };
            let mut out = vec![status];
            out.extend(state(&s));
            oracle.record(1, input, out, format!("pose case={case} tick={tick}"));
            e.ambient_phase = e.ambient_phase.wrapping_add(17);
            s.body.matrix.rotate(Axis::Y, 17);
        }

        let tuning = body::Tuning {
            recoil_decay: if case % 2 == 0 { 15 } else { r.word() as u8 },
            recoil_step: r.word() as i16,
            pitch_damping: tankracer_assets::config::Damping {
                step: r.word() as i16,
                fraction: r.word() as u8,
                shift: r.word() as u8,
            },
            lean_decay: r.word() as u8,
        };
        let dt = [0, 1, 17, 33, -1, i32::MAX, i32::MIN][case % 7];
        let mut articulation = body::Articulation {
            recoil: r.choose(&values),
            recoil_rate: r.choose(&values),
            pitch: r.choose(&values),
            pitch_rate: r.choose(&values),
        };
        let mut vertices: Vec<[i16; 3]> = (0..8)
            .map(|_| std::array::from_fn(|_| r.word() as i16))
            .collect();
        let indices = std::array::from_fn(|_| (r.word() % 8) as u8);
        let original_z = std::array::from_fn(|_| r.word() as i16);
        let present = case % 7 != 0;
        let mut lean = body::Lean {
            lateral: body::Spring {
                position: r.choose(&values),
                rate: r.choose(&values),
            },
            longitudinal: body::Spring {
                position: r.choose(&values),
                rate: r.choose(&values),
            },
        };
        let acceleration = [r.choose(&values), r.choose(&values)];
        for tick in 0..4 {
            let mut input = vec![
                articulation.recoil as u32,
                articulation.recoil_rate as u32,
                articulation.pitch as u32,
                articulation.pitch_rate as u32,
                u32::from(tuning.recoil_decay),
                tuning.recoil_step as u32,
                tuning.pitch_damping.packed(),
                dt as u32,
                u32::from(present),
            ];
            input.extend(indices.map(u32::from));
            input.extend(original_z.map(|v| v as u32));
            input.push(vertices.len() as u32);
            append(&mut input, vertices.iter().flatten().map(|&v| i32::from(v)));
            articulation
                .step(
                    dt,
                    &tuning,
                    present.then_some(body::GunMesh {
                        vertices: &mut vertices,
                        indices,
                        original_z,
                    }),
                )
                .unwrap();
            let mut out = vec![
                0,
                articulation.recoil as u32,
                articulation.recoil_rate as u32,
                articulation.pitch as u32,
                articulation.pitch_rate as u32,
            ];
            append(&mut out, vertices.iter().flatten().map(|&v| i32::from(v)));
            oracle.record(
                2,
                input,
                out,
                format!("articulation case={case} tick={tick}"),
            );
            let mut input = vec![
                lean.lateral.position as u32,
                lean.lateral.rate as u32,
                lean.longitudinal.position as u32,
                lean.longitudinal.rate as u32,
            ];
            append(&mut input, acceleration);
            input.extend([dt as u32, u32::from(tuning.lean_decay)]);
            lean.step(acceleration, dt, &tuning);
            oracle.record(
                3,
                input,
                vec![
                    0,
                    lean.lateral.position as u32,
                    lean.lateral.rate as u32,
                    lean.longitudinal.position as u32,
                    lean.longitudinal.rate as u32,
                ],
                format!("lean case={case} tick={tick}"),
            );
        }
        let current = std::array::from_fn(|_| r.signed());
        let previous = std::array::from_fn(|_| r.signed());
        let contacts = std::array::from_fn(|_| body::Contact {
            world: std::array::from_fn(|_| r.signed()),
            gap: r.signed(),
        });
        let mut input = current.map(|v| v as u32).to_vec();
        append(&mut input, previous);
        for p in contacts {
            append(&mut input, p.world);
            input.push(p.gap as u32);
        }
        let mut hull = body::Hull::default();
        hull.refresh(current, previous, contacts);
        let mut out = vec![0];
        append(&mut out, hull.position);
        append(&mut out, hull.matrix.0);
        append(&mut out, hull.vertices.into_iter().flatten().map(i32::from));
        out.push(u32::from(hull.radius));
        oracle.record(4, input, out, format!("hull case={case}"));
    }
    oracle.compare();
}
