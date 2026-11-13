use std::{
    fs,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_game::{
    fall,
    lifecycle::{Queues, Ticks},
    math::Matrix,
    object_query::{EntityId, ObstacleId},
    obstacle_collision::Grid,
    spatial::{self, Cell},
};
#[derive(Clone)]
struct Fixture {
    position: [i32; 3],
    matrix: Matrix,
    peer: Matrix,
    span: i16,
    speed: i32,
    rotation: i32,
    turn: i32,
    slope: i32,
    flags: u32,
    activity: i32,
    tuning: fall::Tuning,
    cell: u32,
    base_flags: u32,
    effect: bool,
    notifications: Vec<u32>,
    grid: [Vec<u32>; 64],
    steps: Vec<[u32; 4]>,
}
impl Default for Fixture {
    fn default() -> Self {
        Self {
            position: [100, 200, -300],
            matrix: Matrix::IDENTITY,
            peer: Matrix::IDENTITY,
            span: 200,
            speed: -3000,
            rotation: 0,
            turn: 8192,
            slope: 0,
            flags: 1,
            activity: 1,
            tuning: fall::Tuning {
                squash_angle: 2730,
                gravity: 5,
                elasticity_multiplier: 400,
                elasticity_shift: 10,
                ..Default::default()
            },
            cell: 0,
            base_flags: 24,
            effect: false,
            notifications: vec![],
            grid: std::array::from_fn(|_| vec![]),
            steps: vec![[16, 1, 0, 20]; 3],
        }
    }
}
struct Services<'a> {
    grid: &'a mut Grid,
    ticks: &'a mut Ticks<EntityId>,
    queues: &'a mut Queues<EntityId>,
    cell: Option<Cell>,
    limit: u32,
    height_status: i32,
    height_value: i32,
    queries: &'a mut [u32; 3],
}
impl fall::Services for Services<'_> {
    fn height(&mut self, x: i32, z: i32) -> Result<i32, i32> {
        self.queries[0] += 1;
        self.queries[1] = x as u32;
        self.queries[2] = z as u32;
        if self.height_status < 0 {
            Err(self.height_status)
        } else {
            Ok(self.height_value)
        }
    }
    fn stop(&mut self) -> Result<(), i32> {
        spatial::remove(self.grid, ObstacleId(0), self.cell, self.limit).map_err(|v| match v {
            spatial::Error::Limit => -3,
            spatial::Error::InvalidLimit => -1,
        })?;
        self.queues
            .queue(self.ticks, EntityId(1), false, self.limit)
            .map_err(|v| match v {
                tankracer_game::lifecycle::Error::Spatial(spatial::Error::Limit) => -3,
                _ => -1,
            })
    }
}
fn list(out: &mut Vec<u32>, values: impl IntoIterator<Item = u32>) {
    let at = out.len();
    out.push(0);
    out.extend(values);
    out[at] = (out.len() - at - 1) as u32;
}
fn encode(out: &mut Vec<u32>, f: &Fixture) {
    out.push(f.steps.len() as u32);
    out.extend(f.position.map(|v| v as u32));
    out.extend(f.matrix.0.map(|v| v as u32));
    out.extend(f.peer.0.map(|v| v as u32));
    out.extend([
        f.span as u32,
        f.speed as u32,
        f.rotation as u32,
        f.turn as u32,
        f.slope as u32,
        f.flags,
        f.activity as u32,
        f.tuning.squash_angle as u32,
        f.tuning.gravity as u32,
        f.tuning.elasticity_multiplier as u32,
        f.tuning.elasticity_shift,
        f.cell,
        f.base_flags,
        u32::from(f.effect),
    ]);
    list(out, f.notifications.iter().copied());
    for v in &f.grid {
        list(out, v.iter().copied());
    }
    out.extend(f.steps.iter().flatten());
}
fn fixtures() -> Vec<Fixture> {
    let mut result = vec![];
    for flags in [0, 1, 2, 3, 4, 6, 8, 10, 12, 14, 32, u32::MAX] {
        for yaw in [0, 16384, 32768, 49152] {
            result.push(Fixture {
                flags,
                peer: Matrix::from_angles([0, yaw, 0]),
                ..Default::default()
            });
        }
    }
    for limit in 0..=4 {
        for effect in [false, true] {
            for reverse in [false, true] {
                let mut f = Fixture {
                    speed: 0,
                    rotation: 17000,
                    flags: if reverse { 8 } else { 0 },
                    effect,
                    steps: vec![[16, 1, 0, limit], [16, 1, 0, 20]],
                    ..Default::default()
                };
                f.grid[0] = vec![1, 1];
                f.grid[1] = vec![2, 1];
                f.notifications = vec![2, 3];
                result.push(f);
            }
        }
    }
    for status in [-17, -2, 0, 1] {
        for height in [i32::MIN + 1, -100, 100, i32::MAX] {
            result.push(Fixture {
                flags: 2,
                steps: vec![[16, status as u32, height as u32, 20]],
                ..Default::default()
            });
        }
    }
    let mut random = 0xdab3916f_u32;
    let mut word = || {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        random
    };
    for _ in 0..64 {
        let mut f = Fixture {
            matrix: Matrix::from_angles(std::array::from_fn(|_| word())),
            peer: Matrix::from_angles(std::array::from_fn(|_| word())),
            speed: word() as i32,
            rotation: word() as i32,
            turn: word() as i32,
            slope: word() as i32,
            span: word() as i16,
            flags: word(),
            activity: if word() & 1 != 0 { 1 } else { 0 },
            effect: word() & 1 != 0,
            cell: [0, 0x70007, u32::MAX][word() as usize % 3],
            ..Default::default()
        };
        f.tuning.gravity = word() as i32;
        f.tuning.elasticity_multiplier = word() as i32;
        f.tuning.elasticity_shift = word();
        f.position = std::array::from_fn(|_| word() as i32);
        f.steps = (0..3)
            .map(|_| {
                [
                    [0, 1, 16, 127, 256, u32::MAX][word() as usize % 6],
                    1,
                    word(),
                    20,
                ]
            })
            .collect();
        result.push(f);
    }
    result
}
#[test]
#[ignore = "requires C Fall motion oracle; set TANKRACER_FALL_REFERENCE"]
fn turn_fall_rebound_stop_and_partial_error_updates_match_c() {
    let fixtures = fixtures();
    let (mut input, mut expected, mut ranges) = (vec![], vec![], vec![]);
    let (mut errors, mut queries_total, mut stopped) = (0, 0, 0);
    for (case, f) in fixtures.iter().enumerate() {
        encode(&mut input, f);
        let mut state = fall::State {
            angular_velocity: f.speed,
            rotation: f.rotation,
            turn_rate: f.turn,
            slope_limit: f.slope,
            ..Default::default()
        };
        let (mut matrix, mut flags, mut activity) = (f.matrix, f.flags, f.activity);
        let mut grid = Grid {
            origin: [0; 2],
            shifts: [12; 2],
            cells: f
                .grid
                .each_ref()
                .map(|v| v.iter().map(|id| ObstacleId(*id as usize - 1)).collect()),
        };
        let mut ticks = Ticks {
            flags: vec![0, f.base_flags, 0, 0],
            ..Default::default()
        };
        let mut queues = Queues {
            notifications: f.notifications.iter().copied().map(EntityId).collect(),
            ..Default::default()
        };
        let mut queries = [0; 3];
        for (index, &[dt, height_status, height_value, limit]) in f.steps.iter().enumerate() {
            let status = if limit == 0 {
                Err(-1)
            } else {
                fall::tick(
                    fall::Motion {
                        state: &mut state,
                        matrix: &mut matrix,
                        flags: &mut flags,
                        activity: &mut activity,
                        position: f.position,
                        width: i32::from(f.span) * 8,
                        vehicle_basis: f.peer,
                        dt: dt as i32,
                        effect_active: f.effect,
                    },
                    f.tuning,
                    &mut Services {
                        grid: &mut grid,
                        ticks: &mut ticks,
                        queues: &mut queues,
                        cell: Cell::unpack(f.cell),
                        limit,
                        height_status: height_status as i32,
                        height_value: height_value as i32,
                        queries: &mut queries,
                    },
                )
            };
            errors += usize::from(status.is_err());
            stopped += usize::from(activity == 0);
            let start = expected.len();
            expected.push(status.err().unwrap_or(0) as u32);
            expected.extend(matrix.0.map(|v| v as u32));
            expected.extend([
                state.angular_velocity as u32,
                state.rotation as u32,
                state.turn_rate as u32,
                state.slope_limit as u32,
                flags,
                activity as u32,
                ticks.flags[1],
            ]);
            expected.extend(queries);
            list(&mut expected, queues.notifications.iter().map(|v| v.0));
            for cell in &grid.cells {
                list(&mut expected, cell.iter().map(|v| v.0 as u32 + 1));
            }
            ranges.push((start..expected.len(), format!("case {case} step {index}")));
        }
        queries_total += queries[0];
    }
    let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/fall");
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
        std::env::var_os("TANKRACER_FALL_REFERENCE").expect("set TANKRACER_FALL_REFERENCE"),
    )
    .stdin(Stdio::from(fs::File::open(path).unwrap()))
    .output()
    .unwrap();
    fs::write(folder.join("output.bin"), &output.stdout).unwrap();
    assert!(
        output.status.success(),
        "{:?}: {}",
        output.status.code(),
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(&output.stdout[..4], b"FT01");
    let words: Vec<_> = output.stdout[4..]
        .chunks_exact(4)
        .map(|v| u32::from_le_bytes(v.try_into().unwrap()))
        .collect();
    if let Some(index) = words.iter().zip(&expected).position(|(a, b)| a != b) {
        let (range, label) = ranges.iter().find(|(r, _)| r.contains(&index)).unwrap();
        panic!(
            "{label}, word {}: C {}, Rust {}",
            index - range.start,
            words[index],
            expected[index]
        );
    }
    assert_eq!(words.len(), expected.len());
    assert!(errors > 5 && queries_total > 5 && stopped > 5);
    eprintln!(
        "{} Fall scenarios / {} updates / {} words exact; {errors} error prefixes, {queries_total} terrain requests, {stopped} stopped observations",
        fixtures.len(),
        ranges.len(),
        words.len()
    );
}
