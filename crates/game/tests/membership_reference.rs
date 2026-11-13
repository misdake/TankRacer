use std::{
    fs,
    ops::Range,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_game::{
    lifecycle::{self, Ticks},
    object_query::{Collider, EntityId, ObstacleId},
    obstacle_collision::Grid,
    pose::Pose,
    spatial::{self, Cell},
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
fn cursor(id: Option<ObstacleId>) -> u32 {
    id.map_or(u32::MAX, |id| id.0 as u32)
}
#[test]
#[ignore = "requires C membership oracle; set TANKRACER_MEMBERSHIP_REFERENCE"]
fn grid_membership_duplicates_priorities_tick_flags_and_cursor_prefixes_match_c() {
    let mut rng = Random(0x817ca219);
    let (mut input, mut expected, mut records) =
        (Vec::new(), Vec::new(), Vec::<(Range<usize>, String)>::new());
    let mut errors = [0; 4];
    let boundaries = [i32::MIN, i32::MAX, -65536, -1, 0, 1, 4096, 32768, 65536];
    for op in 1..=4 {
        for case in 0..512 {
            let colliders: Vec<_> = (0..3)
                .map(|i| Collider {
                    entity: EntityId(i + 1),
                    pose: Pose {
                        position: std::array::from_fn(|_| {
                            if case % 5 == 0 {
                                rng.choose(&boundaries)
                            } else {
                                rng.signed() % 50000
                            }
                        }),
                        ..Pose::default()
                    },
                    external_basis: None,
                    radius: rng.choose(&[0, 64, 512, 65535]) as u16,
                    bounds: tankracer_game::geometry::Bounds {
                        min: [0; 3],
                        max: [0; 3],
                    },
                })
                .collect();
            let shape: Vec<_> = (0..3).map(|_| !rng.word().is_multiple_of(3)).collect();
            let mut grid = Grid {
                origin: std::array::from_fn(|_| rng.signed() % 100000),
                shifts: if case % 5 == 0 {
                    [rng.word(), rng.word()]
                } else {
                    [12, 12]
                },
                cells: std::array::from_fn(|_| {
                    (0..rng.word() % 6)
                        .map(|_| ObstacleId((rng.word() % 3) as usize))
                        .collect()
                }),
            };
            let mut ticks = Ticks {
                flags: (0..3).map(|_| rng.word() & 63).collect(),
                order: vec![ObstacleId(2), ObstacleId(0), ObstacleId(1)],
                next: None,
            };
            ticks.next = if case % 4 == 0 {
                None
            } else {
                Some(ticks.order[case % 3])
            };
            let id = ObstacleId(case % 3);
            let limit = if op == 1 || op == 3 {
                1 + rng.word() % 6
            } else {
                rng.word() % 6
            };
            let mut packed = if case % 7 == 0 {
                u32::MAX
            } else if case % 5 == 0 {
                rng.word()
            } else {
                ((rng.word() % 8) << 16) | (rng.word() % 8)
            };
            for tick in 0..4 {
                let mut words = vec![op];
                for (i, c) in colliders.iter().enumerate() {
                    append(&mut words, c.pose.position);
                    words.extend([ticks.flags[i], u32::from(shape[i]), u32::from(c.radius)]);
                }
                append(&mut words, grid.origin);
                words.extend(grid.shifts);
                for ids in &grid.cells {
                    list(&mut words, ids);
                }
                list(&mut words, &ticks.order);
                words.extend([cursor(ticks.next), id.0 as u32, limit, packed]);
                input.extend(words.into_iter().flat_map(u32::to_le_bytes));
                let mut result = 0x7182;
                let code = match op {
                    1 => {
                        packed = spatial::insert(
                            &mut grid,
                            id,
                            &colliders[id.0],
                            shape[id.0].then_some(colliders[id.0].radius),
                        )
                        .packed();
                        0
                    }
                    2 => match spatial::remove(&mut grid, id, Cell::unpack(packed), limit) {
                        Ok(()) => 0,
                        Err(spatial::Error::InvalidLimit) => -1,
                        Err(spatial::Error::Limit) => -3,
                    },
                    3 => {
                        result = u32::from(ticks.enable(id).unwrap());
                        0
                    }
                    4 => match ticks.disable(id, limit) {
                        Ok(changed) => {
                            result = u32::from(changed);
                            0
                        }
                        Err(lifecycle::Error::Spatial(spatial::Error::InvalidLimit)) => -1,
                        Err(lifecycle::Error::Spatial(spatial::Error::Limit)) => -3,
                        e => panic!("unexpected fixture error {e:?}"),
                    },
                    _ => unreachable!(),
                };
                if code != 0 {
                    errors[(op - 1) as usize] += 1;
                }
                let start = expected.len();
                expected.extend([code as u32, packed, result]);
                expected.extend(ticks.flags.iter().copied());
                expected.push(cursor(ticks.next));
                for ids in &grid.cells {
                    list(&mut expected, ids);
                }
                list(&mut expected, &ticks.order);
                records.push((
                    start..expected.len(),
                    format!("op={op} case={case} tick={tick} id={id:?} limit={limit}"),
                ));
            }
        }
    }
    let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/membership");
    fs::create_dir_all(&folder).unwrap();
    let path = folder.join("input.bin");
    fs::write(&path, input).unwrap();
    let result = Command::new(
        std::env::var_os("TANKRACER_MEMBERSHIP_REFERENCE")
            .expect("set TANKRACER_MEMBERSHIP_REFERENCE"),
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
    assert_eq!(&result.stdout[..4], b"MB01");
    let words: Vec<_> = result.stdout[4..]
        .chunks_exact(4)
        .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
        .collect();
    assert_eq!(words.len(), expected.len());
    for (range, label) in &records {
        assert_eq!(words[range.clone()], expected[range.clone()], "{label}");
    }
    assert!(errors[1] > 0 && errors[3] > 0);
    eprintln!(
        "{} membership records / {} words exact; errors by insert/remove/enable/disable {errors:?}",
        records.len(),
        words.len()
    );
}
