use std::{
    fs,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_game::deformation::{Geometry, Pose, Record, State};

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
fn pack_record(r: &Record) -> [u32; 4] {
    [
        r.position[0] as u32,
        r.position[1] as u32,
        r.position[2] as u32,
        u32::from_le_bytes([r.state, r.color[0], r.color[1], r.color[2]]),
    ]
}

#[test]
#[ignore = "requires the C deformation oracle; set TANKRACER_DEFORMATION_REFERENCE"]
fn strict_windows_wrapping_clamps_and_record_mutations_match_c() {
    let mut random = Random(0x4073d0);
    let mut input = Vec::<u8>::new();
    let mut expected = Vec::<u32>::new();
    let mut ranges = Vec::new();
    let boundary = [
        i32::MIN,
        -6001,
        -1024,
        -750,
        -749,
        -128,
        -127,
        -1,
        0,
        1,
        127,
        128,
        749,
        750,
        1024,
        6001,
        i32::MAX,
    ];
    for i in 0..2048 {
        let pose = Pose {
            origin: if i < 1024 {
                [0; 3]
            } else {
                std::array::from_fn(|_| random.signed())
            },
            inverse_basis: if i < 1024 {
                [4096, 0, 0, 0, 4096, 0, 0, 0, 4096]
            } else {
                std::array::from_fn(|_| random.signed())
            },
        };
        let geometry = Geometry {
            sample_bounds: if i < 1024 {
                [[-100, 100]; 2]
            } else {
                std::array::from_fn(|_| [random.signed(), random.signed()])
            },
            edges: if i < 1024 {
                [-16000, 16000, -16000, 16000]
            } else {
                std::array::from_fn(|_| random.signed())
            },
            edge_cap: if i < 1024 { 65536 } else { random.signed() },
            vertices: (0..32)
                .map(|v| {
                    if i < 1024 {
                        [
                            ((v % 8) * 32 - 128) as i16,
                            (v * 27 - 300) as i16,
                            ((v / 8) * 64 - 128) as i16,
                        ]
                    } else {
                        std::array::from_fn(|_| random.word() as i16)
                    }
                })
                .collect(),
        };
        let mut state = State {
            pose,
            records: (0..8)
                .map(|j| Record {
                    position: if i < 1024 {
                        [
                            boundary[(i + j) % boundary.len()],
                            0,
                            boundary[(i * 3 + j) % boundary.len()],
                        ]
                    } else {
                        std::array::from_fn(|_| random.signed())
                    },
                    state: if j < 4 { j as u8 } else { random.word() as u8 },
                    color: std::array::from_fn(|_| random.word() as u8),
                })
                .collect(),
            dirty: random.word() as u8,
        };
        for tick in 0..3 {
            let world = if i < 1024 {
                [
                    boundary[(i + tick) % boundary.len()],
                    boundary[i % boundary.len()],
                    boundary[(i * 5 + tick) % boundary.len()],
                ]
            } else {
                std::array::from_fn(|_| random.signed())
            };
            let mut request: Vec<u32> = state.pose.origin.map(|x| x as u32).to_vec();
            request.extend(state.pose.inverse_basis.map(|x| x as u32));
            request.extend(
                geometry
                    .sample_bounds
                    .into_iter()
                    .flatten()
                    .map(|x| x as u32),
            );
            request.extend(geometry.edges.map(|x| x as u32));
            request.push(geometry.edge_cap as u32);
            request.extend(world.map(|x| x as u32));
            request.extend([u32::from(state.dirty), geometry.vertices.len() as u32]);
            request.extend(geometry.vertices.iter().flatten().map(|&x| x as u32));
            request.push(state.records.len() as u32);
            for record in &state.records {
                request.extend(pack_record(record));
            }
            input.extend(request.into_iter().flat_map(u32::to_le_bytes));
            let out = state.sample(&geometry, world);
            let start = expected.len();
            expected.push(0); // C status; typed resources need no memory callbacks.
            expected.extend(out.local_raw.map(|x| x as u32));
            expected.extend(out.local_clamped.map(|x| x as u32));
            expected.extend([
                out.vertex_height as u32,
                out.edge_weight as u32,
                out.offset as u32,
                out.activated_records,
                u32::from(state.dirty),
            ]);
            for record in &state.records {
                expected.extend(pack_record(record));
            }
            ranges.push((start..expected.len(), i, tick));
        }
    }
    let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/deformation");
    fs::create_dir_all(&folder).unwrap();
    let file = folder.join("input.bin");
    fs::write(&file, input).unwrap();
    let output = Command::new(
        std::env::var_os("TANKRACER_DEFORMATION_REFERENCE")
            .expect("set TANKRACER_DEFORMATION_REFERENCE"),
    )
    .stdin(Stdio::from(fs::File::open(file).unwrap()))
    .output()
    .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(&output.stdout[..4], b"DF01");
    let actual: Vec<_> = output.stdout[4..]
        .chunks_exact(4)
        .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
        .collect();
    assert_eq!(actual.len(), expected.len());
    for (range, case, tick) in &ranges {
        assert_eq!(
            actual[range.clone()],
            expected[range.clone()],
            "deformation case={case} tick={tick}"
        );
    }
    eprintln!(
        "{} deformation records / {} output words exactly match C",
        ranges.len(),
        expected.len()
    );
}
